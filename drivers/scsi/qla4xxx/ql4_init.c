/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE                                     *
 *                                                                            *
 * QLogic ISP4xxx device driver for Linux 2.4.x                               *
 * Copyright (C) 2004 Qlogic Corporation                                      *
 * (www.qlogic.com)                                                           *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify it    *
 * under the terms of the GNU General Public License as published by the      *
 * Free Software Foundation; either version 2, or (at your option) any        *
 * later version.                                                             *
 *                                                                            *
 * This program is distributed in the hope that it will be useful, but        *
 * WITHOUT ANY WARRANTY; without even the implied warranty of                 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU          *
 * General Public License for more details.                                   *
 *                                                                            *
 ******************************************************************************
 *             Please see release.txt for revision history.                   *
 *                                                                            *
 ******************************************************************************
 * Function Table of Contents:
 *      qla4xxx_alloc_dma_memory
 *      qla4xxx_free_dma_memory
 *      qla4xxx_free_lun
 *      qla4xxx_free_ddb
 *      qla4xxx_free_ddb_list
 *      qla4xxx_init_rings
 *      qla4xxx_validate_mac_address
 *	qla4xxx_init_local_data
 *      qla4xxx_init_firmware
 *      qla4xxx_send_internal_scsi_passthru
 *      qla4xxx_send_inquiry_cmd
 *      qla4xxx_send_report_luns_cmd
 *	qla4xxx_is_discovered_target
 *      qla4xxx_update_ddb_entry
 *      qla4xxx_alloc_lun
 *      qla4xxx_discover_target_luns
 *      qla4xxx_map_targets_to_ddbs
 *      qla4xxx_alloc_ddb
 *      qla4xxx_build_ddb_list
 *      qla4xxx_initialize_ddb_list
 *      qla4xxx_reinitialize_ddb_list
 *      qla4xxx_relogin_device
 *	qla4xxx_get_topcat_presence
 *	qla4xxx_start_firmware
 *      qla4xxx_initialize_adapter
 *      qla4xxx_find_propname
 *      qla4xxx_get_prop_12chars
 *	qla4xxx_add_device_dynamically
 *	qla4xxx_process_ddb_changed
 *	qla4xxx_login_device
 *	qla4xxx_logout_device
 *	qla4xxx_flush_all_srbs
 *	qla4xxx_delete_device
 ****************************************************************************/

#include "ql4_def.h"

#include <linux/delay.h>

/*
 *  External Function Prototypes.
 */
extern int ql4xdiscoverywait;
extern char *ql4xdevconf;

/*
 * Local routines
 */
static fc_port_t *
qla4xxx_find_or_alloc_fcport(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry);
static void qla4xxx_config_os(scsi_qla_host_t *ha);
static uint16_t
qla4xxx_fcport_bind(scsi_qla_host_t *ha, fc_port_t *fcport);
os_lun_t *
qla4xxx_fclun_bind(scsi_qla_host_t *ha, fc_port_t *fcport, fc_lun_t *fclun);
os_tgt_t *
qla4xxx_tgt_alloc(scsi_qla_host_t *ha, uint16_t tgt);
void
qla4xxx_tgt_free(scsi_qla_host_t *ha, uint16_t tgt);
os_lun_t *
qla4xxx_lun_alloc(scsi_qla_host_t *ha, uint16_t tgt, uint16_t lun);
static void
qla4xxx_lun_free(scsi_qla_host_t *ha, uint16_t tgt, uint16_t lun);
fc_lun_t *
qla4xxx_add_fclun(fc_port_t *fcport, uint16_t lun);
static ddb_entry_t *
qla4xxx_get_ddb_entry(scsi_qla_host_t *ha, uint32_t fw_ddb_index);

/**
 * qla4xxx_alloc_fcport() - Allocate a generic fcport.
 * @ha: HA context
 * @flags: allocation flags
 *
 * Returns a pointer to the allocated fcport, or NULL, if none available.
 */
static fc_port_t *
qla4xxx_alloc_fcport(scsi_qla_host_t *ha, int flags)
{
	fc_port_t *fcport;

	fcport = kmalloc(sizeof(fc_port_t), flags);
	if (fcport == NULL)
		return(fcport);

	/* Setup fcport template structure. */
	memset(fcport, 0, sizeof (fc_port_t));
	fcport->ha = ha;
	fcport->port_type = FCT_UNKNOWN;
	atomic_set(&fcport->state, FCS_DEVICE_DEAD);
	fcport->flags = FCF_RLC_SUPPORT;
	INIT_LIST_HEAD(&fcport->fcluns);

	return(fcport);
}

/*
* qla4xxx_init_tgt_map
*      Initializes target map.
*
* Input:
*      ha = adapter block pointer.
*
* Output:
*      TGT_Q initialized
*/
static void
qla4xxx_init_tgt_map(scsi_qla_host_t *ha)
{
	uint32_t t;

	ENTER(__func__);

	for (t = 0; t < MAX_TARGETS; t++)
		TGT_Q(ha, t) = (os_tgt_t *) NULL;

	LEAVE(__func__);
}




/*
 * qla4xxx_update_fcport
 *	Updates device on list.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	0  - Success
 *  BIT_0 - error
 *
 * Context:
 *	Kernel context.
 */
static void
qla4xxx_update_fcport(scsi_qla_host_t *ha, fc_port_t *fcport)
{
#if 0
	uint16_t        index;
	unsigned long flags;
	srb_t *sp;
#endif

	if (fcport == NULL)
		return;

	ENTER(__func__);
	fcport->ha = ha;
#ifdef CONFIG_SCSI_QLA4XXX_FAILOVER
	fcport->flags &= ~(FCF_FAILOVER_NEEDED);
#endif
	/* XXX need to get this info from option field of DDB entry */ 
	fcport->port_type = FCT_TARGET;
	fcport->iscsi_name = fcport->ddbptr->iscsi_name;

	/*
	 * Check for outstanding cmd on tape Bypass LUN discovery if active
	 * command on tape.
	 */
#if 0
	if (fcport->flags & FCF_TAPE_PRESENT) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
			if ((sp = ha->outstanding_cmds[index]) != 0) {
				if (sp->fclun->fcport == fcport) {
					atomic_set(&fcport->state, FCS_ONLINE);
					spin_unlock_irqrestore(
							      &ha->hardware_lock, flags);
					return;
				}
			}
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}
#endif

	/* Do LUN discovery. */
#ifdef CONFIG_SCSI_QLA4XXX_FAILOVER
	qla4xxx_lun_discovery(ha, fcport);
#endif

	/* Always set online */
	atomic_set(&fcport->state, FCS_ONLINE);
	LEAVE(__func__);
}



/*
 * qla4xxx_add_fclun
 *	Adds LUN to database
 *
 * Input:
 *	fcport:		FC port structure pointer.
 *	lun:		LUN number.
 *
 * Context:
 *	Kernel context.
 */
fc_lun_t *
qla4xxx_add_fclun(fc_port_t *fcport, uint16_t lun)
{
	int             found;
	fc_lun_t        *fclun;

	if (fcport == NULL) {
		DEBUG2(printk("scsi: Unable to add lun to NULL port\n"));
		return(NULL);
	}

	/* Allocate LUN if not already allocated. */
	found = 0;
	list_for_each_entry(fclun, &fcport->fcluns, list) {
		if (fclun->lun == lun) {
			found++;
			break;
		}
	}
	if (found) {
		return(fclun);
	}

	fclun = kmalloc(sizeof(fc_lun_t), GFP_ATOMIC);
	if (fclun == NULL) {
		printk(KERN_WARNING
		       "%s(): Memory Allocation failed - FCLUN\n",
		       __func__);
		return(NULL);
	}

	/* Setup LUN structure. */
	memset(fclun, 0, sizeof(fc_lun_t));
	fclun->lun = lun;
	fclun->fcport = fcport;
	fclun->device_type = fcport->device_type;
	// atomic_set(&fcport->state, FCS_UNCONFIGURED);

	list_add_tail(&fclun->list, &fcport->fcluns);

	return(fclun);
}




/*
 * qla4xxx_config_os
 *	Setup OS target and LUN structures.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Context:
 *	Kernel context.
 */
static void
qla4xxx_config_os(scsi_qla_host_t *ha)
{
	fc_port_t       *fcport;
	fc_lun_t        *fclun;
	os_tgt_t        *tq;
	uint16_t        tgt;


	for (tgt = 0; tgt < MAX_TARGETS; tgt++) {
		if ((tq = TGT_Q(ha, tgt)) == NULL)
			continue;

		tq->flags &= ~TQF_ONLINE;
	}

	list_for_each_entry(fcport, &ha->fcports, list)
	{
		if (atomic_read(&fcport->state) != FCS_ONLINE) {
			fcport->os_target_id = MAX_TARGETS;
			continue;
		}

		/* Bind FC port to OS target number. */
		if (qla4xxx_fcport_bind(ha, fcport) == MAX_TARGETS) {
			continue;
		}

		/* Bind FC LUN to OS LUN number. */
		list_for_each_entry(fclun, &fcport->fcluns, list)
		{
			qla4xxx_fclun_bind(ha, fcport, fclun);
		}
	}
}

/*
 * qla4xxx_fcport_bind
 *	Locates a target number for FC port.
 *
 * Input:
 *	ha = adapter state pointer.
 *	fcport = FC port structure pointer.
 *
 * Returns:
 *	target number
 *
 * Context:
 *	Kernel context.
 */
static uint16_t
qla4xxx_fcport_bind(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	uint16_t        tgt;
	os_tgt_t        *tq = NULL;

	if (fcport->ddbptr == NULL)
		return (MAX_TARGETS);
		
	/* Check for persistent binding. */
	for (tgt = 0; tgt < MAX_TARGETS; tgt++) {
		if ((tq = TGT_Q(ha, tgt)) == NULL)
			continue;

		if (memcmp(fcport->ddbptr->iscsi_name, tq->iscsi_name,
			    ISCSI_NAME_SIZE) == 0) {
				break;	
		}
	}
	/* TODO: honor the ConfigRequired flag */
	if (tgt == MAX_TARGETS) {
	/* Check if targetID 0 available. */
	tgt = 0;

		/* Check if targetID 0 available. */
	if (TGT_Q(ha, tgt) != NULL) {
		/* Locate first free target for device. */
		for (tgt = 0; tgt < MAX_TARGETS; tgt++) {
			if (TGT_Q(ha, tgt) == NULL) {
				break;
			}
		}
	}
	if (tgt != MAX_TARGETS) {
		if ((tq = qla4xxx_tgt_alloc(ha, tgt)) != NULL) {
				memcpy(tq->iscsi_name, fcport->ddbptr->iscsi_name,
			    	ISCSI_NAME_SIZE);
			}
		}
	}

	/* Reset target numbers incase it changed. */
	fcport->os_target_id = tgt;
	if (tgt != MAX_TARGETS && tq != NULL) {
		DEBUG3(printk("scsi(%d): Assigning target ID=%02d @ %p to "
		    "loop id=0x%04x, port state=0x%x, port down retry=%d\n",
		    ha->host_no, tgt, tq, fcport->loop_id,
		    atomic_read(&fcport->state),
		    atomic_read(&fcport->ddbptr->port_down_timer)));

		fcport->tgt_queue = tq;
		fcport->flags |= FCF_PERSISTENT_BOUND;
		tq->fcport = fcport;
		tq->flags |= TQF_ONLINE;
		tq->id = tgt;
	}

	if (tgt == MAX_TARGETS) {
		QL4PRINT(QLP2, printk(KERN_WARNING
		    "Unable to bind fcport, loop_id=%x\n", fcport->loop_id));
	}

	return(tgt);
}

/*
 * qla4xxx_fclun_bind
 *	Binds all FC device LUNS to OS LUNS.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		FC port structure pointer.
 *
 * Returns:
 *	target number
 *
 * Context:
 *	Kernel context.
 */
