/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP2x00 device driver for Linux 2.6.x
* Copyright (C) 2003 QLogic Corporation
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


int ql2xfailover;
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
		if ((atomic_read(&fcport->state) == FCS_ONLINE) ||
		    (atomic_read(&fcport->state) == FCS_DEVICE_DEAD))
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
	struct list_head *list, *temp;
	unsigned long flags;
	unsigned int	t, l;
	scsi_qla_host_t *vis_ha = NULL;

	DEBUG(printk("scsi(%ld): Processing failover for hba.\n", ha->host_no));

	/*
	 * Process all the commands in the failover queue. Attempt to failover
	 * then either complete the command as is or requeue for retry.
	 */

	/* Prevent or allow acceptance of new I/O requests. */
	spin_lock_irqsave(&ha->list_lock, flags);

	/*
	 * Get first entry to find our visible adapter.  We could never get
	 * here if the list is empty
	 */
	list = ha->failover_queue.next;
	sp = list_entry(list, srb_t, list);
	vis_ha = (scsi_qla_host_t *) sp->cmd->device->host->hostdata;
	list_for_each_safe(list, temp, &ha->failover_queue) {
		sp = list_entry(list, srb_t, list);

		tq = sp->tgt_queue;
		lq = sp->lun_queue;
		fcport = lq->fclun->fcport;

		/* Remove srb from failover queue. */
		__del_from_failover_queue(ha, sp);

		DEBUG2(printk("%s(): pid %ld retrycnt=%d\n",
		    __func__, sp->cmd->serial_number, sp->cmd->retries));

		/*** Select an alternate path ***/
		/* 
		 * If the path has already been change by a previous request
		 * sp->fclun != lq->fclun
		 */
		if (sp->fclun != lq->fclun ||
		    atomic_read(&fcport->state) != FCS_DEVICE_DEAD) {

			qla2x00_failover_cleanup(sp);

		} else if (qla2x00_cfg_failover(ha, lq->fclun, tq, sp) ==
		    NULL) {
			/*
			 * We ran out of paths, so just post the status which
			 * is already set in the cmd.
			 */
			printk(KERN_INFO
			    "scsi(%ld): Ran out of paths - pid %ld\n",
			    ha->host_no, sp->cmd->serial_number);
		} else {
			qla2x00_failover_cleanup(sp);

		}
		__add_to_done_queue(ha, sp);
	} /* list_for_each_safe */
	spin_unlock_irqrestore(&ha->list_lock, flags);

	for (t = 0; t < vis_ha->max_targets; t++) {
		if ((tq = vis_ha->otgt[t]) == NULL)
			continue;
		for (l = 0; l < vis_ha->max_luns; l++) {
			if ((lq = (os_lun_t *) tq->olun[l]) == NULL)
				continue;

			if( test_and_clear_bit(LUN_MPIO_BUSY, &lq->q_flag) ) {
				/* EMPTY */
				DEBUG(printk("scsi(%ld): remove suspend for "
				    "lun %d\n", ha->host_no, lq->fclun->lun));
			}
		}
	}

	//qla2x00_restart_queues(ha,TRUE);
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
