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

  $Id: mad_ib.c,v 1.11 2004/02/25 00:35:21 roland Exp $
*/

#include "mad_priv.h"
#include "mad_mem_compat.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"
#include "ts_kernel_cache.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>    /* for in_interrupt() */

union tTS_IB_MAD_WRID {
  tTS_IB_WORK_REQUEST_ID id;
  struct {
    int index:16;
    int port:16;
    int qpn:8;
    int is_send:8;
  } field;
};

int tsIbMadPostSend(
                    tTS_IB_MAD mad,
                    int        index
                    ) {
  tTS_IB_DEVICE                device = mad->device;
  tTS_IB_MAD_PRIVATE           priv = device->mad;
  tTS_IB_GATHER_SCATTER_STRUCT gather_list;
  tTS_IB_SEND_PARAM_STRUCT     send_param;
  tTS_IB_ADDRESS_VECTOR_STRUCT av;
  tTS_IB_ADDRESS_HANDLE        addr;

  gather_list.address = tsIbMadBufferAddress(mad);
  gather_list.length  = TS_IB_MAD_PACKET_SIZE;
  gather_list.key     = priv->lkey;

  send_param.op                 = TS_IB_OP_SEND;
  send_param.gather_list        = &gather_list;
  send_param.num_gather_entries = 1;
  send_param.dest_qpn           = mad->dqpn;
  send_param.pkey_index         = mad->pkey_index;
  send_param.solicited_event    = 1;
  send_param.signaled           = 1;

  av.dlid             = mad->dlid;
  av.port             = mad->port;
  av.source_path_bits = 0;
  av.use_grh          = 0;
  av.service_level    = mad->sl;
  av.static_rate      = 0;

  if (tsIbAddressCreate(priv->pd, &av, &addr)) {
    return -EINVAL;
  }

  {
    union tTS_IB_MAD_WRID wrid;

    wrid.field.is_send = 1;
    wrid.field.port    = mad->port;
    wrid.field.qpn     = mad->sqpn;
    wrid.field.index   = index;

    send_param.work_request_id = wrid.id;
  }
  send_param.dest_address = addr;
  send_param.dest_qkey    =
    mad->dqpn == TS_IB_SMI_QP ? 0 : TS_IB_GSI_WELL_KNOWN_QKEY;

  tsKernelCacheSync(mad, TS_IB_MAD_PACKET_SIZE, TS_DMA_TO_DEVICE);
  if (tsIbSend(priv->qp[mad->port][mad->sqpn], &send_param, 1)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "tsIbSend failed for port %d QPN %d of %s",
                   mad->port, mad->sqpn, device->name);
    return -EINVAL;
  }

  tsIbAddressDestroy(addr);
  return 0;
}

