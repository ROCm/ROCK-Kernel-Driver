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

  $Id: cm_passive.c 32 2004-04-09 03:57:42Z roland $
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
#include <linux/sched.h>
#include <asm/byteorder.h>
#else
#include <os_dep/win/linux/string.h>
#endif

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager packet sending.                                 */
/*                                                                       */
/* --------------------------------------------------------------------- */

static int ib_cm_rep_send(struct ib_cm_connection *connection,
			  void *reply_data, /* private data */
			  int   reply_size  /* private size */)
{
	int    result = 0;

	if (!connection || IB_CM_REJ_MAX_PRIVATE_DATA < reply_size)
		return -EINVAL;

	ib_mad_build_header(&connection->mad);

	if (reply_data && reply_size > 0)
		memcpy(ib_cm_rep_private_data_get(&connection->mad), reply_data, reply_size);

	connection->mad.attribute_id   = cpu_to_be16(IB_COM_MGT_REP);
	connection->mad.transaction_id = cpu_to_be64(connection->transaction_id);

	ib_cm_rep_local_comm_id_set     (&connection->mad, connection->local_comm_id);
	ib_cm_rep_remote_comm_id_set    (&connection->mad, connection->remote_comm_id);
	ib_cm_rep_local_qpn_set        (&connection->mad, connection->local_qpn);
	ib_cm_rep_starting_psn_set     (&connection->mad, connection->receive_psn);
	if (connection->alternate_path.dlid == 0) {
		/* failover rejected */
  		ib_cm_rep_failover_accepted_set(&connection->mad, 1);
	} else {
		/* failover accepted */
		ib_cm_rep_failover_accepted_set(&connection->mad, 0);
	}

	ib_cm_rep_target_max_set       (&connection->mad, connection->responder_resources);
	ib_cm_rep_initiator_max_set    (&connection->mad, connection->initiator_depth);

	/* XXX what should we fill in for these fields ??? */
	ib_cm_rep_target_ack_delay_set  (&connection->mad, 14);
	ib_cm_rep_end_to_end_fc_set      (&connection->mad, 1);
	ib_cm_rep_rnr_retry_count_set   (&connection->mad, 7);

	if (ib_cached_node_guid_get(connection->local_cm_device,
				    ib_cm_rep_local_guid_get(&connection->mad))) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "ib_cached_node_guid_get failed");
	}

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
		TS_REPORT_WARN(MOD_IB_CM, "REP send failed");
	}

	if (connection->state == IB_CM_STATE_REQ_RECEIVED ||
	    connection->state == IB_CM_STATE_MRA_SENT) {
		connection->state = IB_CM_STATE_REP_SENT;
	}

	connection->timer.function = ib_cm_connect_timeout;
	tsKernelTimerModify(&connection->timer,
			    jiffies +
			    ib_cm_timeout_to_jiffies(connection->cm_response_timeout));

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_ACTIVE, "Sent REP");

	return result;
}

int ib_cm_req_qp_setup(struct ib_cm_connection *connection,
		       void                    *response_data,
		       int                      response_size)
{
	struct ib_qp_attribute *qp_attr;
	int result;

	qp_attr = kmalloc(sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT), GFP_KERNEL);
	if (NULL == qp_attr) {
		return -ENOMEM;
	}

	memset(qp_attr, 0, sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT));

	qp_attr->port = connection->local_cm_port;
	if (ib_cached_gid_find(connection->primary_path.sgid, NULL, &qp_attr->port, NULL)) {
		qp_attr->port = connection->local_cm_port;
	}

	if (ib_cached_pkey_find(connection->local_cm_device,
			       qp_attr->port,
			       connection->primary_path.pkey,
			       &qp_attr->pkey_index)) {
		goto fail;
	}
	connection->local_cm_pkey_index = qp_attr->pkey_index;

	qp_attr->valid_fields =
		TS_IB_QP_ATTRIBUTE_PORT |
		TS_IB_QP_ATTRIBUTE_PKEY_INDEX;

	if (ib_cm_qp_modify(connection->local_qp, qp_attr)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "ib_qp_modify INIT->INIT failed");
		goto fail;
	}

	/* modify QP INIT->RTR */
	connection->receive_psn = ib_cm_psn_generate();

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

	if (ib_cm_qp_modify(connection->local_qp, qp_attr)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "ib_qp_modify to RTR failed");
		goto fail;
	}

	result = ib_cm_rep_send(connection, response_data, response_size);
	if (result) {
		TS_REPORT_WARN(MOD_IB_CM, "REP send failed. <%d>", result);
	}

	kfree(qp_attr);
	return 0;

 fail:
	ib_cm_qp_to_error(connection->local_qp);
	kfree(qp_attr);
	return -EINVAL;
}

