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

  $Id: cm_common.c 32 2004-04-09 03:57:42Z roland $
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

void ib_mad_build_header(struct ib_mad *packet)
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
}

int ib_cm_drop_consumer_internal(struct ib_cm_connection *connection)
{
	if (!connection)
		return -ENOTCONN;

	/* prevent future CM callbacks from occuring */
	connection->cm_function = NULL;
	connection->cm_arg      = NULL;

	return 0;
}

void ib_cm_wait_for_callbacks(struct ib_cm_connection **connection)
{
	tTS_IB_CM_COMM_ID comm_id = (*connection)->local_comm_id;

	while (*connection && (*connection)->callbacks_running) {
		ib_cm_connection_put(*connection);
		set_current_state(TASK_RUNNING);
		schedule();
		*connection = ib_cm_connection_find(comm_id);
	}
}

static tTS_IB_CM_CALLBACK_RETURN __ib_cm_consumer_callback(struct ib_cm_connection   **connection,
							   tTS_IB_CM_EVENT             event,
							   void                       *params,
							   tTS_IB_CM_CALLBACK_FUNCTION cm_function,
							   void                       *cm_arg)
{
	tTS_IB_CM_CALLBACK_RETURN result;
	tTS_IB_CM_COMM_ID comm_id  = (*connection)->local_comm_id;

	if (!cm_function)
		return 0;

	/*
	  We drop the reference to the connection before calling into
	  consumer code, since we don't know if the consumer will sleep,
	  call into the CM, etc, etc.

	  We then try to reacquire the connection when the consumer
	  returns.  However, something else may have already destroyed the
	  connection so we may return with connection == NULL.

	  We also use the callbacks_running counter so that
	  ib_cm_drop_consumer() can make sure no callbacks are running.
	*/
	++(*connection)->callbacks_running;
	ib_cm_connection_put(*connection);

	result = cm_function(event, comm_id, params, cm_arg);

	*connection = ib_cm_connection_find(comm_id);
	if (*connection)
		--(*connection)->callbacks_running;

	return result;
}

tTS_IB_CM_CALLBACK_RETURN ib_cm_consumer_callback(struct ib_cm_connection **connection,
						  tTS_IB_CM_EVENT           event,
						  void                     *params)
{
	return __ib_cm_consumer_callback(connection,
					 event,
					 params,
					 (*connection)->cm_function,
					 (*connection)->cm_arg);
}

tTS_IB_CM_CALLBACK_RETURN ib_cm_consumer_free_callback(struct ib_cm_connection **connection,
						       tTS_IB_CM_EVENT           event,
						       void                     *params)
{
	tTS_IB_CM_CALLBACK_RETURN result = 0;
	tTS_IB_CM_CALLBACK_FUNCTION cm_function = (*connection)->cm_function;
	void *                      cm_arg      = (*connection)->cm_arg;

	/* Get rid of the callback function and wait for any running
	   callbacks to finish, so that the consumer doesn't get any
	   callbacks after this one.  (If the consumer has already cleared
	   the cm_function, our cm_function variable will be NULL so no
	   callback will be made here) */
	ib_cm_drop_consumer_internal(*connection);
	ib_cm_wait_for_callbacks(connection);

	if (*connection) {
		result = __ib_cm_consumer_callback(connection,
						   event,
						   params,
						   cm_function,
						   cm_arg);

		if (*connection) {
			ib_cm_connection_free(*connection);
			*connection = NULL;
		}
	}

	return result;
}

void ib_cm_qp_to_error(tTS_IB_QP_HANDLE qp)
{
	struct ib_qp_attribute *qp_attr;

	qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
	if (!qp_attr) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "Couldn't allocate memory to move QP to ERROR");
		return;
	}

	qp_attr->state        = TS_IB_QP_STATE_ERROR;
	qp_attr->valid_fields = TS_IB_QP_ATTRIBUTE_STATE;

	if (ib_cm_qp_modify(qp, qp_attr))
		TS_REPORT_WARN(MOD_IB_CM,
			       "ib_qp_modify to error failed");

	kfree(qp_attr);
}

