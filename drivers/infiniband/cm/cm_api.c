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

  $Id: cm_api.c,v 1.13 2004/02/25 00:35:09 roland Exp $
*/

#include "cm_priv.h"
#include "cm_packet.h"

#include "ts_ib_core.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/random.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#else
#include <os_dep/win/linux/string.h>
#endif

int tsIbCmConnect(
                  tTS_IB_CM_ACTIVE_PARAM      param,
                  tTS_IB_PATH_RECORD          primary_path,
                  tTS_IB_PATH_RECORD          alternate_path,
                  tTS_IB_SERVICE_ID           service_id,
                  int                         peer_to_peer,
                  tTS_IB_CM_CALLBACK_FUNCTION function,
                  void                       *arg,
                  tTS_IB_CM_COMM_ID          *comm_id
                  ) {
  int ret = 0;
  tTS_IB_CM_SERVICE service = NULL;    /* for peer-to-peer */
  tTS_IB_DEVICE_HANDLE device;
  tTS_IB_PORT          port;
  tTS_IB_CM_CONNECTION connection;

  if (param->req_private_data_len < 0
      || param->req_private_data_len > tsIbCmReqPrivateDataGetLength()) {
    return -EINVAL;
  }
  if (param->req_private_data_len && !param->req_private_data) {
    return -EINVAL;
  }

  if (peer_to_peer) {
    service = tsIbCmServiceFind(service_id);

    if (!service) {
      return -EINVAL;
    }

    /* XXX we don't have a good way to check if a service is using the
       exact mask yet */
#if 0                           /* XXX */
    if (service->mask != TS_IB_CM_SERVICE_EXACT_MASK) {
      return -EINVAL;
    }
#endif

    if (service->peer_to_peer_comm_id != TS_IB_CM_COMM_ID_INVALID) {
      return -EINVAL;
    }
  }

  if (tsIbCachedGidFind(primary_path->sgid, &device, &port, NULL)) {
    return -EINVAL;
  }

  connection = tsIbCmConnectionNew();
  if (!connection) {
    ret = -ENOMEM;
    goto out;
  }

  if (peer_to_peer) {
    service->peer_to_peer_comm_id = connection->local_comm_id;
    connection->peer_to_peer_service = service;
    tsIbCmServicePut(service);
  }

  *comm_id = connection->local_comm_id;

  connection->local_qp = param->qp;
  ret = tsIbQpQueryQpn(param->qp, &connection->local_qpn);
  if (ret) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "tsIbQpQueryQpn failed %d", ret);
    goto out;
  }

  connection->transaction_id      = tsIbCmTidGenerate();
  connection->local_cm_device     = device;
  connection->local_cm_port       = port;
  connection->primary_path        = *primary_path;
  connection->remote_cm_lid       = primary_path->dlid;
  connection->remote_cm_qpn       = TS_IB_GSI_QP;
  connection->receive_psn         = tsIbCmPsnGenerate();
  connection->cm_function         = function;
  connection->cm_arg              = arg;
  connection->cm_retry_count      = 0;
  connection->retry_count         = param->retry_count;
  connection->rnr_retry_count     = param->rnr_retry_count;
  connection->responder_resources = param->responder_resources;
  connection->initiator_depth     = param->initiator_depth;
  connection->max_cm_retries      = param->max_cm_retries;
  connection->cm_response_timeout = param->cm_response_timeout;
  connection->active              = 1;

  if (alternate_path) {
    connection->alternate_path          = *alternate_path;
    connection->alternate_remote_cm_lid = alternate_path->dlid;
  } else {
    connection->alternate_path.dlid = 0;
  }

  {
    tTS_IB_QP_ATTRIBUTE qp_attr;

    qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
    if (!qp_attr) {
      ret = -ENOMEM;
      goto out;
    }

    qp_attr->port = port;

    if (tsIbCachedPkeyFind(connection->local_cm_device,
                           qp_attr->port,
                           connection->primary_path.pkey,
                           &qp_attr->pkey_index)) {
      ret = -EINVAL;
      kfree(qp_attr);
      goto out;
    }
    connection->local_cm_pkey_index = qp_attr->pkey_index;

    qp_attr->valid_fields =
      TS_IB_QP_ATTRIBUTE_PORT |
      TS_IB_QP_ATTRIBUTE_PKEY_INDEX;
    ret = tsIbCmQpModify(connection->local_qp, qp_attr);

    kfree(qp_attr);

    if (ret) {
      TS_REPORT_WARN(MOD_IB_CM,
                     "tsIbQpModify to INIT failed");
      goto out;
    }
  }

  if (tsIbCmReqSend(connection,
                    service_id,
                    param->req_private_data,
                    param->req_private_data_len)) {
    ret = -EINVAL;
  }

 out:
  if (connection) {
    if (ret) {
      tsIbCmConnectionFree(connection);
    } else {
      tsIbCmConnectionPut(connection);
    }
  }

  return ret;
}

