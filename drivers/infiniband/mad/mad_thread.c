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

  $Id: mad_thread.c,v 1.13 2004/02/25 00:35:21 roland Exp $
*/

#include "mad_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

enum tTS_IB_SM_MGMT_CLASS {
  TS_IB_SM_DIRECTED_ROUTE = 0x81
};

enum {
  TS_IB_LID_PERMISSIVE = 0xffff
};

static inline int _tsIbMadSmpIsOutgoing(
                                        tTS_IB_MAD mad
                                        ) {
  return !(be16_to_cpu(mad->status) & 0x8000);
}

static inline int _tsIbMadSmpSend(
                                  tTS_IB_DEVICE   device,
                                  tTS_IB_MAD_WORK work,
                                  int            *reuse
                                  ) {
  tTS_IB_MAD_PRIVATE priv = device->mad;
  tTS_IB_MAD         mad  = work->buf;
  tTS_IB_MAD_RESULT  result;

  if (_tsIbMadSmpIsOutgoing(mad)) {

    /* If this is an outgoing 0-hop SMP and we have a
       mad_process method, just process the MAD directly. */
    if (device->mad_process &&
        !mad->route.directed.hop_count) {
      void      *response_buf = kmalloc(sizeof (tTS_IB_MAD_STRUCT) + TS_IB_MAD_GRH_SIZE,
                                        GFP_KERNEL);
      tTS_IB_MAD response     = response_buf + TS_IB_MAD_GRH_SIZE;
      if (!response_buf) {
        TS_REPORT_WARN(MOD_KERNEL_IB, "No memory for loopback processing");
        work->type   = TS_IB_MAD_WORK_SEND_DONE;
        work->index  = -1;
        work->status = -ENOMEM;
        *reuse = 1;
        return 1;
      }

      response->device          = mad->device;
      response->port            = mad->port;
      response->sl              = mad->sl;
      response->dlid            = mad->slid;
      response->sqpn            = mad->dqpn;
      response->dqpn            = mad->sqpn;
      response->completion_func = NULL;

      work->type   = TS_IB_MAD_WORK_SEND_DONE;
      work->index  = -1;
      result = device->mad_process(device, 0, mad, response);

      *reuse = 1;

      if (result & TS_IB_MAD_RESULT_SUCCESS) {
        tTS_IB_MAD_WORK response_work;
        response_work = kmalloc(sizeof *response_work, GFP_KERNEL);
        if (!response_work) {
          work->status = -ENOMEM;
          return 1;
        }

        work->status        = 0;
        response_work->type = TS_IB_MAD_WORK_RECEIVE;
        response_work->buf  = response_buf;
        tsIbMadQueueWork(priv, response_work);
      } else {
        work->status = -EINVAL;
        kfree(response_buf);
      }

      return 1;
    } else {
      if (!mad->route.directed.hop_pointer) {
#if TS_IB_ANAFA2_HOP_COUNT_WORKAROUND
        if (!priv->is_anafa2) {
          ++mad->route.directed.hop_pointer;
        }
#else
        ++mad->route.directed.hop_pointer;
#endif /* TS_IB_ANAFA2_HOP_COUNT_WORKAROUND */
      } else {
        /* Discard (IB Spec 14.2.2.2 #2) */
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "Discarding outgoing DR SMP with hop_pointer %d",
                       mad->route.directed.hop_pointer);
        work->type   = TS_IB_MAD_WORK_SEND_DONE;
        work->status = -EINVAL;
        work->index  = -1;
        *reuse = 1;
        return 1;
      }
    }
  } else {
    /* Process returning SMP: (IB Spec 14.2.2.4 is relevant) */
    if (mad->route.directed.hop_count &&
        mad->route.directed.hop_pointer > 1) {
      --mad->route.directed.hop_pointer;
    }
  }

  return 0;
}

