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

  $Id: ipoib_vlan.c 32 2004-04-09 03:57:42Z roland $
*/

#include "ipoib.h"
#include "ipoib_ioctl.h"

#include "ts_kernel_services.h"
#include "ts_kernel_trace.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/slab.h>

#include <asm/uaccess.h>

typedef struct tTS_IPOIB_VLAN_ITERATOR_STRUCT tTS_IPOIB_VLAN_ITERATOR_STRUCT,
  *tTS_IPOIB_VLAN_ITERATOR;

struct tTS_IPOIB_VLAN_ITERATOR_STRUCT {
  struct list_head *pintf_cur;
  struct list_head *intf_cur;
};

static DECLARE_MUTEX(proc_mutex);

int tsIpoibVlanAdd(
                   struct net_device *pdev,
                   char *intf_name,
                   unsigned short pkey
                   )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *ppriv, *priv;
  int result = -ENOMEM;
  struct list_head *ptr;

#if !defined(TS_KERNEL_2_6) /* XXX ??? */
  /* Make sure this interface belongs to this driver */
  if (pdev->owner != THIS_MODULE) {
    return -EINVAL;
  }
#endif

  if (!capable(CAP_NET_ADMIN)) {
    return -EPERM;
  }

  ppriv = pdev->priv;

  /*
   * First ensure this isn't a duplicate. We check the parent device and
   * then all of the child interfaces to make sure the Pkey doesn't match.
   */
  if (ppriv->pkey == pkey) {
    return -ENOTUNIQ;
  }

  down(&ipoib_device_mutex);
  list_for_each(ptr, &ppriv->child_intfs) {
    priv = list_entry(ptr, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT, list);

    if (priv->pkey == pkey) {
      up(&ipoib_device_mutex);
      return -ENOTUNIQ;
    }
  }
  up(&ipoib_device_mutex);

  priv = tsIpoibAllocateInterface();
  if (!priv) {
    goto alloc_mem_failed;
  }

  set_bit(TS_IPOIB_FLAG_SUBINTERFACE, &priv->flags);

  priv->pkey = pkey;

  strncpy(priv->dev.name, intf_name, sizeof(priv->dev.name));

  result = tsIpoibDeviceInit(&priv->dev, ppriv->ca, ppriv->port);
  if (result < 0) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "failed to initialize net device %d, port %d",
                    ppriv->ca, ppriv->port);
    goto device_init_failed;
  }

  result = register_netdev(&priv->dev);
  if (result) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: failed to initialize; error %i",
                    priv->dev.name, result);
    goto register_failed;
  }

  down(&ipoib_device_mutex);
  list_add_tail(&priv->list, &ppriv->child_intfs);
  up(&ipoib_device_mutex);

  return 0;

register_failed:
  tsIpoibDeviceCleanup(&priv->dev);

device_init_failed:
  kfree(priv);

alloc_mem_failed:

  return result;
}

int tsIpoibVlanDel(
                   struct net_device *pdev,
                   unsigned short pkey
                   )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *ppriv, *priv;
  struct list_head *ptr, *tmp;

#if !defined(TS_KERNEL_2_6) /* XXX ??? */
  /* Make sure this interface belongs to this driver */
  if (pdev->owner != THIS_MODULE) {
    return -EINVAL;
  }
#endif

  if (!capable(CAP_NET_ADMIN)) {
    return -EPERM;
  }

  ppriv = pdev->priv;

  down(&ipoib_device_mutex);
  list_for_each_safe(ptr, tmp, &ppriv->child_intfs) {
    priv = list_entry(ptr, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT, list);

    if (priv->pkey == pkey) {
      if (priv->dev.flags & IFF_UP) {
        up(&ipoib_device_mutex);
        return -EBUSY;
      }

      tsIpoibDeviceCleanup(&priv->dev);
      unregister_netdev(&priv->dev);

      list_del(&priv->list);

      kfree(priv);
      up(&ipoib_device_mutex);

      return 0;
    }
  }
  up(&ipoib_device_mutex);

  return -ENOENT;
}

/* =============================================================== */
/*..tsIpoibVlanIterator -- incr. iter. -- return non-zero at end   */
int tsIpoibVlanIteratorNext(
                            tTS_IPOIB_VLAN_ITERATOR iter
                            )
{
  while (1) {
    struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv;

    priv = list_entry(iter->pintf_cur, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT,
                      list);
    if (!iter->intf_cur)
      iter->intf_cur = priv->child_intfs.next;
    else
      iter->intf_cur = iter->intf_cur->next;

    if (iter->intf_cur == &priv->child_intfs) {
      iter->pintf_cur = iter->pintf_cur->next;
      if (iter->pintf_cur == &ipoib_device_list) {
        return 1;
      }

      iter->intf_cur = NULL;
      return 0;
    } else {
      return 0;
    }
  }
}

/* =============================================================== */
/*.._tsIpoibVlanSeqStart -- seq file handling                      */
static void *_tsIpoibVlanSeqStart(
                                  struct seq_file *file,
                                  loff_t *pos
                                  )
{
  tTS_IPOIB_VLAN_ITERATOR iter;
  loff_t n = *pos;

  iter = kmalloc(sizeof *iter, GFP_KERNEL);
  if (!iter) {
    return NULL;
  }

  iter->pintf_cur = ipoib_device_list.next;
  iter->intf_cur = NULL;

  while (n--) {
    if (tsIpoibVlanIteratorNext(iter)) {
      kfree(iter);
      return NULL;
    }
  }

  return iter;
}

