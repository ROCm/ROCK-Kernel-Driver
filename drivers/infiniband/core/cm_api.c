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

  $Id: cm_api.c 32 2004-04-09 03:57:42Z roland $
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

int ib_cm_connect(struct ib_cm_active_param  *param,
                  struct ib_path_record      *primary_path,
                  struct ib_path_record      *alternate_path,
                  tTS_IB_SERVICE_ID           service_id,
                  int                         peer_to_peer,
                  tTS_IB_CM_CALLBACK_FUNCTION function,
                  void                       *arg,
                  tTS_IB_CM_COMM_ID          *comm_id) 
{
	int ret = 0;
	struct ib_cm_service 	  *service = NULL;    /* for peer-to-peer */
	tTS_IB_DEVICE_HANDLE 	   device;
	tTS_IB_PORT          	   port;
	struct ib_cm_connection *connection;

	if (param->req_private_data_len < 0
	    || param->req_private_data_len > ib_cm_req_private_data_get_length()) {
		return -EINVAL;
	}
	if (param->req_private_data_len && !param->req_private_data) {
		return -EINVAL;
	}

	if (peer_to_peer) {
		service = ib_cm_service_find(service_id);

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

	if (ib_cached_gid_find(primary_path->sgid, &device, &port, NULL)) {
		return -EINVAL;
	}

	connection = ib_cm_connection_new();
	if (!connection) {
		ret = -ENOMEM;
		goto out;
	}

	if (peer_to_peer) {
		service->peer_to_peer_comm_id = connection->local_comm_id;
		connection->peer_to_peer_service = service;
		ib_cm_service_put(service);
	}

	*comm_id = connection->local_comm_id;

	connection->local_qp = param->qp;
	ret = ib_qp_query_qpn(param->qp, &connection->local_qpn);
	if (ret) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "ib_qp_query_qpn failed %d", ret);
		goto out;
	}

	connection->transaction_id      = ib_cm_tid_generate();
	connection->local_cm_device     = device;
	connection->local_cm_port       = port;
	connection->primary_path        = *primary_path;
	connection->remote_cm_lid       = primary_path->dlid;
	connection->remote_cm_qpn       = TS_IB_GSI_QP;
	connection->receive_psn         = ib_cm_psn_generate();
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

		if (ib_cached_pkey_find(connection->local_cm_device,
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
		ret = ib_cm_qp_modify(connection->local_qp, qp_attr);

		kfree(qp_attr);

		if (ret) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "ib_qp_modify to INIT failed");
			goto out;
		}
	}

	if (ib_cm_req_send(connection,
			   service_id,
			   param->req_private_data,
			   param->req_private_data_len)) {
		ret = -EINVAL;
	}

 out:
	if (connection) {
		if (ret) {
			ib_cm_connection_free(connection);
		} else {
			ib_cm_connection_put(connection);
		}
	}

	return ret;
}

int ib_cm_disconnect(tTS_IB_CM_COMM_ID comm_id)
{
	struct ib_cm_connection *connection;
	int result;

	connection = ib_cm_connection_find(comm_id);
	if (!connection) {
		return -EINVAL;
	}

	if (connection->state == IB_CM_STATE_TIME_WAIT ||
	    connection->state == IB_CM_STATE_DREQ_SENT ||
	    connection->state == IB_CM_STATE_DREQ_RECEIVED) {
		/* Disconnect already in progress, just ignore this request */
		result = 0;
		goto out;
	}

	if (connection->state != IB_CM_STATE_ESTABLISHED) {
		result = -EPROTO;
		goto out;
	}

	result = ib_cm_dreq_send(connection);

 out:
	ib_cm_connection_put(connection);
	return result;
}

