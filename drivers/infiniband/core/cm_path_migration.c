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

  $Id: cm_path_migration.c 32 2004-04-09 03:57:42Z roland $
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

static int ib_cm_alt_path_load(struct ib_cm_connection *connection)
{
	struct ib_qp_attribute *qp_attr;
	int result;

	qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
	if (!qp_attr)
		return -ENOMEM;

	memset(qp_attr, 0, sizeof *qp_attr);

	/* XXX need to include CA ACK delay */
	qp_attr->alt_local_ack_timeout        = min(31, connection->alternate_path.packet_life + 1);
	qp_attr->alt_address.service_level    = connection->alternate_path.sl;
	qp_attr->alt_address.dlid             = connection->alternate_path.dlid;
	qp_attr->alt_address.source_path_bits = connection->alternate_path.slid & 0x7f;
	qp_attr->alt_address.static_rate      = 0;
	qp_attr->alt_address.use_grh          = 0;
	qp_attr->migration_state              = TS_IB_REARM;

	if (ib_cached_gid_find(connection->alternate_path.sgid, NULL, &qp_attr->alt_port, NULL)) {
		result = -EINVAL;
		goto out;
	}

	if (ib_cached_pkey_find(connection->local_cm_device,
				qp_attr->alt_port,
				connection->alternate_path.pkey,
				&qp_attr->alt_pkey_index)) {
		result = -EINVAL;
		goto out;
	}

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
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

	result = ib_cm_qp_modify(connection->local_qp,
				 qp_attr);

	if (result) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "ib_qp_modify to load alternate path failed <%d>",
			       result);
	} else {
		connection->alternate_remote_cm_lid = connection->alternate_path.dlid;
	}

 out:
	kfree(qp_attr);

	return result;
}

static void ib_cm_apr_timeout(void *conn_ptr)
{
	tTS_IB_CM_COMM_ID comm_id = (unsigned long) conn_ptr;

	struct ib_cm_connection *connection;

	connection = ib_cm_connection_find(comm_id);
	if (!connection)
		return;

	if (connection->state != IB_CM_STATE_ESTABLISHED
	    || !connection->lap_pending) {
		ib_cm_connection_put(connection);
		return;
	}

	++connection->retry_count;

	if (connection->retry_count < connection->max_cm_retries) {
		int ret;

		ib_cm_count_resend(&connection->mad);
		ret = ib_mad_send(&connection->mad);

		if (ret) {
			TS_REPORT_WARN(MOD_IB_CM, "LAP resend failed. <%d>", ret);
		}

		connection->timer.run_time =
			jiffies + ib_cm_timeout_to_jiffies(connection->cm_response_timeout);
		tsKernelTimerAdd(&connection->timer);
	} else {
		TS_REPORT_WARN(MOD_IB_CM, "LAP retry count exceeded");
		/* XXX call back consumer?? */
	}

	ib_cm_connection_put(connection);
}

int ib_cm_lap_send(struct ib_cm_connection *connection,
		   struct ib_path_record   *alternate_path)
{
	int ret;

	ib_mad_build_header(&connection->mad);

	connection->mad.attribute_id   = cpu_to_be16(IB_COM_MGT_LAP);
	connection->mad.transaction_id = ib_cm_tid_generate();

	connection->mad.device     = connection->local_cm_device;
	connection->mad.port       = connection->local_cm_port;
	connection->mad.pkey_index = connection->local_cm_pkey_index;
	connection->mad.sqpn       = TS_IB_GSI_QP;
	connection->mad.dlid       = connection->remote_cm_lid;
	connection->mad.dqpn       = connection->remote_cm_qpn;

	ib_cm_lap_local_comm_id_set             (&connection->mad, connection->local_comm_id);
	ib_cm_lap_remote_comm_id_set            (&connection->mad, connection->remote_comm_id);
	ib_cm_lap_remote_qpn_set                (&connection->mad, connection->remote_qpn);
	ib_cm_lap_remote_cm_timeout_set         (&connection->mad, connection->cm_response_timeout);
	ib_cm_lap_alternate_local_lid_set       (&connection->mad, alternate_path->slid);
	ib_cm_lap_alternate_remote_lid_set      (&connection->mad, alternate_path->dlid);

	memcpy(ib_cm_lap_alternate_local_gid_get(&connection->mad),
	       alternate_path->sgid,
	       ib_cm_lap_alternate_remote_gid_get_length());
	memcpy(ib_cm_lap_alternate_remote_gid_get(&connection->mad),
	       alternate_path->dgid,
	       ib_cm_lap_alternate_remote_gid_get_length());

	ib_cm_lap_alternate_flow_label_set      (&connection->mad, alternate_path->flowlabel);
	ib_cm_lap_alternate_traffic_set         (&connection->mad, alternate_path->tclass);
	ib_cm_lap_alternate_hop_limit_set       (&connection->mad, alternate_path->hoplmt);
	ib_cm_lap_alternate_sl_set              (&connection->mad, alternate_path->sl);
	ib_cm_lap_alternate_subnet_local_set    (&connection->mad, 1);
	/* XXX need to include CA ACK delay */
	ib_cm_lap_alternate_local_ack_timeout_set(&connection->mad,
						  min(31, alternate_path->packet_life + 1));

	ib_cm_count_send(&connection->mad);
	ret = ib_mad_send(&connection->mad);
	if (ret) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "LAP send failed");
	}

	connection->lap_pending = 1;
	connection->retry_count = 0;

	connection->timer.function = ib_cm_apr_timeout;
	tsKernelTimerModify(&connection->timer,
			    jiffies + ib_cm_timeout_to_jiffies(connection->cm_response_timeout));

	return ret;
}

