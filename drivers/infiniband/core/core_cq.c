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

  $Id: core_cq.c,v 1.3 2004/02/25 00:35:15 roland Exp $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

typedef struct tTS_IB_COMPLETION_EVENT_STRUCT tTS_IB_COMPLETION_EVENT_STRUCT,
  *tTS_IB_COMPLETION_EVENT;

struct tTS_IB_COMPLETION_EVENT_STRUCT {
  tTS_IB_CQ        cq;
  struct list_head list;
};

int tsIbCqCreate(
                 tTS_IB_DEVICE_HANDLE       device_handle,
                 int                       *entries,
                 tTS_IB_CQ_CALLBACK         callback,
                 void                      *device_specific,
                 tTS_IB_CQ_HANDLE          *cq_handle
                 ) {
  tTS_IB_DEVICE device = device_handle;
  tTS_IB_CQ     cq;
  int           ret;

  TS_IB_CHECK_MAGIC(device, DEVICE);
  if (!device->cq_create) {
    return -ENOSYS;
  }

  cq = kmalloc(sizeof *cq, GFP_KERNEL);
  if (!cq) {
    return -ENOMEM;
  }

  cq->device   = device;
  cq->callback = *callback;
  INIT_LIST_HEAD(&cq->async_handler_list);
  spin_lock_init(&cq->async_handler_lock);

  ret = device->cq_create(device, entries, device_specific, cq);
  if (!ret) {
    TS_IB_SET_MAGIC(cq, CQ);
    *cq_handle = cq;

    if (cq->callback.policy == TS_IB_CQ_PROVIDER_REARM) {
      ret = tsIbCqRequestNotification(cq, 0);
      if (ret) {
        tsIbCqDestroy(cq);
        TS_IB_CLEAR_MAGIC(cq);
        kfree(cq);
      }
    }
  } else {
    kfree(cq);
  }

  return ret;
}

int tsIbCqDestroy(
                  tTS_IB_CQ_HANDLE cq_handle
                  ) {
  tTS_IB_CQ cq = cq_handle;
  int       ret;

  TS_IB_CHECK_MAGIC(cq, CQ);
  if (!cq->device->cq_destroy) {
    return -ENOSYS;
  }

  if (!list_empty(&cq->async_handler_list)) {
    return -EBUSY;
  }

  ret = cq->device->cq_destroy(cq);
  if (!ret) {
    TS_IB_CLEAR_MAGIC(cq);
    kfree(cq);
  }

  return ret;
}

int tsIbCqResize(
                 tTS_IB_CQ_HANDLE cq_handle,
                 int             *entries
                 ) {
  tTS_IB_CQ cq = cq_handle;
  TS_IB_CHECK_MAGIC(cq, CQ);
  return cq->device->cq_resize ? cq->device->cq_resize(cq, entries) : -ENOSYS;
}

int tsIbCqPoll(
               tTS_IB_CQ_HANDLE cq_handle,
               tTS_IB_CQ_ENTRY  entry
               ) {
  tTS_IB_CQ cq = cq_handle;
  TS_IB_CHECK_MAGIC(cq, CQ);
  return cq->device->cq_poll ? cq->device->cq_poll(cq, entry) : -ENOSYS;
}

int tsIbCqRequestNotification(
                              tTS_IB_CQ_HANDLE cq_handle,
                              int solicited
                              ) {
  tTS_IB_CQ cq = cq_handle;
  TS_IB_CHECK_MAGIC(cq, CQ);
  return cq->device->cq_arm ? cq->device->cq_arm(cq, solicited) : -ENOSYS;
}

static void _tsIbCqDrain(
                         tTS_IB_CQ cq
                         ) {
  /* We make copies of these for efficiency; we know they won't change
     but the compiler can't know that */
  tTS_IB_CQ_POLL_FUNCTION           cq_poll = cq->device->cq_poll;
  tTS_IB_CQ_ENTRY_CALLBACK_FUNCTION func    = cq->callback.function.entry;
  void                             *arg     = cq->callback.arg;
  tTS_IB_CQ_ENTRY_STRUCT            entry;

  if (!cq_poll) {
    return;
  }

  /*
    We re-arm and then drain the CQ.  There is no way for us to miss
    completions because a new completion will generate a new event and
    send us back through this loop when the event is dispatched.

    We ask for all completions (unsolicited or solicited) to generate
    events, since this interface is for simple consumers.
  */
  if (cq->device->cq_arm(cq, 0)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "CQ rearm failed for device %s",
                   cq->device->name);
  }

  while (!cq_poll(cq, &entry)) {
    func(cq, &entry, arg);
  }
}

void tsIbCompletionEventDispatch(
                                 tTS_IB_CQ cq
                                 ) {
  tTS_IB_DEVICE_PRIVATE   priv   = cq->device->core;
  tTS_IB_COMPLETION_EVENT event;

  if (!TS_IB_TEST_MAGIC(cq, CQ)) {
    TS_REPORT_WARN(MOD_KERNEL_IB, "Bad magic 0x%lx at %p for CQ",
                   TS_IB_GET_MAGIC(cq), cq);
    return;
  }

  if (cq->callback.context == TS_IB_CQ_CALLBACK_PROCESS) {
    event = kmalloc(sizeof *event, GFP_ATOMIC);
    if (event) {
      event->cq = cq;

      tsKernelQueueThreadAdd(priv->completion_thread, &event->list);
    } else {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Failed to allocate completion record, dropping event");
    }
  } else {
    if (cq->callback.policy == TS_IB_CQ_PROVIDER_REARM) {
      _tsIbCqDrain(cq);
    } else {
      cq->callback.function.event(cq, cq->callback.arg);
    }
  }
}

void tsIbCompletionThread(
                          struct list_head *entry,
                          void *device_ptr
                          ) {
  tTS_IB_COMPLETION_EVENT event;

  event = list_entry(entry,
                     tTS_IB_COMPLETION_EVENT_STRUCT,
                     list);

  if (event->cq->callback.policy == TS_IB_CQ_PROVIDER_REARM) {
    _tsIbCqDrain(event->cq);
  } else {
    event->cq->callback.function.event(event->cq, event->cq->callback.arg);
  }

  kfree(event);
}