os_lun_t *
qla4xxx_fclun_bind(scsi_qla_host_t *ha, fc_port_t *fcport, fc_lun_t *fclun)
{
	os_lun_t        *lq;
	uint16_t        tgt;
	uint16_t        lun;

	tgt = fcport->os_target_id;
	lun = fclun->lun;

	/* Allocate LUNs */
	if (lun >= MAX_LUNS) {
		DEBUG2(printk("scsi%d: Unable to bind lun, invalid "
			      "lun=(%x).\n", ha->host_no, lun));
		return(NULL);
	}

	if ((lq = qla4xxx_lun_alloc(ha, tgt, lun)) == NULL) {
		printk(KERN_WARNING "Unable to bind fclun, lun=%x\n",
		       lun);
		return(NULL);
	}

	lq->fclun = fclun;

	return(lq);
}

/*
 * qla4xxx_tgt_alloc
 *	Allocate and pre-initialize target queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *
 * Returns:
 *	NULL = failure
 *
 * Context:
 *	Kernel context.
 */
os_tgt_t *
qla4xxx_tgt_alloc(scsi_qla_host_t *ha, uint16_t tgt)
{
	os_tgt_t        *tq;

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (tgt >= MAX_TARGETS) {
		DEBUG2(printk("scsi%d: Unable to allocate target, invalid "
			      "target number %d.\n", ha->host_no, tgt));
		return(NULL);
	}

	tq = TGT_Q(ha, tgt);
	if (tq == NULL) {
		tq = kmalloc(sizeof(os_tgt_t), GFP_ATOMIC);
		if (tq != NULL) {
			DEBUG3(printk("scsi%d: Alloc Target %d @ %p\n",
				      ha->host_no, tgt, tq));

			memset(tq, 0, sizeof(os_tgt_t));
			tq->ha = ha;

			TGT_Q(ha, tgt) = tq;
		}
	}
	if (tq != NULL) {
		tq->port_down_retry_count = ha->port_down_retry_count;
	}
	else {
		printk(KERN_WARNING "Unable to allocate target.\n");
	}

	return(tq);
}

/*
 * qla4xxx_tgt_free
 *	Frees target and LUN queues.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *
 * Context:
 *	Kernel context.
 */
void
qla4xxx_tgt_free(scsi_qla_host_t *ha, uint16_t tgt)
{
	os_tgt_t        *tq;
	uint16_t        lun;

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (tgt >= MAX_TARGETS) {
		DEBUG2(printk("scsi%d: Unable to de-allocate target, "
			      "invalid target number %d.\n", ha->host_no, tgt));

		return;
	}

	tq = TGT_Q(ha, tgt);
	if (tq != NULL) {
		TGT_Q(ha, tgt) = NULL;

		/* Free LUN structures. */
		for (lun = 0; lun < MAX_LUNS; lun++)
			qla4xxx_lun_free(ha, tgt, lun);

		kfree(tq);
	}

	return;
}

/*
 * qla4xxx_lun_alloc
 *	Allocate and initialize LUN queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *	l = LUN number.
 *
 * Returns:
 *	NULL = failure
 *
 * Context:
 *	Kernel context.
 */
os_lun_t *
qla4xxx_lun_alloc(scsi_qla_host_t *ha, uint16_t tgt, uint16_t lun)
{
	os_lun_t        *lq;

	/*
	 * If SCSI addressing OK, allocate LUN queue.
	 */
	if (lun >= MAX_LUNS || TGT_Q(ha, tgt) == NULL) {
		DEBUG2(printk("scsi%d: Unable to allocate lun, invalid "
			      "parameter.\n", ha->host_no));

		return(NULL);
	}

	lq = LUN_Q(ha, tgt, lun);
	if (lq == NULL) {
		lq = kmalloc(sizeof(os_lun_t), GFP_ATOMIC);
		if (lq != NULL) {
			DEBUG3(printk("scsi%d: Alloc Lun %d @ tgt %d.\n",
				      ha->host_no, lun, tgt));

			memset(lq, 0, sizeof (os_lun_t));
			LUN_Q(ha, tgt, lun) = lq;

			/*
			 * The following lun queue initialization code
			 * must be duplicated in alloc_ioctl_mem function
			 * for ioctl_lq.
			 */
			lq->lun_state = LS_LUN_READY;
			spin_lock_init(&lq->lun_lock);
		}
	}

	if (lq == NULL) {
		printk(KERN_WARNING "Unable to allocate lun.\n");
	}

	return(lq);
}

/*
 * qla4xxx_lun_free
 *	Frees LUN queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *
 * Context:
 *	Kernel context.
 */
static void
qla4xxx_lun_free(scsi_qla_host_t *ha, uint16_t tgt, uint16_t lun)
{
	os_lun_t        *lq;

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (tgt >= MAX_TARGETS || lun >= MAX_LUNS) {
		DEBUG2(printk("scsi%d: Unable to deallocate lun, invalid "
			      "parameter.\n", ha->host_no));

		return;
	}

	if (TGT_Q(ha, tgt) != NULL && (lq = LUN_Q(ha, tgt, lun)) != NULL) {
		LUN_Q(ha, tgt, lun) = NULL;
		kfree(lq);
	}

	return;
}

/**************************************************************************
 * qla4xxx_free_ddb
 *	This routine deallocates and unlinks the specified ddb_entry from the
 *	adapter's
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	ddb_entry - Pointer to device database entry
 *
 * Returns:
 *	None
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
void
qla4xxx_free_ddb(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry)
{
	fc_port_t       *fcport;

	ENTER("qla4xxx_free_ddb");

	/* Remove device entry from list */
	list_del_init(&ddb_entry->list_entry);

	/* Remove device pointer from index mapping arrays */
	ha->fw_ddb_index_map[ddb_entry->fw_ddb_index] = (ddb_entry_t *) INVALID_ENTRY;
	//if (ddb_entry->target < MAX_DDB_ENTRIES)
	//ha->target_map[ddb_entry->target] = (ddb_entry_t *) INVALID_ENTRY;
	ha->tot_ddbs--;

	fcport = ddb_entry->fcport;
	if (fcport) {
		atomic_set(&fcport->state, FCS_DEVICE_DEAD);
		fcport->ddbptr = NULL;
	}
	/* Free memory allocated for all luns */
	//for (lun = 0; lun < MAX_LUNS; lun++)
	//if (ddb_entry->lun_table[lun])
	//qla4xxx_free_lun(ddb_entry, lun);

	/* Free memory for device entry */
	kfree(ddb_entry);
	LEAVE("qla4xxx_free_ddb");
}

/**************************************************************************
 * qla4xxx_free_ddb_list
 *	This routine deallocates and removes all devices on the sppecified
 *	adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	None
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
void
qla4xxx_free_ddb_list(scsi_qla_host_t *ha)
{
	struct list_head *ptr;
	ddb_entry_t *ddb_entry;
	fc_port_t       *fcport;

	ENTER("qla4xxx_free_ddb_list");

	while (!list_empty(&ha->ddb_list)) {
		/* Remove device entry from head of list */
		ptr = ha->ddb_list.next;
		list_del_init(ptr);

		/* Free memory for device entry */
		ddb_entry = list_entry(ptr, ddb_entry_t, list_entry);
		if (ddb_entry) {
			fcport = ddb_entry->fcport;
			if (fcport) {
				atomic_set(&fcport->state, FCS_DEVICE_DEAD);
				fcport->ddbptr = NULL;
			}
			kfree(ddb_entry);
		}
	}

	LEAVE("qla4xxx_free_ddb_list");
}

/**************************************************************************
 * qla4xxx_init_rings
 *	This routine initializes the internal queues for the specified adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Remarks:
 *	The QLA4010 requires us to restart the queues at index 0.
 *	The QLA4000 doesn't care, so just default to QLA4010's requirement.
 * Returns:
 *	QLA_SUCCESS - Always return success.
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_init_rings(scsi_qla_host_t *ha)
{
	uint16_t    i;
	unsigned long flags = 0;

	ENTER("qla4xxx_init_rings");

	/* Initialize request queue. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->request_out = 0;
	ha->request_in = 0;
	ha->request_ptr = &ha->request_ring[ha->request_in];
	ha->req_q_count = REQUEST_QUEUE_DEPTH;

	/* Initialize response queue. */
	ha->response_in = 0;
	ha->response_out = 0;
	ha->response_ptr = &ha->response_ring[ha->response_out];

	QL4PRINT(QLP7, printk("scsi%d: %s response_ptr=%p\n", ha->host_no,
	    __func__, ha->response_ptr));

	/*
	 * Initialize DMA Shadow registers.  The firmware is really supposed to
	 * take care of this, but on some uniprocessor systems, the shadow
	 * registers aren't cleared-- causing the interrupt_handler to think
	 * there are responses to be processed when there aren't.
	 */
	ha->shadow_regs->req_q_out = __constant_cpu_to_le32(0);
	ha->shadow_regs->rsp_q_in = __constant_cpu_to_le32(0);
	wmb();

	WRT_REG_DWORD(&ha->reg->req_q_in, 0);
	WRT_REG_DWORD(&ha->reg->rsp_q_out, 0);
	PCI_POSTING(&ha->reg->rsp_q_out);

	/* Initialize active array */
	for (i = 0; i < MAX_SRBS; i++)
		ha->active_srb_array[i] = 0;
	ha->active_srb_count = 0;

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	LEAVE("qla4xxx_init_rings");

	return (QLA_SUCCESS);
}


#define qla4xxx_mac_is_equal(mac1, mac2) (memcmp(mac1, mac2, MAC_ADDR_LEN) == 0)

/**************************************************************************
 * qla4xxx_validate_mac_address
 *	This routine validates the M.A.C. Address(es) of the adapter
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully validated M.A.C. address
 *	QLA_ERROR   - Failed to validate M.A.C. address
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_validate_mac_address(scsi_qla_host_t *ha)
{
	FLASH_SYS_INFO *sys_info = NULL;
	dma_addr_t sys_info_dma;
	uint8_t status = QLA_ERROR;

	ENTER("qla4xxx_validate_mac_address");
	sys_info = (FLASH_SYS_INFO *) pci_alloc_consistent(ha->pdev,
	    sizeof(*sys_info), &sys_info_dma);
	if (sys_info == NULL) {			
		QL4PRINT(QLP2, printk("scsi%d: %s: Unable to allocate dma "
		    "buffer.\n", ha->host_no, __func__));
		goto exit_validate_mac;
	}
	memset(sys_info, 0, sizeof(*sys_info));

	/* Get flash sys info */
	if (qla4xxx_get_flash(ha, sys_info_dma, FLASH_OFFSET_SYS_INFO,
	    sizeof(*sys_info)) != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s: get_flash "
		    "FLASH_OFFSET_SYS_INFO failed\n", ha->host_no, __func__));
		goto exit_validate_mac;
	}

	/* Save M.A.C. address & serial_number */
	memcpy(ha->my_mac, &sys_info->physAddr[0].address[0],
	    MIN(sizeof(ha->my_mac), sizeof(sys_info->physAddr[0].address)));
	memcpy(ha->serial_number, &sys_info->acSerialNumber,
	    MIN(sizeof(ha->serial_number), sizeof(sys_info->acSerialNumber)));

	/* Display Debug Print Info */
	QL4PRINT(QLP10, printk("scsi%d: Flash Sys Info\n", ha->host_no));
	qla4xxx_dump_bytes(QLP10, sys_info, sizeof(*sys_info));

	/*
	 * If configuration information was specified on the command line,
	 * validate the mac address here.
	 */
	if (ql4xdevconf) {
		char *propbuf;
		uint8_t cfg_mac[MAC_ADDR_LEN];

		propbuf = kmalloc(LINESIZE, GFP_ATOMIC);
		if (propbuf == NULL) {			
			QL4PRINT(QLP2, printk("scsi%d: %s: Unable to "
			    "allocate memory.\n", ha->host_no, __func__));
			goto exit_validate_mac;
		}
		
		/* Get mac address from configuration file. */
		sprintf(propbuf, "scsi-qla%d-mac", ha->instance);
		qla4xxx_get_prop_12chars(ha, propbuf, &cfg_mac[0], ql4xdevconf);

		if (qla4xxx_mac_is_equal(&ha->my_mac, cfg_mac)) {
			QL4PRINT(QLP7, printk("scsi%d: %s: This is a "
			    "registered adapter.\n", ha->host_no, __func__));
			status = QLA_SUCCESS;
		} else {
			QL4PRINT(QLP7, printk("scsi%d: %s: This is NOT a "
			    "registered adapter.\n", ha->host_no, __func__));
		}
		kfree(propbuf);
	} else {
		status = QLA_SUCCESS;
	}

