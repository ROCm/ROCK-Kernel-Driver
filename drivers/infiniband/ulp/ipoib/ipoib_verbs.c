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

  $Id: ipoib_verbs.c 53 2004-04-14 20:10:38Z roland $
*/

#include "ipoib.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_cache.h"

/* =================================================================== */
/*.. tsIpoibDeviceCheckPkeyPresence - Check for the interface P_Key presence */
void tsIpoibDeviceCheckPkeyPresence(struct net_device *dev)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
	int pkey_index = 0;

	if (ib_cached_pkey_find(priv->ca, priv->port, priv->pkey, &pkey_index))
		clear_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
	else
		set_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
} /* tsIpoibPkeyPresent */

int tsIpoibDeviceMulticastAttach(struct net_device *dev,
                                 tTS_IB_LID mlid,
                                 tTS_IB_GID mgid)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
	struct ib_qp_attribute *qp_attr;
	int ret, pkey_index;

	ret = -ENOMEM;
	qp_attr = kmalloc(sizeof *qp_attr, GFP_ATOMIC);
	if (!qp_attr)
		goto out;

	if (ib_cached_pkey_find(priv->ca, priv->port, priv->pkey, &pkey_index)) {
		clear_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
		ret = -ENXIO;
		goto out;
	}
	set_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);

	/* set correct QKey for QP */
	qp_attr->qkey = priv->qkey;
	qp_attr->valid_fields = TS_IB_QP_ATTRIBUTE_QKEY;
	ret = ib_qp_modify(priv->qp, qp_attr);
	if (ret) {
		TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to modify QP, ret = %d",
				dev->name, ret);
		goto out;
	}

	/* attach QP to multicast group */
	down(&priv->mcast_mutex);
	ret = ib_multicast_attach(mlid, mgid, priv->qp);
	up(&priv->mcast_mutex);
	if (ret) {
		TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to attach to multicast group, ret = %d",
				dev->name, ret);
		goto out;
	}

 out:
	kfree(qp_attr);
	return ret;
}

int tsIpoibDeviceMulticastDetach(struct net_device *dev,
                                 tTS_IB_LID mlid,
                                 tTS_IB_GID mgid)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
	int ret;

	down(&priv->mcast_mutex);
	ret = ib_multicast_detach(mlid, mgid, priv->qp);
	up(&priv->mcast_mutex);
	if (ret) {
		TS_REPORT_WARN(MOD_IB_NET,
			       "%s: ib_multicast_detach failed (result = %d)",
			       dev->name, ret);
	}

	return ret;
}

int tsIpoibDeviceTransportQpCreate(
                                   struct net_device *dev
                                   ) {
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
	int ret, pkey_index;
	struct ib_qp_create_param qp_create = {
		.limit = {
			.max_outstanding_send_request       = TS_IPOIB_TX_RING_SIZE,
			.max_outstanding_receive_request    = TS_IPOIB_RX_RING_SIZE,
			.max_send_gather_element            = 1,
			.max_receive_scatter_element        = 1,
		},
		.pd = priv->pd,
		.send_queue = priv->cq,
		.receive_queue = priv->cq,
		.transport = TS_IB_TRANSPORT_UD,
	};
	struct ib_qp_attribute qp_attr;

	/* Search through the port P_Key table for the requested pkey value.      */
	/* The port has to be assigned to the respective IB partition in advance. */
	ret = ib_cached_pkey_find(priv->ca, priv->port, priv->pkey, &pkey_index);

	if (ret) {
		clear_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
		return ret;
	}
	set_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);

	ret = ib_qp_create(&qp_create, &priv->qp, &priv->local_qpn);
	if (ret) {
		TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to create QP",
				dev->name);
		return ret;
	}

	qp_attr.state      = TS_IB_QP_STATE_INIT;
	qp_attr.qkey       = 0;
	qp_attr.port       = priv->port;
	qp_attr.pkey_index = pkey_index;
	qp_attr.valid_fields =
		TS_IB_QP_ATTRIBUTE_QKEY       |
		TS_IB_QP_ATTRIBUTE_PORT       |
		TS_IB_QP_ATTRIBUTE_PKEY_INDEX |
		TS_IB_QP_ATTRIBUTE_STATE;
	ret = ib_qp_modify(priv->qp, &qp_attr);
	if (ret) {
		TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to modify QP to init, ret = %d",
				dev->name, ret);
		goto out_fail;
	}

	qp_attr.state    = TS_IB_QP_STATE_RTR;
	/* Can't set this in a INIT->RTR transition */
	qp_attr.valid_fields &= ~TS_IB_QP_ATTRIBUTE_PORT;
	ret = ib_qp_modify(priv->qp, &qp_attr);
	if (ret) {
		TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to modify QP to RTR, ret = %d",
				dev->name, ret);
		goto out_fail;
	}

	qp_attr.state = TS_IB_QP_STATE_RTS;
	qp_attr.send_psn = 0x12345678;
	qp_attr.send_psn = 0;
	qp_attr.valid_fields |= TS_IB_QP_ATTRIBUTE_SEND_PSN;
	qp_attr.valid_fields &= ~TS_IB_QP_ATTRIBUTE_PKEY_INDEX;
	ret = ib_qp_modify(priv->qp, &qp_attr);
	if (ret) {
		TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to modify QP to RTS, ret = %d",
				dev->name, ret);
		goto out_fail;
	}

	return 0;

 out_fail:
	ib_qp_destroy(priv->qp);
	priv->qp = TS_IB_HANDLE_INVALID;

	return -EINVAL;
}

