/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include <scsi/scsi_host.h>

#include <scsi/libfc/libfc.h>

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("libfc");
MODULE_LICENSE("GPL");

void fc_get_host_port_id(struct Scsi_Host *shost)
{
	struct fc_lport *lp = shost_priv(shost);

	fc_host_port_id(shost) = fc_lport_get_fid(lp);
}
EXPORT_SYMBOL(fc_get_host_port_id);

void fc_get_host_speed(struct Scsi_Host *shost)
{
	/*
	 * should be obtain from DEC or Enet Driver
	 */
	fc_host_speed(shost) = 1;	/* for now it is 1g */
}
EXPORT_SYMBOL(fc_get_host_speed);

void fc_get_host_port_type(struct Scsi_Host *shost)
{
	fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
}
EXPORT_SYMBOL(fc_get_host_port_type);

void fc_get_host_fabric_name(struct Scsi_Host *shost)
{
	struct fc_lport *lp = shost_priv(shost);

	fc_host_fabric_name(shost) = lp->wwnn;
}
EXPORT_SYMBOL(fc_get_host_fabric_name);

void fc_attr_init(struct fc_lport *lp)
{
	fc_host_node_name(lp->host) = lp->wwnn;
	fc_host_port_name(lp->host) = lp->wwpn;
	fc_host_supported_classes(lp->host) = FC_COS_CLASS3;
	memset(fc_host_supported_fc4s(lp->host), 0,
	       sizeof(fc_host_supported_fc4s(lp->host)));
	fc_host_supported_fc4s(lp->host)[2] = 1;
	fc_host_supported_fc4s(lp->host)[7] = 1;
	/* This value is also unchanging */
	memset(fc_host_active_fc4s(lp->host), 0,
	       sizeof(fc_host_active_fc4s(lp->host)));
	fc_host_active_fc4s(lp->host)[2] = 1;
	fc_host_active_fc4s(lp->host)[7] = 1;
	fc_host_maxframe_size(lp->host) = lp->mfs;
}
EXPORT_SYMBOL(fc_attr_init);

void fc_set_rport_loss_tmo(struct fc_rport *rport, u32 timeout)
{
	if (timeout)
		rport->dev_loss_tmo = timeout + 5;
	else
		rport->dev_loss_tmo = 30;

}
EXPORT_SYMBOL(fc_set_rport_loss_tmo);

struct fc_host_statistics *fc_get_host_stats(struct Scsi_Host *shost)
{
	int i;
	struct fc_host_statistics *fcoe_stats;
	struct fc_lport *lp = shost_priv(shost);
	struct timespec v0, v1;

	fcoe_stats = &lp->host_stats;
	memset(fcoe_stats, 0, sizeof(struct fc_host_statistics));

	jiffies_to_timespec(jiffies, &v0);
	jiffies_to_timespec(lp->boot_time, &v1);
	fcoe_stats->seconds_since_last_reset = (v0.tv_sec - v1.tv_sec);

	for_each_online_cpu(i) {
		struct fcoe_dev_stats *stats = lp->dev_stats[i];
		if (stats == NULL)
			continue;
		fcoe_stats->tx_frames += stats->TxFrames;
		fcoe_stats->tx_words += stats->TxWords;
		fcoe_stats->rx_frames += stats->RxFrames;
		fcoe_stats->rx_words += stats->RxWords;
		fcoe_stats->error_frames += stats->ErrorFrames;
		fcoe_stats->invalid_crc_count += stats->InvalidCRCCount;
		fcoe_stats->fcp_input_requests += stats->InputRequests;
		fcoe_stats->fcp_output_requests += stats->OutputRequests;
		fcoe_stats->fcp_control_requests += stats->ControlRequests;
		fcoe_stats->fcp_input_megabytes += stats->InputMegabytes;
		fcoe_stats->fcp_output_megabytes += stats->OutputMegabytes;
		fcoe_stats->link_failure_count += stats->LinkFailureCount;
	}
	fcoe_stats->lip_count = -1;
	fcoe_stats->nos_count = -1;
	fcoe_stats->loss_of_sync_count = -1;
	fcoe_stats->loss_of_signal_count = -1;
	fcoe_stats->prim_seq_protocol_err_count = -1;
	fcoe_stats->dumped_frames = -1;
	return fcoe_stats;
}
EXPORT_SYMBOL(fc_get_host_stats);
