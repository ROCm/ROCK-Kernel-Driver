/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: useraccess_mad.c 32 2004-04-09 03:57:42Z roland $
*/

#include "useraccess.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include "ts_ib_mad.h"

#include <linux/slab.h>
#include <linux/errno.h>

typedef struct tTS_IB_USER_MAD_FILTER_LIST_STRUCT tTS_IB_USER_MAD_FILTER_LIST_STRUCT,
  *tTS_IB_USER_MAD_FILTER_LIST;

struct tTS_IB_USER_MAD_FILTER_LIST_STRUCT {
  tTS_IB_USER_MAD_FILTER_HANDLE user_handle;
  tTS_IB_MAD_FILTER_HANDLE      handle;
  tTS_IB_USERACCESS_PRIVATE     priv;
  struct list_head              list;
};

static LIST_HEAD(filter_list);
static DECLARE_MUTEX(filter_sem);
static tTS_IB_USER_MAD_FILTER_HANDLE filter_serial;

int tsIbCmUserFilter(
                     tTS_IB_MAD packet,
                     tTS_IB_MAD_FILTER filter
                     );

/* =============================================================== */
/*..tsIbUserRead -                                                 */
ssize_t tsIbUserRead(
                     TS_READ_PARAMS(filp, buf, count, pos)
                     ) {
  tTS_IB_USERACCESS_PRIVATE priv = TS_IB_USER_PRIV_FROM_FILE(filp);

  int ret;

  /* Reads must be a multiple of the size of a MAD structure */
  if (!count
      || count % TS_USER_MAD_SIZE) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "read of size %d by '%s' (pid %d) not divisible by mad size %d",
                   count, current->comm, current->pid, TS_USER_MAD_SIZE);
    return -EINVAL;
  }

  if (down_interruptible(&priv->mad_sem)) {
    return -ERESTARTSYS;
  }

  while (list_empty(&priv->mad_list)) {
    up(&priv->mad_sem);

    if (filp->f_flags & O_NONBLOCK) {
      /* nonblocking and no MADs queued */
      return -EAGAIN;
    }

    if (wait_event_interruptible(priv->mad_wait,
                                 !list_empty(&priv->mad_list))) {
      return -ERESTARTSYS;
    }

    if (down_interruptible(&priv->mad_sem)) {
      return -ERESTARTSYS;
    }
  }

  {
    tTS_IB_MAD mad = list_entry(priv->mad_list.next,
                                tTS_IB_MAD_STRUCT,
                                list);
    list_del(&mad->list);
    --priv->mad_queue_length;

    /* got a packet, can release the lock */
    up(&priv->mad_sem);

    if (copy_to_user(buf, mad, TS_USER_MAD_SIZE)) {
      ret = -EFAULT;
    } else {
      ret = TS_USER_MAD_SIZE;
    }

    kfree(mad);
  }

  return ret;
}

/* =============================================================== */
/*..tsIbUserWrite -                                                */
ssize_t tsIbUserWrite(
                      TS_WRITE_PARAMS(filp, buf, count, pos)
                      ) {
  tTS_IB_USERACCESS_PRIVATE priv = TS_IB_USER_PRIV_FROM_FILE(filp);

  tTS_IB_MAD mad;
  int ret;

  if (count != TS_USER_MAD_SIZE) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "write of size %d not equal to mad size %d",
                   count, TS_USER_MAD_SIZE);
    return -EINVAL;
  }

  mad = kmalloc(sizeof *mad, GFP_KERNEL);
  if (!mad) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Failed to allocate MAD send buffer");
    return -ENOMEM;
  }

  if (copy_from_user(mad, buf, TS_USER_MAD_SIZE)) {
    ret = -EFAULT;
    goto out;
  }

  mad->device          = priv->device->ib_device;
  mad->completion_func = NULL;

  ret = tsIbMadSend(mad);
  if (ret) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "tsIbMadSend failed, return code %d", ret);
  } else {
    ret = TS_USER_MAD_SIZE;
  }

 out:
  kfree(mad);

  return ret;
}

/* =============================================================== */
/*..tsIbUserPoll -                                                 */
unsigned int tsIbUserPoll(
                          struct file *filp,
                          poll_table *wait
                          ) {
  tTS_IB_USERACCESS_PRIVATE priv =
    (tTS_IB_USERACCESS_PRIVATE) filp->private_data;

  /* always assume we will be able to post a MAD send */
  unsigned int mask = POLLOUT | POLLWRNORM;

  poll_wait(filp, &priv->mad_wait, wait);

  if (!list_empty(&priv->mad_list)) {
    mask |= POLLIN | POLLRDNORM;
  }

  return mask;
}

