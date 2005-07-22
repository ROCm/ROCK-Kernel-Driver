/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP4xxx device driver for Linux 2.6.x
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
******************************************************************************
* Failover include file
******************************************************************************/

// #include "ql4_os.h"
#include "ql4_def.h"

#include "qlfo.h"
#include "qlfolimits.h"


/*
 * Global variables
 */
SysFoParams_t qla_fo_params;

/*
 * Local routines
 */
static uint8_t qla4xxx_fo_count_retries(scsi_qla_host_t *ha, srb_t *sp);

/*
 * qla4xxx_reset_lun_fo_counts
 *	Reset failover retry counts
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
void
qla4xxx_reset_lun_fo_counts(scsi_qla_host_t *ha, os_lun_t *lq)
{
	srb_t           *tsp;
	os_lun_t        *orig_lq;
	struct list_head *list;
	unsigned long   flags ;

	spin_lock_irqsave(&ha->list_lock, flags);
	/*
	 * the pending queue.
	 */
	list_for_each(list,&ha->pending_srb_q)
	{
		tsp = list_entry(list, srb_t, list_entry);
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = 0;
	}
	/*
	 * the retry queue.
	 */
	list_for_each(list,&ha->retry_srb_q)
	{
		tsp = list_entry(list, srb_t, list_entry);
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = 0;
	}

	/*
	 * the done queue.
	 */
	list_for_each(list, &ha->done_srb_q)
	{
		tsp = list_entry(list, srb_t, list_entry);
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = 0;
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);
}


#if 0
void qla4xxx_find_all_active_ports(srb_t *sp)
{
	scsi_qla_host_t *ha = qla4xxx_hostlist;
	fc_port_t *fcport;
	fc_lun_t        *fclun;
	fc_lun_t        *orig_fclun;

	DEBUG2(printk(KERN_INFO "%s: Scanning for active ports... %d\n",
		      __func__, sp->lun_queue->fclun->lun);)
	orig_fclun = sp->lun_queue->fclun;
	for (; (ha != NULL); ha=ha->next) {
		list_for_each_entry(fcport, &ha->fcports, list)
		{
			if (fcport->port_type != FCT_TARGET)
				continue;
			if ((fcport->flags & (FCF_EVA_DEVICE|FCF_MSA_DEVICE))) {
				list_for_each_entry(fclun, &fcport->fcluns, list)
				{
					if (fclun->flags & FCF_VISIBLE_LUN)
						continue;
					if (orig_fclun->lun != fclun->lun)
						continue;
					qla4xxx_test_active_lun(fcport,fclun);
				}
			}
#if MSA1000_SUPPORTED
			if ((fcport->flags & FCF_MSA_DEVICE))
				qla4xxx_test_active_port(fcport);
#endif
		}
	}
	DEBUG2(printk(KERN_INFO "%s: Scanning ports...Done\n",
		      __func__);)
}
#endif

/*
 * qla4xxx_fo_count_retries
 *	Increment the retry counter for the command.
 *      Set or reset the SRB_RETRY flag.
 *
 * Input:
 *	sp = Pointer to command.
 *
 * Returns:
 *	1 -- retry
 * 	0 -- don't retry
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla4xxx_fo_count_retries(scsi_qla_host_t *ha, srb_t *sp)
{
	uint8_t		retry = 1;
	os_lun_t	*lq;
	os_tgt_t	*tq;
	scsi_qla_host_t	*vis_ha;

	DEBUG9(printk("%s: entered.\n", __func__);)

	if (++sp->fo_retry_cnt >  qla_fo_params.MaxRetriesPerIo) {
		/* no more failovers for this request */
		retry = 0;
		sp->fo_retry_cnt = 0;
		printk(KERN_INFO
		    "qla4xxx: no more failovers for request - pid= %ld\n",
		    sp->cmd->serial_number);
	} else {
		/*
		 * We haven't exceeded the max retries for this request, check
		 * max retries this path
		 */
		if ((sp->fo_retry_cnt % qla_fo_params.MaxRetriesPerPath) == 0) {
			DEBUG2(printk("qla4xxx_fo_count_retries: FAILOVER - "
			    "queuing ha=%d, sp=%p, pid =%ld, "
			    "fo retry= %d \n",
			    ha->host_no,
			    sp, sp->cmd->serial_number,
			    sp->fo_retry_cnt);)

			/*
			 * Note: we don't want it to timeout, so it is
			 * recycling on the retry queue and the fialover queue.
			 */
			lq = sp->lun_queue;
			tq = sp->tgt_queue;
			// set_bit(LUN_MPIO_BUSY, &lq->q_flag);

			/*
			 * ??? We can get a path error on any ha, but always
			 * queue failover on originating ha. This will allow us
			 * to syncronized the requests for a given lun.
			 */
			/* Now queue it on to be failover */
			sp->ha = ha;
			/* we can only failover using the visible HA */
		 	vis_ha =
			    (scsi_qla_host_t *)sp->cmd->device->host->hostdata;
			add_to_failover_queue(vis_ha,sp);
		}
	}

	DEBUG9(printk("%s: exiting. retry = %d.\n", __func__, retry);)

	return retry ;
}