int tsIbCmDisconnect(
                     tTS_IB_CM_COMM_ID comm_id
                     ) {
  tTS_IB_CM_CONNECTION connection;
  int result;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return -EINVAL;
  }

  if (connection->state == TS_IB_CM_STATE_TIME_WAIT ||
      connection->state == TS_IB_CM_STATE_DREQ_SENT ||
      connection->state == TS_IB_CM_STATE_DREQ_RECEIVED) {
    /* Disconnect already in progress, just ignore this request */
    result = 0;
    goto out;
  }

  if (connection->state != TS_IB_CM_STATE_ESTABLISHED) {
    result = -EPROTO;
    goto out;
  }

  result = tsIbCmDreqSend(connection);

 out:
  tsIbCmConnectionPut(connection);
  return result;
}

int tsIbCmKill(
               tTS_IB_CM_COMM_ID comm_id
               ) {
  tTS_IB_CM_CONNECTION connection;
  int result;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return -EINVAL;
  }

  /* Don't try to modify the QP now */
  connection->local_qp = TS_IB_HANDLE_INVALID;

  /* Make sure no callbacks are running */
  connection->cm_function = NULL;
  tsIbCmWaitForCallbacks(&connection);
  if (!connection) {
    return 0;
  }

  if (connection->state == TS_IB_CM_STATE_TIME_WAIT ||
      connection->state == TS_IB_CM_STATE_DREQ_SENT ||
      connection->state == TS_IB_CM_STATE_DREQ_RECEIVED) {
    /* Disconnect already in progress, just ignore this request */
    result = 0;
    goto out;
  }

  if (connection->state != TS_IB_CM_STATE_ESTABLISHED) {
    result = -EPROTO;
    goto out;
  }

  result = tsIbCmDreqSend(connection);

 out:
  tsIbCmConnectionPut(connection);
  return result;
}

int tsIbCmListen(
                 tTS_IB_SERVICE_ID           service_id,
                 tTS_IB_SERVICE_ID           service_mask,
                 tTS_IB_CM_CALLBACK_FUNCTION function,
                 void                       *arg,
                 tTS_IB_LISTEN_HANDLE       *listen_handle
                 ) {
  int ret = 0;
  tTS_IB_CM_SERVICE service;

  /* XXX check that mask is all high bits */

  service_id &= service_mask;

  ret = tsIbCmServiceCreate(service_id, service_mask, &service);
  if (ret) {
    return ret;
  }

  service->cm_function          = function;
  service->cm_arg               = arg;
  service->peer_to_peer_comm_id = TS_IB_CM_COMM_ID_INVALID;

  *listen_handle = service;

  tsIbCmServicePut(service);

  return 0;
}

int tsIbCmListenStop(
                     tTS_IB_LISTEN_HANDLE listen
                     ) {
  tTS_IB_CM_SERVICE service = listen;

  if (service->peer_to_peer_comm_id) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "Attempt to remove a peer-to-peer service while connection exists");
    return -EINVAL;
  }

  tsIbCmServiceFree(service);

  return 0;
}

/* ===================================================================== */
/*..tsIbCmAlternatePathLoad - load new alt path and send LAP             */
int tsIbCmAlternatePathLoad(
                            tTS_IB_CM_COMM_ID  comm_id,
                            tTS_IB_PATH_RECORD alternate_path
                            ) {
  tTS_IB_CM_CONNECTION connection;
  int ret = -EINVAL;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return -ENOTCONN;
  }

  if (!connection->active) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "Attempt to load alternate path from passive side");
    goto out;
  }

  if (connection->state != TS_IB_CM_STATE_ESTABLISHED) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "Attempt to load alternate path for connection in state %d",
                   connection->state);
    goto out;
  }

  if (connection->lap_pending) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "Alternate path load already pending");
    goto out;
  }

  connection->alternate_path = *alternate_path;

  ret = tsIbCmLapSend(connection, alternate_path);

 out:
  tsIbCmConnectionPut(connection);

  return ret;
}

/* ===================================================================== */
/*..tsIbCmDelayResponse - request additional time to process request     */
int tsIbCmDelayResponse(
                        tTS_IB_CM_COMM_ID  comm_id,
                        int                service_timeout,
                        void              *mra_private_data,
                        int                mra_private_data_len
                        ) {
  tTS_IB_CM_CONNECTION connection;
  int ret;

  if (service_timeout < 0 || service_timeout > 31) {
    return -EINVAL;
  }

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return -ENOTCONN;
  }

  ret = tsIbCmMraSend(connection, service_timeout, mra_private_data, mra_private_data_len);

  tsIbCmConnectionPut(connection);

  return ret;
}

/* =================================================================== */
/*..tsIbCmCallbackModify -- update CM callback function and argument   */
int tsIbCmCallbackModify(
                         tTS_IB_CM_COMM_ID           comm_id,
                         tTS_IB_CM_CALLBACK_FUNCTION function,
                         void                       *arg
                         ) {
  tTS_IB_CM_CONNECTION connection;
  int ret = 0;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return -ENOTCONN;
  }

  connection->cm_function = function;
  connection->cm_arg      = arg;

  if (!function) {
    tsIbCmWaitForCallbacks(&connection);
  }

  tsIbCmConnectionPut(connection);

  return ret;
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* asynchronous connection manager responses.                            */
/*                                                                       */
/* --------------------------------------------------------------------- */

