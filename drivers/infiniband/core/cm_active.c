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

  $Id: cm_active.c 32 2004-04-09 03:57:42Z roland $
*/

#include "cm_priv.h"
#include "cm_packet.h"

#include "ts_ib_core.h"
#include "ts_ib_mad.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#ifndef W2K_OS
#include <linux/config.h>
#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/byteorder.h>
#else
#include <os_dep/win/linux/string.h>
#endif


/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager packet sending.                                 */
/*                                                                       */
/* --------------------------------------------------------------------- */

int ib_cm_req_send(struct ib_cm_connection *connection,
		   tTS_IB_SERVICE_ID service_id,
		   void *req_private_data,
		   int req_private_data_len)
{
	int ret;

	ib_mad_build_header(&connection->mad);

	connection->mad.attribute_id   = cpu_to_be16(IB_COM_MGT_REQ);
	connection->mad.transaction_id = cpu_to_be64(connection->transaction_id);

	/* Fields are in order of the IB spec. 12.6.5 */

	ib_cm_req_local_comm_id_set         (&connection->mad, connection->local_comm_id);
	cpu_to_be64s(&service_id);
	memcpy(ib_cm_req_service_id_get(&connection->mad),
	       &service_id,
	       ib_cm_req_service_id_get_length());
	if (ib_cached_node_guid_get(connection->local_cm_device,
				    ib_cm_req_local_guid_get(&connection->mad)))
		TS_REPORT_WARN(MOD_IB_CM, "ib_cached_node_guid_get failed");

	ib_cm_req_local_cm_qkey_set         (&connection->mad, TS_IB_GSI_WELL_KNOWN_QKEY);
	ib_cm_req_local_qkey_set            (&connection->mad, 0);
	ib_cm_req_local_qpn_set             (&connection->mad, connection->local_qpn);

	ib_cm_req_target_max_set            (&connection->mad, connection->responder_resources);
	ib_cm_req_local_eecn_set            (&connection->mad, 0);
	ib_cm_req_initiator_max_set         (&connection->mad, connection->initiator_depth);
	ib_cm_req_remote_eecn_set           (&connection->mad, 0);
	ib_cm_req_remote_cm_timeout_set     (&connection->mad, connection->cm_response_timeout);
	ib_cm_req_transport_service_type_set(&connection->mad, TS_IB_TRANSPORT_RC);
	ib_cm_req_end_to_end_fc_set         (&connection->mad, 1);
	ib_cm_req_starting_psn_set          (&connection->mad, connection->receive_psn);

	ib_cm_req_local_cm_timeout_set      (&connection->mad, connection->cm_response_timeout);
	ib_cm_req_retry_count_set           (&connection->mad, connection->retry_count);
	ib_cm_req_pkey_set                  (&connection->mad, connection->primary_path.pkey);
	ib_cm_req_path_mtu_set              (&connection->mad, (uint8_t) connection->primary_path.mtu);
	ib_cm_req_rdc_exists_set            (&connection->mad, 0);

	ib_cm_req_rnr_retry_count_set       (&connection->mad, connection->rnr_retry_count);
	ib_cm_req_max_cm_retries_set        (&connection->mad, 15);

	ib_cm_req_primary_local_lid_set     (&connection->mad, connection->primary_path.slid);
	ib_cm_req_primary_remote_lid_set    (&connection->mad, connection->primary_path.dlid);

	memcpy(ib_cm_req_primary_local_gid_get(&connection->mad),
	       connection->primary_path.sgid,
	       ib_cm_req_primary_local_gid_get_length());
	memcpy(ib_cm_req_primary_remote_gid_get(&connection->mad),
	       connection->primary_path.dgid,
	       ib_cm_req_primary_remote_gid_get_length());

	ib_cm_req_primary_flow_label_set      (&connection->mad, connection->primary_path.flowlabel);
	ib_cm_req_primary_pkt_rate_set        (&connection->mad, 2);
	ib_cm_req_primary_traffic_set         (&connection->mad, 0);
	ib_cm_req_primary_hop_limit_set       (&connection->mad, 0);
	ib_cm_req_primary_sl_set              (&connection->mad, connection->primary_path.sl);
	ib_cm_req_primary_subnet_local_set    (&connection->mad, 1);
	/* XXX need to include CA ACK delay */
	ib_cm_req_primary_local_ack_timeout_set(&connection->mad,
					   min(31, connection->primary_path.packet_life + 1));

	if (connection->alternate_path.dlid) {
		ib_cm_req_alternate_local_lid_set     (&connection->mad, connection->alternate_path.slid);
		ib_cm_req_alternate_remote_lid_set    (&connection->mad, connection->alternate_path.dlid);

		memcpy(ib_cm_req_alternate_local_gid_get(&connection->mad),
		       connection->alternate_path.sgid,
		       ib_cm_req_alternate_local_gid_get_length());
		memcpy(ib_cm_req_alternate_remote_gid_get(&connection->mad),
		       connection->alternate_path.dgid,
		       ib_cm_req_alternate_remote_gid_get_length());

		ib_cm_req_alternate_flow_label_set      (&connection->mad, connection->alternate_path.flowlabel);
		ib_cm_req_alternate_pkt_rate_set        (&connection->mad, 2);
		ib_cm_req_alternate_traffic_set         (&connection->mad, 0);
		ib_cm_req_alternate_hop_limit_set       (&connection->mad, 0);
		ib_cm_req_alternate_sl_set              (&connection->mad, connection->alternate_path.sl);
		ib_cm_req_alternate_subnet_local_set    (&connection->mad, 1);
		/* XXX need to include CA ACK delay */
		ib_cm_req_alternate_local_ack_timeout_set(&connection->mad,
						     min(31, connection->primary_path.packet_life + 1));
  }

	if (req_private_data) {
		memcpy(ib_cm_req_private_data_get(&connection->mad),
		       req_private_data,
		       req_private_data_len);
	}

	memset(ib_cm_req_private_data_get(&connection->mad) + req_private_data_len,
	       0,
	       ib_cm_req_private_data_get_length() - req_private_data_len);

	connection->mad.device     = connection->local_cm_device;
	connection->mad.port       = connection->local_cm_port;
	connection->mad.pkey_index = connection->local_cm_pkey_index;
	connection->mad.sqpn       = TS_IB_GSI_QP;
	connection->mad.dlid       = connection->remote_cm_lid;
	connection->mad.dqpn       = connection->remote_cm_qpn;

	ib_cm_count_send(&connection->mad);
	ret = ib_mad_send(&connection->mad);
	if (ret) {
		TS_REPORT_WARN(MOD_IB_CM, "REQ send failed. <%d>", ret);
		return -EINVAL;
	}

	connection->state = IB_CM_STATE_REQ_SENT;

	connection->timer.function = ib_cm_connect_timeout;
	tsKernelTimerModify(&connection->timer,
			    jiffies + ib_cm_timeout_to_jiffies(connection->cm_response_timeout));

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_ACTIVE, "Sent REQ");

	return ret;
}