void ib_cm_connect_timeout(void *conn_ptr)
{
	tTS_IB_CM_COMM_ID comm_id = (unsigned long) conn_ptr;
	struct ib_cm_connection *connection;

	connection = ib_cm_connection_find(comm_id);
	if (!connection)
		return;

	switch (connection->state) {
	case IB_CM_STATE_REQ_SENT:
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
			 "Timeout waiting for REP; connection ID 0x%08x",
			 comm_id);
		break;

	case IB_CM_STATE_REP_SENT:
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
			 "Timeout waiting for RTU; connection ID 0x%08x",
			 comm_id);
		break;

	default:
		/* We got our response while the timer was waiting to run */
		ib_cm_connection_put(connection);
		return;
	}

	++connection->cm_retry_count;

	if (connection->cm_retry_count < connection->max_cm_retries) {
		int ret;

		ib_cm_count_resend(&connection->mad);
		ret = ib_mad_send(&connection->mad);

		if (ret)
			TS_REPORT_WARN(MOD_IB_CM, "%s resend failed. <%d>",
				       connection->state == IB_CM_STATE_REQ_SENT
				       ? "REQ" : "REP",
				       ret);

		connection->timer.run_time =
			jiffies + ib_cm_timeout_to_jiffies(connection->cm_response_timeout);
		tsKernelTimerAdd(&connection->timer);

		ib_cm_connection_put(connection);
	} else {
		if (ib_cm_rej_send(connection->local_cm_device,
				   connection->local_cm_port,
				   connection->local_cm_pkey_index,
				   connection->remote_cm_lid,
				   connection->remote_cm_qpn,
				   be64_to_cpu(connection->transaction_id),
				   connection->local_comm_id,
				   connection->remote_comm_id,
				   IB_REJ_NO_MESSAGE,
				   TS_IB_REJ_TIMEOUT,
				   NULL,
				   0))
			TS_REPORT_WARN(MOD_IB_CM, "REJ send failed");

		ib_cm_qp_to_error(connection->local_qp);

		{
			struct ib_cm_idle_param params = { 0 };

			params.reason = TS_IB_CM_IDLE_REMOTE_TIMEOUT;

			connection->state = IB_CM_STATE_IDLE;

			(void) ib_cm_consumer_free_callback(&connection, TS_IB_CM_IDLE, &params);
		}
	}
}

static void ib_cm_timewait_timeout(void *conn_ptr)
{
	tTS_IB_CM_COMM_ID comm_id = (unsigned long) conn_ptr;
	struct ib_cm_connection *connection;
	struct ib_cm_idle_param  params = { 0 };

	params.reason = TS_IB_CM_IDLE_TIME_WAIT_DONE;

	connection = ib_cm_connection_find(comm_id);
	if (!connection)
		return;

	connection->state = IB_CM_STATE_IDLE;

	/* Time Wait timer expired, all we have to do is free this connection */
	(void) ib_cm_consumer_free_callback(&connection, TS_IB_CM_IDLE, &params);
}

