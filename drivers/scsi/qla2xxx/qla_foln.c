/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP2x00 device driver for Linux 2.6.x
* Copyright (C) 2003-2004 QLogic Corporation
* (www.qlogic.com)
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2, or (at your option) any
* later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
******************************************************************************/

#include "qla_os.h"
#include "qla_def.h"

#include "qlfo.h"
#include "qlfolimits.h"
#include "qla_foln.h"

int ql2xfailover = 1;
module_param(ql2xfailover, int, 0);
MODULE_PARM_DESC(ql2xfailover,
		"Driver failover support: 0 to disable; 1 to enable.");

int ql2xrecoveryTime = MAX_RECOVERYTIME;
module_param_named(recoveryTime, ql2xrecoveryTime, int, 0);
MODULE_PARM_DESC(recoveryTime,
		"Recovery time in seconds before a target device is sent I/O "
		"after a failback is performed.");

int ql2xfailbackTime = MAX_FAILBACKTIME;
module_param_named(failbackTime, ql2xfailbackTime, int, 0);
MODULE_PARM_DESC(failbackTime,
		"Delay in seconds before a failback is performed.");

int MaxPathsPerDevice = 0;
module_param(MaxPathsPerDevice, int, 0);
MODULE_PARM_DESC(MaxPathsPerDevice,
		"Maximum number of paths to a device.  Default 8.");

int MaxRetriesPerPath = 0;
module_param(MaxRetriesPerPath, int, 0);
MODULE_PARM_DESC(MaxRetriesPerPath,
		"How many retries to perform on the current path before "
		"failing over to the next path in the path list.");

int MaxRetriesPerIo = 0;
module_param(MaxRetriesPerIo, int, 0);
MODULE_PARM_DESC(MaxRetriesPerIo,
		"How many total retries to do before failing the command and "
		"returning to the OS with a DID_NO_CONNECT status.");

int qlFailoverNotifyType = 0;
module_param(qlFailoverNotifyType, int, 0);
MODULE_PARM_DESC(qlFailoverNotifyType,
		"Failover notification mechanism to use when a failover or "
		"failback occurs.");
		
struct cfg_device_info cfg_device_list[] = {

	{"COMPAQ", "MSA1000", 2, FO_NOTIFY_TYPE_SPINUP, 
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"HITACHI", "OPEN-", 1, FO_NOTIFY_TYPE_NONE,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"HP", "OPEN-", 1, FO_NOTIFY_TYPE_NONE,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"COMPAQ", "HSV110 (C)COMPAQ", 4, FO_NOTIFY_TYPE_SPINUP,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"HP", "HSV100", 4, FO_NOTIFY_TYPE_SPINUP,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"DEC", "HSG80", 8, FO_NOTIFY_TYPE_NONE,   
		qla2x00_export_target, NULL, NULL, NULL },

	/*
	 * Must be at end of list...
	 */
	{NULL, NULL }
};

/*
 * qla2x00_check_for_devices_online
 *
 *	Check fcport state of all devices to make sure online.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Return:
 *	None.
 *
 * Context:
 */
static uint8_t
qla2x00_check_for_devices_online(scsi_qla_host_t *ha) 
{
	fc_port_t	*fcport;


	list_for_each_entry(fcport, &ha->fcports, list) {
		if(fcport->port_type != FCT_TARGET)
			continue;

		if ((atomic_read(&fcport->state) == FCS_ONLINE) ||
		    (atomic_read(&fcport->state) == FCS_DEVICE_DEAD) ||
		    fcport->flags & FCF_FAILBACK_DISABLE)
			continue;

		return 0;
	}

	return 1;
}

/*
 *  qla2x00_failover_cleanup
 *	Cleanup queues after a failover.
 *
 * Input:
 *	sp = command pointer
 *
 * Context:
 *	Interrupt context.
 */
static void
qla2x00_failover_cleanup(srb_t *sp) 
{
	sp->cmd->result = DID_BUS_BUSY << 16;
	sp->cmd->host_scribble = (unsigned char *) NULL;

	/* turn-off all failover flags */
	sp->flags = sp->flags & ~(SRB_RETRY|SRB_FAILOVER|SRB_FO_CANCEL);
}