/* =============================================================== */
/*.._tsIpoibVlanSeqNext -- seq file handling                       */
static void *_tsIpoibVlanSeqNext(
                                 struct seq_file *file,
                                 void *iter_ptr,
                                 loff_t *pos
                                 )
{
  tTS_IPOIB_VLAN_ITERATOR iter = iter_ptr;

  (*pos)++;

  if (tsIpoibVlanIteratorNext(iter)) {
    kfree(iter);
    return NULL;
  }

  return iter;
}

/* =============================================================== */
/*.._tsIpoibVlanSeqStop -- seq file handling                       */
static void _tsIpoibVlanSeqStop(
                                struct seq_file *file,
                                void *iter_ptr
                                )
{
  tTS_IPOIB_VLAN_ITERATOR iter = iter_ptr;

  kfree(iter);
}

/* =============================================================== */
/*.._tsIpoibVlanSeqShow -- seq file handling                       */
static int _tsIpoibVlanSeqShow(
                               struct seq_file *file,
                               void *iter_ptr
                               )
{
  tTS_IPOIB_VLAN_ITERATOR iter = iter_ptr;

  if (iter) {
    struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *ppriv;

    ppriv = list_entry(iter->pintf_cur, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT,
                      list);

    if (!iter->intf_cur) {
      seq_printf(file, "%s 0x%04x\n", ppriv->dev.name, ppriv->pkey);
    } else {
      struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv;

      priv = list_entry(iter->intf_cur, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT,
                        list);

      seq_printf(file, " %s %s 0x%04x\n", ppriv->dev.name, priv->dev.name,
                 priv->pkey);
    }
  }

  return 0;
}

static struct seq_operations ipoib_vlan_seq_operations = {
  .start = _tsIpoibVlanSeqStart,
  .next  = _tsIpoibVlanSeqNext,
  .stop  = _tsIpoibVlanSeqStop,
  .show  = _tsIpoibVlanSeqShow
};

/* =============================================================== */
/*.._tsIpoibVlanProcOpen -- proc file handling                     */
static int _tsIpoibVlanProcOpen(
                                struct inode *inode,
                                struct file *file
                                )
{
  if (down_interruptible(&proc_mutex)) {
    return -ERESTARTSYS;
  }

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
           "opening proc file");

  return seq_open(file, &ipoib_vlan_seq_operations);
}

/* =============================================================== */
/*.._tsIpoibVlanProcWrite -- proc file handling                    */
static ssize_t _tsIpoibVlanProcWrite(
                                     struct file *file,
                                     const char *buffer,
                                     size_t count,
                                     loff_t *pos
                                     )
{
  int result;
  char kernel_buf[256];
  char intf_parent[128], intf_name[128];
  unsigned int pkey;
  struct net_device *pdev;

  count = min(count, sizeof kernel_buf);

  if (copy_from_user(kernel_buf, buffer, count)) {
    return -EFAULT;
  }
  kernel_buf[count - 1] = '\0';

  if (sscanf(kernel_buf, "add %128s %128s %i", intf_parent, intf_name, &pkey) == 3) {
    if (pkey > 0xffff) {
      return -EINVAL;
    }

    pdev = dev_get_by_name(intf_parent);
    if (!pdev) {
      return -ENOENT;
    }

    result = tsIpoibVlanAdd(pdev, intf_name, pkey);

    dev_put(pdev);

    if (result < 0) {
      return result;
    }
  } else if (sscanf(kernel_buf, "del %128s %i", intf_parent, &pkey) == 2) {
    if (pkey > 0xffff) {
      return -EINVAL;
    }

    pdev = dev_get_by_name(intf_parent);
    if (!pdev) {
      return -ENOENT;
    }

    result = tsIpoibVlanDel(pdev, pkey);

    dev_put(pdev);

    if (result < 0) {
      return result;
    }
  } else {
    return -EINVAL;
  }

  return count;
}

/* =============================================================== */
/*.._tsIpoibVlanProcRelease -- proc file handling                  */
static int _tsIpoibVlanProcRelease(
                                   struct inode *inode,
                                   struct file *file
                                   )
{
  up(&proc_mutex);

  return seq_release(inode, file);
}

static struct file_operations ipoib_vlan_proc_operations = {
  .owner   = THIS_MODULE,
  .open    = _tsIpoibVlanProcOpen,
  .read    = seq_read,
  .write   = _tsIpoibVlanProcWrite,
  .llseek  = seq_lseek,
  .release = _tsIpoibVlanProcRelease,
};

struct proc_dir_entry *vlan_proc_entry;

int tsIpoibVlanInit(
                    void
                    )
{
  vlan_proc_entry = create_proc_entry("ipoib_vlan",
                                       S_IRUGO | S_IWUGO,
                                       tsKernelProcDirGet());

  if (!vlan_proc_entry) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "Can't create ipoib_vlan in proc directory\n");
    return -ENOMEM;
  }

  vlan_proc_entry->proc_fops = &ipoib_vlan_proc_operations;

  return 0;
}

void tsIpoibVlanCleanup(
                        void
                        )
{
  if (vlan_proc_entry) {
    TS_REPORT_CLEANUP(MOD_IB_NET,
                      "removing proc file ipoib_vlan");
    remove_proc_entry("ipoib_vlan", tsKernelProcDirGet());
  }
}