void ib_cm_delayed_establish(void *est_ptr)
{
	struct ib_cm_delayed_establish *est = est_ptr;
	struct ib_cm_connection        *connection;
	int                             result;

	connection = ib_cm_connection_find(est->comm_id);
	if (!connection) {
		return;
	}

	if (connection->state == IB_CM_STATE_REP_SENT) {
		result = ib_cm_passive_rts(connection);

		if (result) {
			TS_REPORT_WARN(MOD_IB_CM, "ib_qp_modify to RTS failed");
			goto out_put;
		}

		{
			struct ib_cm_established_param params;

			result = ib_cm_consumer_callback(&connection,
							 IB_CM_STATE_ESTABLISHED,
							 &params);
		}

		if (!connection) {
			goto out;
		}

		if (result == TS_IB_CM_CALLBACK_ABORT) {
			(void) ib_cm_drop_consumer_internal(connection);
			ib_cm_dreq_send(connection);
			goto out_put;
		}
	}

 out_put:
	ib_cm_connection_put(connection);

 out:
	kfree(est_ptr);
}

/* =============================================================== */
/*..ib_cm_passive_rts - Transition a passive connection to RTS      */
int ib_cm_passive_rts(
	struct ib_cm_connection *connection
	) {
	struct ib_qp_attribute *qp_attr;
	int                     result;

	qp_attr = kmalloc(sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT), GFP_KERNEL);
	if (!qp_attr) {
		return -ENOMEM;
	}

	memset(qp_attr, 0, sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT));

	qp_attr->state             = TS_IB_QP_STATE_RTS;
	qp_attr->send_psn          = connection->send_psn;
	qp_attr->initiator_depth   = connection->initiator_depth;
	qp_attr->retry_count       = connection->retry_count;
	qp_attr->rnr_retry_count   = connection->rnr_retry_count;
	/* We abuse packet life and put local ACK timeout there */
	qp_attr->local_ack_timeout = connection->primary_path.packet_life;

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
		/* We abuse packet life and put local ACK timeout there */
		qp_attr->alt_local_ack_timeout        = connection->alternate_path.packet_life;
		qp_attr->alt_address.service_level    = connection->alternate_path.sl;
		qp_attr->alt_address.dlid             = connection->alternate_path.dlid;
		qp_attr->alt_address.source_path_bits = connection->alternate_path.slid & 0x7f;
		qp_attr->alt_address.static_rate      = 0;
		qp_attr->alt_address.use_grh          = 0;
		qp_attr->migration_state              = TS_IB_REARM;

		ib_cached_gid_find(connection->alternate_path.sgid, NULL, &qp_attr->alt_port, NULL);
		/* XXX check return value: */
		ib_cached_pkey_find(connection->local_cm_device,
				    qp_attr->alt_port,
				    connection->alternate_path.pkey,
				    &qp_attr->alt_pkey_index);
	}

	result = ib_cm_qp_modify(connection->local_qp, qp_attr);
	kfree(qp_attr);

	tsKernelTimerRemove(&connection->timer);
	connection->state             = IB_CM_STATE_ESTABLISHED;
	connection->establish_jiffies = jiffies;

	return result;
}

int ib_cm_passive_param_store(struct ib_cm_connection    *connection,
			      struct ib_cm_passive_param *param)
{
	int result;

	connection->local_qp = param->qp;

