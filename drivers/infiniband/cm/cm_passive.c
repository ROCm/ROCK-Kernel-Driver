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

  $Id: cm_passive.c,v 1.18 2004/02/25 00:35:11 roland Exp $
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
#include <linux/sched.h>        /* for NR_CPUS, etc for interrupt.h */
#include <asm/byteorder.h>
#else
#include <os_dep/win/linux/string.h>
void tsIbCmRepLocalGuidSet(tTS_IB_MAD packet, uint64_t value);
#endif

/* XXX this #if must be removed */
#ifdef W2K_OS
#define time_after(a,b)    ((long)(b) - (long)(a) < 0)
#endif

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager packet sending.                                 */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ===================================================================== */
/*.._tsIbCmRepSend -- send a REP MAD for a connection */
static int _tsIbCmRepSend
(
 tTS_IB_CM_CONNECTION connection,
 void *reply_data, /* private data */
 int   reply_size  /* private size */
)
{
  int    result = 0;

  if (NULL == connection ||
      TS_IB_CM_REJ_MAX_PRIVATE_DATA < reply_size) {

    return -EINVAL;
  } /* if */

  tsIbMadBuildHeader(&connection->mad);
  /*
   * copy private data
   */
  if (NULL != reply_data &&
      0 < reply_size) {

    memcpy(tsIbCmRepPrivateDataGet(&connection->mad), reply_data, reply_size);
  } /* if */

  connection->mad.attribute_id   = cpu_to_be16(TS_IB_COM_MGT_REP);
  connection->mad.transaction_id = cpu_to_be64(connection->transaction_id);

  tsIbCmRepLocalCommIdSet     (&connection->mad, connection->local_comm_id);
  tsIbCmRepRemoteCommIdSet    (&connection->mad, connection->remote_comm_id);
  tsIbCmRepLocalQpnSet        (&connection->mad, connection->local_qpn);
  tsIbCmRepStartingPsnSet     (&connection->mad, connection->receive_psn);
  if (connection->alternate_path.dlid == 0) {
    /* failover rejected */
    tsIbCmRepFailoverAcceptedSet(&connection->mad, 1);
  } else {
    /* failover accepted */
    tsIbCmRepFailoverAcceptedSet(&connection->mad, 0);
  }

  tsIbCmRepTargetMaxSet       (&connection->mad, connection->responder_resources);
  tsIbCmRepInitiatorMaxSet    (&connection->mad, connection->initiator_depth);

  /* XXX what should we fill in for these fields ??? */
  tsIbCmRepTargetAckDelaySet  (&connection->mad, 14);
  tsIbCmRepEndToEndFcSet      (&connection->mad, 1);
  tsIbCmRepRnrRetryCountSet   (&connection->mad, 7);

  if (tsIbCachedNodeGuidGet(connection->local_cm_device,
                            tsIbCmRepLocalGuidGet(&connection->mad))) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbCachedNodeGuidGet failed");
  }

  connection->mad.device     = connection->local_cm_device;
  connection->mad.port       = connection->local_cm_port;
  connection->mad.pkey_index = connection->local_cm_pkey_index;
  connection->mad.sqpn       = TS_IB_GSI_QP;
  connection->mad.dlid       = connection->remote_cm_lid;
  connection->mad.dqpn       = connection->remote_cm_qpn;

  /* send the packet */
  tsIbCmCountSend(&connection->mad);
  result = tsIbMadSend(&connection->mad);
  if (result) {
    TS_REPORT_WARN(MOD_IB_CM, "REP send failed");
  }

  if (connection->state == TS_IB_CM_STATE_REQ_RECEIVED ||
      connection->state == TS_IB_CM_STATE_MRA_SENT) {
    connection->state = TS_IB_CM_STATE_REP_SENT;
  }

  connection->timer.function = tsIbCmConnectTimeout;
  tsKernelTimerModify(&connection->timer,
                      jiffies + tsIbCmTimeoutToJiffies(connection->cm_response_timeout));

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_ACTIVE, "Sent REP");

  return result;
} /* _tsIbCmRepSend */

