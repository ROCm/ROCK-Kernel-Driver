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
 *
 ****************************************************************************/

/**************************************************************************
 * qla4xxx_lookup_lun_handle
 *	This routine locates a lun handle given the device handle and lun
 *	number.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	ddb_entry - Pointer to device database entry
 *	lun - SCSI LUN
 *
 * Returns:
 *	Pointer to corresponding lun_entry structure
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static inline os_lun_t *
qla4xxx_lookup_lun_handle(scsi_qla_host_t *ha, os_tgt_t *tq, uint16_t lun)
{
	os_lun_t *lq = NULL;

	if (tq && lun < MAX_LUNS)
		lq = tq->olun[lun];
	 
	QL4PRINT(QLP3, printk("scsi%d: %s: lun %d, lun_entry = %p\n",
	    ha->host_no, __func__, lun, lq));

	return lq;
}

/**************************************************************************
 * qla4xxx_lookup_target_by_SCSIID
 *	This routine locates a target handle given the SCSI bus and
 *	target IDs.  If device doesn't exist, returns NULL.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	bus - SCSI bus number
 *	target - SCSI target ID.
 *
 * Returns:
 *	Pointer to the corresponding internal device database structure
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static inline os_tgt_t *
qla4xxx_lookup_target_by_SCSIID(scsi_qla_host_t *ha, uint32_t bus,
    uint32_t target)
{
	os_tgt_t *tq = NULL;

	if (target < MAX_TARGETS)
		tq = TGT_Q(ha, target);

	QL4PRINT(QLP3, printk("scsi%d: %s: b%d:t%d, tgt = %p\n",
	    ha->host_no, __func__, bus, target, tq));

	return tq;
}

/**************************************************************************
 * qla4xxx_lookup_target_by_fcport
 *	This routine locates a target handle given the fcport
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	fcport - port handle
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static inline os_tgt_t *
qla4xxx_lookup_target_by_fcport(scsi_qla_host_t *ha, fc_port_t  *fcport)
{
	int t;
	os_tgt_t *tq = NULL;

	for (t = 0; t < MAX_TARGETS; t++) {
		if ((tq = TGT_Q(ha, t)) == NULL)
			continue;

		if (fcport == tq->fcport)
			break;
	}

	return tq;
}


/**************************************************************************
 * qla4xxx_lookup_ddb_by_fw_index
 *	This routine locates a device handle given the firmware device
 *	database index.  If device doesn't exist, returns NULL.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *      fw_ddb_index - Firmware's device database index
 *
 * Returns:
 *	Pointer to the corresponding internal device database structure
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static inline ddb_entry_t *
qla4xxx_lookup_ddb_by_fw_index(scsi_qla_host_t *ha, uint32_t fw_ddb_index)
{
	ddb_entry_t *ddb_entry = NULL;

	if ((fw_ddb_index < MAX_DDB_ENTRIES) &&
	    (ha->fw_ddb_index_map[fw_ddb_index] !=
		(ddb_entry_t *) INVALID_ENTRY)) {
		ddb_entry = ha->fw_ddb_index_map[fw_ddb_index];
	}

	QL4PRINT(QLP3, printk("scsi%d: %s: index [%d], ddb_entry = %p\n",
	    ha->host_no, __func__, fw_ddb_index, ddb_entry));

	return ddb_entry;
}

/**************************************************************************
 * qla4xxx_mark_device_missing
 *	This routine marks a device missing and resets the relogin retry count.
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
static inline void
qla4xxx_mark_device_missing(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry)
{
	atomic_set(&ddb_entry->state, DEV_STATE_MISSING);
	if (ddb_entry->fcport != NULL)
		atomic_set(&ddb_entry->fcport->state, FCS_DEVICE_LOST);

	QL4PRINT(QLP3, printk(KERN_INFO "scsi%d:%d:%d: index [%d] marked "
	    "MISSING\n", ha->host_no, ddb_entry->bus, ddb_entry->target,
	    ddb_entry->fw_ddb_index));
}

/**************************************************************************
 * qla4xxx_enable_intrs
 *	This routine enables the PCI interrupt request by clearing the
 *	appropriate bit.
 *
 * qla4xxx_disable_intrs
 *	This routine disables the PCI interrupt request by setting the
 *	appropriate bit.
 *
 * Remarks:
 *	The hardware_lock must be unlocked upon entry.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	None
 *
 * Context:
 *	Kernel/Interrupt context.
 **************************************************************************/
static inline void __qla4xxx_enable_intrs(scsi_qla_host_t *ha)
{
	ENTER("qla4xxx_enable_intrs");
	set_bit(AF_INTERRUPTS_ON, &ha->flags);

	WRT_REG_DWORD(&ha->reg->ctrl_status, SET_RMASK(CSR_SCSI_INTR_ENABLE));
	PCI_POSTING(&ha->reg->ctrl_status);
	QL4PRINT(QLP7, printk("scsi%d: %s: intSET_RMASK = %08x\n",
			      ha->host_no, __func__,
			      RD_REG_DWORD(&ha->reg->ctrl_status)));
	LEAVE("qla4xxx_enable_intrs");
}

static inline void __qla4xxx_disable_intrs(scsi_qla_host_t *ha)
{

	ENTER("qla4xxx_disable_intrs");
	clear_bit(AF_INTERRUPTS_ON, &ha->flags);
	
	WRT_REG_DWORD(&ha->reg->ctrl_status, CLR_RMASK(CSR_SCSI_INTR_ENABLE));
	PCI_POSTING(&ha->reg->ctrl_status);
	QL4PRINT(QLP7, printk("scsi%d: %s: intSET_RMASK = %08x\n",
			      ha->host_no, __func__,
			      RD_REG_DWORD(&ha->reg->ctrl_status)));
	LEAVE("qla4xxx_disable_intrs");
}
static inline void qla4xxx_enable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__qla4xxx_enable_intrs(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static inline void qla4xxx_disable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__qla4xxx_disable_intrs(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static __inline__ void
qla4xxx_suspend_lun(scsi_qla_host_t *, srb_t *sp, os_lun_t *, int, int);
static __inline__ void
qla4xxx_delay_lun(scsi_qla_host_t *, os_lun_t *, int);

static __inline__ void
qla4xxx_suspend_lun(scsi_qla_host_t *ha, srb_t *sp, os_lun_t *lq, int time, int count)
{
	return (__qla4xxx_suspend_lun(ha, sp, lq, time, count, 0));
}

static __inline__ void
qla4xxx_delay_lun(scsi_qla_host_t *ha, os_lun_t *lq, int time)
{
	return (__qla4xxx_suspend_lun(ha, NULL, lq, time, 1, 1));
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

