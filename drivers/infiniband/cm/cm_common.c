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

  $Id: cm_common.c,v 1.11 2004/02/25 00:35:09 roland Exp $
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
/*..tsIbMadBuildHeader - make basic header for a MAD               */
void tsIbMadBuildHeader
(
 tTS_IB_MAD packet
)
{
  packet->format_version           = 1;
  packet->mgmt_class               = TS_IB_MGMT_CLASS_COMM_MGT;
  packet->class_version            = 2; /* IB Spec version 1.1 */
  packet->r_method                 = TS_IB_MGMT_METHOD_SEND;
  packet->status                   = 0;
  packet->route.lid.class_specific = 0;

  /* caller will fill in */
  packet->sl                 = 0;
  packet->attribute_id       = 0;
  packet->transaction_id     = 0;

  packet->reserved           = 0;
  packet->attribute_modifier = 0;

  /* clear the payload */
  memset(packet->payload, 0, sizeof packet->payload);

  return;
} /* tsIbMadBuildHeader */

/* =================================================================== */
/*..tsIbCmDropConsumerInternal -- drop consumers' ref to connection    */
int tsIbCmDropConsumerInternal
(
 tTS_IB_CM_CONNECTION connection
)
{
  if (!connection) {
    return -ENOTCONN;
  }

  /*
    prevent future CM callbacks from occuring, and set QP callbacks
    to NULL, in case the QP is still going to be modified.
   */
  connection->cm_function = NULL;
  connection->cm_arg      = NULL;

  return 0;
}

/* =============================================================== */
/*..tsIbCmWaitForCallbacks - wait for all callbacks to be done     */
void tsIbCmWaitForCallbacks(
                            tTS_IB_CM_CONNECTION *connection
                            ) {
  tTS_IB_CM_COMM_ID comm_id = (*connection)->local_comm_id;

  while (*connection && (*connection)->callbacks_running) {
    tsIbCmConnectionPut(*connection);
    set_current_state(TASK_RUNNING);
    schedule();
    *connection = tsIbCmConnectionFind(comm_id);
  }
}

/* =============================================================== */
/*.._tsIbCmConsumerCallbackInternal - drop ref, callback, get ref  */
static tTS_IB_CM_CALLBACK_RETURN _tsIbCmConsumerCallbackInternal(
                                                                 tTS_IB_CM_CONNECTION       *connection,
                                                                 tTS_IB_CM_EVENT             event,
                                                                 void                       *params,
                                                                 tTS_IB_CM_CALLBACK_FUNCTION cm_function,
                                                                 void                       *cm_arg
                           ) {
  tTS_IB_CM_CALLBACK_RETURN result;
  tTS_IB_CM_COMM_ID comm_id  = (*connection)->local_comm_id;

  if (!cm_function) {
    return 0;
  }

  /*
    We drop the reference to the connection before calling into
    consumer code, since we don't know if the consumer will sleep,
    call into the CM, etc, etc.

    We then try to reacquire the connection when the consumer
    returns.  However, something else may have already destroyed the
    connection so we may return with connection == NULL.

    We also use the callbacks_running counter so that
    tsIbCmDropConsumer() can make sure no callbacks are running.
  */
  ++(*connection)->callbacks_running;
  tsIbCmConnectionPut(*connection);

  result = cm_function(event,
                       comm_id,
                       params,
                       cm_arg);

  *connection = tsIbCmConnectionFind(comm_id);
  if (*connection) {
    --(*connection)->callbacks_running;
  }

  return result;
}

/* =============================================================== */
/*..tsIbCmConsumerCallback - drop ref, callback, get ref           */
tTS_IB_CM_CALLBACK_RETURN tsIbCmConsumerCallback(
                                                 tTS_IB_CM_CONNECTION *connection,
                                                 tTS_IB_CM_EVENT       event,
                                                 void                 *params
                                                 ) {
  return _tsIbCmConsumerCallbackInternal(connection,
                                         event,
                                         params,
                                         (*connection)->cm_function,
                                         (*connection)->cm_arg);
}