/* ===================================================================== */
/*..tsIbCmReqQpSetup -- set up a QP after a REQ                          */
int tsIbCmReqQpSetup(
 tTS_IB_CM_CONNECTION connection,
 void                *response_data,
 int                  response_size
) {
  tTS_IB_QP_ATTRIBUTE qp_attr;
  int result;

  qp_attr = kmalloc(sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT), GFP_KERNEL);
  if (NULL == qp_attr) {
    return -ENOMEM;
  }

  memset(qp_attr, 0, sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT));

  qp_attr->port = connection->local_cm_port;
  if (tsIbCachedGidFind(connection->primary_path.sgid, NULL, &qp_attr->port, NULL)) {
    qp_attr->port = connection->local_cm_port;
  }

  if (tsIbCachedPkeyFind(connection->local_cm_device,
                         qp_attr->port,
                         connection->primary_path.pkey,
                         &qp_attr->pkey_index)) {
    goto fail;
  }
  connection->local_cm_pkey_index = qp_attr->pkey_index;

  qp_attr->valid_fields =
    TS_IB_QP_ATTRIBUTE_PORT |
    TS_IB_QP_ATTRIBUTE_PKEY_INDEX;

  if (tsIbCmQpModify(connection->local_qp, qp_attr)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbQpModify INIT->INIT failed");
    goto fail;
  }

  /* modify QP INIT->RTR */
  connection->receive_psn = tsIbCmPsnGenerate();

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

  if (tsIbCmQpModify(connection->local_qp, qp_attr)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbQpModify to RTR failed");
    goto fail;
  }

  result = _tsIbCmRepSend(connection, response_data, response_size);
  if (result) {
    TS_REPORT_WARN(MOD_IB_CM, "REP send failed. <%d>", result);
  }

  kfree(qp_attr);
  return 0;

 fail:
  tsIbCmQpToError(connection->local_qp);
  kfree(qp_attr);
  return -EINVAL;
}

void tsIbCmDelayedEstablish(
                            void *est_ptr
                            ) {
  struct tTS_IB_CM_DELAYED_ESTABLISH_STRUCT *est = est_ptr;
  tTS_IB_CM_CONNECTION   connection;
  int                    result;

  connection = tsIbCmConnectionFind(est->comm_id);
  if (!connection) {
    return;
  }

  if (connection->state == TS_IB_CM_STATE_REP_SENT) {
    result = tsIbCmPassiveRts(connection);

    if (result) {
      TS_REPORT_WARN(MOD_IB_CM, "tsIbQpModify to RTS failed");
      goto out_put;
    }

    {
      tTS_IB_CM_ESTABLISHED_PARAM_STRUCT params;

      result = tsIbCmConsumerCallback(&connection, TS_IB_CM_STATE_ESTABLISHED, &params);
    }

    if (!connection) {
      goto out;
    }

    if (result == TS_IB_CM_CALLBACK_ABORT) {
      (void) tsIbCmDropConsumerInternal(connection);
      tsIbCmDreqSend(connection);
      goto out_put;
    }
  }

 out_put:
  tsIbCmConnectionPut(connection);

 out:
  kfree(est_ptr);
}

/* =============================================================== */
/*..tsIbCmPassiveRts - Transition a passive connection to RTS      */
int tsIbCmPassiveRts(
                     tTS_IB_CM_CONNECTION connection
                     ) {
  tTS_IB_QP_ATTRIBUTE qp_attr;
  int                 result;

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

    tsIbCachedGidFind(connection->alternate_path.sgid, NULL, &qp_attr->alt_port, NULL);
    /* XXX check return value: */
    tsIbCachedPkeyFind(connection->local_cm_device,
                       qp_attr->alt_port,
                       connection->alternate_path.pkey,
                       &qp_attr->alt_pkey_index);
  }

  result = tsIbCmQpModify(connection->local_qp, qp_attr);
  kfree(qp_attr);

  tsKernelTimerRemove(&connection->timer);
  connection->state             = TS_IB_CM_STATE_ESTABLISHED;
  connection->establish_jiffies = jiffies;

  return result;
}

/* =============================================================== */
/*..                                                               */
int tsIbCmPassiveParamStore(
                            tTS_IB_CM_CONNECTION    connection,
                            tTS_IB_CM_PASSIVE_PARAM param
                            ) {
  int result;

  connection->local_qp = param->qp;

  result = tsIbQpQueryQpn(param->qp, &connection->local_qpn);
  if (result) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbQpQueryQpn failed (return %d)",
                   result);
  }

  return result;
}