void ib_cm_timewait(struct ib_cm_connection      **connection,
                    tTS_IB_CM_DISCONNECTED_REASON  reason)
{
	struct ib_cm_disconnected_param params;
	tTS_IB_CM_CALLBACK_RETURN result;

	/*
	  Make sure that an active connection gets an ESTABLISH callback
	  before a TIME_WAIT callback.  Since we do the ESTABLISH callback
	  from a timer, it is possible for the remote side to disconnect
	  before the ESTABLISH callback gets to run unless we're careful.
	*/
	if ((*connection)->establish_pending) {
		tTS_IB_CM_COMM_ID comm_id = (*connection)->local_comm_id;

		ib_cm_connection_put(*connection);
		ib_cm_rtu_done((void *) (unsigned long) comm_id);
		*connection = ib_cm_connection_find(comm_id);
		if (!*connection)
			return;
	}

	params.reason = reason;

	(*connection)->state          = IB_CM_STATE_TIME_WAIT;
	(*connection)->cm_retry_count = 0;
	(*connection)->timer.function = ib_cm_timewait_timeout;
	/* XXX - should use packet lifetime to compute timeout */
	tsKernelTimerModify(&(*connection)->timer, jiffies + HZ / 50);

	result = ib_cm_consumer_callback(connection,
					 TS_IB_CM_DISCONNECTED,
					 &params);
	if (*connection && result != TS_IB_CM_CALLBACK_PROCEED)
		(void) ib_cm_drop_consumer_internal(*connection);
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager packet sending.                                 */
/*                                                                       */
/* --------------------------------------------------------------------- */

int ib_cm_mra_send(struct ib_cm_connection *connection,
		   int                      service_timeout,
		   void                	   *mra_private_data,
		   int                 	    mra_private_data_len)
{
	int message;

	switch (connection->state) {
	case IB_CM_STATE_REQ_RECEIVED:
		message = IB_MRA_REQ;
		connection->state = IB_CM_STATE_MRA_SENT;
		break;

	case IB_CM_STATE_REP_RECEIVED:
		message = IB_MRA_REP;
		connection->state = IB_CM_STATE_MRA_REP_SENT;
		break;

	default:
		return -EINVAL;
	}

	ib_mad_build_header(&connection->mad);

	connection->mad.attribute_id   = cpu_to_be16(IB_COM_MGT_MRA);
	connection->mad.transaction_id = cpu_to_be64(connection->transaction_id);

	connection->mad.device     = connection->local_cm_device;
	connection->mad.port       = connection->local_cm_port;
	connection->mad.pkey_index = connection->local_cm_pkey_index;
	connection->mad.sqpn       = TS_IB_GSI_QP;
	connection->mad.dlid       = connection->remote_cm_lid;
	connection->mad.dqpn       = connection->remote_cm_qpn;

	ib_cm_mra_local_comm_id_set  (&connection->mad, connection->local_comm_id);
	ib_cm_mra_remote_comm_id_set (&connection->mad, connection->remote_comm_id);
	ib_cm_mra_message_set        (&connection->mad, message);
	ib_cm_mra_service_timeout_set(&connection->mad, service_timeout);

	if (mra_private_data && mra_private_data_len > 0)
		memcpy(ib_cm_mra_private_data_get(&connection->mad),
		       mra_private_data,
		       min(mra_private_data_len, ib_cm_mra_private_data_get_length()));

	ib_cm_count_send(&connection->mad);
	return ib_mad_send(&connection->mad);
}

int ib_cm_rej_send(tTS_IB_DEVICE_HANDLE local_cm_device,
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
		   int               reply_size  /* private size */)
{
	int            result = 0;
	struct ib_mad *packet;

	if (IB_CM_REJ_MAX_PRIVATE_DATA < reply_size)
		return -EINVAL;

	if (IB_REJ_REP != type && IB_REJ_REQ != type && IB_REJ_NO_MESSAGE != type)
		return -EINVAL;

	packet = kmalloc(sizeof *packet, GFP_KERNEL);
	if (!packet)
		return -ENOMEM;

	ib_mad_build_header(packet);

	/* copy private data */
	if (reply_data && reply_size > 0)
		memcpy(ib_cm_rej_private_data_get(packet), reply_data, reply_size);

	packet->attribute_id   = cpu_to_be16(IB_COM_MGT_REJ);
	packet->transaction_id = cpu_to_be64(transaction_id);

	ib_cm_rej_local_comm_id_set (packet, local_comm_id);
	ib_cm_rej_remote_comm_id_set(packet, remote_comm_id);
	ib_cm_rej_message_set       (packet, type);
	ib_cm_rej_info_length_set   (packet, 0);
	ib_cm_rej_reason_set        (packet, reason);

	packet->device     = local_cm_device;
	packet->port       = local_cm_port;
	packet->pkey_index = pkey_index;
	packet->sqpn       = TS_IB_GSI_QP;
	packet->dlid       = remote_cm_lid;
	packet->dqpn       = remote_cm_qpn;

	packet->completion_func = NULL;

	ib_cm_count_send(packet);
	result = ib_mad_send(packet);
	if (result)
		TS_REPORT_WARN(MOD_IB_CM, "REJ send failed");

	kfree(packet);

	return result;
}

static void ib_cm_drep_timeout(void *conn_ptr)
{
	tTS_IB_CM_COMM_ID comm_id = (unsigned long) conn_ptr;
	struct ib_cm_connection *connection;

	connection = ib_cm_connection_find(comm_id);
	if (!connection)
		return;

	if (connection->state != IB_CM_STATE_DREQ_SENT) {
		/* We got the response while the timer was waiting to run */
		ib_cm_connection_put(connection);
		return;
	}

	TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
		 "Timeout waiting for DREP; connection ID 0x%08x",
		 connection->local_comm_id);

	++connection->cm_retry_count;

	if (connection->cm_retry_count < connection->max_cm_retries) {
		int ret;

		ib_cm_count_resend(&connection->mad);
		ret = ib_mad_send(&connection->mad);

		if (ret) {
			TS_REPORT_WARN(MOD_IB_CM, "DREQ resend failed. <%d>", ret);
		}

		connection->timer.run_time =
			jiffies + ib_cm_timeout_to_jiffies(connection->cm_response_timeout);
		tsKernelTimerAdd(&connection->timer);
	} else {
		ib_cm_timewait(&connection, TS_IB_CM_DISCONNECTED_REMOTE_TIMEOUT);
	}

	ib_cm_connection_put(connection);
}