/* =============================================================== */
/*..tsIbCmConsumerFreeCallback - drop ref, callback, free          */
tTS_IB_CM_CALLBACK_RETURN tsIbCmConsumerFreeCallback(
                                                     tTS_IB_CM_CONNECTION *connection,
                                                     tTS_IB_CM_EVENT       event,
                                                     void                 *params
                                                     ) {
  tTS_IB_CM_CALLBACK_RETURN result = 0;
  tTS_IB_CM_CALLBACK_FUNCTION cm_function = (*connection)->cm_function;
  void *                      cm_arg      = (*connection)->cm_arg;

  /* Get rid of the callback function and wait for any running
     callbacks to finish, so that the consumer doesn't get any
     callbacks after this one.  (If the consumer has already cleared
     the cm_function, our cm_function variable will be NULL so no
     callback will be made here) */
  tsIbCmDropConsumerInternal(*connection);
  tsIbCmWaitForCallbacks(connection);

  if (*connection) {
    result = _tsIbCmConsumerCallbackInternal(connection,
                                             event,
                                             params,
                                             cm_function,
                                             cm_arg);

    if (*connection) {
      tsIbCmConnectionFree(*connection);
      *connection = NULL;
    }
  }

  return result;
}

void tsIbCmQpToError(
                     tTS_IB_QP_HANDLE qp
                     ) {
  tTS_IB_QP_ATTRIBUTE qp_attr;

  qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
  if (!qp_attr) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "Couldn't allocate memory to move QP to ERROR");
    return;
  }

  qp_attr->state        = TS_IB_QP_STATE_ERROR;
  qp_attr->valid_fields = TS_IB_QP_ATTRIBUTE_STATE;

  if (tsIbCmQpModify(qp, qp_attr)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbQpModify to error failed");
  }

  kfree(qp_attr);
}

/* =============================================================== */
/*..tsIbCmConnectTimeout - Handle a timeout waiting for REP/RTU    */
void tsIbCmConnectTimeout(
                          void *conn_ptr
                          ) {
  tTS_IB_CM_COMM_ID comm_id = (tTS_IB_CM_COMM_ID) (unsigned long) conn_ptr;
  tTS_IB_CM_CONNECTION connection;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return;
  }

  switch (connection->state) {
  case TS_IB_CM_STATE_REQ_SENT:
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Timeout waiting for REP; connection ID 0x%08x",
             comm_id);
    break;

  case TS_IB_CM_STATE_REP_SENT:
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Timeout waiting for RTU; connection ID 0x%08x",
             comm_id);
    break;

  default:
    /* We got our response while the timer was waiting to run */
    tsIbCmConnectionPut(connection);
    return;
  }

  ++connection->cm_retry_count;

  if (connection->cm_retry_count < connection->max_cm_retries) {
    int ret;

    tsIbCmCountResend(&connection->mad);
    ret = tsIbMadSend(&connection->mad);

    if (ret) {
      TS_REPORT_WARN(MOD_IB_CM, "%s resend failed. <%d>",
                     connection->state == TS_IB_CM_STATE_REQ_SENT
                     ? "REQ" : "REP",
                     ret);
    }

    connection->timer.run_time =
      jiffies + tsIbCmTimeoutToJiffies(connection->cm_response_timeout);
    tsKernelTimerAdd(&connection->timer);

    tsIbCmConnectionPut(connection);
  } else {
    if (tsIbCmRejSend(connection->local_cm_device,
                      connection->local_cm_port,
                      connection->local_cm_pkey_index,
                      connection->remote_cm_lid,
                      connection->remote_cm_qpn,
                      be64_to_cpu(connection->transaction_id),
                      connection->local_comm_id,
                      connection->remote_comm_id,
                      TS_IB_REJ_NO_MESSAGE,
                      TS_IB_REJ_TIMEOUT,
                      NULL,
                      0)) {
      TS_REPORT_WARN(MOD_IB_CM, "REJ send failed");
    }

    tsIbCmQpToError(connection->local_qp);

    {
      tTS_IB_CM_IDLE_PARAM_STRUCT params = { 0 };

      params.reason = TS_IB_CM_IDLE_REMOTE_TIMEOUT;

      connection->state = TS_IB_CM_STATE_IDLE;

      (void) tsIbCmConsumerFreeCallback(&connection, TS_IB_CM_IDLE, &params);
    }
  }
}