exit_validate_mac:
	if (sys_info)
		pci_free_consistent(ha->pdev, sizeof(*sys_info), sys_info,
		    sys_info_dma);

	LEAVE("qla4xxx_validate_mac_address");

	return (status);
}

/**************************************************************************
 * qla4xxx_init_local_data
 *	This routine initializes the local data for the specified adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully initialized local data
 *	QLA_ERROR   - Failed to initialize local data
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_init_local_data(scsi_qla_host_t *ha)
{
	int i;

	/* Initialize passthru PDU list */
	memset(ha->pdu_buf_used, 0, sizeof(ha->pdu_buf_used));
	memset(ha->pdu_buffsv, 0, sizeof(ha->pdu_buff_size));
	for (i = 0; i < (MAX_PDU_ENTRIES - 1); i++) {
		ha->pdu_queue[i].Next = &ha->pdu_queue[i+1];
	}
	ha->free_pdu_top = &ha->pdu_queue[0];
	ha->free_pdu_bottom = &ha->pdu_queue[MAX_PDU_ENTRIES - 1];
	ha->free_pdu_bottom->Next = NULL;
	ha->pdu_active = 0;

	/* Initilize aen queue */
	ha->aen_q_count = MAX_AEN_ENTRIES;

	/* Initialize local iSNS data */
	qla4xxx_isns_init_attributes(ha);
	ha->isns_flags = 0;
	atomic_set(&ha->isns_restart_timer, 0);
	ha->isns_connection_id = 0;
	ha->isns_remote_port_num = 0;
	ha->isns_scn_port_num = 0;
	ha->isns_esi_port_num = 0;
	ha->isns_nsh_port_num = 0;
	memset(ha->isns_entity_id, 0, sizeof(ha->isns_entity_id));
	ha->isns_num_discovered_targets = 0;

	return (QLA_SUCCESS);
}

/**************************************************************************
 * qla4xxx_init_firmware
 *	This routine initializes the firmware.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully initialized firmware
 *	QLA_ERROR   - Failed to initialize firmware
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_init_firmware(scsi_qla_host_t *ha)
{
	uint8_t  status = QLA_ERROR;
	uint32_t timeout_count;

	ENTER("qla4xxx_init_firmware");

	if (qla4xxx_initialize_fw_cb(ha) == QLA_ERROR) {
		LEAVE("qla4xxx_init_firmware");
		return (QLA_ERROR);
	}

	/* If iSNS is enabled, start the iSNS service now. */
	if ((ha->tcp_options & TOPT_ISNS_ENABLE) &&
	    !IPAddrIsZero(ha->isns_ip_address)) {
		uint32_t ip_addr = 0;

		IPAddr2Uint32(ha->isns_ip_address, &ip_addr);
		qla4xxx_isns_reenable(ha, ip_addr, ha->isns_server_port_number);
	}

	for (timeout_count = ADAPTER_INIT_TOV; timeout_count > 0;
	    timeout_count--) {
		/* Get firmware state. */
		if (qla4xxx_get_firmware_state(ha) != QLA_SUCCESS) {
			QL4PRINT(QLP2, printk("scsi%d: %s: unable to get "
			    "firmware state\n", ha->host_no, __func__));
			LEAVE("qla4xxx_init_firmware");

			return (QLA_ERROR);
		}

		if (ha->firmware_state & FW_STATE_ERROR) {
			QL4PRINT(QLP1, printk("scsi%d: %s: an unrecoverable "
			    "error has occurred\n", ha->host_no, __func__));
			LEAVE("qla4xxx_init_firmware");

			return (QLA_ERROR);
		}
		if (ha->firmware_state & FW_STATE_CONFIG_WAIT) {
			/*
			 * The firmware has not yet been issued an Initialize
			 * Firmware command, so issue it now.
			 */
			if (qla4xxx_initialize_fw_cb(ha) == QLA_ERROR) {
				LEAVE("qla4xxx_init_firmware");
				return (status);
			}

			/* Go back and test for ready state - no wait. */
			continue;
		}

		if (ha->firmware_state & FW_STATE_WAIT_LOGIN) {
			QL4PRINT(QLP7, printk("scsi%d: %s: waiting for "
			    "firmware to initialize\n", ha->host_no, __func__));
		}

		if (ha->firmware_state & FW_STATE_DHCP_IN_PROGRESS) {
			QL4PRINT(QLP7, printk("scsi%d: %s: DHCP in progress\n",
			    ha->host_no, __func__));
		}

		if (ha->firmware_state == FW_STATE_READY) {
			/* The firmware is ready to process SCSI commands. */
			QL4PRINT(QLP7, printk("scsi%d: %s: FW STATE - READY\n",
			    ha->host_no, __func__));
			QL4PRINT(QLP7, printk("scsi%d: %s: MEDIA TYPE - %s\n",
			    ha->host_no, __func__,
			    ((ha->addl_fw_state & FW_ADDSTATE_OPTICAL_MEDIA) !=
			    0) ? "OPTICAL" : "COPPER"));
			QL4PRINT(QLP7, printk("scsi%d: %s: DHCP STATE Enabled "
			    "%s\n", ha->host_no, __func__,
			    ((ha->addl_fw_state & FW_ADDSTATE_DHCP_ENABLED) !=
				0) ? "YES" : "NO"));
			QL4PRINT(QLP7, printk("scsi%d: %s: DHCP STATE Lease "
			    "Acquired  %s\n", ha->host_no, __func__,
			    ((ha->addl_fw_state &
				FW_ADDSTATE_DHCP_LEASE_ACQUIRED) != 0) ?
				    "YES" : "NO"));
			QL4PRINT(QLP7, printk("scsi%d: %s: DHCP STATE Lease "
			    "Expired  %s\n", ha->host_no, __func__,
			    ((ha->addl_fw_state &
				FW_ADDSTATE_DHCP_LEASE_EXPIRED) != 0) ?
				    "YES" : "NO"));
			QL4PRINT(QLP7, printk("scsi%d: %s: LINK  %s\n",
			    ha->host_no, __func__,
			    ((ha->addl_fw_state & FW_ADDSTATE_LINK_UP) != 0) ?
				    "UP" : "DOWN"));
			QL4PRINT(QLP7, printk("scsi%d: %s: iSNS Service "
			    "Started  %s\n", ha->host_no, __func__,
			    ((ha->addl_fw_state &
				FW_ADDSTATE_ISNS_SVC_ENABLED) != 0) ?
				    "YES" : "NO"));
			QL4PRINT(QLP7, printk("scsi%d: %s: QLA4040 TopCat "
			    "Initialized  %s\n", ha->host_no, __func__,
			    ((ha->addl_fw_state &
				FW_ADDSTATE_TOPCAT_NOT_INITIALIZED) == 0) ?
				    "YES" : "NO"));

			goto exit_init_fw;
		}

		QL4PRINT(QLP7, printk("scsi%d: %s: (%x/%x) delay 1 sec, time "
		    "remaining %d\n", ha->host_no, __func__, ha->firmware_state,
		    ha->addl_fw_state, timeout_count));
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1 * HZ);
	} /* for */

	QL4PRINT(QLP2, printk("scsi%d: %s: FW Initialization timed out!\n",
	    ha->host_no, __func__));

exit_init_fw:
	set_bit(AF_ONLINE, &ha->flags);
	LEAVE("qla4xxx_init_firmware");

	return (QLA_SUCCESS);
}

#if 0
static void
qla4xxx_map_lun( scsi_qla_host_t *ha,
		 uint16_t target, uint16_t lun, fc_lun_t *fclun )
{
	os_tgt_t        *tq;
	os_lun_t        *lq;

	tq =  &ha->temp_tgt;
	lq =  &ha->temp_lun;
	TGT_Q(ha,target) = tq;
	LUN_Q(ha,target,lun) = lq;
	lq->fclun = fclun;

	QL4PRINT(QLP3, printk("scsi%d: %s: TGT %d, LUN %d\n", ha->host_no, __func__, tq->id, lq->lun));
}

static void
qla4xxx_unmap_lun( scsi_qla_host_t *ha,
		   uint16_t target, uint16_t lun )
{

	if (LUN_Q(ha,target,lun) != NULL) {
		LUN_Q(ha,target,lun) = NULL;
		// qla4xxx_free_lun(ha,target );		
	}
	if (TGT_Q(ha,target) != NULL) {

		TGT_Q(ha,target) = NULL;
	}
	QL4PRINT(QLP3, printk("scsi%d: %s: TGT %d, LUN %d\n", ha->host_no, __func__, target, lun));
}
#endif

/**************************************************************************
 * qla4xxx_is_discovered_target
 *	This routine locates a device handle given iSNS information.
 *	If device doesn't exist, returns NULL.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *      ip_addr - Pointer to IP address
 *      alias - Pointer to iSCSI alias
 *
 * Returns:
 *	Pointer to the corresponding internal device database structure
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static inline uint8_t
qla4xxx_is_discovered_target(scsi_qla_host_t *ha,
			     uint8_t *ip_addr,
			     uint8_t *alias,
			     uint8_t *name_str)
{
	ISNS_DISCOVERED_TARGET *discovered_target = NULL;
	int i,j;

	for (i=0; i < ha->isns_num_discovered_targets; i++) {
		discovered_target = &ha->isns_disc_tgt_databasev[i];

		for (j = 0; j < discovered_target->NumPortals; j++) {
			if (memcmp(discovered_target->Portal[j].IPAddr, ip_addr,
				MIN(sizeof(discovered_target->Portal[j].IPAddr),
				sizeof(*ip_addr)) == 0) &&
			    memcmp(discovered_target->Alias, alias,
				MIN(sizeof(discovered_target->Alias),
				sizeof(*alias)) == 0) &&
			    memcmp(discovered_target->NameString, name_str,
				MIN(sizeof(discovered_target->Alias),
				sizeof(*name_str)) == 0)) {

				return (QLA_SUCCESS);
			}
		}
	}

	return (QLA_ERROR);
}

static ddb_entry_t *
qla4xxx_get_ddb_entry(scsi_qla_host_t *ha, uint32_t fw_ddb_index)
{
	DEV_DB_ENTRY *fw_ddb_entry = NULL;
	dma_addr_t   fw_ddb_entry_dma;
	ddb_entry_t	*ddb_entry = NULL;
	int		found = 0;

	ENTER(__func__);

	/* Make sure the dma buffer is valid */
	fw_ddb_entry = pci_alloc_consistent(ha->pdev, sizeof(*fw_ddb_entry),
	    &fw_ddb_entry_dma);
	if (fw_ddb_entry == NULL) {
		DEBUG2(printk("scsi%d: %s: Unable to allocate dma "
		    "buffer.\n", ha->host_no, __func__));

		return NULL;
	}

	if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, fw_ddb_entry,
	    fw_ddb_entry_dma, NULL, NULL, NULL, NULL, NULL,
	    NULL) == QLA_ERROR) {
		QL4PRINT(QLP2, printk("scsi%d: %s: failed get_ddb_entry for "
		    "fw_ddb_index %d\n", ha->host_no, __func__, fw_ddb_index));
		return NULL;
	}

	/* Allocate DDB if not already allocated. */
	DEBUG2(printk("scsi%d: %s: Looking for ddb match [%d]\n", ha->host_no,
		    __func__, fw_ddb_index));
	list_for_each_entry(ddb_entry, &ha->ddb_list, list_entry) {
	   	if (memcmp(ddb_entry->iscsi_name, fw_ddb_entry->iscsiName,
	   			    ISCSI_NAME_SIZE) == 0) {
			found++; 
	   		break;
	   	}
	}

	if( !found ) {
	DEBUG2(printk("scsi%d: %s: ddb match [%d] not found - allocating new ddb\n", ha->host_no,
		    __func__, fw_ddb_index));
		ddb_entry = qla4xxx_alloc_ddb(ha, fw_ddb_index);
	}
	
	/* if not found allocate new ddb */

	if (fw_ddb_entry)
		pci_free_consistent(ha->pdev, sizeof(*fw_ddb_entry),
		    fw_ddb_entry, fw_ddb_entry_dma);

	return ddb_entry;
}