int ib_cm_dreq_send(struct ib_cm_connection *connection)
{
	int ret = 0;

	ib_mad_build_header(&connection->mad);

	connection->mad.attribute_id   = cpu_to_be16(IB_COM_MGT_DREQ);
	connection->mad.transaction_id = ib_cm_tid_generate();

	ib_cm_dreq_local_comm_id_set (&connection->mad, connection->local_comm_id);
	ib_cm_dreq_remote_comm_id_set(&connection->mad, connection->remote_comm_id);
	ib_cm_dreq_remote_qpn_set    (&connection->mad, connection->remote_qpn);

	connection->mad.device     = connection->local_cm_device;
	connection->mad.port       = connection->local_cm_port;
	connection->mad.pkey_index = connection->local_cm_pkey_index;
	connection->mad.sqpn       = TS_IB_GSI_QP;
	connection->mad.dlid       = connection->remote_cm_lid;
	connection->mad.dqpn       = connection->remote_cm_qpn;

	ib_cm_count_send(&connection->mad);
	ret = ib_mad_send(&connection->mad);
	if (ret)
		TS_REPORT_WARN(MOD_IB_CM, "DREQ send failed");

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "Sent DREQ");

	ib_cm_qp_to_error(connection->local_qp);

	if (connection->state != IB_CM_STATE_DREQ_SENT
	    && connection->state != IB_CM_STATE_TIME_WAIT
	    && connection->state != IB_CM_STATE_DREQ_RECEIVED) {
		connection->state          = IB_CM_STATE_DREQ_SENT;
		connection->cm_retry_count = 0;

		connection->timer.function = ib_cm_drep_timeout;
		tsKernelTimerModify(&connection->timer,
				    jiffies +
				    ib_cm_timeout_to_jiffies(connection->cm_response_timeout));
	}

	return ret;
}

/* --------------------------------------------------------------------- */
/*                                                                       */
/* communication manager state callbacks.                                */
/*                                                                       */
/* --------------------------------------------------------------------- */