	result = ib_qp_query_qpn(param->qp, &connection->local_qpn);
	if (result) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "ib_qp_query_qpn failed (return %d)",
			       result);
	}

	return result;
}

static void ib_cm_service_store(struct ib_cm_service    *service,
				struct ib_cm_connection *connection)
{
	connection->cm_function = service->cm_function;
	connection->cm_arg      = service->cm_arg;
}

static void ib_cm_req_store(struct ib_mad           *packet,
			    struct ib_cm_connection *connection)
{
	connection->remote_comm_id      = ib_cm_req_local_comm_id_get(packet);
	connection->remote_qpn          = ib_cm_req_local_qpn_get(packet);
	connection->cm_response_timeout = ib_cm_req_remote_cm_timeout_get(packet);
	connection->send_psn            = ib_cm_req_starting_psn_get(packet);
	connection->max_cm_retries      = ib_cm_req_max_cm_retries_get(packet);
	connection->initiator_depth     = ib_cm_req_target_max_get(packet);
	connection->responder_resources = ib_cm_req_initiator_max_get(packet);
	connection->retry_count         = ib_cm_req_retry_count_get(packet);
	connection->rnr_retry_count     = ib_cm_req_rnr_retry_count_get(packet);

	/* path information */
	connection->primary_path.pkey   = ib_cm_req_pkey_get(packet);
	connection->primary_path.dlid   = ib_cm_req_primary_local_lid_get(packet);
	connection->primary_path.slid   = ib_cm_req_primary_remote_lid_get(packet);
	connection->primary_path.sl     = ib_cm_req_primary_sl_get(packet);
	memcpy(connection->primary_path.sgid,
	       ib_cm_req_primary_remote_gid_get(packet),
	       sizeof (tTS_IB_GID));
	memcpy(connection->primary_path.dgid,
	       ib_cm_req_primary_local_gid_get(packet),
	       sizeof (tTS_IB_GID));
	/* We abuse packet life and put local ACK timeout there */
	connection->primary_path.packet_life = ib_cm_req_primary_local_ack_timeout_get(packet);
	connection->primary_path.mtu         = ib_cm_req_path_mtu_get(packet);

	connection->alternate_path.pkey      = ib_cm_req_pkey_get(packet);
	connection->alternate_path.dlid      = ib_cm_req_alternate_local_lid_get(packet);
	connection->alternate_path.slid      = ib_cm_req_alternate_remote_lid_get(packet);
	connection->alternate_path.sl        = ib_cm_req_alternate_sl_get(packet);
	memcpy(connection->alternate_path.sgid,
	       ib_cm_req_alternate_remote_gid_get(packet),
	       sizeof (tTS_IB_GID));
	memcpy(connection->alternate_path.dgid,
	       ib_cm_req_alternate_local_gid_get(packet),
	       sizeof (tTS_IB_GID));
	/* We abuse packet life and put local ACK timeout there */
	connection->alternate_path.packet_life = ib_cm_req_alternate_local_ack_timeout_get(packet);
	connection->alternate_path.mtu         = ib_cm_req_path_mtu_get(packet);
	connection->alternate_remote_cm_lid    = connection->alternate_path.dlid;
	connection->local_cm_pkey_index        = packet->pkey_index;

	/* information from header */
	connection->local_cm_device          = packet->device;
	connection->local_cm_port            = packet->port;
	connection->remote_cm_lid            = packet->slid;
	connection->remote_cm_qpn            = packet->sqpn;
	connection->state                    = IB_CM_STATE_REQ_RECEIVED;
	connection->cm_retry_count           = 0;

	connection->transaction_id           = be64_to_cpu(packet->transaction_id);

	ib_cm_connection_insert_remote(connection);
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager state callbacks.                                */
/*                                                                       */
/* --------------------------------------------------------------------- */

static void ib_cm_req_reject(struct ib_mad *packet,
                             struct ib_cm_connection *connection,
                             uint16_t rej_reason,
                             char *response_data)
{
	int result;

	result = ib_cm_rej_send(packet->device,
				packet->port,
				packet->pkey_index,
				packet->slid,
				packet->sqpn,
				be64_to_cpu(packet->transaction_id),
				0,
				ib_cm_req_local_comm_id_get(packet),
				IB_REJ_REQ,
				rej_reason,
				response_data,
				IB_CM_REJ_MAX_PRIVATE_DATA);
	if (result) {
		TS_REPORT_WARN(MOD_IB_CM, "REJ send failed. <%d>", result);
	} else {
		TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_PASSIVE,
			 "REJ sent (reason = %d)", rej_reason);
	}