/* =============================================================== */
/*.._tsIbCmServiceStore - Store info from service into connection  */
static void _tsIbCmServiceStore(
                               tTS_IB_CM_SERVICE service,
                               tTS_IB_CM_CONNECTION connection
                               ) {
  connection->cm_function = service->cm_function;
  connection->cm_arg      = service->cm_arg;
}

/* =============================================================== */
/*.._tsIbCmReqStore - Store info from REQ into connection struct   */
static void _tsIbCmReqStore(
                           tTS_IB_MAD packet,
                           tTS_IB_CM_CONNECTION connection
                           ) {
  connection->remote_comm_id      = tsIbCmReqLocalCommIdGet(packet);
  connection->remote_qpn          = tsIbCmReqLocalQpnGet(packet);
  connection->cm_response_timeout = tsIbCmReqRemoteCmTimeoutGet(packet);
  connection->send_psn            = tsIbCmReqStartingPsnGet(packet);
  connection->max_cm_retries      = tsIbCmReqMaxCmRetriesGet(packet);
  connection->initiator_depth     = tsIbCmReqTargetMaxGet(packet);
  connection->responder_resources = tsIbCmReqInitiatorMaxGet(packet);
  connection->retry_count         = tsIbCmReqRetryCountGet(packet);
  connection->rnr_retry_count     = tsIbCmReqRnrRetryCountGet(packet);

  /* path information */
  connection->primary_path.pkey   = tsIbCmReqPkeyGet(packet);
  connection->primary_path.dlid   = tsIbCmReqPrimaryLocalLidGet(packet);
  connection->primary_path.slid   = tsIbCmReqPrimaryRemoteLidGet(packet);
  connection->primary_path.sl     = tsIbCmReqPrimarySlGet(packet);
  memcpy(connection->primary_path.sgid,
         tsIbCmReqPrimaryRemoteGidGet(packet),
         sizeof (tTS_IB_GID));
  memcpy(connection->primary_path.dgid,
         tsIbCmReqPrimaryLocalGidGet(packet),
         sizeof (tTS_IB_GID));
  /* We abuse packet life and put local ACK timeout there */
  connection->primary_path.packet_life = tsIbCmReqPrimaryLocalAckTimeoutGet(packet);
  connection->primary_path.mtu         = tsIbCmReqPathMtuGet(packet);

  connection->alternate_path.pkey      = tsIbCmReqPkeyGet(packet);
  connection->alternate_path.dlid      = tsIbCmReqAlternateLocalLidGet(packet);
  connection->alternate_path.slid      = tsIbCmReqAlternateRemoteLidGet(packet);
  connection->alternate_path.sl        = tsIbCmReqAlternateSlGet(packet);
  memcpy(connection->alternate_path.sgid,
         tsIbCmReqAlternateRemoteGidGet(packet),
         sizeof (tTS_IB_GID));
  memcpy(connection->alternate_path.dgid,
         tsIbCmReqAlternateLocalGidGet(packet),
         sizeof (tTS_IB_GID));
  /* We abuse packet life and put local ACK timeout there */
  connection->alternate_path.packet_life = tsIbCmReqAlternateLocalAckTimeoutGet(packet);
  connection->alternate_path.mtu         = tsIbCmReqPathMtuGet(packet);
  connection->alternate_remote_cm_lid    = connection->alternate_path.dlid;
  connection->local_cm_pkey_index        = packet->pkey_index;

  /* information from header */
  connection->local_cm_device          = packet->device;
  connection->local_cm_port            = packet->port;
  connection->remote_cm_lid            = packet->slid;
  connection->remote_cm_qpn            = packet->sqpn;
  connection->state                    = TS_IB_CM_STATE_REQ_RECEIVED;
  connection->cm_retry_count           = 0;

  connection->transaction_id           = be64_to_cpu(packet->transaction_id);

  tsIbCmConnectionInsertRemote(connection);
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager state callbacks.                                */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* =============================================================== */
/*.._tsIbCmReqReject - build REJ for received REQ                  */
static void _tsIbCmReqReject(
                             tTS_IB_MAD packet,
                             tTS_IB_CM_CONNECTION connection,
                             uint16_t rej_reason,
                             char *response_data
                             ) {
  int result;

  result = tsIbCmRejSend(packet->device,
                         packet->port,
                         packet->pkey_index,
			 packet->slid,
			 packet->sqpn,
			 be64_to_cpu(packet->transaction_id),
                         0,
                         tsIbCmReqLocalCommIdGet(packet),
                         TS_IB_REJ_REQ,
			 rej_reason,
			 response_data,
			 TS_IB_CM_REJ_MAX_PRIVATE_DATA);
  if (result) {
    TS_REPORT_WARN(MOD_IB_CM, "REJ send failed. <%d>", result);
  } else {
    TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_PASSIVE,
             "REJ sent (reason = %d)", rej_reason);
  }

  if (rej_reason == TS_IB_REJ_STALE_CONNECTION) {
    if (connection->state == TS_IB_CM_STATE_ESTABLISHED) {
      /* According to 12.9.8.3.1 we should send a DREQ after sending the REJ */
      result = tsIbCmDreqSend(connection);
      if (result) {
        TS_REPORT_WARN(MOD_IB_CM, "DREQ send failed. <%d>", result);
      }

      tsIbCmTimeWait(&connection, TS_IB_CM_DISCONNECTED_STALE_CONNECTION);
    }

    tsIbCmConnectionPut(connection);

    /* Timer will clean up the connection */
    return;
  }

  if (connection) {
    tTS_IB_CM_IDLE_PARAM_STRUCT params = { 0 };

    params.reason     = TS_IB_CM_IDLE_LOCAL_REJECT;
    params.rej_reason = rej_reason;

    connection->state = TS_IB_CM_STATE_IDLE;

    (void) tsIbCmConsumerFreeCallback(&connection, TS_IB_CM_IDLE, &params);
  }
}