/**************************************************************************
 * qla4xxx_update_ddb_entry
 *	This routine updates the driver's internal device database entry
 *	with information retrieved from the firmware's device database
 *	entry for the specified device.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	ddb_entry - Pointer to device database entry
 *
 * Output:
 *	ddb_entry - Structure filled in.
 *
 * Remarks:
 *	The ddb_entry->fw_ddb_index field must be initialized prior to
 *	calling this routine
 *
 * Returns:
 *	QLA_SUCCESS - Successfully update ddb_entry
 *	QLA_ERROR   - Failed to update ddb_entry
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_update_ddb_entry(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry)
{
	DEV_DB_ENTRY *fw_ddb_entry = NULL;
	dma_addr_t   fw_ddb_entry_dma;
	uint32_t     fw_ddb_index;
	uint8_t      status = QLA_ERROR;

	ENTER("qla4xxx_update_ddb_entry");

	if (ddb_entry == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: ddb_entry is NULL\n",
				      ha->host_no, __func__));
		goto exit_update_ddb;
	}

	/* Make sure the dma buffer is valid */
	fw_ddb_entry = pci_alloc_consistent(ha->pdev, sizeof(*fw_ddb_entry),
	    &fw_ddb_entry_dma);
	if (fw_ddb_entry == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Unable to allocate dma "
		    "buffer.\n", ha->host_no, __func__));

		goto exit_update_ddb;
	}

	fw_ddb_index = ddb_entry->fw_ddb_index;
	if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, fw_ddb_entry,
	    fw_ddb_entry_dma, NULL, NULL, &ddb_entry->fw_ddb_device_state,
	    &ddb_entry->default_time2wait, &ddb_entry->tcp_source_port_num,
	    &ddb_entry->connection_id) == QLA_ERROR) {
		QL4PRINT(QLP2, printk("scsi%d: %s: failed get_ddb_entry for "
		    "fw_ddb_index %d\n", ha->host_no, __func__, fw_ddb_index));

		goto exit_update_ddb;
	}

	status = QLA_SUCCESS;
	switch (ddb_entry->fw_ddb_device_state) {
	case DDB_DS_SESSION_ACTIVE:
		ddb_entry->target_session_id = le16_to_cpu(fw_ddb_entry->TSID);
		ddb_entry->task_mgmt_timeout =
		    le16_to_cpu(fw_ddb_entry->taskMngmntTimeout);
		ddb_entry->CmdSn = 0;
		ddb_entry->exe_throttle =
		    le16_to_cpu(fw_ddb_entry->exeThrottle);
		ddb_entry->default_relogin_timeout =
		    le16_to_cpu(fw_ddb_entry->taskMngmntTimeout);

		memcpy(&ddb_entry->iscsi_name[0], &fw_ddb_entry->iscsiName[0],
		    MIN(sizeof(ddb_entry->iscsi_name),
		    sizeof(fw_ddb_entry->iscsiName)));
		memcpy(&ddb_entry->ip_addr[0], &fw_ddb_entry->ipAddr[0],
		    MIN(sizeof(ddb_entry->ip_addr),
		    sizeof(fw_ddb_entry->ipAddr)));

		if (qla4xxx_is_discovered_target(ha, fw_ddb_entry->ipAddr,
		    fw_ddb_entry->iSCSIAlias, fw_ddb_entry->iscsiName) ==
		    QLA_SUCCESS) {
			set_bit(DF_ISNS_DISCOVERED, &ddb_entry->flags);
		}

		//QL4PRINT(QLP7, printk("scsi%d: %s: index [%d] \"%s\"\n",
		//		      ha->host_no, __func__,
		//		      fw_ddb_index,
		//		      ddb_state_msg[ddb_entry->fw_ddb_device_state]));
		break;

	case DDB_DS_NO_CONNECTION_ACTIVE:
	case DDB_DS_NO_SESSION_ACTIVE:
	case DDB_DS_SESSION_FAILED:
		ddb_entry->target_session_id = 0;
		ddb_entry->task_mgmt_timeout = 0;
		ddb_entry->connection_id = 0;
		ddb_entry->CmdSn = 0;
		ddb_entry->exe_throttle = 0;
		ddb_entry->default_time2wait = 0;

		//QL4PRINT(QLP7, printk("scsi%d: %s: index [%d] \"%s\"\n",
		//		      ha->host_no, __func__,
		//		      fw_ddb_index,
		//		      ddb_state_msg[ddb_entry->fw_ddb_device_state]));
		break;

	case DDB_DS_UNASSIGNED:
	case DDB_DS_DISCOVERY:
	case DDB_DS_LOGGING_OUT:
		//QL4PRINT(QLP7, printk("scsi%d: %s: index [%d] \"%s\"\n",
		//		      ha->host_no, __func__,
		//		      fw_ddb_index,
		//		      ddb_state_msg[ddb_entry->fw_ddb_device_state]));
		break;

		//default:
		//QL4PRINT(QLP7, printk("scsi%d: %s: index [%d] State %x. "
		//		      "Illegal state\n",
		//		      ha->host_no, __func__,
		//		      fw_ddb_index,
		//		      ddb_entry->fw_ddb_device_state));

	}

	DEBUG2(printk("scsi%d: %s: index [%d] State %x. state\n",
    		ha->host_no, __func__, fw_ddb_index, ddb_entry->fw_ddb_device_state);)

exit_update_ddb:
	if (fw_ddb_entry)
		pci_free_consistent(ha->pdev, sizeof(*fw_ddb_entry),
		    fw_ddb_entry, fw_ddb_entry_dma);

	LEAVE("qla4xxx_update_ddb_entry");

	return (status);
}


static  void
qla4xxx_configure_fcports(scsi_qla_host_t *ha)
{
	fc_port_t       *fcport;

	list_for_each_entry(fcport, &ha->fcports, list) {
		qla4xxx_update_fcport(ha, fcport);
	}
}

static fc_port_t *
qla4xxx_find_or_alloc_fcport(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry)
{
	fc_port_t       *fcport;
	int     found;

	ENTER(__func__);
	/* Check for matching device in port list. */
	found = 0;
	fcport = NULL;
	list_for_each_entry(fcport, &ha->fcports, list) {
		//if (memcmp(new_fcport->port_name, fcport->port_name,
		//WWN_SIZE) == 0)
		if (fcport->ddbptr == ddb_entry) {
			fcport->flags &= ~(FCF_PERSISTENT_BOUND);
			found++;
			break;
		}
	}

	if (!found) {
		/* Allocate a new replacement fcport. */
		fcport = qla4xxx_alloc_fcport(ha, GFP_KERNEL);
		if (fcport != NULL) {
			/* New device, add to fcports list. */
			list_add_tail(&fcport->list, &ha->fcports);
			fcport->ddbptr = ddb_entry;
		}
		// new_fcport->flags &= ~FCF_FABRIC_DEVICE;
	}

	// qla4xxx_update_fcport(ha, fcport);
	LEAVE(__func__);

	return (fcport);
}


/**************************************************************************
 * qla4xxx_alloc_ddb
 *	This routine allocates a ddb_entry, ititializes some values, and
 *	inserts it into the ddb list.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *      fw_ddb_index - Firmware's device database index
 *
 * Returns:
 *	Pointer to internal device database structure
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
ddb_entry_t *
qla4xxx_alloc_ddb(scsi_qla_host_t *ha, uint32_t fw_ddb_index)
{
	ddb_entry_t *ddb_entry;

	QL4PRINT(QLP12, printk("scsi%d: %s: fw_ddb_index [%d]\n", ha->host_no,
	    __func__, fw_ddb_index));

	ddb_entry = (ddb_entry_t *) kmalloc(sizeof(*ddb_entry), GFP_KERNEL);
	if (ddb_entry == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Unable to allocate memory "
		    "to add fw_ddb_index [%d]\n", ha->host_no, __func__,
		    fw_ddb_index));
	} else {
		memset(ddb_entry, 0, sizeof(*ddb_entry));
		ddb_entry->fw_ddb_index = fw_ddb_index;
		atomic_set(&ddb_entry->port_down_timer,
		    ha->port_down_retry_count);
		atomic_set(&ddb_entry->retry_relogin_timer, INVALID_ENTRY);
		atomic_set(&ddb_entry->relogin_timer, 0);
		atomic_set(&ddb_entry->relogin_retry_count, 0);
		atomic_set(&ddb_entry->state, DEV_STATE_ONLINE);
		list_add_tail(&ddb_entry->list_entry, &ha->ddb_list);
		ha->fw_ddb_index_map[fw_ddb_index] = ddb_entry;
		ha->tot_ddbs++;
		ddb_entry->fcport = qla4xxx_find_or_alloc_fcport(ha, ddb_entry);
	}
	return (ddb_entry);
}

/**************************************************************************
 * qla4xxx_build_ddb_list
 *	This routine searches for all valid firmware ddb entries and builds
 *	an internal ddb list.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Remarks:
 *	Ddbs that are considered valid are those with a device state of
 *	SESSION_ACTIVE.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully built internal ddb list
 *	QLA_ERROR   - Failed to build internal ddb list
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_build_ddb_list(scsi_qla_host_t *ha)
{
	uint8_t         status = QLA_ERROR;
	uint32_t        fw_ddb_index = 0;
	uint32_t        next_fw_ddb_index = 0;
	uint32_t        ddb_state;
	ddb_entry_t     *ddb_entry;

	ENTER("qla4xxx_build_ddb_list");

	for (fw_ddb_index = 0; fw_ddb_index < MAX_DDB_ENTRIES;
	    fw_ddb_index = next_fw_ddb_index) {
		/* First, let's see if a device exists here */
		if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, NULL, 0, NULL,
		    &next_fw_ddb_index, &ddb_state, NULL, NULL, NULL) ==
		    QLA_ERROR) {
			QL4PRINT(QLP2, printk("scsi%d: %s: get_ddb_entry, "
			    "fw_ddb_index %d failed", ha->host_no, __func__,
			    fw_ddb_index));
			goto exit_build_ddb_list;
		}

		/* If the device is logged in (SESSION_ACTIVE) then
		 * add it to internal our ddb list. */
		if (ddb_state == DDB_DS_SESSION_ACTIVE) {
			/* Allocate a device structure */
			ddb_entry = qla4xxx_get_ddb_entry(ha, fw_ddb_index);
			if (ddb_entry == NULL) {
				QL4PRINT(QLP2, printk("scsi%d: %s: Unable to "
				    "allocate memory for device at "
				    "fw_ddb_index %d\n", ha->host_no, __func__,
				    fw_ddb_index));
				goto exit_build_ddb_list;
			}

			/* Fill in the device structure */
			if (qla4xxx_update_ddb_entry(ha, ddb_entry) ==
			    QLA_ERROR) {
				ha->fw_ddb_index_map[fw_ddb_index] = 
		    			(ddb_entry_t *) INVALID_ENTRY;
			//	qla4xxx_free_ddb(ha, ddb_entry);
				QL4PRINT(QLP2, printk("scsi%d: %s: "
				    "update_ddb_index, fw_ddb_index %d "
				    "failed\n", ha->host_no, __func__,
				    fw_ddb_index));
				goto exit_build_ddb_list;
			}

			/* if fw_ddb with session active state found,
			 * add to ddb_list */
			QL4PRINT(QLP7, printk("scsi%d: %s: fw_ddb index [%d] "
			    "added to ddb list\n", ha->host_no, __func__,
			    fw_ddb_index));
		} else if (ddb_state == DDB_DS_SESSION_FAILED) {
			QL4PRINT(QLP7, printk("scsi%d: %s: Attempt to login "
			    "index [%d]\n", ha->host_no, __func__,
			    fw_ddb_index));
			qla4xxx_set_ddb_entry(ha, fw_ddb_index, NULL, 0);
		}

		/* We know we've reached the last device when
		 * next_fw_ddb_index is 0 */
		if (next_fw_ddb_index == 0)
			break;
	}

	/* tot_ddbs updated in alloc/free_ddb routines */
	if (ha->tot_ddbs)
		status = QLA_SUCCESS;