int
qla4xxx_fo_check_device(scsi_qla_host_t *ha, srb_t *sp)
{
	int		retry = 0;
	os_lun_t	*lq;
	struct scsi_cmnd 	 *cp;
	fc_port_t 	 *fcport;
	
	if ( !(sp->flags & SRB_GOT_SENSE) )
		return retry;

	cp = sp->cmd;
	lq = sp->lun_queue;
	fcport = lq->fclun->fcport;
	switch (cp->sense_buffer[2] & 0xf) {
	case NOT_READY:
		if (fcport->flags & (FCF_MSA_DEVICE | FCF_EVA_DEVICE)) {
			/*
			 * if we can't access port 
			 */
			if ((cp->sense_buffer[12] == 0x4 &&
			    (cp->sense_buffer[13] == 0x0 ||
			     cp->sense_buffer[13] == 0x3 ||
			     cp->sense_buffer[13] == 0x2))) {
				sp->err_id = SRB_ERR_DEVICE;
				return 1;
			}
		} 
		break;

	case UNIT_ATTENTION:
		if (fcport->flags & FCF_EVA_DEVICE) {
			if ((cp->sense_buffer[12] == 0xa &&
			    cp->sense_buffer[13] == 0x8)) {
				sp->err_id = SRB_ERR_DEVICE;
				return 1;
			}
			if ((cp->sense_buffer[12] == 0xa &&
			    cp->sense_buffer[13] == 0x9)) {
				/* failback lun */
			}
		} 
		break;

	}

	return (retry);
}

/*
 * qla4xxx_fo_check
 *	This function is called from the done routine to see if
 *  the SRB requires a failover.
 *
 *	This function examines the available os returned status and
 *  if meets condition, the command(srb) is placed ont the failover
 *  queue for processing.
 *
 * Input:
 *	sp  = Pointer to the SCSI Request Block
 *
 * Output:
 *      sp->flags SRB_RETRY bit id command is to
 *      be retried otherwise bit is reset.
 *
 * Returns:
 *      None.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
uint8_t
qla4xxx_fo_check(scsi_qla_host_t *ha, srb_t *sp)
{
	uint8_t		retry = 0;
	int host_status;
#ifdef QL_DEBUG_LEVEL_2
	static char *reason[] = {
		"DID_OK",
		"DID_NO_CONNECT",
		"DID_BUS_BUSY",
		"DID_TIME_OUT",
		"DID_BAD_TARGET",
		"DID_ABORT",
		"DID_PARITY",
		"DID_ERROR",
		"DID_RESET",
		"DID_BAD_INTR"
	};
#endif

	DEBUG9(printk("%s: entered.\n", __func__);)

	/* we failover on selction timeouts only */
	host_status = host_byte(sp->cmd->result);
	if( host_status == DID_NO_CONNECT ||
		qla4xxx_fo_check_device(ha, sp) ) {
			
		if (qla4xxx_fo_count_retries(ha, sp)) {
			/* Force a retry  on this request, it will
			 * cause the LINUX timer to get reset, while we
			 * we are processing the failover.
			 */
			sp->cmd->result = DID_BUS_BUSY << 16;
			retry = 1;
		}
		DEBUG2(printk("qla4xxx_fo_check: pid= %ld sp %p/%d/%d retry count=%d, "
		    "retry flag = %d, host status (%s)\n",
		    sp->cmd->serial_number, sp, sp->state, sp->err_id, sp->fo_retry_cnt, retry,
		    reason[host_status]);)
	}

	DEBUG9(printk("%s: exiting. retry = %d.\n", __func__, retry);)

	return retry;
}