/* =============================================================== */
/*.._tsIbCmTimeWaitTimeout - Free a connection when time wait ends  */
static void _tsIbCmTimeWaitTimeout(
                                   void *conn_ptr
                                   ) {
  tTS_IB_CM_COMM_ID comm_id = (tTS_IB_CM_COMM_ID) (unsigned long) conn_ptr;
  tTS_IB_CM_CONNECTION connection;
  tTS_IB_CM_IDLE_PARAM_STRUCT params = { 0 };

  params.reason = TS_IB_CM_IDLE_TIME_WAIT_DONE;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return;
  }

  connection->state = TS_IB_CM_STATE_IDLE;

  /* Time Wait timer expired, all we have to do is free this connection */
  (void) tsIbCmConsumerFreeCallback(&connection, TS_IB_CM_IDLE, &params);
}

/* =============================================================== */
/*..tsIbCmTimeWait - Move a connection to time wait state          */
void tsIbCmTimeWait(
                    tTS_IB_CM_CONNECTION         *connection,
                    tTS_IB_CM_DISCONNECTED_REASON reason
                    ) {
  tTS_IB_CM_DISCONNECTED_PARAM_STRUCT params;
  tTS_IB_CM_CALLBACK_RETURN result;

  /*
    Make sure that an active connection gets an ESTABLISH callback
    before a TIME_WAIT callback.  Since we do the ESTABLISH callback
    from a timer, it is possible for the remote side to disconnect
    before the ESTABLISH callback gets to run unless we're careful.
  */
  if ((*connection)->establish_pending) {
    tTS_IB_CM_COMM_ID comm_id = (*connection)->local_comm_id;

    tsIbCmConnectionPut(*connection);
    tsIbCmRtuDone((void *) (unsigned long) comm_id);
    *connection = tsIbCmConnectionFind(comm_id);
    if (!*connection) {
      return;
    }
  }

  params.reason = reason;

  (*connection)->state          = TS_IB_CM_STATE_TIME_WAIT;
  (*connection)->cm_retry_count = 0;
  (*connection)->timer.function = _tsIbCmTimeWaitTimeout;
  /* XXX - should use packet lifetime to compute timeout */
  tsKernelTimerModify(&(*connection)->timer, jiffies + HZ / 50);

  result = tsIbCmConsumerCallback(connection,
                                  TS_IB_CM_DISCONNECTED,
                                  &params);
  if (*connection) {
    if (result != TS_IB_CM_CALLBACK_PROCEED) {
      (void) tsIbCmDropConsumerInternal(*connection);
    }
  }
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager packet sending.                                 */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ===================================================================== */
/*..tsIbCmMraSend -- send an MRA MAD for a connection */
int tsIbCmMraSend(
                  tTS_IB_CM_CONNECTION connection,
                  int                  service_timeout,
                  void                *mra_private_data,
                  int                  mra_private_data_len
                  ) {
  int message;

  switch (connection->state) {
  case TS_IB_CM_STATE_REQ_RECEIVED:
    message = TS_IB_MRA_REQ;
    connection->state = TS_IB_CM_STATE_MRA_SENT;
    break;

  case TS_IB_CM_STATE_REP_RECEIVED:
    message = TS_IB_MRA_REP;
    connection->state = TS_IB_CM_STATE_MRA_REP_SENT;
    break;

  default:
    return -EINVAL;
  }

  tsIbMadBuildHeader(&connection->mad);

  connection->mad.attribute_id   = cpu_to_be16(TS_IB_COM_MGT_MRA);
  connection->mad.transaction_id = cpu_to_be64(connection->transaction_id);

  connection->mad.device     = connection->local_cm_device;
  connection->mad.port       = connection->local_cm_port;
  connection->mad.pkey_index = connection->local_cm_pkey_index;
  connection->mad.sqpn       = TS_IB_GSI_QP;
  connection->mad.dlid       = connection->remote_cm_lid;
  connection->mad.dqpn       = connection->remote_cm_qpn;

  tsIbCmMraLocalCommIdSet(&connection->mad, connection->local_comm_id);
  tsIbCmMraRemoteCommIdSet            (&connection->mad, connection->remote_comm_id);
  tsIbCmMraMessageSet(&connection->mad, message);
  tsIbCmMraServiceTimeoutSet(&connection->mad, service_timeout);

  if (mra_private_data && mra_private_data_len > 0) {
    memcpy(tsIbCmMraPrivateDataGet(&connection->mad),
           mra_private_data,
           min(mra_private_data_len, tsIbCmMraPrivateDataGetLength()));
  }

  tsIbCmCountSend(&connection->mad);
  return tsIbMadSend(&connection->mad);
}

/* ===================================================================== */
/*..tsIbCmRejSend -- send a REJ MAD for a connection */
int tsIbCmRejSend
(
 tTS_IB_DEVICE_HANDLE local_cm_device,
 tTS_IB_PORT       local_cm_port,
 int               pkey_index,
 tTS_IB_LID        remote_cm_lid,
 uint32_t          remote_cm_qpn,
 uint64_t          transaction_id,
 tTS_IB_CM_COMM_ID local_comm_id,
 tTS_IB_CM_COMM_ID remote_comm_id,
 int               type,
 int               reason,
 void             *reply_data, /* private data */
 int               reply_size  /* private size */
)
{
  int        result = 0;
  tTS_IB_MAD packet;

  if (TS_IB_CM_REJ_MAX_PRIVATE_DATA < reply_size) {
    return -EINVAL;
  }

  if (TS_IB_REJ_REP != type && TS_IB_REJ_REQ != type && TS_IB_REJ_NO_MESSAGE != type) {
    return -EINVAL;
  }

  packet = kmalloc(sizeof *packet, GFP_KERNEL);
  if (!packet) {
    return -ENOMEM;
  }

  tsIbMadBuildHeader(packet);

  /* copy private data */
  if (reply_data && 0 < reply_size) {
    memcpy(tsIbCmRejPrivateDataGet(packet), reply_data, reply_size);
  }

  packet->attribute_id   = cpu_to_be16(TS_IB_COM_MGT_REJ);
  packet->transaction_id = cpu_to_be64(transaction_id);

  tsIbCmRejLocalCommIdSet (packet, local_comm_id);
  tsIbCmRejRemoteCommIdSet(packet, remote_comm_id);
  tsIbCmRejMessageSet     (packet, type);
  tsIbCmRejInfoLengthSet  (packet, 0);
  tsIbCmRejReasonSet      (packet, reason);

  packet->device     = local_cm_device;
  packet->port       = local_cm_port;
  packet->pkey_index = pkey_index;
  packet->sqpn       = TS_IB_GSI_QP;
  packet->dlid       = remote_cm_lid;
  packet->dqpn       = remote_cm_qpn;

  packet->completion_func = NULL;

  tsIbCmCountSend(packet);
  result = tsIbMadSend(packet);
  if (result) {
    TS_REPORT_WARN(MOD_IB_CM, "REJ send failed");
  }

  kfree(packet);

  return result;
} /* tsIbCmRejSend */

/* =============================================================== */
/*.._tsIbCmDrepTimeout - Handle a timeout waiting for DREP         */
static void _tsIbCmDrepTimeout(
                               void *conn_ptr
                               ) {
  tTS_IB_CM_COMM_ID comm_id = (tTS_IB_CM_COMM_ID) (unsigned long) conn_ptr;
  tTS_IB_CM_CONNECTION connection;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return;
  }

  if (connection->state != TS_IB_CM_STATE_DREQ_SENT) {
    /* We got the response while the timer was waiting to run */
    tsIbCmConnectionPut(connection);
    return;
  }

  TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
           "Timeout waiting for DREP; connection ID 0x%08x",
	   connection->local_comm_id);

  ++connection->cm_retry_count;

  if (connection->cm_retry_count < connection->max_cm_retries) {
    int ret;

    tsIbCmCountResend(&connection->mad);
    ret = tsIbMadSend(&connection->mad);

    if (ret) {
      TS_REPORT_WARN(MOD_IB_CM, "DREQ resend failed. <%d>", ret);
    }

    connection->timer.run_time =
      jiffies + tsIbCmTimeoutToJiffies(connection->cm_response_timeout);
    tsKernelTimerAdd(&connection->timer);
  } else {
    tsIbCmTimeWait(&connection, TS_IB_CM_DISCONNECTED_REMOTE_TIMEOUT);
  }

  tsIbCmConnectionPut(connection);
}