int ib_cm_kill(tTS_IB_CM_COMM_ID comm_id)
{
	struct ib_cm_connection *connection;
	int result;

	connection = ib_cm_connection_find(comm_id);
	if (!connection) {
		return -EINVAL;
	}

	/* Don't try to modify the QP now */
	connection->local_qp = TS_IB_HANDLE_INVALID;

	/* Make sure no callbacks are running */
	connection->cm_function = NULL;
	ib_cm_wait_for_callbacks(&connection);
	if (!connection) {
		return 0;
	}

	if (connection->state == IB_CM_STATE_TIME_WAIT ||
	    connection->state == IB_CM_STATE_DREQ_SENT ||
	    connection->state == IB_CM_STATE_DREQ_RECEIVED) {
		/* Disconnect already in progress, just ignore this request */
		result = 0;
		goto out;
	}

	result = ib_cm_dreq_send(connection);

 out:
	ib_cm_connection_put(connection);
	return result;
}

int ib_cm_listen(tTS_IB_SERVICE_ID           service_id,
                 tTS_IB_SERVICE_ID           service_mask,
                 tTS_IB_CM_CALLBACK_FUNCTION function,
                 void                       *arg,
                 tTS_IB_LISTEN_HANDLE       *listen_handle)
{
	int ret = 0;
	struct ib_cm_service *service;

	/* XXX check that mask is all high bits */

	service_id &= service_mask;

	ret = ib_cm_service_create(service_id, service_mask, &service);
	if (ret) {
		return ret;
	}

	service->cm_function          = function;
	service->cm_arg               = arg;
	service->peer_to_peer_comm_id = TS_IB_CM_COMM_ID_INVALID;

	*listen_handle = service;

	ib_cm_service_put(service);

	return 0;
}

int ib_cm_listen_stop(tTS_IB_LISTEN_HANDLE listen)
{
	struct ib_cm_service *service = listen;

	if (service->peer_to_peer_comm_id) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "Attempt to remove a peer-to-peer service while connection exists");
		return -EINVAL;
	}

	ib_cm_service_free(service);

	return 0;
}

int ib_cm_alternate_path_load(tTS_IB_CM_COMM_ID      comm_id,
			      struct ib_path_record *alternate_path)
{
	struct ib_cm_connection *connection;
	int ret = -EINVAL;

	connection = ib_cm_connection_find(comm_id);
	if (!connection) {
		return -ENOTCONN;
	}

	if (!connection->active) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "Attempt to load alternate path from passive side");
		goto out;
	}

	if (connection->state != IB_CM_STATE_ESTABLISHED) {
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

	ret = ib_cm_lap_send(connection, alternate_path);

 out:
	ib_cm_connection_put(connection);

	return ret;
}

int ib_cm_delay_response(tTS_IB_CM_COMM_ID  comm_id,
			 int                service_timeout,
			 void              *mra_private_data,
			 int                mra_private_data_len)
{
	struct ib_cm_connection *connection;
	int ret;

	if (service_timeout < 0 || service_timeout > 31) {
		return -EINVAL;
	}

	connection = ib_cm_connection_find(comm_id);
	if (!connection) {
		return -ENOTCONN;
	}

	ret = ib_cm_mra_send(connection, service_timeout,
			     mra_private_data, mra_private_data_len);

	ib_cm_connection_put(connection);

	return ret;
}

int ib_cm_callback_modify(tTS_IB_CM_COMM_ID           comm_id,
			  tTS_IB_CM_CALLBACK_FUNCTION function,
			  void                       *arg)
{
	struct ib_cm_connection *connection;
	int ret = 0;

	connection = ib_cm_connection_find(comm_id);
	if (!connection) {
		return -ENOTCONN;
	}

	connection->cm_function = function;
	connection->cm_arg      = arg;

	if (!function) {
		ib_cm_wait_for_callbacks(&connection);
	}

	ib_cm_connection_put(connection);

	return ret;
}

/* ------------------------------------------------------------- */
/*                                                               */
/* asynchronous connection responses.                            */
/*                                                               */
/* ------------------------------------------------------------- */

int ib_cm_accept(tTS_IB_CM_COMM_ID           comm_id,
                 struct ib_cm_passive_param *param)
{
	struct ib_cm_connection *connection;
	int result;

	connection = ib_cm_connection_find(comm_id);
	if (!connection) {
		return -ENOTCONN;
	}