/*
 * qla4xxx_fo_path_change
 *	This function is called from configuration mgr to notify
 *	of a path change.
 *
 * Input:
 *      type    = Failover notify type, FO_NOTIFY_LUN_RESET or FO_NOTIFY_LOGOUT
 *      newlunp = Pointer to the fc_lun struct for current path.
 *      oldlunp = Pointer to fc_lun struct for previous path.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
uint32_t
qla4xxx_fo_path_change(uint32_t type, fc_lun_t *newlunp, fc_lun_t *oldlunp)
{
	uint32_t	ret = QLA_SUCCESS;

	newlunp->max_path_retries = 0;
	return ret;
}

#if 0
/*
 * qla4xxx_fo_get_params
 *	Process an ioctl request to get system wide failover parameters.
 *
 * Input:
 *	pp = Pointer to FO_PARAMS structure.
 *
 * Returns:
 *	EXT_STATUS code.
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla4xxx_fo_get_params(PFO_PARAMS pp)
{
	DEBUG9(printk("%s: entered.\n", __func__);)

	pp->MaxPathsPerDevice = qla_fo_params.MaxPathsPerDevice;
	pp->MaxRetriesPerPath = qla_fo_params.MaxRetriesPerPath;
	pp->MaxRetriesPerIo = qla_fo_params.MaxRetriesPerIo;
	pp->Flags = qla_fo_params.Flags;
	pp->FailoverNotifyType = qla_fo_params.FailoverNotifyType;
	pp->FailoverNotifyCdbLength = qla_fo_params.FailoverNotifyCdbLength;
	memset(pp->FailoverNotifyCdb, 0, sizeof(pp->FailoverNotifyCdb));
	memcpy(pp->FailoverNotifyCdb,
	    &qla_fo_params.FailoverNotifyCdb[0], sizeof(pp->FailoverNotifyCdb));

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return EXT_STATUS_OK;
}

/*
 * qla4xxx_fo_set_params
 *	Process an ioctl request to set system wide failover parameters.
 *
 * Input:
 *	pp = Pointer to FO_PARAMS structure.
 *
 * Returns:
 *	EXT_STATUS code.
 *
 * Context:
 *	Kernel context.
 */
static uint32_t
qla4xxx_fo_set_params(PFO_PARAMS pp)
{
	DEBUG9(printk("%s: entered.\n", __func__);)

	/* Check values for defined MIN and MAX */
	if ((pp->MaxPathsPerDevice > SDM_DEF_MAX_PATHS_PER_DEVICE) ||
	    (pp->MaxRetriesPerPath < FO_MAX_RETRIES_PER_PATH_MIN) ||
	    (pp->MaxRetriesPerPath > FO_MAX_RETRIES_PER_PATH_MAX) ||
	    (pp->MaxRetriesPerIo < FO_MAX_RETRIES_PER_IO_MIN) ||
	    (pp->MaxRetriesPerPath > FO_MAX_RETRIES_PER_IO_MAX)) {
		DEBUG2(printk("%s: got invalid params.\n", __func__);)
		return EXT_STATUS_INVALID_PARAM;
	}

	/* Update the global structure. */
	qla_fo_params.MaxPathsPerDevice = pp->MaxPathsPerDevice;
	qla_fo_params.MaxRetriesPerPath = pp->MaxRetriesPerPath;
	qla_fo_params.MaxRetriesPerIo = pp->MaxRetriesPerIo;
	qla_fo_params.Flags = pp->Flags;
	qla_fo_params.FailoverNotifyType = pp->FailoverNotifyType;
	qla_fo_params.FailoverNotifyCdbLength = pp->FailoverNotifyCdbLength;
	if (pp->FailoverNotifyType & FO_NOTIFY_TYPE_CDB) {
		if (pp->FailoverNotifyCdbLength >
		    sizeof(qla_fo_params.FailoverNotifyCdb)) {
			DEBUG2(printk("%s: got invalid cdb length.\n",
			    __func__);)
			return EXT_STATUS_INVALID_PARAM;
		}

		memcpy(qla_fo_params.FailoverNotifyCdb,
		    pp->FailoverNotifyCdb,
		    sizeof(qla_fo_params.FailoverNotifyCdb));
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return EXT_STATUS_OK;
}
#endif


/*
 * qla4xxx_fo_init_params
 *	Gets driver configuration file failover properties to initalize
 *	the global failover parameters structure.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla4xxx_fo_init_params(scsi_qla_host_t *ha)
{
	DEBUG3(printk("%s: entered.\n", __func__);)

	/* For parameters that are not completely implemented yet, */

	memset(&qla_fo_params, 0, sizeof(qla_fo_params));

	if(MaxPathsPerDevice) {
		qla_fo_params.MaxPathsPerDevice = MaxPathsPerDevice;
	} else
		qla_fo_params.MaxPathsPerDevice =FO_MAX_PATHS_PER_DEVICE_DEF ;
	if(MaxRetriesPerPath) {
		qla_fo_params.MaxRetriesPerPath = MaxRetriesPerPath;
	} else
		qla_fo_params.MaxRetriesPerPath =FO_MAX_RETRIES_PER_PATH_DEF;
	if(MaxRetriesPerIo) {
		qla_fo_params.MaxRetriesPerIo =MaxRetriesPerIo;
	} else
		qla_fo_params.MaxRetriesPerIo =FO_MAX_RETRIES_PER_IO_DEF;

	qla_fo_params.Flags =  0;
	qla_fo_params.FailoverNotifyType = FO_NOTIFY_TYPE_NONE;
	
	/* Set it to whatever user specified on the cmdline */
	if (qlFailoverNotifyType != FO_NOTIFY_TYPE_NONE)
		qla_fo_params.FailoverNotifyType = qlFailoverNotifyType;
	

	DEBUG3(printk("%s: exiting.\n", __func__);)
}