void ib_cm_rtu_done(void *conn_ptr)
{
	tTS_IB_CM_COMM_ID comm_id = (unsigned long) conn_ptr;
	struct ib_cm_connection *connection;
	int result;

	connection = ib_cm_connection_find(comm_id);
	if (!connection)
		return;

	/* Make sure we don't give the consumer two ESTABLISH callbacks. */
	if (!connection->establish_pending)
		goto out;

	connection->establish_pending = 0;

	/*
	  We still issue the callback in state DREQ_RECEIVED, even though
	  the consumer is just about to get a TIME_WAIT callback too.  This
	  makes sure that the consumer always gets an ESTABLISHED callback
	  so the consumer only sees allowed state transitions.
	*/
	if (connection->state != IB_CM_STATE_ESTABLISHED &&
	    connection->state != IB_CM_STATE_DREQ_RECEIVED)
		goto out;

	{
		struct ib_cm_established_param params;
		result = ib_cm_consumer_callback(&connection, TS_IB_CM_ESTABLISHED, &params);
	}

	if (connection) {
		if (result == TS_IB_CM_CALLBACK_ABORT) {
			(void) ib_cm_drop_consumer_internal(connection);
			ib_cm_dreq_send(connection);
		}
	}

 out:
	ib_cm_connection_put(connection);
}