exit_build_ddb_list:
	LEAVE("qla4xxx_build_ddb_list");

	return (status);
}

/**************************************************************************
 * qla4xxx_fw_ready
 *	This routine obtains device information from the F/W database during
 *	driver load time.  The device table is rebuilt from scratch.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully (re)built internal ddb list
 *	QLA_ERROR   - Failed to (re)build internal ddb list
 *
 * Context:
 *	Kernel context.
 **************************************************************************/

static int
qla4xxx_fw_ready(scsi_qla_host_t *ha)
{
	int	rval = QLA_ERROR;
	int	rval1;
	unsigned long	wtime;

	//wtime = jiffies + (ql4xdiscoverywait * HZ);
	wtime = jiffies + (30 * HZ);
		
	DEBUG3(printk("Waiting for discovered devices ...\n"));
	do {
		rval1 = qla4xxx_get_firmware_state(ha);
		if (rval1 == QLA_SUCCESS) {
			DEBUG3(printk("fw state=0x%x, curr time=%lx\n",
			    ha->firmware_state,jiffies);)

			/* ready? */
			if (!(ha->firmware_state & (BIT_3|BIT_2|BIT_1|BIT_0))) {
				if (test_bit(DPC_AEN, &ha->dpc_flags)) {
					rval = QLA_SUCCESS;
					DEBUG3(printk("Done...\n"));
					break;
				}
			}
			/* error */
			if (ha->firmware_state & (BIT_2|BIT_0))
				break;
			/* in process */
		}
		if (rval == QLA_SUCCESS)
			break;

		/* delay */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 2);
	} while (!time_after_eq(jiffies,wtime));

	return (rval);
}

static uint8_t
qla4xxx_initialize_ddb_list(scsi_qla_host_t *ha)
{
	uint16_t fw_ddb_index;
	uint8_t status = QLA_SUCCESS;

	ENTER("qla4xxx_initialize_ddb_list");

	/* Reinitialize suspended lun queue. */
	INIT_LIST_HEAD(&ha->suspended_lun_q);
	ha->suspended_lun_q_count = 0;

	/* free the ddb list if is not empty */
	if (!list_empty(&ha->ddb_list))
		qla4xxx_free_ddb_list(ha);

	/* Initialize internal DDB list and mappingss */
	qla4xxx_init_tgt_map(ha);

	for (fw_ddb_index = 0; fw_ddb_index < MAX_DDB_ENTRIES; fw_ddb_index++)
		ha->fw_ddb_index_map[fw_ddb_index] =
		    (ddb_entry_t *) INVALID_ENTRY;

	ha->tot_ddbs = 0;

	QL4PRINT(QLP7, printk(KERN_INFO "scsi%d: Delay while targets are "
	    "being discovered.\n", ha->host_no));
	qla4xxx_fw_ready(ha);

	/*
	 * First perform device discovery for active fw ddb indexes and build
	 * ddb list.
	 */
	qla4xxx_build_ddb_list(ha);

	/*
	 * Here we map a SCSI target to a fw_ddb_index and discover all
	 * possible luns.
	 */
	qla4xxx_configure_fcports(ha);
#ifdef CONFIG_SCSI_QLA4XXX_FAILOVER
	if (!qla4xxx_failover_enabled())
		qla4xxx_config_os(ha);
#else
	qla4xxx_config_os(ha);
#endif

	/*
	 * Targets can come online after the inital discovery, so processing
	 * the aens here will catch them.
	 */
	if (test_and_clear_bit(DPC_AEN, &ha->dpc_flags))
		qla4xxx_process_aen(ha, PROCESS_ALL_AENS);

	if (!ha->tot_ddbs)
		status = QLA_ERROR;

	LEAVE("qla4xxx_initialize_ddb_list");

	return (status);
}

/**************************************************************************
 * qla4xxx_reinitialize_ddb_list
 *	This routine obtains device information from the F/W database after
 *	firmware or adapter resets.  The device table is preserved.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully updated internal ddb list
 *	QLA_ERROR   - Failed to update internal ddb list
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_reinitialize_ddb_list(scsi_qla_host_t *ha)
{
	uint8_t         status = QLA_SUCCESS;
	ddb_entry_t *ddb_entry, *detemp;

	ENTER("qla4xxx_reinitialize_ddb_list");

	/* Update the device information for all devices. */
	list_for_each_entry_safe(ddb_entry, detemp, &ha->ddb_list, list_entry) {
		qla4xxx_update_ddb_entry(ha, ddb_entry);
		if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE) {
			atomic_set(&ddb_entry->state, DEV_STATE_ONLINE);
			// DG XXX
			//atomic_set(&ddb_entry->fcport->state, FCS_ONLINE);
			qla4xxx_update_fcport(ha, ddb_entry->fcport);

			QL4PRINT(QLP3|QLP7, printk(KERN_INFO
			    "scsi%d:%d:%d: %s: index [%d] marked ONLINE\n",
			    ha->host_no, ddb_entry->bus, ddb_entry->target,
			    __func__, ddb_entry->fw_ddb_index));
		} else if (atomic_read(&ddb_entry->state) == DEV_STATE_ONLINE)
			qla4xxx_mark_device_missing(ha, ddb_entry);
	}

	LEAVE("qla4xxx_reinitialize_ddb_list");
	return (status);
}

