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

  $Id: cm_path_migration.c,v 1.5 2004/02/25 00:35:11 roland Exp $
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

/* =============================================================== */
/*.._tsIbCmAltPathLoad - Load an alternate path for a QP           */
static int _tsIbCmAltPathLoad(
                              tTS_IB_CM_CONNECTION connection
                              ) {
  tTS_IB_QP_ATTRIBUTE qp_attr;
  int result;

  qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
  if (!qp_attr) {
    return -ENOMEM;
  }

  memset(qp_attr, 0, sizeof *qp_attr);

  /* XXX need to include CA ACK delay */
  qp_attr->alt_local_ack_timeout        = min(31, connection->alternate_path.packet_life + 1);
  qp_attr->alt_address.service_level    = connection->alternate_path.sl;
  qp_attr->alt_address.dlid             = connection->alternate_path.dlid;
  qp_attr->alt_address.source_path_bits = connection->alternate_path.slid & 0x7f;
  qp_attr->alt_address.static_rate      = 0;
  qp_attr->alt_address.use_grh          = 0;
  qp_attr->migration_state              = TS_IB_REARM;

  if (tsIbCachedGidFind(connection->alternate_path.sgid, NULL, &qp_attr->alt_port, NULL)) {
    result = -EINVAL;
    goto out;
  }

  if (tsIbCachedPkeyFind(connection->local_cm_device,
                         qp_attr->alt_port,
                         connection->alternate_path.pkey,
                         &qp_attr->alt_pkey_index)) {
    result = -EINVAL;
    goto out;
  }

  TS_REPORT_WARN(MOD_IB_CM,
                 "Loading alternate path: port %d, timeout %d, 0x%04x -> 0x%04x",
                 qp_attr->alt_port,
                 qp_attr->alt_local_ack_timeout,
                 connection->alternate_path.slid,
                 qp_attr->alt_address.dlid);

  qp_attr->valid_fields =
    TS_IB_QP_ATTRIBUTE_ALT_PORT       |
    TS_IB_QP_ATTRIBUTE_ALT_ADDRESS    |
    TS_IB_QP_ATTRIBUTE_ALT_PKEY_INDEX |
    TS_IB_QP_ATTRIBUTE_MIGRATION_STATE;

  result = tsIbCmQpModify(connection->local_qp,
                          qp_attr);

  if (result) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbQpModify to load alternate path failed <%d>",
                   result);
  } else {
    connection->alternate_remote_cm_lid = connection->alternate_path.dlid;
  }

 out:
  kfree(qp_attr);

  return result;
}

/* =============================================================== */
/*.._tsIbCmAprTimeout - Handle a timeout waiting for APR           */
static void _tsIbCmAprTimeout(
                              void *conn_ptr
                              ) {
  tTS_IB_CM_COMM_ID comm_id = (tTS_IB_CM_COMM_ID) (unsigned long) conn_ptr;

  tTS_IB_CM_CONNECTION connection;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return;
  }

  if (connection->state != TS_IB_CM_STATE_ESTABLISHED
      || !connection->lap_pending) {
    tsIbCmConnectionPut(connection);
    return;
  }

  ++connection->retry_count;

  if (connection->retry_count < connection->max_cm_retries) {
        int ret;

    tsIbCmCountResend(&connection->mad);
    ret = tsIbMadSend(&connection->mad);

    if (ret) {
      TS_REPORT_WARN(MOD_IB_CM, "LAP resend failed. <%d>", ret);
    }

    connection->timer.run_time =
      jiffies + tsIbCmTimeoutToJiffies(connection->cm_response_timeout);
    tsKernelTimerAdd(&connection->timer);
  } else {
    TS_REPORT_WARN(MOD_IB_CM, "LAP retry count exceeded");
    /* XXX call back consumer?? */
  }

  tsIbCmConnectionPut(connection);
}