/* =============================================================== */
/*..tsIbCmDreqSend - send DREQ to close connection                 */
int tsIbCmDreqSend(
                   tTS_IB_CM_CONNECTION connection
                   ) {
  int ret = 0;

  tsIbMadBuildHeader(&connection->mad);

  connection->mad.attribute_id   = cpu_to_be16(TS_IB_COM_MGT_DREQ);
  connection->mad.transaction_id = tsIbCmTidGenerate();

  tsIbCmDreqLocalCommIdSet (&connection->mad, connection->local_comm_id);
  tsIbCmDreqRemoteCommIdSet(&connection->mad, connection->remote_comm_id);
  tsIbCmDreqRemoteQpnSet   (&connection->mad, connection->remote_qpn);

  connection->mad.device     = connection->local_cm_device;
  connection->mad.port       = connection->local_cm_port;
  connection->mad.pkey_index = connection->local_cm_pkey_index;
  connection->mad.sqpn       = TS_IB_GSI_QP;
  connection->mad.dlid       = connection->remote_cm_lid;
  connection->mad.dqpn       = connection->remote_cm_qpn;

  tsIbCmCountSend(&connection->mad);
  ret = tsIbMadSend(&connection->mad);
  if (ret) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "DREQ send failed");
  }

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "Sent DREQ");

  tsIbCmQpToError(connection->local_qp);

  if (connection->state != TS_IB_CM_STATE_DREQ_SENT
      && connection->state != TS_IB_CM_STATE_TIME_WAIT
      && connection->state != TS_IB_CM_STATE_DREQ_RECEIVED) {
    connection->state          = TS_IB_CM_STATE_DREQ_SENT;
    connection->cm_retry_count = 0;

    connection->timer.function = _tsIbCmDrepTimeout;
    tsKernelTimerModify(&connection->timer,
                        jiffies + tsIbCmTimeoutToJiffies(connection->cm_response_timeout));
  }

  return ret;
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager state callbacks.                                */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* =============================================================== */
/*..tsIbCmRejHandler - handle reception of a REJ                   */
void tsIbCmRejHandler(
                      tTS_IB_MAD packet
                      ) {
  tTS_IB_CM_CONNECTION connection;

  int result;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "REJ received");

  if (tsIbCmRejRemoteCommIdGet(packet) != TS_IB_CM_COMM_ID_INVALID) {
    connection = tsIbCmConnectionFind(tsIbCmRejRemoteCommIdGet(packet));

    if (!connection) {
      TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
               "REJ (reason %d) received for unknown local comm id 0x%08x",
               tsIbCmRejReasonGet(packet),
               tsIbCmRejRemoteCommIdGet(packet));
      return;
    }

    if (connection->remote_comm_id      != TS_IB_CM_COMM_ID_INVALID &&
        tsIbCmRejLocalCommIdGet(packet) != TS_IB_CM_COMM_ID_INVALID &&
        tsIbCmRejLocalCommIdGet(packet) != connection->remote_comm_id) {
      TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
               "REJ (reason %d) has local comm id 0x%08x, expected 0x%08x",
               tsIbCmRejLocalCommIdGet(packet), connection->remote_comm_id);
      tsIbCmConnectionPut(connection);
      return;
    }
  } else {
    connection = tsIbCmConnectionFindRemoteId(tsIbCmRejLocalCommIdGet(packet));

    if (!connection) {
      TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
               "REJ (reason %d) received for unknown remote comm id 0x%08x",
               tsIbCmRejReasonGet(packet),
               tsIbCmRejRemoteCommIdGet(packet));
      return;
    }
  }

  switch (connection->state) {
  case TS_IB_CM_STATE_REQ_SENT:
  case TS_IB_CM_STATE_REQ_RECEIVED:
  case TS_IB_CM_STATE_REP_SENT:
  case TS_IB_CM_STATE_MRA_SENT:
  case TS_IB_CM_STATE_MRA_REP_SENT:
    if (connection->local_qp != TS_IB_HANDLE_INVALID) {
      tsIbCmQpToError(connection->local_qp);
    }
    break;

  default:
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "REJ received for connection in inappropriate state %d",
             connection->state);
    tsIbCmConnectionPut(connection);
    return;
  }

  /* call connection callback. result dosn't matter */
  {
    tTS_IB_CM_IDLE_PARAM_STRUCT params = { 0 };

    params.reason       = TS_IB_CM_IDLE_REMOTE_REJECT;
    params.rej_reason   = tsIbCmRejReasonGet(packet);
    params.rej_info_len = tsIbCmRejInfoLengthGet(packet);
    params.rej_info     = tsIbCmRejAdditionalInfoGet(packet);

    connection->state = TS_IB_CM_STATE_IDLE;
    result = tsIbCmConsumerFreeCallback(&connection, TS_IB_CM_IDLE, &params);
  }

  if (result) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
             "REJ: connect callback error <%d> ignored.", result);
  }
}

