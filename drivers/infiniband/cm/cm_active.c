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

  $Id: cm_active.c,v 1.12 2004/02/25 00:35:08 roland Exp $
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

/* =============================================================== */
/*..tsIbCmReqSend - send REQ to create new connection              */
int tsIbCmReqSend
(
 tTS_IB_CM_CONNECTION connection,
 tTS_IB_SERVICE_ID service_id,
 void *req_private_data,
 int req_private_data_len
)
{
  int ret;

  /*
   * Start building the MAD packet
   */
  tsIbMadBuildHeader(&connection->mad);

  connection->mad.attribute_id   = cpu_to_be16(TS_IB_COM_MGT_REQ);
  connection->mad.transaction_id = cpu_to_be64(connection->transaction_id);

  /* Fields are in order of the IB spec. 12.6.5 */

  tsIbCmReqLocalCommIdSet         (&connection->mad, connection->local_comm_id);
  cpu_to_be64s(&service_id);
  memcpy(tsIbCmReqServiceIdGet(&connection->mad),
         &service_id,
         tsIbCmReqServiceIdGetLength());
  if (tsIbCachedNodeGuidGet(connection->local_cm_device,
                            tsIbCmReqLocalGuidGet(&connection->mad))) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbCachedNodeGuidGet failed");
  }
  tsIbCmReqLocalCmQkeySet         (&connection->mad, TS_IB_GSI_WELL_KNOWN_QKEY);
  tsIbCmReqLocalQkeySet           (&connection->mad, 0);
  tsIbCmReqLocalQpnSet            (&connection->mad, connection->local_qpn);

  tsIbCmReqTargetMaxSet           (&connection->mad, connection->responder_resources);
  tsIbCmReqLocalEecnSet           (&connection->mad, 0);
  tsIbCmReqInitiatorMaxSet        (&connection->mad, connection->initiator_depth);
  tsIbCmReqRemoteEecnSet          (&connection->mad, 0);
  tsIbCmReqRemoteCmTimeoutSet     (&connection->mad, connection->cm_response_timeout);
  tsIbCmReqTransportServiceTypeSet(&connection->mad, TS_IB_TRANSPORT_RC);
  tsIbCmReqEndToEndFcSet          (&connection->mad, 1);
  tsIbCmReqStartingPsnSet         (&connection->mad, connection->receive_psn);

  tsIbCmReqLocalCmTimeoutSet      (&connection->mad, connection->cm_response_timeout);
  tsIbCmReqRetryCountSet          (&connection->mad, connection->retry_count);
  tsIbCmReqPkeySet                (&connection->mad, connection->primary_path.pkey);
  tsIbCmReqPathMtuSet             (&connection->mad, (uint8_t) connection->primary_path.mtu);
  tsIbCmReqRdcExistsSet           (&connection->mad, 0);

  tsIbCmReqRnrRetryCountSet       (&connection->mad, connection->rnr_retry_count);
  tsIbCmReqMaxCmRetriesSet        (&connection->mad, 15);

  tsIbCmReqPrimaryLocalLidSet     (&connection->mad, connection->primary_path.slid);
  tsIbCmReqPrimaryRemoteLidSet    (&connection->mad, connection->primary_path.dlid);

  memcpy(tsIbCmReqPrimaryLocalGidGet(&connection->mad),
	 connection->primary_path.sgid,
	 tsIbCmReqPrimaryLocalGidGetLength());
  memcpy(tsIbCmReqPrimaryRemoteGidGet(&connection->mad),
	 connection->primary_path.dgid,
	 tsIbCmReqPrimaryRemoteGidGetLength());

  tsIbCmReqPrimaryFlowLabelSet      (&connection->mad, connection->primary_path.flowlabel);
  tsIbCmReqPrimaryPktRateSet        (&connection->mad, 2);
  tsIbCmReqPrimaryTrafficSet        (&connection->mad, 0);
  tsIbCmReqPrimaryHopLimitSet       (&connection->mad, 0);
  tsIbCmReqPrimarySlSet             (&connection->mad, connection->primary_path.sl);
  tsIbCmReqPrimarySubnetLocalSet    (&connection->mad, 1);
  /* XXX need to include CA ACK delay */
  tsIbCmReqPrimaryLocalAckTimeoutSet(&connection->mad,
                                     min(31, connection->primary_path.packet_life + 1));

  if (connection->alternate_path.dlid) {
    tsIbCmReqAlternateLocalLidSet     (&connection->mad, connection->alternate_path.slid);
    tsIbCmReqAlternateRemoteLidSet    (&connection->mad, connection->alternate_path.dlid);

    memcpy(tsIbCmReqAlternateLocalGidGet(&connection->mad),
           connection->alternate_path.sgid,
           tsIbCmReqAlternateLocalGidGetLength());
    memcpy(tsIbCmReqAlternateRemoteGidGet(&connection->mad),
           connection->alternate_path.dgid,
           tsIbCmReqAlternateRemoteGidGetLength());

    tsIbCmReqAlternateFlowLabelSet      (&connection->mad, connection->alternate_path.flowlabel);
    tsIbCmReqAlternatePktRateSet        (&connection->mad, 2);
    tsIbCmReqAlternateTrafficSet        (&connection->mad, 0);
    tsIbCmReqAlternateHopLimitSet       (&connection->mad, 0);
    tsIbCmReqAlternateSlSet             (&connection->mad, connection->alternate_path.sl);
    tsIbCmReqAlternateSubnetLocalSet    (&connection->mad, 1);
    /* XXX need to include CA ACK delay */
    tsIbCmReqAlternateLocalAckTimeoutSet(&connection->mad,
                                         min(31, connection->primary_path.packet_life + 1));
  }

  if (req_private_data) {
    memcpy(tsIbCmReqPrivateDataGet(&connection->mad),
	   req_private_data,
           req_private_data_len);
  }

  memset(tsIbCmReqPrivateDataGet(&connection->mad) + req_private_data_len,
         0,
         tsIbCmReqPrivateDataGetLength() - req_private_data_len);

  connection->mad.device     = connection->local_cm_device;
  connection->mad.port       = connection->local_cm_port;
  connection->mad.pkey_index = connection->local_cm_pkey_index;
  connection->mad.sqpn       = TS_IB_GSI_QP;
  connection->mad.dlid       = connection->remote_cm_lid;
  connection->mad.dqpn       = connection->remote_cm_qpn;

  tsIbCmCountSend(&connection->mad);
  ret = tsIbMadSend(&connection->mad);
  if (ret) {
    TS_REPORT_WARN(MOD_IB_CM, "REQ send failed. <%d>", ret);
    return -EINVAL;
  }

  connection->state = TS_IB_CM_STATE_REQ_SENT;

  connection->timer.function = tsIbCmConnectTimeout;
  tsKernelTimerModify(&connection->timer,
                      jiffies + tsIbCmTimeoutToJiffies(connection->cm_response_timeout));

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_ACTIVE, "Sent REQ");

  return ret;
} /* tsIbCmReqSend */