static int _tsIbMadHandleSendQueue(
                                   tTS_IB_MAD_PRIVATE priv,
                                   tTS_IB_PORT        port,
                                   tTS_IB_QPN         sqpn,
                                   int                index
                                   ) {
  tTS_IB_MAD      mad;
  tTS_IB_MAD_WORK work;
  int status;
  int posted = 0;

  while (!list_empty(&priv->send_list[port][sqpn])) {
    mad = list_entry(priv->send_list[port][sqpn].next,
                     tTS_IB_MAD_STRUCT,
                     list);
    list_del(&mad->list);

    priv->send_buf[port][sqpn][index] = mad;
    status = tsIbMadPostSend(mad, index);
    if (!status) {
      priv->send_next[port][sqpn] = (index + 1) % TS_IB_MAD_SENDS_PER_QP;
      ++posted;
      break;
    } else {
      priv->send_buf[port][sqpn][index] = NULL;

      work = kmalloc(sizeof *work, GFP_KERNEL);
      if (!work) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "Couldn't allocate new work struct, we're hosed");
        break;
      }
      work->type   = TS_IB_MAD_WORK_SEND_DONE;
      work->index  = -1;
      work->status = status;
      work->buf    = mad;
      tsIbMadQueueWork(priv, work);
    }
  }

  return posted;
}

void tsIbMadWorkThread(
                       struct list_head *entry,
                       void *device_ptr
                       ) {
  tTS_IB_DEVICE      device = device_ptr;
  tTS_IB_MAD_PRIVATE priv = device->mad;
  tTS_IB_MAD_WORK    work;
  tTS_IB_MAD         mad;
  int                reuse;

  work = list_entry(entry,
                    tTS_IB_MAD_WORK_STRUCT,
                    list);

  reuse = 0;

  switch (work->type) {
  case TS_IB_MAD_WORK_SEND_POST:
    {
      int send_index;

      mad = work->buf;

      /* Dump MADs with destination LID 0.  These may get generated by
         query modules before the SM has set the SM LID */
      if (mad->dlid == 0) {
        work->type   = TS_IB_MAD_WORK_SEND_DONE;
        work->index  = -1;
        work->status = -EHOSTUNREACH;
        reuse = 1;
        break;
      }

      /* Check to see if any filters match the outgoing MAD
         (incoming MADs are checked by tsIbMadDispatch()). */
      tsIbMadInvokeFilters(mad, TS_IB_MAD_DIRECTION_OUT);

      /* Handle directed route SMPs */
      if (mad->dqpn       == TS_IB_SMI_QP            &&
          mad->dlid       == TS_IB_LID_PERMISSIVE    &&
          mad->mgmt_class == TS_IB_SM_DIRECTED_ROUTE) {
        if (_tsIbMadSmpSend(device, work, &reuse)) {
          break;
        }
      }

      if (!priv->send_free[mad->port][mad->sqpn]) {
        list_add_tail(&mad->list, &priv->send_list[mad->port][mad->sqpn]);
        break;
      }

      send_index = priv->send_next[mad->port][mad->sqpn];

      priv->send_buf[mad->port][mad->sqpn][send_index] = mad;
      work->status = tsIbMadPostSend(mad, send_index);
      if (!work->status) {
        --priv->send_free[mad->port][mad->sqpn];
        priv->send_next[mad->port][mad->sqpn] = (send_index + 1) % TS_IB_MAD_SENDS_PER_QP;
      } else {
        priv->send_buf[mad->port][mad->sqpn][send_index] = NULL;
        work->type  = TS_IB_MAD_WORK_SEND_DONE;
        work->index = -1;
        reuse = 1;
      }
    }
    break;

  case TS_IB_MAD_WORK_SEND_DONE:
    {
      tTS_IB_PORT port;
      tTS_IB_QPN  sqpn;

      mad  = work->buf;
      port = mad->port;
      sqpn = mad->sqpn;

      if (mad->completion_func) {
        mad->completion_func(work->status, mad->arg);
      }
      kmem_cache_free(mad_cache, mad);

      if (work->index == -1) {
        break;
      }

      priv->send_buf[port][sqpn][work->index] = NULL;

      if (!_tsIbMadHandleSendQueue(priv, port, sqpn, work->index)) {
        ++priv->send_free[port][sqpn];
      }
    }

    break;

  case TS_IB_MAD_WORK_RECEIVE:
    mad = work->buf + TS_IB_MAD_GRH_SIZE;
    tsIbMadDispatch(mad);
    kfree(work->buf);
    break;
  }

  if (reuse) {
    tsIbMadQueueWork(priv, work);
  } else {
    kfree(work);
  }
}