/* =============================================================== */
/*..tsIbCmMraHandler - handle reception of a MRA                   */
void tsIbCmMraHandler(
                      tTS_IB_MAD packet
                      ) {
  tTS_IB_CM_CONNECTION connection;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "MRA received");

  connection = tsIbCmConnectionFind(tsIbCmMraRemoteCommIdGet(packet));

  if (!connection) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "MRA for unknown comm id 0x%08x",
             tsIbCmMraRemoteCommIdGet(packet));
    return;
  }

  if (connection->state != TS_IB_CM_STATE_REQ_SENT &&
      connection->state != TS_IB_CM_STATE_REP_SENT) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "MRA for connection 0x%08x in inappropriate state %d",
             tsIbCmMraRemoteCommIdGet(packet),
             connection->state);
    goto out;
  }

  if ((connection->state == TS_IB_CM_STATE_REQ_SENT &&
       tsIbCmMraMessageGet(packet) != TS_IB_MRA_REQ) ||
      (connection->state == TS_IB_CM_STATE_REP_SENT &&
       tsIbCmMraMessageGet(packet) != TS_IB_MRA_REP)) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "MRA with message %d for connection 0x%08x in state %d",
             tsIbCmMraMessageGet(packet),
             tsIbCmMraRemoteCommIdGet(packet),
             connection->state);
    goto out;
  }

  connection->cm_retry_count = 0;
  tsKernelTimerModify(&connection->timer,
                      jiffies + tsIbCmTimeoutToJiffies(tsIbCmMraServiceTimeoutGet(packet)));

 out:
  tsIbCmConnectionPut(connection);
}