/*
 * qla2100_fo_enabled
 *      Reads and validates the failover enabled property.
 *
 * Input:
 *      ha = adapter state pointer.
 *      instance = HBA number.
 *
 * Returns:
 *      1 when failover is authorized else 0
 *
 * Context:
 *      Kernel context.
 */
uint8_t
qla4xxx_fo_enabled(scsi_qla_host_t *ha, int instance)
{
	return qla4xxx_failover_enabled(ha);
}

/*
 * qla4xxx_send_fo_notification
 *      Sends failover notification if needed.  Change the fc_lun pointer
 *      in the old path lun queue.
 *
 * Input:
 *      old_lp = Pointer to old fc_lun.
 *      new_lp = Pointer to new fc_lun.
 *
 * Returns:
 *      Local function status code.
 *
 * Context:
 *      Kernel context.
 */
uint32_t
qla4xxx_send_fo_notification(fc_lun_t *old_lp, fc_lun_t *new_lp)
{
	int		rval = QLA_SUCCESS;
#if 0
	scsi_qla_host_t	*old_ha = old_lp->fcport->ha;
	inq_cmd_rsp_t	*pkt;
	uint16_t	loop_id, lun;
	dma_addr_t	phys_address;
#endif


	ENTER("qla4xxx_send_fo_notification");
	DEBUG3(printk("%s: entered.\n", __func__);)

#if 0
	if( new_lp->fcport == NULL ){
		DEBUG2(printk("qla4xxx_send_fo_notification: No "
			    "new fcport for lun pointer\n");)
		return QLA_ERROR;
	}
	loop_id = new_lp->fcport->loop_id;
	lun = new_lp->lun;

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_LUN_RESET) {
		rval = qla4xxx_lun_reset(old_ha, loop_id, lun);
		if (rval == QLA_SUCCESS) {
			DEBUG4(printk("qla4xxx_send_fo_notification: LUN "
			    "reset succeded\n");)
		} else {
			DEBUG4(printk("qla4xxx_send_fo_notification: LUN "
			    "reset failed\n");)
		}

	}
	if ( (qla_fo_params.FailoverNotifyType ==
	     FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET) ||
	    (qla_fo_params.FailoverNotifyType ==
	     FO_NOTIFY_TYPE_LOGOUT_OR_CDB) )  {

		rval = qla4xxx_fabric_logout(old_ha, loop_id);
		if (rval == QLA_SUCCESS) {
			DEBUG4(printk("qla4xxx_send_fo_failover_notify: "
			    "logout succeded\n");)
		} else {
			DEBUG4(printk("qla4xxx_send_fo_failover_notify: "
			    "logout failed\n");)
		}

	}

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_SPINUP ||
		new_lp->fcport->notify_type == FO_NOTIFY_TYPE_SPINUP ) {
		rval = qla4xxx_spinup(new_lp->fcport->ha, new_lp->fcport, 
			new_lp->lun); 
	}

	if (qla_fo_params.FailoverNotifyType == FO_NOTIFY_TYPE_CDB) {
	}
#endif

	DEBUG3(printk("%s: exiting. rval = %d.\n", __func__, rval);)

	return rval;
}