/* =============================================================== */
/*..tsIbCmLapSend - send LAP to load new path                      */
int tsIbCmLapSend(
                  tTS_IB_CM_CONNECTION connection,
                  tTS_IB_PATH_RECORD   alternate_path
                  ) {
  int ret;

  tsIbMadBuildHeader(&connection->mad);

  connection->mad.attribute_id   = cpu_to_be16(TS_IB_COM_MGT_LAP);
  connection->mad.transaction_id = tsIbCmTidGenerate();

  connection->mad.device     = connection->local_cm_device;
  connection->mad.port       = connection->local_cm_port;
  connection->mad.pkey_index = connection->local_cm_pkey_index;
  connection->mad.sqpn       = TS_IB_GSI_QP;
  connection->mad.dlid       = connection->remote_cm_lid;
  connection->mad.dqpn       = connection->remote_cm_qpn;

  tsIbCmLapLocalCommIdSet             (&connection->mad, connection->local_comm_id);
  tsIbCmLapRemoteCommIdSet            (&connection->mad, connection->remote_comm_id);
  tsIbCmLapRemoteQpnSet               (&connection->mad, connection->remote_qpn);
  tsIbCmLapRemoteCmTimeoutSet         (&connection->mad, connection->cm_response_timeout);
  tsIbCmLapAlternateLocalLidSet       (&connection->mad, alternate_path->slid);
  tsIbCmLapAlternateRemoteLidSet      (&connection->mad, alternate_path->dlid);

  memcpy(tsIbCmLapAlternateLocalGidGet(&connection->mad),
	 alternate_path->sgid,
	 tsIbCmLapAlternateRemoteGidGetLength());
  memcpy(tsIbCmLapAlternateRemoteGidGet(&connection->mad),
	 alternate_path->dgid,
	 tsIbCmLapAlternateRemoteGidGetLength());

  tsIbCmLapAlternateFlowLabelSet      (&connection->mad, alternate_path->flowlabel);
  tsIbCmLapAlternateTrafficSet        (&connection->mad, alternate_path->tclass);
  tsIbCmLapAlternateHopLimitSet       (&connection->mad, alternate_path->hoplmt);
  tsIbCmLapAlternateSlSet             (&connection->mad, alternate_path->sl);
  tsIbCmLapAlternateSubnetLocalSet    (&connection->mad, 1);
  /* XXX need to include CA ACK delay */
  tsIbCmLapAlternateLocalAckTimeoutSet(&connection->mad,
                                       min(31, alternate_path->packet_life + 1));

  tsIbCmCountSend(&connection->mad);
  ret = tsIbMadSend(&connection->mad);
  if (ret) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "LAP send failed");
  }

  connection->lap_pending = 1;
  connection->retry_count = 0;

  connection->timer.function = _tsIbCmAprTimeout;
  tsKernelTimerModify(&connection->timer,
                      jiffies + tsIbCmTimeoutToJiffies(connection->cm_response_timeout));

  return ret;
}