	if (!connection->cm_function) {
		result = -EBADE;
		goto out;
	}

	result = ib_cm_passive_param_store(connection, param);
	if (result) {
		goto out;
	}

	result = ib_cm_req_qp_setup(connection,
				    param->reply_private_data,
				    param->reply_private_data_len);

 out:
	ib_cm_connection_put(connection);
	return result;
}

int ib_cm_reject(tTS_IB_CM_COMM_ID comm_id,
                 void             *rej_private_data,
                 int               rej_private_data_len)
{
	struct ib_cm_connection *connection;
	int result;

	connection = ib_cm_connection_find(comm_id);
	if (!connection) {
		return -ENOTCONN;
	}

	if (!connection->cm_function) {
		ib_cm_connection_put(connection);
		return -EBADE;
	}

	result = ib_cm_rej_send(connection->local_cm_device,
				connection->local_cm_port,
				connection->local_cm_pkey_index,
				connection->remote_cm_lid,
				connection->remote_cm_qpn,
				connection->transaction_id,
				connection->active ? connection->local_comm_id : 0,
				connection->remote_comm_id,
				connection->active ? IB_REJ_REP : IB_REJ_REQ,
				TS_IB_REJ_CONSUMER_REJECT,
				rej_private_data,
				rej_private_data_len);

	ib_cm_connection_free(connection);

	return result;
}

int ib_cm_confirm(tTS_IB_CM_COMM_ID comm_id,
                  void             *rtu_private_data,
                  int               rtu_private_data_len)
{
	struct ib_cm_connection *connection;
	int ret;

	if (rtu_private_data || rtu_private_data_len) {
		return -EOPNOTSUPP;
	}

	connection = ib_cm_connection_find(comm_id);
	if (!connection) {
		return -ENOTCONN;
	}

	if (!connection->cm_function) {
		ib_cm_connection_put(connection);
		return -EBADE;
	}

	ret = ib_cm_rtu_send(connection);
	ib_cm_connection_put(connection);

	return ret;
}

int ib_cm_establish(tTS_IB_CM_COMM_ID comm_id,
                    int               immediate)
{
	struct ib_cm_connection *connection;

	if (immediate) {
		connection = ib_cm_connection_find(comm_id);
		if (!connection) {
			return -ENOTCONN;
		}

		if (connection->state == IB_CM_STATE_REP_SENT) {
			if (ib_cm_passive_rts(connection)) {
				TS_REPORT_WARN(MOD_IB_CM, "ib_qp_modify to RTS failed");
			}
		}

		ib_cm_connection_put(connection);
	} else {
		struct ib_cm_delayed_establish *est;

		est = kmalloc(sizeof *est, GFP_ATOMIC);
		if (!est) {
			return -ENOMEM;
		}

		/* XXX - move to CM's work queue in 2.6 */
		INIT_WORK(&est->work, ib_cm_delayed_establish, est);
		schedule_work(&est->work);
	}

	return 0;
}

int ib_cm_path_migrate(tTS_IB_CM_COMM_ID id)
{
	struct ib_cm_connection *connection;
	int ret = 0;

	connection = ib_cm_connection_find(id);
	if (!connection) {
		return -ENOTCONN;
	}

	if (connection->alternate_remote_cm_lid) {
		connection->remote_cm_lid = connection->alternate_remote_cm_lid;
		/* We now have no alternate path, so mark it as invalid */
		connection->alternate_remote_cm_lid = 0;

		connection->primary_path = connection->alternate_path;
		ib_cached_gid_find(connection->alternate_path.sgid, 
				   NULL, &connection->local_cm_port, NULL);
		/* XXX check return value: */
		ib_cached_pkey_find(connection->local_cm_device,
				    connection->local_cm_port,
				    connection->alternate_path.pkey,
				    &connection->local_cm_pkey_index);
	} else {
		TS_REPORT_WARN(MOD_IB_CM,
			       "alternate path is not valid");
		ret = -EINVAL;
	}

	ib_cm_connection_put(connection);

	return ret;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