void tsIpoibDeviceTransportQpDestroy(struct net_device *dev)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  if (ib_qp_destroy(priv->qp)) {
	  TS_REPORT_WARN(MOD_IB_NET,
			 "%s: ib_qp_destroy failed",
			 dev->name);
  }
  priv->qp = TS_IB_HANDLE_INVALID;
}

int tsIpoibDeviceTransportInit(struct net_device *dev,
                               tTS_IB_DEVICE_HANDLE ca)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
	struct ib_cq_callback cq_callback = {
		.context  = TS_IB_CQ_CALLBACK_PROCESS,
		.policy   = TS_IB_CQ_PROVIDER_REARM,
		.function = {
			.entry = ipoib_completion
		},
		.arg      = dev,
	};
	int entries;

	if (ib_pd_create(priv->ca, NULL, &priv->pd)) {
		TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to allocate PD",
				dev->name);
		return -ENODEV;
	}

	entries = TS_IPOIB_TX_RING_SIZE + TS_IPOIB_RX_RING_SIZE + 1;
	if (ib_cq_create(priv->ca, &entries, &cq_callback, NULL, &priv->cq)) {
		TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to create CQ",
				dev->name);
		goto out_free_pd;
	}
	TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
		 "%s: CQ with %d entries", dev->name, entries);

	{
		/* XXX we assume physical memory starts at address 0. */
		struct ib_physical_buffer buffer_list = {
			.address = 0,
			.size    = 1
		};
		uint64_t dummy_iova = PAGE_OFFSET;
		unsigned long tsize = (unsigned long) high_memory - PAGE_OFFSET;
		tTS_IB_RKEY rkey;

		/* make our region have size the size of low memory rounded up to
		   the next power of 2 (so we use as few TPT entries as possible) */
		while (tsize) {
			buffer_list.size <<= 1;
			tsize            >>= 1;
		}

		if (ib_memory_register_physical(priv->pd,
						&buffer_list,
						1, /* list_len */
						&dummy_iova,
						buffer_list.size,
						0, /* iova_offset */
						TS_IB_ACCESS_LOCAL_WRITE,
						&priv->mr,
						&priv->lkey,
						&rkey)) {
			TS_REPORT_FATAL(MOD_IB_NET,
					"%s: ib_memory_register_physical failed",
					dev->name);
			goto out_free_cq;
		}
	}

	return 0;

 out_free_cq:
	ib_cq_destroy(priv->cq);

 out_free_pd:
	ib_pd_destroy(priv->pd);
	return -ENODEV;
}