/**************************************************************************
 * qla4xxx_relogin_device
 *	This routine does a session relogin with the specified device.
 *	The ddb entry must be assigned prior to making this call.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	ddb_entry - Pointer to device database entry
 *
 * Returns:
 *    QLA_SUCCESS = Successfully relogged in device
 *    QLA_ERROR   = Failed to relogin device
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_relogin_device(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry)
{
	uint16_t relogin_timer;

	ENTER("qla4xxx_relogin_device");

	relogin_timer = MAX(ddb_entry->default_relogin_timeout, RELOGIN_TOV);
	atomic_set(&ddb_entry->relogin_timer, relogin_timer);

	QL4PRINT(QLP3, printk(KERN_WARNING
	    "scsi%d:%d:%d: Relogin index [%d]. TOV=%d\n", ha->host_no,
	    ddb_entry->bus, ddb_entry->target, ddb_entry->fw_ddb_index,
	    relogin_timer));

	qla4xxx_set_ddb_entry(ha, ddb_entry->fw_ddb_index, NULL, 0);

	LEAVE("qla4xxx_relogin_device");

	return (QLA_SUCCESS);
}

/**************************************************************************
 * qla4010_topcat_soft_reset
 *	This routine determines if the QLA4040 TopCat chip is present.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	None.
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static void
qla4010_get_topcat_presence(scsi_qla_host_t *ha)
{
	unsigned long flags;
	uint16_t topcat;

	if (qla4xxx_take_hw_semaphore(ha, SEM_NVRAM, SEM_FLG_TIMED_WAIT) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2, printk(KERN_WARNING
		    "scsi%d: %s: Unable to take SEM_NVRAM semaphore\n",
		    ha->host_no, __func__));
		return;
	}
//XXX DG fixme please!
#ifdef CONFIG_SCSI_QLA4XXX_FAILOVER
	set_bit(DPC_FAILOVER_EVENT_NEEDED, &ha->dpc_flags);
	ha->failover_type = MP_NOTIFY_LOOP_UP;
#endif

	spin_lock_irqsave(&ha->hardware_lock, flags);
	topcat = RD_NVRAM_WORD(ha, EEPROM_EXT_HW_CONF_OFFSET());
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	
	if ((topcat & TOPCAT_MASK) == TOPCAT_PRESENT)
		set_bit(AF_TOPCAT_CHIP_PRESENT, &ha->flags);
	else
		clear_bit(AF_TOPCAT_CHIP_PRESENT, &ha->flags);

	qla4xxx_clear_hw_semaphore(ha, SEM_NVRAM);
}

/**************************************************************************
 * qla4xxx_start_firmware
 *	This routine performs the neccessary steps to start the firmware for
 *	the QLA4010 adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully started QLA4010 firmware
 *	QLA_ERROR   - Failed to start QLA4010 firmware
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_start_firmware(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	uint32_t mbox_status;
	uint8_t status = QLA_ERROR;
	uint8_t soft_reset = 0;
	uint8_t boot_firmware = 0;
	uint8_t configure_hardware = 0;

	ENTER("qla4xxx_start_firmware");

	if (IS_QLA4010(ha))
                qla4010_get_topcat_presence(ha);

	/* Is Hardware already initialized? */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	if ((RD_REG_DWORD(ISP_PORT_STATUS(ha)) & PSR_INIT_COMPLETE) != 0) {
		QL4PRINT(QLP7, printk("scsi%d: %s: Hardware has already been "
		    "initialized\n", ha->host_no, __func__));

		/* Is firmware already booted? */
		if (IS_QLA4022(ha)) {
			if ((RD_REG_DWORD(&ha->reg->u1.isp4022.semaphore) &
			    SR_FIRWMARE_BOOTED) != 0) {
				QL4PRINT(QLP7, printk("scsi%d: %s: Firmware "
				    "has already been booted\n", ha->host_no,
				    __func__));

				/* Receive firmware boot acknowledgement */
				mbox_status =
				    RD_REG_DWORD(&ha->reg->mailbox[0]);
				if (mbox_status == MBOX_STS_COMMAND_COMPLETE) {
					/* Acknowledge interrupt */
					WRT_REG_DWORD(&ha->reg->ctrl_status,
					    SET_RMASK(CSR_SCSI_PROCESSOR_INTR));
					PCI_POSTING(&ha->reg->ctrl_status);

					spin_unlock_irqrestore(
					    &ha->hardware_lock, flags);
					qla4xxx_get_fw_version(ha);

					return QLA_SUCCESS;
				} else {
					QL4PRINT(QLP7, printk("scsi%d: %s: "
					    "ERROR: Hardware initialized but "
					    "firmware not successfully "
					    "booted\n", ha->host_no, __func__));

					boot_firmware = 1;
				}
			} else {
				QL4PRINT(QLP7, printk("scsi%d: %s: Firmware "
				    "has NOT already been booted\n",
				    ha->host_no, __func__));

				boot_firmware = 1;
			}
		}
		//XXX Why are we not checking for !boot_firmware?
		//if (!boot_firmware) {
			/* Did BIOS initialize hardware? */
			/*
			 * If the BIOS is loaded then the firmware is already
			 * initialized.  Reinitializing it without first
			 * performing a reset is a NO-NO.  We need to check
			 * here if the BIOS is loaded (i.e.
			 * FW_STATE_CONFIG_WAIT == 0).  If so, force a soft
			 * reset.
			 */   
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			if (qla4xxx_get_firmware_state(ha) == QLA_SUCCESS) {
				if (!(ha->firmware_state &
				    FW_STATE_CONFIG_WAIT)) {
					QL4PRINT(QLP7, printk("scsi%d: %s: "
					    "Firmware has been initialized by "
					    "BIOS -- RESET\n", ha->host_no,
					    __func__));

					soft_reset = 1;

					qla4xxx_process_aen(ha,
					    FLUSH_DDB_CHANGED_AENS);
				}
			} else {
				QL4PRINT(QLP2, printk("scsi%d: %s: Error "
				    "detecting if firmware has already been "
				    "initialized by BIOS -- RESET\n",
				    ha->host_no, __func__));

				soft_reset = 1;
			}
			spin_lock_irqsave(&ha->hardware_lock, flags);
		//}
	} else {
		QL4PRINT(QLP7, printk("scsi%d: %s: Hardware has NOT already "
		    "been initialized\n", ha->host_no, __func__));

		configure_hardware = 1;
		boot_firmware = 1;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (soft_reset) {
		QL4PRINT(QLP7, printk("scsi%d: %s: Issue Soft Reset\n",
		    ha->host_no, __func__));

		status = qla4xxx_soft_reset(ha);

		if (qla4xxx_take_hw_semaphore(ha, SEM_DRIVER, TIMED_WAIT) !=
		    QLA_SUCCESS) {
			QL4PRINT(QLP2, printk(KERN_WARNING
			    "scsi%d: %s: Unable to take SEM_DRIVER semaphore "
			    "(2)\n", ha->host_no, __func__));

			return QLA_ERROR;
		}

		if (status == QLA_ERROR) {
			QL4PRINT(QLP3|QLP7, printk("scsi%d: %s: Soft Reset "
			    "failed!\n", ha->host_no, __func__));

			qla4xxx_clear_hw_semaphore(ha, SEM_DRIVER);

			return status;
		}

		configure_hardware = 1;
		boot_firmware = 1;
	}

	if (configure_hardware) {
		QL4PRINT(QLP7, printk("scsi%d: %s: Set up Hardware "
		    "Configuration Register\n", ha->host_no, __func__));

		if (qla4xxx_take_hw_semaphore(ha, SEM_FLASH, TIMED_WAIT) !=
		    QLA_SUCCESS) {
			QL4PRINT(QLP2, printk(KERN_WARNING
			    "scsi%d: %s: Unable to take SEM_FLASH semaphore\n",
			    ha->host_no, __func__));

			return QLA_ERROR;
		}
		if (qla4xxx_take_hw_semaphore(ha, SEM_NVRAM, TIMED_WAIT) !=
		    QLA_SUCCESS) {
			QL4PRINT(QLP2, printk(KERN_WARNING
			    "scsi%d: %s: Unable to take SEM_NVRAM semaphore\n",
			    ha->host_no, __func__));

			qla4xxx_clear_hw_semaphore(ha, SEM_FLASH);

			return QLA_ERROR;
		}

printk("scsi%d: %s: about to NVRAM config\n", ha->host_no, __func__);
		if (qla4xxx_is_NVRAM_configuration_valid(ha) == QLA_SUCCESS) {
			EXTERNAL_HW_CONFIG_REG	extHwConfig;
			
			spin_lock_irqsave(&ha->hardware_lock, flags);
			extHwConfig.AsUINT32 = RD_NVRAM_WORD(ha,
			    EEPROM_EXT_HW_CONF_OFFSET());
			
			QL4PRINT(QLP7, printk("scsi%d: %s: Setting extHwConfig "
			    "to 0xFFFF%04x\n", ha->host_no, __func__,
			    extHwConfig.AsUINT32));

			WRT_REG_DWORD(ISP_NVRAM(ha),
			    ((0xFFFF << 16) | extHwConfig.AsUINT32));
			PCI_POSTING(ISP_NVRAM(ha));

			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			qla4xxx_clear_hw_semaphore(ha, SEM_NVRAM);
			qla4xxx_clear_hw_semaphore(ha, SEM_FLASH);

			status = QLA_SUCCESS;
		} else {
			/*
			 * QLogic adapters should always have a valid NVRAM.
			 * If not valid, do not load.
			 */
			QL4PRINT(QLP7, printk("scsi%d: %s: EEProm checksum "
			    "invalid.  Please update your EEPROM\n",
			    ha->host_no, __func__));

			qla4xxx_clear_hw_semaphore(ha, SEM_NVRAM);
			qla4xxx_clear_hw_semaphore(ha, SEM_FLASH);

			return QLA_ERROR;
		}
	}

	if (boot_firmware) {
		uint32_t	max_wait_time;

		/*
		 * Start firmware from flash ROM
		 *
		 * WORKAROUND: Stuff a non-constant value that the firmware can
		 * use as a seed for a random number generator in MB7 prior to
		 * setting BOOT_ENABLE.  Fixes problem where the TCP
		 * connections use the same TCP ports after each reboot,
		 * causing some connections to not get re-established.
		 */
		QL4PRINT(QLP7, printk("scsi%d: %s: Start firmware from flash "
		    "ROM\n", ha->host_no, __func__));

		spin_lock_irqsave(&ha->hardware_lock, flags);
		WRT_REG_DWORD(&ha->reg->mailbox[7], jiffies);
		WRT_REG_DWORD(&ha->reg->ctrl_status,
		    SET_RMASK(CSR_BOOT_ENABLE));
		PCI_POSTING(&ha->reg->ctrl_status);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* Wait for firmware to come UP. */
		max_wait_time = FIRMWARE_UP_TOV;
		do {
			uint32_t ctrl_status;

			spin_lock_irqsave(&ha->hardware_lock, flags);
			ctrl_status = RD_REG_DWORD(&ha->reg->ctrl_status);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			if (ctrl_status & SET_RMASK(CSR_SCSI_PROCESSOR_INTR))
				break;
					
			QL4PRINT(QLP7, printk("scsi%d: %s: Waiting for "
			    "firmware to come up... ctrl_sts=0x%x, "
			    "remaining=%d\n", ha->host_no, __func__,
			    ctrl_status, max_wait_time));

			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1 * HZ);
		} while ((max_wait_time--));

		spin_lock_irqsave(&ha->hardware_lock, flags);
		mbox_status = RD_REG_DWORD(&ha->reg->mailbox[0]);
		if (mbox_status == MBOX_STS_COMMAND_COMPLETE) {
			QL4PRINT(QLP7, printk("scsi%d: %s: Firmware has "
			    "started\n", ha->host_no, __func__));

			if (IS_QLA4010(ha)) {
				ha->firmware_version[0] =
				    RD_REG_DWORD(&ha->reg->mailbox[1]);
				ha->firmware_version[1] =
				    RD_REG_DWORD(&ha->reg->mailbox[2]);
				ha->patch_number =
				    RD_REG_DWORD(&ha->reg->mailbox[3]);
				ha->build_number =
				    RD_REG_DWORD(&ha->reg->mailbox[4]);

				QL4PRINT(QLP7, printk("scsi%d: FW Version "
				    "%02d.%02d Patch %02d Build %02d\n",
				    ha->host_no, ha->firmware_version[0],
				    ha->firmware_version[1], ha->patch_number,
				    ha->build_number));
			}

			WRT_REG_DWORD(&ha->reg->ctrl_status,
			    SET_RMASK(CSR_SCSI_PROCESSOR_INTR));
			PCI_POSTING(&ha->reg->ctrl_status);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			status = QLA_SUCCESS;
		} else {
			QL4PRINT(QLP2, printk("scsi%d: %s: Self Test failed "
			    "with status 0x%x\n", ha->host_no, __func__,
			    mbox_status));

			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			status = QLA_ERROR;
		}
	}

	if (status == QLA_SUCCESS) {
		if (test_and_clear_bit(AF_GET_CRASH_RECORD, &ha->flags))
			qla4xxx_get_crash_record(ha);
	} else {
		QL4PRINT(QLP7, printk("scsi%d: %s: Firmware has NOT started\n",
		    ha->host_no, __func__));

		qla4xxx_dump_registers(QLP7, ha);
	}

	LEAVE("qla4xxx_start_firmware");
	return status;
}

static void
qla2x00_pci_config(scsi_qla_host_t *ha)
{
	uint16_t        w, mwi;

	ql4_printk(KERN_INFO, ha, "Configuring PCI space...\n");

	pci_set_master(ha->pdev);
	mwi = 0;
	if (pci_set_mwi(ha->pdev))
		mwi = PCI_COMMAND_INVALIDATE;

	/*
	 * We want to respect framework's setting of PCI configuration space
	 * command register and also want to make sure that all bits of
	 * interest to us are properly set in command register.
	 */
	pci_read_config_word(ha->pdev, PCI_COMMAND, &w);
	w |= mwi | (PCI_COMMAND_PARITY | PCI_COMMAND_SERR);
	w &= ~PCI_COMMAND_INTX_DISABLE;
	pci_write_config_word(ha->pdev, PCI_COMMAND, w);
}	

/**************************************************************************
 * qla4xxx_initialize_adapter
 *	This routine parforms all of the steps necessary to initialize the
 *	adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	renew_ddb_list - Indicates what to do with the adapter's ddb list
 *			after adapter recovery has completed.
 *			0=preserve ddb list, 1=destroy and rebuild ddb list
 *
 * Returns:
 *	QLA_SUCCESS - Successfully initialized adapter
 *	QLA_ERROR   - Failed to initialize adapter
 *
 * Context:
 *	Kernel context.
 **************************************************************************/

uint8_t
qla4xxx_initialize_adapter(scsi_qla_host_t *ha, uint8_t renew_ddb_list)
{
	uint8_t      status;

	ENTER("qla4xxx_initialize_adapter");

	qla2x00_pci_config(ha);

	/* Initialize the Host adapter request/response queues and firmware */
	if ((status = qla4xxx_start_firmware(ha)) == QLA_ERROR) {
		QL4PRINT(QLP2, printk(KERN_INFO
		    "scsi%d: Failed to start QLA4010 firmware\n", ha->host_no));
	} else if ((status = qla4xxx_validate_mac_address(ha)) == QLA_ERROR) {
		QL4PRINT(QLP2, printk(KERN_WARNING
		    "scsi%d: Failed to validate mac address\n", ha->host_no));
	} else if ((status = qla4xxx_init_local_data(ha)) == QLA_ERROR) {
		QL4PRINT(QLP2, printk(KERN_WARNING
		    "scsi%d: Failed to initialize local data\n", ha->host_no));
	} else if ((status = qla4xxx_init_firmware(ha)) == QLA_ERROR) {
		QL4PRINT(QLP2, printk(KERN_WARNING
		    "scsi%d: Failed to initialize firmware\n", ha->host_no));
	} else {
		if (renew_ddb_list == PRESERVE_DDB_LIST) {
			/*
			 * We want to preserve lun states (i.e. suspended, etc.)
			 * for recovery initiated by the driver.  So just update
			 * the device states for the existing ddb_list
			 */
			qla4xxx_reinitialize_ddb_list(ha);
		}
		else if (renew_ddb_list == REBUILD_DDB_LIST) {
			/*
			 * We want to build the ddb_list from scratch during
			 * driver initialization and recovery initiated by the
			 * INT_HBA_RESET IOCTL.
			 */
			qla4xxx_initialize_ddb_list(ha);
		}

		if (test_bit(ISNS_FLAG_ISNS_ENABLED_IN_ISP, &ha->isns_flags)) {
			u_long wait_cnt = jiffies + ql4xdiscoverywait * HZ;

			if (!test_bit(ISNS_FLAG_ISNS_SRV_ENABLED,
			    &ha->isns_flags)) {
				QL4PRINT(QLP7, printk(KERN_INFO
				    "scsi%d: Delay up to %d seconds while "
				    "targets are being discovered.\n",
				    ha->host_no, ql4xdiscoverywait));

				while (wait_cnt > jiffies){
					if (test_bit(ISNS_FLAG_ISNS_SRV_ENABLED,
					    &ha->isns_flags))
						break;
					QL4PRINT(QLP7, printk("."));
					set_current_state(TASK_UNINTERRUPTIBLE);
					schedule_timeout(1 * HZ);
				}
			}

			if (!test_bit(ISNS_FLAG_ISNS_SRV_ENABLED,
			    &ha->isns_flags)) {
				QL4PRINT(QLP2, printk(KERN_WARNING
				    "scsi%d: iSNS service failed to start\n",
				    ha->host_no));
			}
		}

		if (!ha->tot_ddbs)
			QL4PRINT(QLP2, printk(KERN_WARNING
			    "scsi%d: Failed to initialize devices\n",
			    ha->host_no));
	}

	LEAVE("qla4xxx_initialize_adapter");
	return (status);
}

