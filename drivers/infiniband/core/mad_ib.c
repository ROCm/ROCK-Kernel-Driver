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


  $Id: mad_ib.c 32 2004-04-09 03:57:42Z roland $
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

union ib_mad_wrid {
	tTS_IB_WORK_REQUEST_ID id;
	struct {
		int index:16;
		int port:16;
		int qpn:8;
		int is_send:8;
	} field;
};

int ib_mad_post_send(struct ib_mad *mad,
		     int            index)
{
	struct ib_device            *device = mad->device;
	struct ib_mad_private       *priv = device->mad;
	struct ib_gather_scatter     gather_list;
	struct ib_send_param         send_param;
	struct ib_address_vector     av;
	tTS_IB_ADDRESS_HANDLE        addr;

	gather_list.address = ib_mad_buffer_address(mad);
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

	if (ib_address_create(priv->pd, &av, &addr))
		return -EINVAL;

	{
		union ib_mad_wrid wrid;

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
	if (ib_send(priv->qp[mad->port][mad->sqpn], &send_param, 1)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "tsIbSend failed for port %d QPN %d of %s",
			       mad->port, mad->sqpn, device->name);
		return -EINVAL;
	}

	ib_address_destroy(addr);
	return 0;
}

int ib_mad_send_no_copy(struct ib_mad *mad)
{
	struct ib_device      *device = mad->device;
	struct ib_mad_private *priv = device->mad;
	struct ib_mad_work    *work;

	work = kmalloc(sizeof *work,
		       in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!work) {
		kmem_cache_free(mad_cache, mad);
		return -ENOMEM;
	}

	work->type = IB_MAD_WORK_SEND_POST;
	work->buf  = mad;
	ib_mad_queue_work(priv, work);

	return 0;
}

int ib_mad_send(struct ib_mad *mad)
{
	struct ib_mad *buf;

	TS_IB_CHECK_MAGIC(mad->device, DEVICE);

	buf = kmem_cache_alloc(mad_cache,
			       in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	*buf = *mad;
	return ib_mad_send_no_copy(buf);
}

void ib_mad_completion(tTS_IB_CQ_HANDLE cq,
                       tTS_IB_CQ_ENTRY  entry,
                       void *           device_ptr)
{
	struct ib_device      *device = device_ptr;
	struct ib_mad_private *priv = device->mad;
	union ib_mad_wrid      wrid;

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
		struct ib_mad_work *work;

		work = kmalloc(sizeof *work, GFP_KERNEL);
		if (work) {
			work->type   = IB_MAD_WORK_SEND_DONE;
			work->buf    = priv->send_buf[wrid.field.port][wrid.field.qpn][wrid.field.index];
			work->status = 0;
			work->index  = wrid.field.index;

			ib_mad_queue_work(priv, work);
		} else {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "No memory for MAD send completion");
		}
	} else {
		/* handle receive completion */
		struct ib_mad_work *work;
		struct ib_mad      *mad = 
			priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index] +
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
			work->type = IB_MAD_WORK_RECEIVE;
			work->buf  = priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index];
			ib_mad_queue_work(priv, work);
		} else {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "No memory to queue received MAD");
			kfree(priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index]);
		}

		priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index] = NULL;

		if (ib_mad_post_receive(device, wrid.field.port, wrid.field.qpn, wrid.field.index)) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "Failed to post MAD receive for %s port %d QPN %d",
				       device->name, wrid.field.port, wrid.field.qpn);
			priv->receive_buf[wrid.field.port][wrid.field.qpn][wrid.field.index] = NULL;
		}
	}
}

int ib_mad_post_receive(struct ib_device *device,
			tTS_IB_PORT   	  port,
			tTS_IB_QPN    	  qpn,
			int           	  index)
{
	struct ib_mad_private   *priv = device->mad;
	void                    *buf;
	struct ib_receive_param  receive_param;
	struct ib_gather_scatter scatter_list;

	buf = kmalloc(sizeof (struct ib_mad) + TS_IB_MAD_GRH_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	scatter_list.address = ib_mad_buffer_address(buf);
	scatter_list.length  = TS_IB_MAD_BUFFER_SIZE;
	scatter_list.key     = priv->lkey;

	receive_param.scatter_list        = &scatter_list;
	receive_param.num_scatter_entries = 1;
	receive_param.device_specific     = NULL;
	receive_param.signaled            = 1;

	{
		union ib_mad_wrid wrid;

		wrid.field.is_send = 0;
		wrid.field.port    = port;
		wrid.field.qpn     = qpn;
		wrid.field.index   = index;

		receive_param.work_request_id = wrid.id;
	}

	priv->receive_buf[port][qpn][index] = buf;

	tsKernelCacheSync(buf, TS_IB_MAD_BUFFER_SIZE, TS_DMA_FROM_DEVICE);
	if (ib_receive(priv->qp[port][qpn], &receive_param, 1)) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "ib_receive failed for port %d QPN %d of %s",
			       port, qpn, device->name);
		kfree(buf);
		priv->receive_buf[port][qpn][index] = NULL;
		return -EINVAL;
	}

	return 0;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