int ib_cm_rtu_send(struct ib_cm_connection *connection)
{
	int result = 0;
	struct ib_qp_attribute *qp_attr;

	if (!connection)
		return -EINVAL;

	qp_attr = kmalloc(sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT), GFP_KERNEL);
	if (!qp_attr)
		return -ENOMEM;

	ib_mad_build_header(&connection->mad);

	connection->mad.attribute_id = cpu_to_be16(IB_COM_MGT_RTU);
	connection->mad.transaction_id = cpu_to_be64(connection->transaction_id);

	ib_cm_rtu_local_comm_id_set (&connection->mad, connection->local_comm_id);
	ib_cm_rtu_remote_comm_id_set(&connection->mad, connection->remote_comm_id);

	connection->mad.device     = connection->local_cm_device;
	connection->mad.port       = connection->local_cm_port;
	connection->mad.pkey_index = connection->local_cm_pkey_index;
	connection->mad.sqpn       = TS_IB_GSI_QP;
	connection->mad.dlid       = connection->remote_cm_lid;
	connection->mad.dqpn       = connection->remote_cm_qpn;

	/* send the packet */
	ib_cm_count_send(&connection->mad);
	result = ib_mad_send(&connection->mad);
	if (result) {
		TS_REPORT_WARN(MOD_IB_CM, "RTU send failed. <%d>", result);
		goto free;
	}

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "Sent RTU");

	/* move connection to established. */
	qp_attr->state             = TS_IB_QP_STATE_RTS;
	qp_attr->send_psn          = connection->send_psn;
	qp_attr->initiator_depth   = connection->initiator_depth;
	qp_attr->retry_count       = connection->retry_count;
	qp_attr->rnr_retry_count   = connection->rnr_retry_count;
	/* XXX need to include CA ACK delay */
	qp_attr->local_ack_timeout = min(31, connection->primary_path.packet_life + 1);

	qp_attr->valid_fields      =
		TS_IB_QP_ATTRIBUTE_STATE           |
		TS_IB_QP_ATTRIBUTE_SEND_PSN        |
		TS_IB_QP_ATTRIBUTE_INITIATOR_DEPTH |
		TS_IB_QP_ATTRIBUTE_RETRY_COUNT     |
		TS_IB_QP_ATTRIBUTE_RNR_RETRY_COUNT |
		TS_IB_QP_ATTRIBUTE_LOCAL_ACK_TIMEOUT;

	if (connection->alternate_path.dlid) {
		qp_attr->valid_fields                |=
			TS_IB_QP_ATTRIBUTE_ALT_PORT       |
			TS_IB_QP_ATTRIBUTE_ALT_ADDRESS    |
			TS_IB_QP_ATTRIBUTE_ALT_PKEY_INDEX |
			TS_IB_QP_ATTRIBUTE_MIGRATION_STATE;
		/* XXX need to include CA ACK delay */
		qp_attr->alt_local_ack_timeout        = min(31, connection->alternate_path.packet_life + 1);
		qp_attr->alt_address.service_level    = connection->alternate_path.sl;
		qp_attr->alt_address.dlid             = connection->alternate_path.dlid;
		qp_attr->alt_address.source_path_bits = connection->alternate_path.slid & 0x7f;
		qp_attr->alt_address.static_rate      = 0;
		qp_attr->alt_address.use_grh          = 0;
		qp_attr->migration_state              = TS_IB_REARM;

		ib_cached_gid_find(connection->alternate_path.sgid, NULL,
				   &qp_attr->alt_port, NULL);
		ib_cached_pkey_find(connection->local_cm_device,
				    qp_attr->alt_port,
				    connection->alternate_path.pkey,
				    &qp_attr->alt_pkey_index);
	}
	result = ib_cm_qp_modify(connection->local_qp, qp_attr);
	if (result) {
		TS_REPORT_WARN(MOD_IB_CM, "ib_qp_modify to RTS failed");
		goto free;
	}

	connection->state             = IB_CM_STATE_ESTABLISHED;
	connection->establish_jiffies = jiffies;

	/*
	  Call back the consumer ASAP, but from a different context.  This
	  lets the consumer hold locks while calling into the CM (since the
	  CM will never call back from the same context as the consumer
	  calls into the CM).
	*/
	/* XXX - move to CM's work queue in 2.6 */
	connection->establish_pending = 1;
	INIT_WORK(&connection->work,
		  ib_cm_rtu_done,
		  (void *) (unsigned long) connection->local_comm_id);
	schedule_work(&connection->work);

 free:
	kfree(qp_attr);
	return result;
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager state callbacks.                                */
/*                                                                       */
/* --------------------------------------------------------------------- */