/* =============================================================== */
/*..tsIbCmRtuDone -- callback consumer after RTU send              */
void tsIbCmRtuDone(
                   void *conn_ptr
                   ) {
  tTS_IB_CM_COMM_ID comm_id = (tTS_IB_CM_COMM_ID) (unsigned long) conn_ptr;
  tTS_IB_CM_CONNECTION connection;
  int result;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return;
  }

  /* Make sure we don't give the consumer two ESTABLISH callbacks. */
  if (!connection->establish_pending) {
    goto out;
  }

  connection->establish_pending = 0;

  /*
    We still issue the callback in state DREQ_RECEIVED, even though
    the consumer is just about to get a TIME_WAIT callback too.  This
    makes sure that the consumer always gets an ESTABLISHED callback
    so the consumer only sees allowed state transitions.
  */
  if (connection->state != TS_IB_CM_STATE_ESTABLISHED &&
      connection->state != TS_IB_CM_STATE_DREQ_RECEIVED) {
    goto out;
  }

  {
    tTS_IB_CM_ESTABLISHED_PARAM_STRUCT params;
    result = tsIbCmConsumerCallback(&connection, TS_IB_CM_ESTABLISHED, &params);
  }

  if (connection) {
    if (result == TS_IB_CM_CALLBACK_ABORT) {
      (void) tsIbCmDropConsumerInternal(connection);
      tsIbCmDreqSend(connection);
    }
  }

 out:
  tsIbCmConnectionPut(connection);
}

/* =============================================================== */
/*..tsIbCmRtuSend -- send a RTU MAD for a connection               */
/* caller must hold connection semaphore */
int tsIbCmRtuSend
(
 tTS_IB_CM_CONNECTION connection
)
{
  int result = 0;
  tTS_IB_QP_ATTRIBUTE qp_attr;

  if (!connection) {
    return -EINVAL;
  }

  qp_attr = kmalloc(sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT), GFP_KERNEL);
  if (NULL == qp_attr) {
    return -ENOMEM;
  }

  tsIbMadBuildHeader(&connection->mad);

  connection->mad.attribute_id = cpu_to_be16(TS_IB_COM_MGT_RTU);
  connection->mad.transaction_id = cpu_to_be64(connection->transaction_id);

  tsIbCmRtuLocalCommIdSet (&connection->mad, connection->local_comm_id);
  tsIbCmRtuRemoteCommIdSet(&connection->mad, connection->remote_comm_id);

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

    tsIbCachedGidFind(connection->alternate_path.sgid, NULL, &qp_attr->alt_port, NULL);
    tsIbCachedPkeyFind(connection->local_cm_device,
                       qp_attr->alt_port,
                       connection->alternate_path.pkey,
                       &qp_attr->alt_pkey_index);
  }
  result = tsIbCmQpModify(connection->local_qp, qp_attr);
  if (result) {
    TS_REPORT_WARN(MOD_IB_CM, "tsIbQpModify to RTS failed");
    goto free;
  }

  connection->state             = TS_IB_CM_STATE_ESTABLISHED;
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
            tsIbCmRtuDone,
            (void *) (unsigned long) connection->local_comm_id);
  schedule_work(&connection->work);

