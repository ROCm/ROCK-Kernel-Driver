/******************************************************************************
 *     Copyright (C)  2003 -2005 QLogic Corporation
 * QLogic ISP4xxx Device Driver
 *
 * This program includes a device driver for Linux 2.6.x that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software Foundation
 * (version 2 or a later version) and/or under the following terms,
 * as applicable:
 *
 * 	1. Redistribution of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in
 *         the documentation and/or other materials provided with the
 *         distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 * 	
 * You may redistribute the hardware specific firmware binary file under
 * the following terms:
 * 	1. Redistribution of source code (only if applicable), must
 *         retain the above copyright notice, this list of conditions and
 *         the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT CREATE
 * OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR OTHERWISE
 * IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT, TRADE SECRET,
 * MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN ANY OTHER QLOGIC
 * HARDWARE OR SOFTWARE EITHER SOLELY OR IN COMBINATION WITH THIS PROGRAM
 *
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

	DEBUG3(printk("scsi%d: %s: index [%d], ddb_entry = %p\n",
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

	QL4PRINT(QLP2, printk("scsi%d:%d:%d: index [%d] marked "
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

	if( IS_QLA4022(ha) ) {
		WRT_REG_DWORD(&ha->reg->u1.isp4022.intr_mask, SET_RMASK(IMR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->u1.isp4022.intr_mask);
	} else {
		WRT_REG_DWORD(&ha->reg->ctrl_status, SET_RMASK(CSR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->ctrl_status);
		QL4PRINT(QLP7, printk("scsi%d: %s: intSET_RMASK = %08x\n",
			      ha->host_no, __func__,
			      RD_REG_DWORD(&ha->reg->ctrl_status)));
	}
	LEAVE("qla4xxx_enable_intrs");
}

static inline void __qla4xxx_disable_intrs(scsi_qla_host_t *ha)
{

	ENTER("qla4xxx_disable_intrs");
	clear_bit(AF_INTERRUPTS_ON, &ha->flags);
	
	if( IS_QLA4022(ha) ) {
		WRT_REG_DWORD(&ha->reg->u1.isp4022.intr_mask, CLR_RMASK(IMR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->u1.isp4022.intr_mask);
		QL4PRINT(QLP7, printk("scsi%d: %s: intr_mask = %08x\n",
			      ha->host_no, __func__,
			      RD_REG_DWORD(&ha->reg->u1.isp4022.intr_mask)));
	} else {
		WRT_REG_DWORD(&ha->reg->ctrl_status, CLR_RMASK(CSR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->ctrl_status);
		QL4PRINT(QLP7, printk("scsi%d: %s: intSET_RMASK = %08x\n",
			      ha->host_no, __func__,
			      RD_REG_DWORD(&ha->reg->ctrl_status)));
	}
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

static inline int
qla4xxx_is_eh_active(struct Scsi_Host *shost)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
 if (shost->eh_active)
 return 1;
#else
 if (shost->shost_state == SHOST_RECOVERY)
 return 1;
#endif
 return 0;
};

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