/* =============================================================== */
/*.._tsIbCmDreqCheck - check if we should handle DREQ              */
static int _tsIbCmDreqCheck(
                            tTS_IB_MAD packet,
                            tTS_IB_CM_CONNECTION connection
                            ) {
  if (connection->local_qpn != tsIbCmDreqRemoteQpnGet(packet)) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "DREQ for comm id 0x%08x, "
             "qpn mismatch rcvd:%d != stored:%d",
             tsIbCmDreqLocalCommIdGet(packet),
             tsIbCmDreqRemoteQpnGet(packet),
             connection->local_qpn);
    return 1;
  }

  if (connection->remote_comm_id != tsIbCmDreqLocalCommIdGet(packet)) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "DREQ for comm id rcvd:0x%08x != stored:0x%08x",
             tsIbCmDreqRemoteCommIdGet(packet), connection->local_comm_id);
    return 1;
  }

  if (connection->state != TS_IB_CM_STATE_ESTABLISHED
      && connection->state != TS_IB_CM_STATE_DREQ_SENT
      && connection->state != TS_IB_CM_STATE_TIME_WAIT) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring DREQ for connection 0x%08x in state %d",
             connection->local_comm_id,
             connection->state);
    return 1;
  }

  if (connection->state == TS_IB_CM_STATE_TIME_WAIT
      && ++connection->cm_retry_count > connection->max_cm_retries) {
    return 1;
  }

  return 0;
}