/* =============================================================== */
/*.._tsIbCmReqExisting - handle REQ for existing connection        */
static void _tsIbCmReqExisting(
                               tTS_IB_MAD packet,
                               tTS_IB_CM_CONNECTION connection
                               ) {
  switch (connection->state) {
  case TS_IB_CM_STATE_REQ_RECEIVED:
    /* We'll get around to sending a reply (REP or REJ) */
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring REQ for connection 0x%08x in state %d",
             connection->local_comm_id,
             connection->state);
    break;

  case TS_IB_CM_STATE_MRA_SENT:
    /* resend the MRA we sent last time */
    tsIbCmCountResend(&connection->mad);
    if (tsIbMadSend(&connection->mad)) {
      TS_REPORT_WARN(MOD_IB_CM,
                     "MRA resend failed");
    }
    break;

  case TS_IB_CM_STATE_REP_SENT:
    /* resend the REP we sent last time */
    tsIbCmCountResend(&connection->mad);
    if (tsIbMadSend(&connection->mad)) {
      TS_REPORT_WARN(MOD_IB_CM,
                     "REP resend failed");
    }
    break;

  case TS_IB_CM_STATE_DREQ_RECEIVED:
  case TS_IB_CM_STATE_DREQ_SENT:
  case TS_IB_CM_STATE_TIME_WAIT:
    /*
      If we're in DREQ send/recv or time wait, just ignore this REQ and
      hope it's retransmitted.
    */
    break;

  case TS_IB_CM_STATE_ESTABLISHED:
    /* If the comm ID matches, the last packet we sent was a REP and
       it hasn't been too long since we entered the established state,
       then treat this REQ as a resend.  Otherwise our connection is a
       stale connection.  (See section 12.9.8.3.1 of the IB spec) */
    if (tsIbCmReqLocalCommIdGet(packet) == connection->remote_comm_id &&
        be16_to_cpu(connection->mad.attribute_id) == TS_IB_COM_MGT_REP &&
        time_after(connection->establish_jiffies +
                   tsIbCmTimeoutToJiffies(connection->cm_response_timeout),
                   jiffies)) {
      /* resend the REP we sent last time */
      tsIbCmCountResend(&connection->mad);
      if (tsIbMadSend(&connection->mad)) {
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
    _tsIbCmReqReject(packet, connection, TS_IB_REJ_STALE_CONNECTION, NULL);
    break;
  }

  tsIbCmConnectionPut(connection);
  return;
}

/* =============================================================== */
/*..tsIbCmReqHandler - handle reception of a REQ                   */
void tsIbCmReqHandler
(
 tTS_IB_MAD packet
)
{
  uint64_t             service_id;
  tTS_IB_CM_SERVICE    service;
  tTS_IB_CM_CONNECTION connection = 0;
  char                *response_data;
  int                  response_size;
  int                  result;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_PASSIVE,
           "REQ received");

  response_size = min(TS_IB_CM_REP_MAX_PRIVATE_DATA,
		      TS_IB_CM_REJ_MAX_PRIVATE_DATA);
  response_data = kmalloc(response_size, GFP_KERNEL);
  if (NULL == response_data) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "No memory for response_data");
    return;
  }

  if (tsIbCmReqTransportServiceTypeGet(packet) !=
      TS_IB_TRANSPORT_RC) {
    _tsIbCmReqReject(packet, NULL, TS_IB_REJ_UNSUPPORTED, NULL);
    goto free_data;
  }

  connection = tsIbCmConnectionFindRemoteQp(tsIbCmReqPrimaryLocalGidGet(packet),
                                            tsIbCmReqLocalQpnGet(packet));
  if (connection) {
    _tsIbCmReqExisting(packet, connection);
    goto free_data;
  }

  memcpy(&service_id, tsIbCmReqServiceIdGet(packet), tsIbCmReqServiceIdGetLength());
  cpu_to_be64s(&service_id);
  service = tsIbCmServiceFind(service_id);

  if (!service) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
             "Connection attempted; "
             "invalid service %016Lx", service_id);

    _tsIbCmReqReject(packet, NULL, TS_IB_REJ_INVALID_SERVICE_ID, NULL);
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
    tTS_IB_CM_CONNECTION p2p_conn = tsIbCmConnectionFind(service->peer_to_peer_comm_id);

    if (p2p_conn) {
      uint64_t local_source_guid;
      uint64_t remote_source_guid;

      if (tsIbCachedNodeGuidGet(p2p_conn->local_cm_device,
                                (uint8_t *) &local_source_guid)) {
        TS_REPORT_WARN(MOD_IB_CM,
                       "tsIbCachedNodeGuidGet failed");
      }

      memcpy((uint8_t *) &remote_source_guid,
             tsIbCmReqLocalGuidGet(packet),
             tsIbCmReqLocalGuidGetLength());

      /* XXX should make sure the REQ came from the same endpoint that
         we think we're talking to */

      if (be64_to_cpu(local_source_guid) > be64_to_cpu(remote_source_guid) ||
          (local_source_guid == remote_source_guid &&
           p2p_conn->local_qpn > tsIbCmReqLocalQpnGet(packet))) {
        /* We win election and stay in REQ sent state */
        tsIbCmConnectionPut(p2p_conn);
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
    connection = tsIbCmConnectionNew();
  }

  if (!connection) {
    _tsIbCmReqReject(packet, NULL, TS_IB_REJ_NO_RESOURCES, NULL);
    goto unlock_service;
  }

  connection->active = 0;
  _tsIbCmServiceStore(service, connection);
  _tsIbCmReqStore    (packet,  connection);

  /* Now call back the listen completion handler.  This allows eg the
     user to guarantee that a receive is posted before the other side
     posts a send (which could happen before the RTU is processed on
     this side) */
  {
    tTS_IB_CM_REQ_RECEIVED_PARAM_STRUCT params = { { 0 } };

    params.listen_handle             = service;
    params.service_id                = service_id;
    params.remote_qpn                = connection->remote_qpn;
    params.local_qpn                 = connection->local_qpn;
    params.dlid                      = connection->primary_path.dlid;
    params.slid                      = connection->primary_path.slid;
    params.remote_private_data       = tsIbCmReqPrivateDataGet(packet);
    params.remote_private_data_len   = tsIbCmReqPrivateDataGetLength();
    params.accept_param.reply_private_data     = (void *) response_data;
    params.accept_param.reply_private_data_len = response_size;

    if (tsIbCachedGidFind(connection->primary_path.sgid, &params.device, &params.port, NULL)) {
      params.port = packet->port;
    }

    memcpy(params.dgid,
           tsIbCmReqPrimaryLocalGidGet(packet),
           sizeof (tTS_IB_GID));
    memcpy(params.sgid,
           tsIbCmReqPrimaryRemoteGidGet(packet),
           sizeof (tTS_IB_GID));
    memcpy(params.remote_guid, tsIbCmReqLocalGuidGet(packet), tsIbCmReqLocalGuidGetLength());
    memset(response_data, 0, response_size);

    connection->state = TS_IB_CM_STATE_REQ_RECEIVED;

    result = tsIbCmConsumerCallback(&connection, TS_IB_CM_REQ_RECEIVED, &params);

    if (!connection) {
      goto unlock_service;
    }

    if (result == TS_IB_CM_CALLBACK_DEFER) {
      /*
        don't send any responses. Should be a timer started, to make
        sure this connection response dosn't hang... Only if the
        connection has't already completed...
       */
      tsIbCmConnectionPut(connection);
      goto unlock_service;
    }

    if (result == TS_IB_CM_CALLBACK_ABORT) {
      (void) tsIbCmDropConsumerInternal(connection);

      if (connection->local_qp != TS_IB_HANDLE_INVALID) {
        tsIbCmQpToError(connection->local_qp);
      }

      _tsIbCmReqReject(packet, connection, TS_IB_REJ_CONSUMER_REJECT, response_data);
      goto unlock_service;
    } /* if*/

    result = tsIbCmPassiveParamStore(connection, &params.accept_param);
    if (result) {
      TS_REPORT_WARN(MOD_IB_CM,
                     "tsIbCmPassiveParamStore failed for QP handle");
    }

    response_size = params.accept_param.reply_private_data_len;
  }

  if (tsIbCmReqQpSetup(connection, response_data, response_size)) {
    tsIbCmConnectionFree(connection);
    _tsIbCmReqReject(packet, NULL, TS_IB_REJ_NO_QP, NULL);
    goto unlock_service;
  }

  tsIbCmConnectionPut(connection);

unlock_service:
  tsIbCmServicePut(service);

free_data:
  kfree(response_data);
}