int tsIbMadSendNoCopy(
                      tTS_IB_MAD mad
                      ) {
  tTS_IB_DEVICE      device = mad->device;
  tTS_IB_MAD_PRIVATE priv = device->mad;
  tTS_IB_MAD_WORK    work;

  work = kmalloc(sizeof *work,
                 in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
  if (!work) {
    kmem_cache_free(mad_cache, mad);
    return -ENOMEM;
  }

  work->type = TS_IB_MAD_WORK_SEND_POST;
  work->buf  = mad;
  tsIbMadQueueWork(priv, work);

  return 0;
}

int tsIbMadSend(
                tTS_IB_MAD mad
                ) {
  tTS_IB_MAD buf;

  TS_IB_CHECK_MAGIC(mad->device, DEVICE);

  buf = kmem_cache_alloc(mad_cache,
                         in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
  if (!buf) {
    return -ENOMEM;
  }

  *buf = *mad;
  return tsIbMadSendNoCopy(buf);
}

void tsIbMadCompletion(
                       tTS_IB_CQ_HANDLE cq,
                       tTS_IB_CQ_ENTRY  entry,
                       void *           device_ptr
                       ) {
  tTS_IB_DEVICE         device = device_ptr;
  tTS_IB_MAD_PRIVATE    priv = device->mad;
  union tTS_IB_MAD_WRID wrid;

  wrid.id = entry->work_request_id;

  if (entry->status != TS_IB_COMPLETION_STATUS_SUCCESS) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "completion status %d for %s index %d port %d qpn %d send %d bytes %d",
                   entry->status,
                   device->name,
                   wrid.field.index,
                   wrid.field.port,
                   wrid.field.qpn,
                   wrid.field.is_send,
                   entry->bytes_transferred);
    return;
  }

  TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
           "completion (%s), index %d port %d qpn %d send %d bytes %d",
           device->name,
           wrid.field.index,
           wrid.field.port,
           wrid.field.qpn,
           wrid.field.is_send,
           entry->bytes_transferred);

  if (wrid.field.is_send) {
    tTS_IB_MAD_WORK work;

    work = kmalloc(sizeof *work, GFP_KERNEL);
    if (work) {
      work->type   = TS_IB_MAD_WORK_SEND_DONE;
      work->buf    = priv->send_buf[wrid.field.port][wrid.field.qpn][wrid.field.index];
      work->status = 0;
      work->index  = wrid.field.index;

      tsIbMadQueueWork(priv, work);
    } else {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "No memory for MAD send completion");
    }
  } else {
    /* handle receive completion */
    tTS_IB_MAD_WORK work;
    tTS_IB_MAD mad = priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index] +
      TS_IB_MAD_GRH_SIZE;

    mad->device     = device;
    mad->port       = wrid.field.port;
    mad->sl         = entry->sl;
    mad->pkey_index = entry->pkey_index;
    mad->slid       = entry->slid;
    mad->dlid       = entry->dlid_path_bits;
    mad->sqpn       = entry->sqpn;
    mad->dqpn       = wrid.field.qpn;

    work = kmalloc(sizeof *work, GFP_KERNEL);
    if (work) {
      work->type = TS_IB_MAD_WORK_RECEIVE;
      work->buf  = priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index];
      tsIbMadQueueWork(priv, work);
    } else {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "No memory to queue received MAD");
      kfree(priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index]);
    }

    priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index] = NULL;

    if (tsIbMadPostReceive(device, wrid.field.port, wrid.field.qpn, wrid.field.index)) {
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Failed to post MAD receive for %s port %d QPN %d",
                     device->name, wrid.field.port, wrid.field.qpn);
      priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index] = NULL;
    }
  }
}

int tsIbMadPostReceive(
                       tTS_IB_DEVICE device,
                       tTS_IB_PORT   port,
                       tTS_IB_QPN    qpn,
                       int           index
                       ) {
  tTS_IB_MAD_PRIVATE           priv = device->mad;
  void                        *buf;
  tTS_IB_RECEIVE_PARAM_STRUCT  receive_param;
  tTS_IB_GATHER_SCATTER_STRUCT scatter_list;

  buf = kmalloc(sizeof (tTS_IB_MAD_STRUCT) + TS_IB_MAD_GRH_SIZE, GFP_KERNEL);
  if (!buf) {
    return -ENOMEM;
  }

  scatter_list.address = tsIbMadBufferAddress(buf);
  scatter_list.length  = TS_IB_MAD_BUFFER_SIZE;
  scatter_list.key     = priv->lkey;

  receive_param.scatter_list        = &scatter_list;
  receive_param.num_scatter_entries = 1;
  receive_param.device_specific     = NULL;
  receive_param.signaled            = 1;

  {
    union tTS_IB_MAD_WRID wrid;

    wrid.field.is_send = 0;
    wrid.field.port    = port;
    wrid.field.qpn     = qpn;
    wrid.field.index   = index;

    receive_param.work_request_id = wrid.id;
  }

  priv->receive_buf[port][qpn][index] = buf;

  tsKernelCacheSync(buf, TS_IB_MAD_BUFFER_SIZE, TS_DMA_FROM_DEVICE);
  if (tsIbReceive(priv->qp[port][qpn], &receive_param, 1)) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "tsIbReceive failed for port %d QPN %d of %s",
                   port, qpn, device->name);
    kfree(buf);
    priv->receive_buf[port][qpn][index] = NULL;
    return -EINVAL;
  }

  return 0;
}