/**************************************************************************
 * qla4xxx_find_propname
 *	Get property in database.
 *
 * Input:
 *	ha = adapter structure pointer.
 *      db = pointer to database
 *      propstr = pointer to dest array for string
 *	propname = name of property to search for.
 *	siz = size of property
 *
 * Returns:
 *	0 = no property
 *      size = index of property
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static uint8_t
qla4xxx_find_propname(scsi_qla_host_t *ha,
		      char *propname, char *propstr,
		      char *db, int siz)
{
	char    *cp;

	/* find the specified string */
	if (db) {
		/* find the property name */
		if ((cp = strstr(db,propname)) != NULL) {
			while ((*cp)  && *cp != '=')
				cp++;
			if (*cp) {
				strncpy(propstr, cp, siz+1);
				propstr[siz+1] = '\0';
				QL4PRINT(QLP7, printk("scsi%d: %s: found "
						      "property = {%s}\n",
						      ha->host_no, __func__,
						      propstr));
				return(siz);	       /* match */
			}
		}
	}

	return(0);
}


/**************************************************************************
 * qla4xxx_get_prop_12chars
 *	Get a 6-byte property value for the specified property name by
 *      converting from the property string found in the configuration file.
 *      The resulting converted value is in big endian format (MSB at byte0).
 *
 * Input:
 *	ha = adapter state pointer.
 *	propname = property name pointer.
 *	propval  = pointer to location for the converted property val.
 *      db = pointer to database
 *
 * Returns:
 *	0 = value returned successfully.
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_get_prop_12chars(scsi_qla_host_t *ha, uint8_t *propname,
			 uint8_t *propval, uint8_t *db)
{
	char            *propstr;
	int             i, k;
	int             rval;
	uint8_t         nval;
	uint8_t         *pchar;
	uint8_t         *ret_byte;
	uint8_t         *tmp_byte;
	uint8_t         *retval = (uint8_t*)propval;
	uint8_t         tmpval[6] = {0, 0, 0, 0, 0, 0};
	uint16_t        max_byte_cnt = 6;	  /* 12 chars = 6 bytes */
	uint16_t        max_strlen = 12;
	static char     buf[LINESIZE];

	rval = qla4xxx_find_propname(ha, propname, buf, db, max_strlen);

	propstr = &buf[0];
	if (*propstr == '=')
		propstr++;	     /* ignore equal sign */

	if (rval == 0) {
		return(1);
	}

	/* Convert string to numbers. */
	pchar = (uint8_t *)propstr;
	tmp_byte = (uint8_t *)tmpval;

	rval = 0;
	for (i = 0; i < max_strlen; i++) {
		/*
		 * Check for invalid character, two at a time,
		 * then convert them starting with first byte.
		 */

		if ((pchar[i] >= '0') && (pchar[i] <= '9')) {
			nval = pchar[i] - '0';
		}
		else if ((pchar[i] >= 'A') && (pchar[i] <= 'F')) {
			nval = pchar[i] - 'A' + 10;
		}
		else if ((pchar[i] >= 'a') && (pchar[i] <= 'f')) {
			nval = pchar[i] - 'a' + 10;
		}
		else {
			/* invalid character */
			rval = 1;
			break;
		}

		if (i & 0x01) {
			*tmp_byte = *tmp_byte | nval;
			tmp_byte++;
		}
		else {
			*tmp_byte = *tmp_byte | nval << 4;
		}
	}

	if (rval != 0) {
		/* Encountered invalid character. */
		return(rval);
	}

	/* Copy over the converted value. */
	ret_byte = retval;
	tmp_byte = tmpval;

	i = max_byte_cnt;
	k = 0;
	while (i--) {
		*ret_byte++ = *tmp_byte++;
	}

	/* big endian retval[0]; */
	return(QLA_SUCCESS);
}

/**************************************************************************
 * qla4xxx_add_device_dynamically
 *	This routine processes adds a device as a result of an 8014h AEN.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *      fw_ddb_index - Firmware's device database index
 *
 * Returns:
 *	None
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static void
qla4xxx_add_device_dynamically(scsi_qla_host_t *ha,
			       uint32_t fw_ddb_index)
{
	ddb_entry_t *ddb_entry;

	ENTER("qla4xxx_add_device_dynamically");

	/* First allocate a device structure */
	ddb_entry = qla4xxx_get_ddb_entry(ha, fw_ddb_index);
	if (ddb_entry == NULL) {
		QL4PRINT(QLP2, printk(KERN_WARNING
		    "scsi%d: Unable to allocate memory to add fw_ddb_index "
		    "%d\n", ha->host_no, fw_ddb_index));
	} else if (qla4xxx_update_ddb_entry(ha, ddb_entry) == QLA_ERROR) {
		QL4PRINT(QLP2, printk(KERN_WARNING
		    "scsi%d: failed to add new device at index [%d]\n"
		    "Unable to retrieve fw ddb entry\n", ha->host_no,
		    fw_ddb_index));
	} else {
		/* New device. Let's add it to the database */
		QL4PRINT(QLP7, printk("scsi%d: %s: new device at index [%d]\n",
		    ha->host_no, __func__, fw_ddb_index));

		qla4xxx_update_fcport(ha, ddb_entry->fcport);
		qla4xxx_config_os(ha);
	}

	LEAVE("qla4xxx_add_device_dynamically");
}


/**************************************************************************
 * qla4xxx_process_ddb_changed
 *	This routine processes a Decive Database Changed AEN Event.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *      fw_ddb_index - Firmware's device database index
 *      state - Device state
 *
 * Returns:
 *	QLA_SUCCESS - Successfully processed ddb_changed aen
 *	QLA_ERROR   - Failed to process ddb_changed aen
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_process_ddb_changed(scsi_qla_host_t *ha, uint32_t fw_ddb_index,
    uint32_t state)
{
	ddb_entry_t *ddb_entry;
	uint32_t    old_fw_ddb_device_state;

	ENTER(__func__);

	/* check for out of range index */
	if (fw_ddb_index >= MAX_DDB_ENTRIES) {
		QL4PRINT(QLP2, printk("scsi%d: %s: device index [%d] out of "
		    "range\n", ha->host_no, __func__, fw_ddb_index));

		LEAVE(__func__);
		return (QLA_ERROR);
	}

	/* Get the corresponging ddb entry */
	ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, fw_ddb_index);

	/* Device does not currently exist in our database. */
	if (ddb_entry == NULL) {
		if (state == DDB_DS_SESSION_ACTIVE) {
			qla4xxx_add_device_dynamically(ha, fw_ddb_index);
		}
//FIXME: Is this really necessary?
#if 0
		else if (state == DDB_DS_SESSION_FAILED ) {
			ddb_entry = qla4xxx_get_ddb_entry(ha, fw_ddb_index);
			if( ddb_entry ) {
		       	    atomic_set(&ddb_entry->retry_relogin_timer,
			    ddb_entry->default_time2wait);
			    qla4xxx_mark_device_missing(ha, ddb_entry);
			}
		}
#endif
		LEAVE(__func__);

		return (QLA_SUCCESS);
	}

	/* Device already exists in our database. */
	old_fw_ddb_device_state = ddb_entry->fw_ddb_device_state;
	DEBUG2(printk("scsi%d: %s DDB - old fw state= 0x%x, "
		"new fw state=0x%x for index [%d]\n",
		ha->host_no, __func__, ddb_entry->fw_ddb_device_state,
		state,
		fw_ddb_index));
	if (old_fw_ddb_device_state == state) {
		/* Do nothing, state not changed. */
		LEAVE(__func__);

		return (QLA_SUCCESS);
	}

//FIXME: Is this really necessary?
#if 0
	if (qla4xxx_get_fwddb_entry(ha, ddb_entry->fw_ddb_index, NULL, 0, NULL,
	    NULL, &ddb_entry->fw_ddb_device_state, NULL, NULL, NULL) ==
	    QLA_ERROR) {
		#if 0
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: unable to retrieve "
				"fw_ddb_device_state for index [%d]\n",
				ha->host_no, __func__, fw_ddb_index));

		LEAVE(__func__);
		return(QLA_ERROR);
		#else
		ddb_entry->fw_ddb_device_state = state;
		#endif
	}
	
	DEBUG2(printk("scsi%d: %s DDB after query - old fw state= 0x%x, "
		"new fw state=0x%x for index [%d]\n",
		ha->host_no, __func__, ddb_entry->fw_ddb_device_state,
		state,
		fw_ddb_index));
#else
	ddb_entry->fw_ddb_device_state = state;
#endif
	/* Device is back online. */
	if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE) {
		atomic_set(&ddb_entry->port_down_timer,
		    ha->port_down_retry_count);
		atomic_set(&ddb_entry->state, DEV_STATE_ONLINE);
		atomic_set(&ddb_entry->relogin_retry_count, 0);
		atomic_set(&ddb_entry->relogin_timer, 0);
		clear_bit(DF_RELOGIN, &ddb_entry->flags);
		clear_bit(DF_NO_RELOGIN, &ddb_entry->flags);
		qla4xxx_update_fcport(ha, ddb_entry->fcport);

/* XXX FIXUP LUN_READY/SUSPEND code -- dg */
		/*
		 * Change the lun state to READY in case the lun TIMEOUT before
		 * the device came back.
		 */
		if (ddb_entry->fcport->vis_ha) {
			int t, l;
			unsigned long cpu_flags;
			os_lun_t *lq;
			scsi_qla_host_t *os_ha;

			os_ha = ddb_entry->fcport->vis_ha;
			for (t = 0; t < MAX_TARGETS; t++) {
				for (l = 0; l < MAX_LUNS; l++) {
					if (!(lq = GET_LU_Q(os_ha, t, l)))
						continue;

					spin_lock_irqsave(&lq->lun_lock,
						cpu_flags);
					lq->lun_state = LS_LUN_READY;
					ddb_entry->fcport->vis_ha = NULL;
					spin_unlock_irqrestore(&lq->lun_lock,
				    		cpu_flags);

				}
			}
		}

		// DG XXX
#ifdef CONFIG_SCSI_QLA4XXX_FAILOVER
		set_bit(DPC_FAILOVER_EVENT_NEEDED, &ha->dpc_flags);
		ha->failover_type = MP_NOTIFY_LOOP_UP;
#endif
	} else {		
		/* Device went away, try to relogin. */
		/* Mark device missing */
		if (atomic_read(&ddb_entry->state) == DEV_STATE_ONLINE)
			qla4xxx_mark_device_missing(ha, ddb_entry);

		/*
		 * Relogin if device state changed to a not active state.
		 * However, do not relogin if this aen is a result of an IOCTL
		 * logout (DF_NO_RELOGIN) or if this is a discovered device.
		 */
		if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_FAILED &&
		    (!test_bit(DF_RELOGIN, &ddb_entry->flags)) &&
		    (!test_bit(DF_NO_RELOGIN, &ddb_entry->flags)) &&
		    (!test_bit(DF_ISNS_DISCOVERED, &ddb_entry->flags))) {
			QL4PRINT(QLP3, printk("scsi%d:%d:%d: index [%d] "
			    "initate relogin after %d seconds\n", ha->host_no,
			    ddb_entry->bus, ddb_entry->target,
			    ddb_entry->fw_ddb_index,
			    ddb_entry->default_time2wait));

#ifndef CONFIG_SCSI_QLA4XXX_FAILOVER
			// DG XXX
			qla4xxx_update_fcport(ha, ddb_entry->fcport);
#endif

			/*
			 * This triggers a relogin.  After the relogin_timer
			 * expires, the relogin gets scheduled.  We must wait a
			 * minimum amount of time since receiving an 0x8014 AEN
			 * with failed device_state or a logout response before
			 * we can issue another relogin.
			 */
			atomic_set(&ddb_entry->retry_relogin_timer,
			    ddb_entry->default_time2wait);
		}
	}

	LEAVE(__func__);

	return (QLA_SUCCESS);
}