/* =============================================================== */
/*..tsIbCmLapHandler - handle reception of a LAP                   */
void tsIbCmLapHandler(
                      tTS_IB_MAD packet
                      ) {
  tTS_IB_CM_CONNECTION                connection;
  tTS_IB_CM_LAP_RECEIVED_PARAM_STRUCT params;
  tTS_IB_CM_APR_STATUS                ap_status;
  int                                 result;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "LAP received");

  connection = tsIbCmConnectionFind(tsIbCmLapRemoteCommIdGet(packet));
  if (!connection) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "LAP for unknown comm id 0x%08x",
             tsIbCmLapLocalCommIdGet(packet));
    return;
  }

  if (connection->state != TS_IB_CM_STATE_ESTABLISHED) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring LAP for connection 0x%08x in state %d",
             connection->local_comm_id,
             connection->state);
    goto out;
  }

  if (connection->active) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring LAP for active connection 0x%08x",
             connection->local_comm_id);
    goto out;
  }

  if (connection->local_qpn != tsIbCmLapRemoteQpnGet(packet)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "LAP for connection 0x%08x has QPN 0x%06x (expected 0x%06x)",
                   connection->local_comm_id,
                   tsIbCmLapRemoteQpnGet(packet),
                   connection->local_qpn);
    ap_status = TS_IB_APR_QPN_MISMATCH;
    goto out_send_apr;
  }

  params.alternate_path.slid      = tsIbCmLapAlternateRemoteLidGet(packet);
  params.alternate_path.dlid      = tsIbCmLapAlternateLocalLidGet (packet);

  memcpy(params.alternate_path.sgid,
         tsIbCmLapAlternateRemoteGidGet(packet),
         tsIbCmLapAlternateRemoteGidGetLength());
  memcpy(params.alternate_path.dgid,
         tsIbCmLapAlternateLocalGidGet(packet),
         tsIbCmLapAlternateLocalGidGetLength());

  params.alternate_path.flowlabel   = tsIbCmLapAlternateFlowLabelGet      (packet);
  params.alternate_path.tclass      = tsIbCmLapAlternateTrafficGet        (packet);
  params.alternate_path.hoplmt      = tsIbCmLapAlternateHopLimitGet       (packet);
  params.alternate_path.sl          = tsIbCmLapAlternateSlGet             (packet);
  /* We abuse packet life and put local ACK timeout there */
  params.alternate_path.packet_life = tsIbCmLapAlternateLocalAckTimeoutGet(packet);

  /* Call the consumer back to see if we should accept the new path */
  result = tsIbCmConsumerCallback(&connection, TS_IB_CM_LAP_RECEIVED, &params);

  /* See if the connection went away -- if so, just give up */
  if (!connection) {
    return;
  }

  /* If the consumer returned non-zero, reject the alternate path */
  if (result) {
    ap_status = TS_IB_APR_PATH_REJECTED;
    goto out_send_apr;
  }

  connection->alternate_path = params.alternate_path;

  if (_tsIbCmAltPathLoad(connection)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "Failed to load alternate path for connection 0x%08x",
                   connection->local_comm_id);
    /* XXX should we reject path? */
  }

  ap_status = TS_IB_APR_PATH_LOADED;

 out_send_apr:
  tsIbMadBuildHeader(&connection->mad);

  connection->mad.attribute_id   = cpu_to_be16(TS_IB_COM_MGT_APR);
  connection->mad.transaction_id = packet->transaction_id;

  connection->mad.device     = connection->local_cm_device;
  connection->mad.port       = connection->local_cm_port;
  connection->mad.pkey_index = connection->local_cm_pkey_index;
  connection->mad.sqpn       = TS_IB_GSI_QP;
  connection->mad.dlid       = connection->remote_cm_lid;
  connection->mad.dqpn       = connection->remote_cm_qpn;

  tsIbCmAprLocalCommIdSet (&connection->mad, connection->local_comm_id);
  tsIbCmAprRemoteCommIdSet(&connection->mad, connection->remote_comm_id);
  tsIbCmAprApStatusSet    (&connection->mad, ap_status);
  /* XXX we leave info_length as 0 and don't set additional_info */

  tsIbCmCountSend(&connection->mad);
  if (tsIbMadSend(&connection->mad)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "APR send failed");
  }

 out:
  tsIbCmConnectionPut(connection);
  return;

}

/* =============================================================== */
/*..tsIbCmAprHandler - handle reception of a APR                   */
void tsIbCmAprHandler(
                      tTS_IB_MAD packet
                      ) {
  tTS_IB_CM_CONNECTION connection;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "APR received");

  connection = tsIbCmConnectionFind(tsIbCmAprRemoteCommIdGet(packet));
  if (!connection) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "APR for unknown comm id 0x%08x",
             tsIbCmAprLocalCommIdGet(packet));
    return;
  }

  if (connection->state != TS_IB_CM_STATE_ESTABLISHED) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring APR for connection 0x%08x in state %d",
             connection->local_comm_id,
             connection->state);
    goto out;
  }

  if (!connection->active) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring APR for passive connection 0x%08x",
             connection->local_comm_id);
    goto out;
  }

  if (!connection->lap_pending) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring APR for connection 0x%08x with no LAP pending",
             connection->local_comm_id);
    goto out;
  }

  connection->lap_pending = 0;

  if (tsIbCmAprApStatusGet(packet) == TS_IB_APR_PATH_LOADED) {
    if (_tsIbCmAltPathLoad(connection)) {
      TS_REPORT_WARN(MOD_IB_CM,
                     "Alternate path load failed for connection 0x%08x",
                     connection->local_comm_id);
    }
  } else {
    TS_REPORT_WARN(MOD_IB_CM,
                   "Remote CM rejected APR for connection 0x%08x (status %d)",
                   connection->local_comm_id,
                   tsIbCmAprApStatusGet(packet));
  }

  /* Call the consumer back with APR status */
  {
    tTS_IB_CM_APR_RECEIVED_PARAM_STRUCT params = { 0 };

    params.ap_status    = tsIbCmAprApStatusGet      (packet);
    params.apr_info_len = tsIbCmAprInfoLengthGet    (packet);
    params.apr_info     = tsIbCmAprAdditionalInfoGet(packet);

    /* ignore return value */
    (void) tsIbCmConsumerCallback(&connection, TS_IB_CM_APR_RECEIVED, &params);
  }

 out:
  if (connection) {
    tsIbCmConnectionPut(connection);
  }
}