void ib_cm_rej_handler(struct ib_mad *packet)
{
	struct ib_cm_connection *connection;

	int result;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "REJ received");

	if (ib_cm_rej_remote_comm_id_get(packet) != TS_IB_CM_COMM_ID_INVALID) {
		connection = ib_cm_connection_find(ib_cm_rej_remote_comm_id_get(packet));

		if (!connection) {
			TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
				 "REJ (reason %d) received for unknown local comm id 0x%08x",
				 ib_cm_rej_reason_get(packet),
				 ib_cm_rej_remote_comm_id_get(packet));
			return;
		}

		if (connection->remote_comm_id          != TS_IB_CM_COMM_ID_INVALID &&
		    ib_cm_rej_local_comm_id_get(packet) != TS_IB_CM_COMM_ID_INVALID &&
		    ib_cm_rej_local_comm_id_get(packet) != connection->remote_comm_id) {
			TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
				 "REJ (reason %d) has local comm id 0x%08x, expected 0x%08x",
				 ib_cm_rej_reason_get(packet),
				 ib_cm_rej_local_comm_id_get(packet),
				 connection->remote_comm_id);
			ib_cm_connection_put(connection);
			return;
		}
	} else {
		connection = ib_cm_connection_find_remote_id(ib_cm_rej_local_comm_id_get(packet));

		if (!connection) {
			TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
				 "REJ (reason %d) received for unknown remote comm id 0x%08x",
				 ib_cm_rej_reason_get(packet),
				 ib_cm_rej_remote_comm_id_get(packet));
			return;
		}
	}

	switch (connection->state) {
	case IB_CM_STATE_REQ_SENT:
	case IB_CM_STATE_REQ_RECEIVED:
	case IB_CM_STATE_REP_SENT:
	case IB_CM_STATE_MRA_SENT:
	case IB_CM_STATE_MRA_REP_SENT:
		if (connection->local_qp != TS_IB_HANDLE_INVALID)
			ib_cm_qp_to_error(connection->local_qp);
		break;

	default:
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "REJ received for connection in inappropriate state %d",
			 connection->state);
		ib_cm_connection_put(connection);
		return;
	}

	/* call connection callback. result dosn't matter */
	{
		struct ib_cm_idle_param params = { 0 };

		params.reason       = TS_IB_CM_IDLE_REMOTE_REJECT;
		params.rej_reason   = ib_cm_rej_reason_get(packet);
		params.rej_info_len = ib_cm_rej_info_length_get(packet);
		params.rej_info     = ib_cm_rej_additional_info_get(packet);

		connection->state = IB_CM_STATE_IDLE;
		result = ib_cm_consumer_free_callback(&connection, TS_IB_CM_IDLE, &params);
	}

	if (result)
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_PASSIVE,
			 "REJ: connect callback error <%d> ignored.", result);
}

void ib_cm_mra_handler(struct ib_mad *packet)
{
	struct ib_cm_connection *connection;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "MRA received");

	connection = ib_cm_connection_find(ib_cm_mra_remote_comm_id_get(packet));

	if (!connection) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "MRA for unknown comm id 0x%08x",
			 ib_cm_mra_remote_comm_id_get(packet));
		return;
	}

	if (connection->state != IB_CM_STATE_REQ_SENT &&
	    connection->state != IB_CM_STATE_REP_SENT) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "MRA for connection 0x%08x in inappropriate state %d",
			 ib_cm_mra_remote_comm_id_get(packet),
			 connection->state);
		goto out;
	}

	if ((connection->state == IB_CM_STATE_REQ_SENT &&
	     ib_cm_mra_message_get(packet) != IB_MRA_REQ) ||
	    (connection->state == IB_CM_STATE_REP_SENT &&
	     ib_cm_mra_message_get(packet) != IB_MRA_REP)) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "MRA with message %d for connection 0x%08x in state %d",
			 ib_cm_mra_message_get(packet),
			 ib_cm_mra_remote_comm_id_get(packet),
			 connection->state);
		goto out;
	}

	connection->cm_retry_count = 0;
	tsKernelTimerModify(&connection->timer,
			    jiffies + 
			    ib_cm_timeout_to_jiffies(ib_cm_mra_service_timeout_get(packet)));

 out:
	ib_cm_connection_put(connection);
}

static int ib_cm_dreq_check(struct ib_mad *packet,
                            struct ib_cm_connection *connection)
{
	if (connection->local_qpn != ib_cm_dreq_remote_qpn_get(packet)) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "DREQ for comm id 0x%08x, "
			 "qpn mismatch rcvd:%d != stored:%d",
			 ib_cm_dreq_local_comm_id_get(packet),
			 ib_cm_dreq_remote_qpn_get(packet),
			 connection->local_qpn);
		return 1;
	}

	if (connection->remote_comm_id != ib_cm_dreq_local_comm_id_get(packet)) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "DREQ for comm id rcvd:0x%08x != stored:0x%08x",
			 ib_cm_dreq_remote_comm_id_get(packet), connection->local_comm_id);
		return 1;
	}

	if (connection->state == IB_CM_STATE_TIME_WAIT &&
	    ++connection->cm_retry_count > connection->max_cm_retries)
		return 1;

	return 0;
}