int
qla2x00_suspend_failover_targets(scsi_qla_host_t *ha)
{
	unsigned long flags;
	struct list_head *list, *temp;
	srb_t *sp;
	int count;
	os_tgt_t *tq;

	spin_lock_irqsave(&ha->list_lock, flags);
	count = ha->failover_cnt;
	list_for_each_safe(list, temp, &ha->failover_queue) {
		sp = list_entry(ha->failover_queue.next, srb_t, list);
		tq = sp->tgt_queue;
		if (!(test_bit(TQF_SUSPENDED, &tq->flags)))
			set_bit(TQF_SUSPENDED, &tq->flags);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	return count;
}

srb_t *
qla2x00_failover_next_request(scsi_qla_host_t *ha)
{
	unsigned long flags;
	srb_t *sp = NULL;

	spin_lock_irqsave(&ha->list_lock, flags);
	if (!list_empty(&ha->failover_queue)) {
		sp = list_entry(ha->failover_queue.next, srb_t, list);
		__del_from_failover_queue(ha, sp);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	return sp;
}

/*
 *  qla2x00_process_failover
 *	Process any command on the failover queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
static void
qla2x00_process_failover(scsi_qla_host_t *ha) 
{

	os_tgt_t	*tq;
	os_lun_t	*lq;
	srb_t       *sp;
	fc_port_t *fcport;
	uint32_t    t, l;
	scsi_qla_host_t *vis_ha = ha;
	int count, i;

	DEBUG2(printk(KERN_INFO "%s: hba %ld active=%ld, retry=%d, "
			"done=%ld, failover=%d, scsi retry=%d commands.\n",
			__func__,
			ha->host_no,
			ha->actthreads,
			ha->retry_q_cnt,
			ha->done_q_cnt,
			ha->failover_cnt,
			ha->scsi_retry_q_cnt);)

	/* Prevent acceptance of new I/O requests for failover target. */
	count = qla2x00_suspend_failover_targets(ha);

	/*
	 * Process all the commands in the failover queue. Attempt to failover
	 * then either complete the command as is or requeue for retry.
	 */
	for (i = 0; i < count ; i++) {
		sp = qla2x00_failover_next_request(ha);
		if (!sp)
			break;

		qla2x00_extend_timeout(sp->cmd, 360);
		if (i == 0)
			vis_ha =
			    (scsi_qla_host_t *)sp->cmd->device->host->hostdata;

		tq = sp->tgt_queue;
		lq = sp->lun_queue;
		fcport = lq->fclun->fcport;

		DEBUG(printk("%s(): pid %ld retrycnt=%d, fcport =%p, "
		    "state=0x%x, \nloop state=0x%x fclun=%p, lq fclun=%p, "
		    "lq=%p, lun=%d\n", __func__, sp->cmd->serial_number,
		    sp->cmd->retries, fcport, atomic_read(&fcport->state),
		    atomic_read(&ha->loop_state), sp->fclun, lq->fclun, lq,
		    lq->fclun->lun));
		if (sp->err_id == SRB_ERR_DEVICE && sp->fclun == lq->fclun &&
		    atomic_read(&fcport->state) == FCS_ONLINE) {
			if (!(qla2x00_test_active_lun(fcport, sp->fclun))) { 
				DEBUG2(printk("scsi(%ld) %s Detected INACTIVE "
				    "Port 0x%02x \n", ha->host_no, __func__,
				    fcport->loop_id));
				sp->err_id = SRB_ERR_OTHER;
				sp->cmd->sense_buffer[2] = 0;
				sp->cmd->result = DID_BUS_BUSY << 16;
			}	
		}
		if ((sp->flags & SRB_GOT_SENSE)) {
		 	 sp->flags &= ~SRB_GOT_SENSE;
		 	 sp->cmd->sense_buffer[0] = 0;
		 	 sp->cmd->result = DID_BUS_BUSY << 16;
		 	 sp->cmd->host_scribble = (unsigned char *) NULL;
		}

		/*** Select an alternate path ***/
		/* 
		 * If the path has already been change by a previous request
		 * sp->fclun != lq->fclun
		 */
		if (sp->fclun != lq->fclun || (sp->err_id != SRB_ERR_OTHER &&
		    (atomic_read(&fcport->ha->loop_state) != LOOP_DEAD) &&
		    atomic_read(&fcport->state) != FCS_DEVICE_DEAD)) {
			qla2x00_failover_cleanup(sp);
		} else if (qla2x00_cfg_failover(ha,
		    lq->fclun, tq, sp) == NULL) {
			/*
			 * We ran out of paths, so just retry the status which
			 * is already set in the cmd. We want to serialize the 
			 * failovers, so we make them go thur visible HBA.
			 */
			printk(KERN_INFO
			    "%s(): Ran out of paths - pid %ld - retrying\n",
			    __func__, sp->cmd->serial_number);
		} else {
			qla2x00_failover_cleanup(sp);

		}
		add_to_done_queue(ha, sp);
	}

	for (t = 0; t < vis_ha->max_targets; t++) {
		if ((tq = vis_ha->otgt[t]) == NULL)
			continue;
		if (test_and_clear_bit(TQF_SUSPENDED, &tq->flags)) {
			/* EMPTY */
			DEBUG2(printk("%s(): remove suspend for target %d\n",
			    __func__, t));
		}
		for (l = 0; l < vis_ha->max_luns; l++) {
			if ((lq = (os_lun_t *) tq->olun[l]) == NULL)
				continue;

			if( test_and_clear_bit(LUN_MPIO_BUSY, &lq->q_flag) ) {
				/* EMPTY */
				DEBUG(printk("%s(): remove suspend for "
				    "lun %d\n", __func__, lq->fclun->lun));
			}
		}
	}
	qla2x00_restart_queues(ha, FALSE);

	DEBUG(printk("%s() - done", __func__));
}

int
qla2x00_search_failover_queue(scsi_qla_host_t *ha, struct scsi_cmnd *cmd)
{
	struct list_head *list, *temp;
	unsigned long flags;
	srb_t *sp;

	DEBUG3(printk("qla2xxx_eh_abort: searching sp %p in "
				"failover queue.\n", sp);)

	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_safe(list, temp, &ha->failover_queue) {
		sp = list_entry(list, srb_t, list);

		if (cmd == sp->cmd)
			goto found;

	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	return 0;

 found:
	/* Remove srb from failover queue. */
	__del_from_failover_queue(ha, sp);
	cmd->result = DID_ABORT << 16;
	__add_to_done_queue(ha, sp);

	spin_unlock_irqrestore(&ha->list_lock, flags);
	return 1;
}

/*
 * If we are not processing a ioctl or one of
 * the ports are still MISSING or need a resync
 * then process the failover event.
 */  
void
qla2x00_process_failover_event(scsi_qla_host_t *ha)
{
	if (test_bit(CFG_ACTIVE, &ha->cfg_flags))
		return;
	if (qla2x00_check_for_devices_online(ha)) {
		if (test_and_clear_bit(FAILOVER_EVENT, &ha->dpc_flags)) {
			if (ha->flags.online)
				qla2x00_cfg_event_notify(ha, ha->failover_type);
		}
	}

	/*
	 * Get any requests from failover queue
	 */
	if (test_and_clear_bit(FAILOVER_NEEDED, &ha->dpc_flags))
		qla2x00_process_failover(ha);
}

int
qla2x00_do_fo_check(scsi_qla_host_t *ha, srb_t *sp, scsi_qla_host_t *vis_ha)
{
	/*
	 * This routine checks for DID_NO_CONNECT to decide
	 * whether to failover to another path or not. We only
	 * failover on that status.
	 */
	if (sp->lun_queue->fclun->fcport->flags & FCF_FAILOVER_DISABLE)
		return 0;

	if (sp->lun_queue->fclun->flags & FLF_VISIBLE_LUN)
		return 0;

	if (!qla2x00_fo_check(ha, sp))
		return 0;

	if ((sp->state != SRB_FAILOVER_STATE)) {
		/*
		 * Retry the command on this path
		 * several times before selecting a new
		 * path.
		 */
		add_to_pending_queue_head(vis_ha, sp);
		qla2x00_next(vis_ha);
	} else
		qla2x00_extend_timeout(sp->cmd, EXTEND_CMD_TIMEOUT);

	return 1;
}

void
qla2xxx_start_all_adapters(scsi_qla_host_t *ha)
{
	struct list_head *hal;
	scsi_qla_host_t *vis_ha;

	/* Try and start all visible adapters */
	read_lock(&qla_hostlist_lock);
	list_for_each(hal, &qla_hostlist) {
		vis_ha = list_entry(hal, scsi_qla_host_t, list);

		if (!list_empty(&vis_ha->pending_queue))
			qla2x00_next(vis_ha);

		DEBUG2(printk("host(%ld):Commands busy=%d "
				"failed=%d\neh_active=%d\n ",
				vis_ha->host_no,
				vis_ha->host->host_busy,
				vis_ha->host->host_failed,
				vis_ha->host->eh_active);)	
	}
	read_unlock(&qla_hostlist_lock);
}