void ib_cm_rep_handler(struct ib_mad *packet)
{
	struct ib_cm_connection *connection;
	struct ib_qp_attribute *qp_attr;
	tTS_IB_CM_REJ_REASON rej_reason;
	int result;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_ACTIVE,
		 "REP received");

	connection = ib_cm_connection_find(ib_cm_rep_remote_comm_id_get(packet));

	if (!connection) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "REP for unknown comm id 0x%08x",
			 ib_cm_rep_remote_comm_id_get(packet));
		rej_reason = TS_IB_REJ_INVALID_COMM_ID;
		goto reject;
	}

	if (connection->state == IB_CM_STATE_MRA_REP_SENT) {
		/* Resend MRA */
		ib_cm_count_resend(&connection->mad);
		result = ib_mad_send(&connection->mad);
		if (result) {
			TS_REPORT_WARN(MOD_IB_CM, "MRA resend failed. <%d>", result);
			goto out;
		}
	}

	if (connection->state == IB_CM_STATE_ESTABLISHED) {
		/* Resend RTU if connection is established, but make sure we
		   haven't already sent some other kind of CM packet. */
		if (connection->mad.attribute_id == cpu_to_be16(IB_COM_MGT_RTU)) {
			ib_cm_count_resend(&connection->mad);
			result = ib_mad_send(&connection->mad);
			if (result) {
				TS_REPORT_WARN(MOD_IB_CM, "RTU resend failed. <%d>", result);
			}
			goto out;
		}
	}

	if (connection->state != IB_CM_STATE_REQ_SENT) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
			 "Ignoring REP for connection 0x%08x in state %d",
			 connection->local_comm_id,
			 connection->state);
		goto out;
	}

	qp_attr = kmalloc(sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT), GFP_KERNEL);
	if (!qp_attr) {
		rej_reason = TS_IB_REJ_NO_RESOURCES;
		goto reject;
	}

	tsKernelTimerRemove(&connection->timer);

	connection->state = IB_CM_STATE_REP_RECEIVED;

	connection->remote_qpn     = ib_cm_rep_local_qpn_get(packet);
	connection->remote_comm_id = ib_cm_rep_local_comm_id_get(packet);
	connection->send_psn       = ib_cm_rep_starting_psn_get(packet);

	memset(qp_attr, 0, sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT));

	qp_attr->state                    = TS_IB_QP_STATE_RTR;
	qp_attr->receive_psn              = connection->receive_psn;
	qp_attr->destination_qpn          = connection->remote_qpn;
	qp_attr->responder_resources      = connection->responder_resources;
	qp_attr->rnr_timeout              = TS_IB_RNR_TIMER_122_88; /* XXX settable? */
	qp_attr->path_mtu                 = connection->primary_path.mtu;
	qp_attr->address.service_level    = connection->primary_path.sl;
	qp_attr->address.dlid             = connection->primary_path.dlid;
	qp_attr->address.source_path_bits = connection->primary_path.slid & 0x7f;
	qp_attr->address.static_rate      = 0;
	qp_attr->address.use_grh          = 0;

	qp_attr->valid_fields =
		TS_IB_QP_ATTRIBUTE_STATE               |
		TS_IB_QP_ATTRIBUTE_RECEIVE_PSN         |
		TS_IB_QP_ATTRIBUTE_DESTINATION_QPN     |
		TS_IB_QP_ATTRIBUTE_RESPONDER_RESOURCES |
		TS_IB_QP_ATTRIBUTE_RNR_TIMEOUT         |
		TS_IB_QP_ATTRIBUTE_PATH_MTU            |
		TS_IB_QP_ATTRIBUTE_ADDRESS;

	result = ib_cm_qp_modify(connection->local_qp, qp_attr);
	if (result) {
		TS_REPORT_WARN(MOD_IB_CM, "ib_qp_modify to RTR failed. <%d>", result);
		kfree(qp_attr);

		rej_reason = TS_IB_REJ_NO_QP;
		goto reject;
	}

	kfree(qp_attr);

	{
		struct ib_cm_rep_received_param params = { 0 };

		params.remote_qpn              = connection->remote_qpn;
		params.local_qpn               = connection->local_qpn;
		params.remote_private_data     = ib_cm_rep_private_data_get(packet);
		params.remote_private_data_len = ib_cm_rep_private_data_get_length();

		result = ib_cm_consumer_callback(&connection, TS_IB_CM_REP_RECEIVED, &params);
		if (!connection)
			return;
	}

	if (result == TS_IB_CM_CALLBACK_DEFER) {
		/*
		  don't send any responses. Should be a timer started, to make
		  sure this connection response dosn't hang... Only if the
		  connection has't already completed...
		*/
		goto out;
	}

	if (result == TS_IB_CM_CALLBACK_ABORT) {
		(void) ib_cm_drop_consumer_internal(connection);
		ib_cm_qp_to_error(connection->local_qp);

		rej_reason = TS_IB_REJ_CONSUMER_REJECT;
		goto reject;
	}

	result = ib_cm_rtu_send(connection);
	if (result)
		TS_REPORT_WARN(MOD_IB_CM, "RTU send failed. <%d>", result);

 out:
	ib_cm_connection_put(connection);
	return;

 reject:
	result = ib_cm_rej_send(packet->device,
				packet->port,
				packet->pkey_index,
				packet->slid,
				packet->sqpn,
				be64_to_cpu(packet->transaction_id),
				ib_cm_rep_remote_comm_id_get(packet),
				ib_cm_rep_local_comm_id_get(packet),
				IB_REJ_REP,
				rej_reason,
				NULL,
				0);

	if (result) {
		TS_REPORT_WARN(MOD_IB_CM, "REJ send failed. <%d>", result);
	} else {
		TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_ACTIVE,
			 "Sent REJ (reason = %d)", rej_reason);
	}

	if (connection) {
		struct ib_cm_idle_param params = { 0 };

		params.reason     = TS_IB_CM_IDLE_LOCAL_REJECT;
		params.rej_reason = rej_reason;

		connection->state = IB_CM_STATE_IDLE;

		(void) ib_cm_consumer_free_callback(&connection, TS_IB_CM_IDLE, &params);
	}
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