void ib_cm_dreq_handler(struct ib_mad *packet)
{
	struct ib_cm_connection *connection;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "DREQ received");

	connection = ib_cm_connection_find(ib_cm_dreq_remote_comm_id_get(packet));
	if (!connection) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "DREQ for unknown comm id 0x%08x",
			 ib_cm_dreq_local_comm_id_get(packet));
		/* We send a DREP anyway, since this is probably a connection
		   where our DREP was dropped.  We went to TIME_WAIT and then
		   IDLE, but the other side is still waiting for a DREP. */
		{
			struct ib_mad *drep;

			drep = kmalloc(sizeof *drep, GFP_KERNEL);
			if (!drep) {
				TS_REPORT_WARN(MOD_IB_CM,
					       "No memory for DREP");
				return;
			}

			ib_mad_build_header(drep);

			drep->attribute_id   = cpu_to_be16(IB_COM_MGT_DREP);
			drep->transaction_id = packet->transaction_id;

			ib_cm_drep_local_comm_id_set (drep, ib_cm_dreq_remote_comm_id_get(packet));
			ib_cm_drep_remote_comm_id_set(drep, ib_cm_drep_local_comm_id_get (packet));

			drep->device     = packet->device;
			drep->port       = packet->port;
			drep->pkey_index = packet->pkey_index;
			drep->sqpn       = TS_IB_GSI_QP;
			drep->dlid       = packet->slid;
			drep->dqpn       = packet->sqpn;

			drep->completion_func = NULL;

			ib_cm_count_resend(drep);
			if (ib_mad_send(drep))
				TS_REPORT_WARN(MOD_IB_CM, "DREP resend failed");

			kfree(drep);
		}
		return;
	}

	if (ib_cm_dreq_check(packet, connection)) {
		ib_cm_connection_put(connection);
		return;
	}

	if (connection->state != IB_CM_STATE_TIME_WAIT) {
		connection->state = IB_CM_STATE_DREQ_RECEIVED;

		ib_cm_qp_to_error(connection->local_qp);
	}

	ib_mad_build_header(&connection->mad);

	connection->mad.attribute_id   = cpu_to_be16(IB_COM_MGT_DREP);
	connection->mad.transaction_id = packet->transaction_id;

	ib_cm_drep_local_comm_id_set (&connection->mad, connection->local_comm_id);
	ib_cm_drep_remote_comm_id_set(&connection->mad, connection->remote_comm_id);

	connection->mad.device     = connection->local_cm_device;
	connection->mad.port       = connection->local_cm_port;
	connection->mad.pkey_index = connection->local_cm_pkey_index;
	connection->mad.dlid       = connection->remote_cm_lid;
	connection->mad.dqpn       = connection->remote_cm_qpn;

	ib_cm_count_send(&connection->mad);
	if (ib_mad_send(&connection->mad))
		TS_REPORT_WARN(MOD_IB_CM, "DREP send failed");

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "Sent DREP");

	if (connection->state != IB_CM_STATE_TIME_WAIT)
		ib_cm_timewait(&connection, TS_IB_CM_DISCONNECTED_REMOTE_CLOSE);

	if (connection)
		ib_cm_connection_put(connection);
}

void ib_cm_drep_handler(struct ib_mad *packet)
{
	struct ib_cm_connection *connection;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "DREP received");

	connection = ib_cm_connection_find(ib_cm_drep_remote_comm_id_get(packet));
	if (!connection) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "DREP for unknown comm id 0x%08x",
			 ib_cm_drep_local_comm_id_get(packet));
		return;
	}

	if (connection->state == IB_CM_STATE_DREQ_SENT) {
		ib_cm_timewait(&connection, TS_IB_CM_DISCONNECTED_LOCAL_CLOSE);
	} else {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_ACTIVE,
			 "Ignoring DREP for connection 0x%08x in state %d",
			 connection->local_comm_id,
			 connection->state);
	}

	ib_cm_connection_put(connection);
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