	if (rej_reason == TS_IB_REJ_STALE_CONNECTION) {
		if (connection->state == IB_CM_STATE_ESTABLISHED) {
			/* According to 12.9.8.3.1 we should send a DREQ after sending the REJ */
			result = ib_cm_dreq_send(connection);
			if (result) {
				TS_REPORT_WARN(MOD_IB_CM, "DREQ send failed. <%d>", result);
			}

			ib_cm_timewait(&connection, TS_IB_CM_DISCONNECTED_STALE_CONNECTION);
		}

		ib_cm_connection_put(connection);

		/* Timer will clean up the connection */
		return;
	}

	if (connection) {
		struct ib_cm_idle_param params = { 0 };

		params.reason     = TS_IB_CM_IDLE_LOCAL_REJECT;
		params.rej_reason = rej_reason;

		connection->state = IB_CM_STATE_IDLE;

		(void) ib_cm_consumer_free_callback(&connection, TS_IB_CM_IDLE, &params);
	}
}

static void ib_cm_req_existing(struct ib_mad *packet,
                               struct ib_cm_connection *connection)
{
	switch (connection->state) {
	case IB_CM_STATE_REQ_RECEIVED:
		/* We'll get around to sending a reply (REP or REJ) */
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
			 "Ignoring REQ for connection 0x%08x in state %d",
			 connection->local_comm_id,
			 connection->state);
		break;

	case IB_CM_STATE_MRA_SENT:
		/* resend the MRA we sent last time */
		ib_cm_count_resend(&connection->mad);
		if (ib_mad_send(&connection->mad)) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "MRA resend failed");
		}
		break;

	case IB_CM_STATE_REP_SENT:
		/* resend the REP we sent last time */
		ib_cm_count_resend(&connection->mad);
		if (ib_mad_send(&connection->mad)) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "REP resend failed");
		}
		break;

	case IB_CM_STATE_DREQ_RECEIVED:
	case IB_CM_STATE_DREQ_SENT:
	case IB_CM_STATE_TIME_WAIT:
		/*
		  If we're in DREQ send/recv or time wait, just ignore this REQ and
		  hope it's retransmitted.
		*/
		break;

	case IB_CM_STATE_ESTABLISHED:
		/* If the comm ID matches, the last packet we sent was a REP and
		   it hasn't been too long since we entered the established state,
		   then treat this REQ as a resend.  Otherwise our connection is a
		   stale connection.  (See section 12.9.8.3.1 of the IB spec) */
		if (ib_cm_req_local_comm_id_get(packet) == connection->remote_comm_id &&
		    be16_to_cpu(connection->mad.attribute_id) == IB_COM_MGT_REP &&
		    time_after(connection->establish_jiffies +
			       ib_cm_timeout_to_jiffies(connection->cm_response_timeout),
			       jiffies)) {
			/* resend the REP we sent last time */
			ib_cm_count_resend(&connection->mad);
			if (ib_mad_send(&connection->mad)) {
				TS_REPORT_WARN(MOD_IB_CM,
					       "REP resend failed");
			}
			break;
		} else {
			/* fall through and reject stale connection */
		}

	default:
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
			 "Rejecting REQ for stale connection 0x%08x",
			 connection->local_comm_id);
		ib_cm_req_reject(packet, connection, TS_IB_REJ_STALE_CONNECTION, NULL);
		break;
	}

	ib_cm_connection_put(connection);
	return;
}