/* ====================================================================== */
/*..tsIbCmAccept -- Accept a connection which generated a listen callback */
int tsIbCmAccept(
                 tTS_IB_CM_COMM_ID       comm_id,
                 tTS_IB_CM_PASSIVE_PARAM param
                 ) {
  tTS_IB_CM_CONNECTION connection;
  int result;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return -ENOTCONN;
  }

  if (!connection->cm_function) {
    result = -EBADE;
    goto out;
  }

  result = tsIbCmPassiveParamStore(connection, param);
  if (result) {
    goto out;
  }

  result = tsIbCmReqQpSetup(connection,
                            param->reply_private_data,
                            param->reply_private_data_len);

 out:
  tsIbCmConnectionPut(connection);
  return result;
}

/* =============================================================== */
/*..tsIbCmReject -- Reject a connection which generated a callback */
int tsIbCmReject(
                 tTS_IB_CM_COMM_ID comm_id,
                 void             *rej_private_data,
                 int               rej_private_data_len
                 ) {
  tTS_IB_CM_CONNECTION connection;
  int result;

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return -ENOTCONN;
  }

  if (!connection->cm_function) {
    tsIbCmConnectionPut(connection);
    return -EBADE;
  }

  result = tsIbCmRejSend(connection->local_cm_device,
                         connection->local_cm_port,
                         connection->local_cm_pkey_index,
			 connection->remote_cm_lid,
			 connection->remote_cm_qpn,
			 connection->transaction_id,
                         connection->active ? connection->local_comm_id : 0,
                         connection->remote_comm_id,
			 connection->active ? TS_IB_REJ_REP : TS_IB_REJ_REQ,
			 TS_IB_REJ_CONSUMER_REJECT,
			 rej_private_data,
			 rej_private_data_len);

  tsIbCmConnectionFree(connection);

  return result;
} /* tsIbReject */

/* ========================================================================= */
/*..tsIbCmConfirm -- Confirm a connection which generated a connect callback */
int tsIbCmConfirm(
                  tTS_IB_CM_COMM_ID comm_id,
                  void             *rtu_private_data,
                  int               rtu_private_data_len
                  ) {
  tTS_IB_CM_CONNECTION connection;
  int ret;

  if (rtu_private_data || rtu_private_data_len) {
    return -EOPNOTSUPP;
  }

  connection = tsIbCmConnectionFind(comm_id);
  if (!connection) {
    return -ENOTCONN;
  }

  if (!connection->cm_function) {
    tsIbCmConnectionPut(connection);
    return -EBADE;
  }

  ret = tsIbCmRtuSend(connection);
  tsIbCmConnectionPut(connection);

  return ret;
} /* tsIbConfirm */

/* ======================================================================== */
/*..tsIbCmEstablish -- move connection to RTS/ESTABLISHED                   */
int tsIbCmEstablish(
                    tTS_IB_CM_COMM_ID comm_id,
                    int               immediate
                    ) {
  tTS_IB_CM_CONNECTION connection;

  if (immediate) {
    connection = tsIbCmConnectionFind(comm_id);
    if (!connection) {
      return -ENOTCONN;
    }

    if (connection->state == TS_IB_CM_STATE_REP_SENT) {
      if (tsIbCmPassiveRts(connection)) {
        TS_REPORT_WARN(MOD_IB_CM, "tsIbQpModify to RTS failed");
      }
    }

    tsIbCmConnectionPut(connection);
  } else {
    struct tTS_IB_CM_DELAYED_ESTABLISH_STRUCT *est;

    est = kmalloc(sizeof *est, GFP_ATOMIC);
    if (!est) {
      return -ENOMEM;
    }

    /* XXX - move to CM's work queue in 2.6 */
    INIT_WORK(&est->work, tsIbCmDelayedEstablish, est);
    schedule_work(&est->work);
  }

  return 0;
}

/* ======================================================================== */
/*..tsIbCmPathMigrate -- tell CM to use alternate path                      */
int tsIbCmPathMigrate(
                      tTS_IB_CM_COMM_ID id
                      ) {
  tTS_IB_CM_CONNECTION connection;
  int ret = 0;

  connection = tsIbCmConnectionFind(id);
  if (!connection) {
    return -ENOTCONN;
  }

  if (connection->alternate_remote_cm_lid) {
    connection->remote_cm_lid = connection->alternate_remote_cm_lid;
    /* We now have no alternate path, so mark it as invalid */
    connection->alternate_remote_cm_lid = 0;

    connection->primary_path = connection->alternate_path;
    tsIbCachedGidFind(connection->alternate_path.sgid, NULL, &connection->local_cm_port, NULL);
    /* XXX check return value: */
    tsIbCachedPkeyFind(connection->local_cm_device,
                       connection->local_cm_port,
                       connection->alternate_path.pkey,
                       &connection->local_cm_pkey_index);
  } else {
    TS_REPORT_WARN(MOD_IB_CM,
                   "alternate path is not valid");
    ret = -EINVAL;
  }

  tsIbCmConnectionPut(connection);

  return ret;
}