/**************************************************************************
 * qla4xxx_login_device
 *	This routine is called by the login IOCTL to log in the specified
 *	device.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 * 	fw_ddb_index - Index of the device to login
 * 	connection_id - Connection ID of the device to login
 *
 * Returns:
 *	QLA_SUCCESS - Successfully logged in device
 *	QLA_ERROR   - Failed to login device
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_login_device(scsi_qla_host_t *ha, uint16_t fw_ddb_index,
    uint16_t connection_id)
{
	ddb_entry_t *ddb_entry;
	uint8_t status = QLA_ERROR;

	ENTER("qla4xxx_login_device");

	QL4PRINT(QLP3, printk("scsi%d: %s: Login index [%d]\n", ha->host_no,
	    __func__, fw_ddb_index));

	ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, fw_ddb_index);
	if (ddb_entry == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Invalid index [%d]\n",
		    ha->host_no, __func__, fw_ddb_index));
		goto exit_login_device;
	}

	if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, NULL, 0, NULL, NULL,
	    &ddb_entry->fw_ddb_device_state, NULL, NULL, NULL) == QLA_ERROR) {
		QL4PRINT(QLP2, printk("scsi%d: %s: 1st get ddb entry failed\n",
		    ha->host_no, __func__));
		goto exit_login_device;
	}

	if (ddb_entry->fw_ddb_device_state == DDB_DS_SESSION_ACTIVE) {
		QL4PRINT(QLP3, printk("scsi%d: %s: login successful for index "
		    "[%d]\n", ha->host_no, __func__, ddb_entry->fw_ddb_index));

		status = QLA_SUCCESS;

		goto exit_login_device;
	}

	if (qla4xxx_conn_close_sess_logout(ha, fw_ddb_index, connection_id,
	    LOGOUT_OPTION_RELOGIN) != QLA_SUCCESS) {
		goto exit_login_device;
	}

	status = QLA_SUCCESS;

exit_login_device:
	LEAVE("qla4xxx_login_device");

	return (status);
}

/**************************************************************************
 * qla4xxx_logout_device
 *	This support routine is called by the logout IOCTL to log out
 *	the specified device.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 * 	fw_ddb_index - Index of the device to logout
 * 	connection_id - Connection ID of the device to logout
 *
 * Returns:
 *	QLA_SUCCESS - Successfully logged out device
 *	QLA_ERROR   - Failed to logout device
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_logout_device(scsi_qla_host_t *ha, uint16_t fw_ddb_index,
    uint16_t connection_id)
{
	uint8_t     status = QLA_ERROR;
	ddb_entry_t *ddb_entry;
	uint32_t    old_fw_ddb_device_state;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered. index=%d.\n",
	    ha->host_no, __func__, ha->instance, fw_ddb_index));

	ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, fw_ddb_index);
	if (ddb_entry == NULL) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: Invalid index [%d]\n",
		    ha->host_no, __func__, fw_ddb_index));
		goto exit_logout_device;
	}

	if (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, NULL, 0, NULL, NULL,
	    &old_fw_ddb_device_state, NULL, NULL, NULL) != QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: get_ddb_entry failed\n",
		    ha->host_no, __func__));
		goto exit_logout_device;
	}

	set_bit(DF_NO_RELOGIN, &ddb_entry->flags);

	if (qla4xxx_conn_close_sess_logout(ha, fw_ddb_index, connection_id,
	    LOGOUT_OPTION_CLOSE_SESSION) != QLA_SUCCESS) {
		goto exit_logout_device;
	}

	status = QLA_SUCCESS;

exit_logout_device:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return (status);
}

void
qla4xxx_flush_all_srbs(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry,
    os_lun_t *lun_entry)
{
	int         i;
	unsigned long flags;
	srb_t       *srb, *stemp;

	if (lun_entry == NULL || ddb_entry == NULL)
		return;

	spin_lock_irqsave(&ha->list_lock, flags);
	/* free pending commands */
	list_for_each_entry_safe(srb, stemp, &ha->pending_srb_q, list_entry) {
		QL4PRINT(QLP3, printk("scsi%d:%d:%d:%d: %s: found srb %p in "
		    "pending_q\n", ha->host_no, ddb_entry->bus,
		    ddb_entry->target, lun_entry->lun, __func__, srb));

		if (srb->lun_queue != lun_entry)
			continue;

		QL4PRINT(QLP3, printk("scsi%d:%d:%d:%d: %s: flushing srb %p "
		    "from pending_q\n", ha->host_no, ddb_entry->bus,
		    ddb_entry->target, lun_entry->lun, __func__, srb));

		__del_from_pending_srb_q(ha, srb);
		srb->cmd->result = DID_NO_CONNECT << 16;
		__add_to_done_srb_q(ha, srb);
	}

	/* free retry commands */
	list_for_each_entry_safe(srb, stemp, &ha->retry_srb_q, list_entry) {
		QL4PRINT(QLP3, printk("scsi%d:%d:%d:%d: %s: found srb %p in "
		    "retry_q\n", ha->host_no, ddb_entry->bus,
		    ddb_entry->target, lun_entry->lun, __func__, srb));

		if (srb->lun_queue != lun_entry)
			continue;

		QL4PRINT(QLP3, printk("scsi%d:%d:%d:%d: %s: flushing srb %p "
		    "from retry_q\n", ha->host_no, ddb_entry->bus,
		    ddb_entry->target, lun_entry->lun, __func__, srb));

		__del_from_retry_srb_q(ha, srb);
		srb->cmd->result = DID_NO_CONNECT << 16;
		__add_to_done_srb_q(ha, srb);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	/* free active commands */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (lun_entry->out_count != 0) {
		for (i = 1; i < MAX_SRBS; i++) {
			srb = ha->active_srb_array[i];
			if (!srb)
				continue;

			QL4PRINT(QLP3, printk("scsi%d:%d:%d:%d: %s: found srb "
			    "%p in active_q\n", ha->host_no, ddb_entry->bus,
			    ddb_entry->target, lun_entry->lun, __func__, srb));

			if (srb->lun_queue != lun_entry)
				continue;

			QL4PRINT(QLP3, printk("scsi%d:%d:%d:%d: %s: flushing "
			    "srb %p from active_q\n", ha->host_no,
			    ddb_entry->bus, ddb_entry->target, lun_entry->lun,
			    __func__, srb));
			del_from_active_array(ha, i);
			srb->cmd->result = DID_NO_CONNECT << 16;
			add_to_done_srb_q(ha,srb);
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Free Failover commands */
#ifdef CONFIG_SCSI_QLA4XXX_FAILOVER
	qla4xxx_flush_failover_q(ha, lun_entry);
#endif

	/* Send all srbs back to OS */
	if (!list_empty(&ha->done_srb_q)) {
		while ((srb = del_from_done_srb_q_head(ha)) != NULL)
			qla4xxx_complete_request(ha, srb);
	}
}


/**************************************************************************
 * qla4xxx_delete_device
 *	This routine is called by the logout IOCTL to delete the specified
 *      device.	 Send the LOGOUT and DELETE_DDB commands for the specified
 *      target, even if it's not in our internal database.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 * 	fw_ddb_index - Index of the device to delete
 * 	connection_id - Connection ID of the device to delete
 *
 * Returns:
 *	QLA_SUCCESS - Successfully deleted device
 *	QLA_ERROR   - Failed to delete device
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_delete_device(scsi_qla_host_t *ha, uint16_t fw_ddb_index,
    uint16_t connection_id)
{
	uint8_t     status = QLA_ERROR;
	uint32_t    fw_ddb_device_state = 0xFFFF;
	u_long     wait_count;
	ddb_entry_t *ddb_entry;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered. index=%d.\n",
	    ha->host_no, __func__, ha->instance, fw_ddb_index));

	/* If the device is in our internal tables, set the NO_RELOGIN bit. */
	ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, fw_ddb_index);
	if (ddb_entry != NULL) {
		QL4PRINT(QLP4,
		    printk("scsi%d:%d:%d: %s:  setting NO_RELOGIN flag\n",
		    ha->host_no, ddb_entry->bus, ddb_entry->target, __func__));

		set_bit(DF_NO_RELOGIN, &ddb_entry->flags);
	}

	/*
	 * If the device state is already one that we can delete, bypass the
	 * logout command.
	 */
	qla4xxx_get_fwddb_entry(ha, fw_ddb_index, NULL, 0, NULL, NULL,
	    &fw_ddb_device_state, NULL, NULL, NULL);
	if (fw_ddb_device_state == DDB_DS_UNASSIGNED ||
	    fw_ddb_device_state == DDB_DS_NO_CONNECTION_ACTIVE ||
	    fw_ddb_device_state == DDB_DS_SESSION_FAILED)
		goto delete_ddb;

	/* First logout index */
	if (qla4xxx_conn_close_sess_logout(ha, fw_ddb_index, connection_id,
	    LOGOUT_OPTION_CLOSE_SESSION) != QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: LOGOUT_OPTION_CLOSE_SESSION "
		    "failed index [%d]\n", ha->host_no, __func__,
		    fw_ddb_index));
		goto exit_delete_ddb;
	}

	/* Wait enough time to complete logout */
	wait_count = jiffies + LOGOUT_TOV * HZ;
	while (qla4xxx_get_fwddb_entry(ha, fw_ddb_index, NULL, 0, NULL, NULL,
	    &fw_ddb_device_state, NULL, NULL, NULL) == QLA_SUCCESS) {
		if (wait_count <= jiffies)
			goto exit_delete_ddb;

		if (fw_ddb_device_state == DDB_DS_UNASSIGNED ||
                    fw_ddb_device_state == DDB_DS_NO_CONNECTION_ACTIVE ||
                    fw_ddb_device_state == DDB_DS_SESSION_FAILED)
			break;

		udelay(50);
	}

delete_ddb:
	/* Now delete index */
	if (qla4xxx_clear_database_entry(ha, fw_ddb_index) == QLA_SUCCESS) {
		uint16_t lun;
		os_lun_t *lun_entry;
		os_tgt_t *tgt_entry;

		status = QLA_SUCCESS;
		if (!ddb_entry)
			goto exit_delete_ddb;

		atomic_set(&ddb_entry->state, DEV_STATE_DEAD);
		atomic_set(&ddb_entry->fcport->state, FCS_DEVICE_DEAD);
/* XXX FIXUP LUN_READY/SUSPEND code -- dg */
		tgt_entry = qla4xxx_lookup_target_by_fcport(ha,
		    ddb_entry->fcport);
		if (tgt_entry) {
			for (lun = 0; lun < MAX_LUNS; lun++) {
				lun_entry = tgt_entry->olun[lun];
				if (lun_entry != NULL) {
					unsigned long cpu_flags;

					spin_lock_irqsave(&lun_entry->lun_lock,
					    cpu_flags);

					QL4PRINT(QLP4, printk(
					    "scsi%d:%d:%d:%d: %s: flushing "
					    "srbs, pendq_cnt=%d, retryq_cnt="
					    "%d, activeq_cnt=%d\n", ha->host_no,
					    ddb_entry->bus, tgt_entry->id, lun,
					    __func__, ha->pending_srb_q_count,
					    ha->retry_srb_q_count,
					    ha->active_srb_count));

					qla4xxx_flush_all_srbs(ha, ddb_entry,
					    lun_entry);
					if (lun_entry->lun_state ==
					    LS_LUN_SUSPENDED) {
						lun_entry->lun_state =
						    LS_LUN_READY;
					}

					spin_unlock_irqrestore(
					    &lun_entry->lun_lock, cpu_flags);
				}
			}
		}
		ha->fw_ddb_index_map[fw_ddb_index] = 
		    (ddb_entry_t *) INVALID_ENTRY;
		// qla4xxx_free_ddb(ha, ddb_entry);
	}

exit_delete_ddb:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return (status);
}


/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