void ib_cm_req_handler(struct ib_mad *packet)
{
	uint64_t                 service_id;
	struct ib_cm_service    *service;
	struct ib_cm_connection *connection = 0;
	char                	  *response_data;
	int                 	   response_size;
	int                 	   result;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_PASSIVE,
		 "REQ received");

	response_size = min(IB_CM_REP_MAX_PRIVATE_DATA,
			    IB_CM_REJ_MAX_PRIVATE_DATA);
	response_data = kmalloc(response_size, GFP_KERNEL);
	if (!response_data) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "No memory for response_data");
		return;
	}

	if (ib_cm_req_transport_service_type_get(packet) !=
	    TS_IB_TRANSPORT_RC) {
		ib_cm_req_reject(packet, NULL, TS_IB_REJ_UNSUPPORTED, NULL);
		goto free_data;
	}

	connection = ib_cm_connection_find_remote_qp(ib_cm_req_primary_local_gid_get(packet),
						     ib_cm_req_local_qpn_get(packet));
	if (connection) {
		ib_cm_req_existing(packet, connection);
		goto free_data;
	}

	memcpy(&service_id, ib_cm_req_service_id_get(packet), ib_cm_req_service_id_get_length());
	cpu_to_be64s(&service_id);
	service = ib_cm_service_find(service_id);

	if (!service) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
			 "Connection attempted; "
			 "invalid service %016Lx", service_id);

		ib_cm_req_reject(packet, NULL, TS_IB_REJ_INVALID_SERVICE_ID, NULL);
		goto free_data;
	}

	connection = NULL;

	/*
	  If this is a peer-to-peer connection (ie have we already sent a
	  REQ for the same service), then we have to go through the "Peer
	  Compare" state as described in IB Spec 1.1 sections 12.9 and
	  12.10.
	*/
	if (service->peer_to_peer_comm_id != TS_IB_CM_COMM_ID_INVALID) {
		struct ib_cm_connection *p2p_conn = ib_cm_connection_find(service->peer_to_peer_comm_id);

		if (p2p_conn) {
			uint64_t local_source_guid;
			uint64_t remote_source_guid;

			if (ib_cached_node_guid_get(p2p_conn->local_cm_device,
						    (uint8_t *) &local_source_guid)) {
				TS_REPORT_WARN(MOD_IB_CM,
					       "ib_cached_node_guid_get failed");
			}

			memcpy((uint8_t *) &remote_source_guid,
			       ib_cm_req_local_guid_get(packet),
			       ib_cm_req_local_guid_get_length());

			/* XXX should make sure the REQ came from the same endpoint that
			   we think we're talking to */

			if (be64_to_cpu(local_source_guid) > be64_to_cpu(remote_source_guid) ||
			    (local_source_guid == remote_source_guid &&
			     p2p_conn->local_qpn > ib_cm_req_local_qpn_get(packet))) {
				/* We win election and stay in REQ sent state */
				ib_cm_connection_put(p2p_conn);
				goto unlock_service;
			} else {
				connection = p2p_conn;
				connection->active = 0;
				/* Get rid of our REP wait timeout */
				tsKernelTimerRemove(&connection->timer);
			}
		}
	}

	if (!connection) {
		/* not peer-to-peer, just get a new connection struct */
		connection = ib_cm_connection_new();
	}

	if (!connection) {
		ib_cm_req_reject(packet, NULL, TS_IB_REJ_NO_RESOURCES, NULL);
		goto unlock_service;
	}

	connection->active = 0;
	ib_cm_service_store(service, connection);
	ib_cm_req_store    (packet,  connection);

	/* Now call back the listen completion handler.  This allows eg the
	   user to guarantee that a receive is posted before the other side
	   posts a send (which could happen before the RTU is processed on
	   this side) */
	{
		struct ib_cm_req_received_param params = { { 0 } };

		params.listen_handle             = service;
		params.service_id                = service_id;
		params.remote_qpn                = connection->remote_qpn;
		params.local_qpn                 = connection->local_qpn;
		params.dlid                      = connection->primary_path.dlid;
		params.slid                      = connection->primary_path.slid;
		params.remote_private_data       = ib_cm_req_private_data_get(packet);
		params.remote_private_data_len   = ib_cm_req_private_data_get_length();
		params.accept_param.reply_private_data     = (void *) response_data;
		params.accept_param.reply_private_data_len = response_size;

		if (ib_cached_gid_find(connection->primary_path.sgid, &params.device, &params.port, NULL)) {
			params.port = packet->port;
		}

		memcpy(params.dgid,
		       ib_cm_req_primary_local_gid_get(packet),
		       sizeof (tTS_IB_GID));
		memcpy(params.sgid,
		       ib_cm_req_primary_remote_gid_get(packet),
		       sizeof (tTS_IB_GID));
		memcpy(params.remote_guid, ib_cm_req_local_guid_get(packet),
		       ib_cm_req_local_guid_get_length());
		memset(response_data, 0, response_size);

		connection->state = IB_CM_STATE_REQ_RECEIVED;

		result = ib_cm_consumer_callback(&connection, TS_IB_CM_REQ_RECEIVED, &params);

		if (!connection) {
			goto unlock_service;
		}

		if (result == TS_IB_CM_CALLBACK_DEFER) {
			/*
			  don't send any responses. Should be a timer started, to make
			  sure this connection response dosn't hang... Only if the
			  connection has't already completed...
			*/
			ib_cm_connection_put(connection);
			goto unlock_service;
		}

		if (result == TS_IB_CM_CALLBACK_ABORT) {
			ib_cm_drop_consumer_internal(connection);

			if (connection->local_qp != TS_IB_HANDLE_INVALID) {
				ib_cm_qp_to_error(connection->local_qp);
			}

			ib_cm_req_reject(packet, connection, TS_IB_REJ_CONSUMER_REJECT, response_data);
			goto unlock_service;
		} /* if*/

		result = ib_cm_passive_param_store(connection, &params.accept_param);
		if (result) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "ib_cm_passive_param_store failed for QP handle");
		}

		response_size = params.accept_param.reply_private_data_len;
	}

	if (ib_cm_req_qp_setup(connection, response_data, response_size)) {
		ib_cm_connection_free(connection);
		ib_cm_req_reject(packet, NULL, TS_IB_REJ_NO_QP, NULL);
		goto unlock_service;
	}

	ib_cm_connection_put(connection);

 unlock_service:
	ib_cm_service_put(service);

 free_data:
	kfree(response_data);
}