/* =============================================================== */
/*..tsIbUserMadHandler - dispatch function MADs -> userspace       */
static void _tsIbUserMadHandler(
                                tTS_IB_MAD mad,
                                void *filter_ptr
                                ) {
  tTS_IB_USER_MAD_FILTER_LIST filter = filter_ptr;
  tTS_IB_MAD                  newp;

  TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
           "user layer got MAD: "
           "slid=0x%04x, port=%d, dqpn=%d, class=0x%02x, method=0x%02x, attr id=0x%04x",
           mad->slid,
           mad->port,
           mad->dqpn,
           mad->mgmt_class,
           mad->r_method,
           be16_to_cpu(mad->attribute_id));

  down(&filter->priv->mad_sem);
  if (filter->priv->mad_queue_length < filter->priv->max_mad_queue_length) {
    newp = kmalloc(sizeof *newp, GFP_KERNEL);
    if (newp) {
      *newp = *mad;
      list_add_tail(&newp->list, &filter->priv->mad_list);
      ++filter->priv->mad_queue_length;
      wake_up_interruptible(&filter->priv->mad_wait);
    }
  }
  up(&filter->priv->mad_sem);
}

/* =============================================================== */
/*..tsIbUserFilterAdd - add a MAD filter for a driver instance     */
int tsIbUserFilterAdd(
                      tTS_IB_USERACCESS_PRIVATE priv,
                      tTS_IB_USER_MAD_FILTER user_filter
                      ) {
  tTS_IB_MAD_FILTER_STRUCT    filter;
  tTS_IB_MAD_FILTER_HANDLE    handle;
  tTS_IB_USER_MAD_FILTER_LIST new;
  int ret = 0;

  filter.device       = priv->device->ib_device;
  filter.port         = user_filter->port;
  filter.qpn          = user_filter->qpn;
  filter.mgmt_class   = user_filter->mgmt_class;
  filter.r_method     = user_filter->r_method;
  filter.attribute_id = user_filter->attribute_id;
  filter.direction    = user_filter->direction;
  filter.mask         = user_filter->mask | TS_IB_MAD_FILTER_DEVICE;
  snprintf(filter.name, sizeof filter.name, "user (%.16s/%d)",
           current->comm, current->pid);

  if (down_interruptible(&filter_sem)) {
    return -ERESTARTSYS;
  }

  new = kmalloc(sizeof *new, GFP_KERNEL);
  if (!new) {
    ret = -ENOMEM;
    goto out;
  }

  ret = tsIbMadHandlerRegister(&filter,
                               _tsIbUserMadHandler,
                               new,
                               &handle);
  if (ret) {
    kfree(new);
    goto out;
  }

  new->priv           = priv;
  new->handle         = handle;
  new->user_handle    = filter_serial;
  user_filter->handle = filter_serial;
  ++filter_serial;

  list_add_tail(&new->list, &filter_list);

  TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
           "filter 0x%08x: class=0x%02x, method=0x%02x, attrib=0x%04x, mask=%x",
           new->user_handle,
           filter.mgmt_class,
           filter.r_method,
           filter.attribute_id,
           filter.mask);

 out:
  up(&filter_sem);

  return ret;
}

/* =============================================================== */
/*..tsIbUserFilterDel - remove a MAD filter                        */
int tsIbUserFilterDel(
                      tTS_IB_USERACCESS_PRIVATE priv,
                      tTS_IB_USER_MAD_FILTER_HANDLE handle
                      ) {
  int ret = -EINVAL;
  struct list_head *ptr;
  tTS_IB_USER_MAD_FILTER_LIST entry;

  if (down_interruptible(&filter_sem)) {
    return -ERESTARTSYS;
  }

  list_for_each(ptr, &filter_list) {
    entry = list_entry(ptr, tTS_IB_USER_MAD_FILTER_LIST_STRUCT, list);
    if (entry->priv == priv && entry->user_handle == handle) {
      TS_TRACE(MOD_KERNEL_IB, T_SCREAM, TRACE_KERNEL_IB_GEN,
               "freeing filter, handle 0x%08x",
               entry->user_handle);

      list_del(ptr);
      tsIbMadHandlerDeregister(entry->handle);
      kfree(entry);
      ret = 0;
      break;
    }
  }

  up(&filter_sem);

  return ret;
}

/* =============================================================== */
/*..tsIbUserFilterClear - remove all MAD filter for a fd           */
void tsIbUserFilterClear(
                         tTS_IB_USERACCESS_PRIVATE priv
                         ) {
  struct list_head *ptr;
  struct list_head *tmp;
  tTS_IB_USER_MAD_FILTER_LIST entry;

  if (down_interruptible(&filter_sem)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "signal while getting filter semaphore");
    return;
  }

  list_for_each_safe(ptr, tmp, &filter_list) {
    entry = list_entry(ptr, tTS_IB_USER_MAD_FILTER_LIST_STRUCT, list);
    if (entry->priv == priv) {
      TS_TRACE(MOD_KERNEL_IB, T_SCREAM, TRACE_KERNEL_IB_GEN,
               "freeing filter, handle 0x%08x",
               entry->user_handle);

      list_del(ptr);
      tsIbMadHandlerDeregister(entry->handle);
      kfree(entry);
    }
  }

  up(&filter_sem);
}