/* =============================================================== */
/*..tsIbCmDreqHandler - handle reception of an DREQ                */
void tsIbCmDreqHandler(
                       tTS_IB_MAD packet
                       ) {
  tTS_IB_CM_CONNECTION connection;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "DREQ received");

  connection = tsIbCmConnectionFind(tsIbCmDreqRemoteCommIdGet(packet));
  if (!connection) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "DREQ for unknown comm id 0x%08x",
             tsIbCmDreqLocalCommIdGet(packet));
    /* We send a DREP anyway, since this is probably a connection
       where our DREP was dropped.  We went to TIME_WAIT and then
       IDLE, but the other side is still waiting for a DREP. */
    {
      tTS_IB_MAD drep;

      drep = kmalloc(sizeof *drep, GFP_KERNEL);
      if (!drep) {
        TS_REPORT_WARN(MOD_IB_CM,
                       "No memory for DREP");
        return;
      }

      tsIbMadBuildHeader(drep);

      drep->attribute_id   = cpu_to_be16(TS_IB_COM_MGT_DREP);
      drep->transaction_id = packet->transaction_id;

      tsIbCmDrepLocalCommIdSet (drep, tsIbCmDreqRemoteCommIdGet(packet));
      tsIbCmDrepRemoteCommIdSet(drep, tsIbCmDrepLocalCommIdGet (packet));

      drep->device     = packet->device;
      drep->port       = packet->port;
      drep->pkey_index = packet->pkey_index;
      drep->sqpn       = TS_IB_GSI_QP;
      drep->dlid       = packet->slid;
      drep->dqpn       = packet->sqpn;

      drep->completion_func = NULL;

      tsIbCmCountResend(drep);
      if (tsIbMadSend(drep)) {
        TS_REPORT_WARN(MOD_IB_CM,
                       "DREP resend failed");
      }

      kfree(drep);
    }
    return;
  }

  if (_tsIbCmDreqCheck(packet, connection)) {
    tsIbCmConnectionPut(connection);
    return;
  }

  if (connection->state != TS_IB_CM_STATE_TIME_WAIT) {
    connection->state = TS_IB_CM_STATE_DREQ_RECEIVED;

    tsIbCmQpToError(connection->local_qp);
  }

  tsIbMadBuildHeader(&connection->mad);

  connection->mad.attribute_id   = cpu_to_be16(TS_IB_COM_MGT_DREP);
  connection->mad.transaction_id = packet->transaction_id;

  tsIbCmDrepLocalCommIdSet (&connection->mad, connection->local_comm_id);
  tsIbCmDrepRemoteCommIdSet(&connection->mad, connection->remote_comm_id);

  connection->mad.device     = connection->local_cm_device;
  connection->mad.port       = connection->local_cm_port;
  connection->mad.pkey_index = connection->local_cm_pkey_index;
  connection->mad.dlid       = connection->remote_cm_lid;
  connection->mad.dqpn       = connection->remote_cm_qpn;

  tsIbCmCountSend(&connection->mad);
  if (tsIbMadSend(&connection->mad)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "DREP send failed");
  }

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "Sent DREP");

  if (connection->state != TS_IB_CM_STATE_TIME_WAIT) {
    tsIbCmTimeWait(&connection, TS_IB_CM_DISCONNECTED_REMOTE_CLOSE);
  }

  tsIbCmConnectionPut(connection);
} /* tsIbCmDreqHandler */

/* =============================================================== */
/*..tsIbCmDrepHandler - handle reception of an DREP                */
void tsIbCmDrepHandler(
                       tTS_IB_MAD packet
                       ) {
  tTS_IB_CM_CONNECTION connection;

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "DREP received");

  connection = tsIbCmConnectionFind(tsIbCmDrepRemoteCommIdGet(packet));
  if (!connection) {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
             "DREP for unknown comm id 0x%08x",
             tsIbCmDrepLocalCommIdGet(packet));
    return;
  }

  if (connection->state == TS_IB_CM_STATE_DREQ_SENT) {
    tsIbCmTimeWait(&connection, TS_IB_CM_DISCONNECTED_LOCAL_CLOSE);
  } else {
    TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
             "Ignoring DREP for connection 0x%08x in state %d",
             connection->local_comm_id,
             connection->state);
  }

  tsIbCmConnectionPut(connection);
}