static int ib_cm_rtu_check(struct ib_mad *packet,
                           struct ib_cm_connection *connection)
{
	if (connection->remote_comm_id != ib_cm_rtu_local_comm_id_get(packet)) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
			 "RTU comm id mismatch, rcvd:0x%08x != stored:0x%08x",
			 connection->local_comm_id, ib_cm_rtu_remote_comm_id_get(packet));
		return 1;
	}

	if (connection->state == IB_CM_STATE_ESTABLISHED) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
			 "RTU for already established connection 0x%08x",
			 connection->local_comm_id);
		return 1;
	}

	if (connection->state != IB_CM_STATE_REP_SENT) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
			 "Ignoring RTU for connection 0x%08x in state %d",
			 connection->local_comm_id,
			 connection->state);
		return 1;
	}

	return 0;
}

void ib_cm_rtu_handler(struct ib_mad *packet)
{
	struct ib_cm_connection *connection;
	int result;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_PASSIVE,
		 "RTU received");

	connection = ib_cm_connection_find(ib_cm_rtu_remote_comm_id_get(packet));
	if (!connection) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
			 "RTU for unknown comm id 0x%08x",
			 ib_cm_rtu_local_comm_id_get(packet));
		return;
	}

	if (ib_cm_rtu_check(packet, connection)) {
		ib_cm_connection_put(connection);
		return;
	}

	if (ib_cm_passive_rts(connection)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "ib_qp_modify to RTS failed");
	}

	{
		struct ib_cm_established_param params;

		result = ib_cm_consumer_callback(&connection, TS_IB_CM_ESTABLISHED, &params);
	}

	if (connection) {
		if (result == TS_IB_CM_CALLBACK_ABORT) {
			ib_cm_drop_consumer_internal(connection);
			ib_cm_dreq_send(connection);
		}

		ib_cm_connection_put(connection);
	}

	return;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