free:
  kfree(qp_attr);
  return result;
} /* tsIbCmRtuSend */

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager state callbacks.                                */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* =============================================================== */
/*..tsIbCmRepHandler - handle reception of a REP                   */
void tsIbCmRepHandler(
                      tTS_IB_MAD packet
                      ) {
  tTS_IB_CM_CONNECTION connection;
  tTS_IB_QP_ATTRIBUTE qp_attr;
  tTS_IB_CM_REJ_REASON rej_reason;
  int result;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_ACTIVE,
           "REP received");

  connection = tsIbCmConnectionFind(tsIbCmRepRemoteCommIdGet(packet));

  if (!connection) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "REP for unknown comm id 0x%08x",
             tsIbCmRepRemoteCommIdGet(packet));
    rej_reason = TS_IB_REJ_INVALID_COMM_ID;
    goto reject;
  }

  if (connection->state == TS_IB_CM_STATE_MRA_REP_SENT) {
    /* Resend MRA */
    tsIbCmCountResend(&connection->mad);
    result = tsIbMadSend(&connection->mad);
    if (result) {
      TS_REPORT_WARN(MOD_IB_CM, "MRA resend failed. <%d>", result);
      goto out;
    }
  }

  if (connection->state == TS_IB_CM_STATE_ESTABLISHED) {
    /* Resend RTU if connection is established, but make sure we
       haven't already sent some other kind of CM packet. */
    if (connection->mad.attribute_id == cpu_to_be16(TS_IB_COM_MGT_RTU)) {
      tsIbCmCountResend(&connection->mad);
      result = tsIbMadSend(&connection->mad);
      if (result) {
        TS_REPORT_WARN(MOD_IB_CM, "RTU resend failed. <%d>", result);
      }
      goto out;
    }
  }

  if (connection->state != TS_IB_CM_STATE_REQ_SENT) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring REP for connection 0x%08x in state %d",
             connection->local_comm_id,
             connection->state);
    goto out;
  }

  qp_attr = kmalloc(sizeof(tTS_IB_QP_ATTRIBUTE_STRUCT), GFP_KERNEL);
  if (NULL == qp_attr) {

    rej_reason = TS_IB_REJ_NO_RESOURCES;
    goto reject;
  }

  tsKernelTimerRemove(&connection->timer);

  connection->state = TS_IB_CM_STATE_REP_RECEIVED;

  connection->remote_qpn     = tsIbCmRepLocalQpnGet(packet);
  connection->remote_comm_id = tsIbCmRepLocalCommIdGet(packet);
  connection->send_psn       = tsIbCmRepStartingPsnGet(packet); /* remote recv cq */

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

  result = tsIbCmQpModify(connection->local_qp, qp_attr);
  if (result) {
    TS_REPORT_WARN(MOD_IB_CM, "tsIbQpModify to RTR failed. <%d>", result);
    kfree(qp_attr);

    rej_reason = TS_IB_REJ_NO_QP;
    goto reject;
  }

  kfree(qp_attr);

  {
    tTS_IB_CM_REP_RECEIVED_PARAM_STRUCT params = { 0 };

    params.remote_qpn              = connection->remote_qpn;
    params.local_qpn               = connection->local_qpn;
    params.remote_private_data     = tsIbCmRepPrivateDataGet(packet);
    params.remote_private_data_len = tsIbCmRepPrivateDataGetLength();

    result = tsIbCmConsumerCallback(&connection, TS_IB_CM_REP_RECEIVED, &params);
    if (!connection) {
      return;
    }
  }

  if (result == TS_IB_CM_CALLBACK_DEFER) {
      /*
        don't send any responses. Should be a timer started, to make
        sure this connection response dosn't hang... Only if the
        connection has't already completed...
       */
    goto out;
  } /* if */

  if (result == TS_IB_CM_CALLBACK_ABORT) {
    (void) tsIbCmDropConsumerInternal(connection);
    tsIbCmQpToError(connection->local_qp);

    rej_reason = TS_IB_REJ_CONSUMER_REJECT;
    goto reject;
  } /* if */

  result = tsIbCmRtuSend(connection);
  if (result) {
    TS_REPORT_WARN(MOD_IB_CM, "RTU send failed. <%d>", result);
  }

 out:
  tsIbCmConnectionPut(connection);
  return;

 reject:
  result = tsIbCmRejSend(packet->device,
                         packet->port,
                         packet->pkey_index,
			 packet->slid,
			 packet->sqpn,
			 be64_to_cpu(packet->transaction_id),
                         tsIbCmRepRemoteCommIdGet(packet),
                         tsIbCmRepLocalCommIdGet(packet),
                         TS_IB_REJ_REP,
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
    tTS_IB_CM_IDLE_PARAM_STRUCT params = { 0 };

    params.reason     = TS_IB_CM_IDLE_LOCAL_REJECT;
    params.rej_reason = rej_reason;

    connection->state = TS_IB_CM_STATE_IDLE;

    (void) tsIbCmConsumerFreeCallback(&connection, TS_IB_CM_IDLE, &params);
  }
}