void tsIpoibDeviceTransportCleanup(struct net_device *dev)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

	if (priv->qp != TS_IB_HANDLE_INVALID) {
		if (ib_qp_destroy(priv->qp)) {
			TS_REPORT_WARN(MOD_IB_NET,
				       "%s: ib_qp_destroy failed",
				       dev->name);
		}

		priv->qp = TS_IB_HANDLE_INVALID;
		clear_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags);
	}

	if (ib_memory_deregister(priv->mr)) {
		TS_REPORT_WARN(MOD_IB_NET,
			       "%s: ib_memory_deregister failed",
			       dev->name);
	}

	if (ib_cq_destroy(priv->cq)) {
		TS_REPORT_WARN(MOD_IB_NET,
			       "%s: ib_cq_destroy failed",
			       dev->name);
	}

	if (ib_pd_destroy(priv->pd)) {
		TS_REPORT_WARN(MOD_IB_NET,
			       "%s: ib_pd_destroy failed",
			       dev->name);
	}
}

static void ipoib_device_notifier(struct ib_device_notifier *self,
                                  tTS_IB_DEVICE_HANDLE       device,
                                  int                        event)
{
	struct ib_device_properties props;
	int port;

	if (ib_device_properties_get(device, &props)) {
		TS_REPORT_WARN(MOD_IB_NET, "ib_device_properties_get failed");
		return;
	}

	switch (event) {
	case IB_DEVICE_NOTIFIER_ADD:
		if (props.is_switch) {
			ipoib_add_port("ib%d", device, 0, 0);
		} else {
			for (port = 1; port <= props.num_port; ++port)
				ipoib_add_port("ib%d", device, port, 0);
		}
#ifndef TS_HOST_DRIVER
		/*
		 * Now we setup interfaces with a prefix of
		 * "ts". These will be used by the chassis to
		 * communicate between cards and sits on a different
		 * multicast domain.
		 */
		if (props.is_switch) {
			ipoib_add_port("ts%d", device, 0, 1);
		} else {
			for (port = 1; port <= props.num_port; ++port)
				ipoib_add_port("ts%d", device, port, 1);
		}
#endif /* TS_HOST_DRIVER */
		break;

	case IB_DEVICE_NOTIFIER_REMOVE:
		/* Yikes! We don't support devices going away from
		   underneath us yet! */
		TS_REPORT_WARN(MOD_IB_NET,
			       "IPoIB driver can't handle removal of device %s",
			       props.name);
		break;

	default:
		TS_REPORT_WARN(MOD_IB_NET,
			       "Unknown device notifier event %d.");
		break;
	}
}

static struct ib_device_notifier ipoib_notifier = {
	.notifier = ipoib_device_notifier
};

int ipoib_transport_create_devices(void)
{
	ib_device_notifier_register(&ipoib_notifier);
	return 0;
}

void ipoib_transport_cleanup(void)
{
	ib_device_notifier_deregister(&ipoib_notifier);
}

static void ipoib_async_event(struct ib_async_event_record *record,
			      void *priv_ptr)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = priv_ptr;

	if (record->event == TS_IB_PORT_ACTIVE) {
		TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
			 "%s: Port active Event", priv->dev.name);

		tsIpoibDeviceIbFlush(&priv->dev);
	} else {
		TS_REPORT_WARN(MOD_IB_NET,
			       "%s: Unexpected event %d", priv->dev.name, record->event);
	}
}

int tsIpoibTransportPortMonitorStart(struct net_device *dev)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
	struct ib_async_event_record event_record = {
		.device = priv->ca,
		.event  = TS_IB_PORT_ACTIVE,
	};

	if (ib_async_event_handler_register(&event_record,
					    ipoib_async_event,
					    priv,
					    &priv->active_handler)) {
		TS_REPORT_FATAL(MOD_IB_NET,
				"ib_async_event_handler_register failed for TS_IB_PORT_ACTIVE");
		return -EINVAL;
	}

	return 0;
}

void tsIpoibTransportPortMonitorStop(struct net_device *dev)
{
	struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
	ib_async_event_handler_deregister(priv->active_handler);
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