/* =============================================================== */
/*.._tsIbCmRtuCheck - check if we should handle RTU                */
static int _tsIbCmRtuCheck(
                           tTS_IB_MAD packet,
                           tTS_IB_CM_CONNECTION connection
                           ) {
  if (connection->remote_comm_id != tsIbCmRtuLocalCommIdGet(packet)) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
             "RTU comm id mismatch, rcvd:0x%08x != stored:0x%08x",
             connection->local_comm_id, tsIbCmRtuRemoteCommIdGet(packet));
    return 1;
  }

  if (connection->state == TS_IB_CM_STATE_ESTABLISHED) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
             "RTU for already established connection 0x%08x",
             connection->local_comm_id);
    return 1;
  }

  if (connection->state != TS_IB_CM_STATE_REP_SENT) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring RTU for connection 0x%08x in state %d",
             connection->local_comm_id,
             connection->state);
    return 1;
  }

  return 0;
}

/* =============================================================== */
/*..tsIbCmRtuHandler - handle reception of an RTU                  */
void tsIbCmRtuHandler(
                      tTS_IB_MAD packet
                      ) {
  tTS_IB_CM_CONNECTION connection;
  int result;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_PASSIVE,
           "RTU received");

  connection = tsIbCmConnectionFind(tsIbCmRtuRemoteCommIdGet(packet));
  if (!connection) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
             "RTU for unknown comm id 0x%08x",
             tsIbCmRtuLocalCommIdGet(packet));
    return;
  }

  if (_tsIbCmRtuCheck(packet, connection)) {
    tsIbCmConnectionPut(connection);
    return;
  }

  if (tsIbCmPassiveRts(connection)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbQpModify to RTS failed");
  }

  {
    tTS_IB_CM_ESTABLISHED_PARAM_STRUCT params;

    result = tsIbCmConsumerCallback(&connection, TS_IB_CM_ESTABLISHED, &params);
  }

  if (connection) {
    if (result == TS_IB_CM_CALLBACK_ABORT) {
      (void) tsIbCmDropConsumerInternal(connection);
      tsIbCmDreqSend(connection);
    } /* if */

    tsIbCmConnectionPut(connection);
  }

  return;
}