void ib_cm_lap_handler(struct ib_mad *packet)
{
	struct ib_cm_connection         *connection;
	struct ib_cm_lap_received_param  params;
	tTS_IB_CM_APR_STATUS             ap_status;
	int                              result;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "LAP received");

	connection = ib_cm_connection_find(ib_cm_lap_remote_comm_id_get(packet));
	if (!connection) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "LAP for unknown comm id 0x%08x",
			 ib_cm_lap_local_comm_id_get(packet));
		return;
	}

	if (connection->state != IB_CM_STATE_ESTABLISHED) {
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

	if (connection->local_qpn != ib_cm_lap_remote_qpn_get(packet)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "LAP for connection 0x%08x has QPN 0x%06x (expected 0x%06x)",
			       connection->local_comm_id,
			       ib_cm_lap_remote_qpn_get(packet),
			       connection->local_qpn);
		ap_status = TS_IB_APR_QPN_MISMATCH;
		goto out_send_apr;
	}

	params.alternate_path.slid      = ib_cm_lap_alternate_remote_lid_get(packet);
	params.alternate_path.dlid      = ib_cm_lap_alternate_local_lid_get (packet);

	memcpy(params.alternate_path.sgid,
	       ib_cm_lap_alternate_remote_gid_get(packet),
	       ib_cm_lap_alternate_remote_gid_get_length());
	memcpy(params.alternate_path.dgid,
	       ib_cm_lap_alternate_local_gid_get(packet),
	       ib_cm_lap_alternate_local_gid_get_length());

	params.alternate_path.flowlabel   = ib_cm_lap_alternate_flow_label_get       (packet);
	params.alternate_path.tclass      = ib_cm_lap_alternate_traffic_get          (packet);
	params.alternate_path.hoplmt      = ib_cm_lap_alternate_hop_limit_get        (packet);
	params.alternate_path.sl          = ib_cm_lap_alternate_sl_get               (packet);
	/* We abuse packet life and put local ACK timeout there */
	params.alternate_path.packet_life = ib_cm_lap_alternate_local_ack_timeout_get(packet);

	/* Call the consumer back to see if we should accept the new path */
	result = ib_cm_consumer_callback(&connection, TS_IB_CM_LAP_RECEIVED, &params);

	/* See if the connection went away -- if so, just give up */
	if (!connection)
		return;

	/* If the consumer returned non-zero, reject the alternate path */
	if (result) {
		ap_status = TS_IB_APR_PATH_REJECTED;
		goto out_send_apr;
	}

	connection->alternate_path = params.alternate_path;

	if (ib_cm_alt_path_load(connection)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "Failed to load alternate path for connection 0x%08x",
			       connection->local_comm_id);
		/* XXX should we reject path? */
	}

	ap_status = TS_IB_APR_PATH_LOADED;

 out_send_apr:
	ib_mad_build_header(&connection->mad);

	connection->mad.attribute_id   = cpu_to_be16(IB_COM_MGT_APR);
	connection->mad.transaction_id = packet->transaction_id;

	connection->mad.device     = connection->local_cm_device;
	connection->mad.port       = connection->local_cm_port;
	connection->mad.pkey_index = connection->local_cm_pkey_index;
	connection->mad.sqpn       = TS_IB_GSI_QP;
	connection->mad.dlid       = connection->remote_cm_lid;
	connection->mad.dqpn       = connection->remote_cm_qpn;

	ib_cm_apr_local_comm_id_set (&connection->mad, connection->local_comm_id);
	ib_cm_apr_remote_comm_id_set(&connection->mad, connection->remote_comm_id);
	ib_cm_apr_ap_status_set    (&connection->mad, ap_status);
	/* XXX we leave info_length as 0 and don't set additional_info */

	ib_cm_count_send(&connection->mad);
	if (ib_mad_send(&connection->mad)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "APR send failed");
	}

 out:
	ib_cm_connection_put(connection);
	return;

}

void ib_cm_apr_handler(struct ib_mad *packet)
{
	struct ib_cm_connection *connection;

	TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
		 "APR received");

	connection = ib_cm_connection_find(ib_cm_apr_remote_comm_id_get(packet));
	if (!connection) {
		TS_TRACE(MOD_IB_CM, T_VERBOSE, TRACE_IB_CM_GEN,
			 "APR for unknown comm id 0x%08x",
			 ib_cm_apr_local_comm_id_get(packet));
		return;
	}

	if (connection->state != IB_CM_STATE_ESTABLISHED) {
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

	if (ib_cm_apr_ap_status_get(packet) == TS_IB_APR_PATH_LOADED) {
		if (ib_cm_alt_path_load(connection)) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "Alternate path load failed for connection 0x%08x",
				       connection->local_comm_id);
		}
	} else {
		TS_REPORT_WARN(MOD_IB_CM,
			       "Remote CM rejected APR for connection 0x%08x (status %d)",
			       connection->local_comm_id,
			       ib_cm_apr_ap_status_get(packet));
	}

	/* Call the consumer back with APR status */
	{
		struct ib_cm_apr_received_param params = { 0 };

		params.ap_status    = ib_cm_apr_ap_status_get      (packet);
		params.apr_info_len = ib_cm_apr_info_length_get    (packet);
		params.apr_info     = ib_cm_apr_additional_info_get(packet);

		/* ignore return value */
		(void) ib_cm_consumer_callback(&connection, TS_IB_CM_APR_RECEIVED, &params);
	}

 out:
	if (connection) {
		ib_cm_connection_put(connection);
	}
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
