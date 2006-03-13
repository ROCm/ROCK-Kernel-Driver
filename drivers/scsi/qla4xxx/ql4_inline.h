/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

/*
 *
 * qla4xxx_lookup_ddb_by_fw_index
 *      This routine locates a device handle given the firmware device
 *      database index.  If device doesn't exist, returns NULL.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      fw_ddb_index - Firmware's device database index
 *
 * Returns:
 *      Pointer to the corresponding internal device database structure
 */
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

static inline void
__qla4xxx_enable_intrs(scsi_qla_host_t *ha)
{
	if (IS_QLA4022(ha)) {
		WRT_REG_DWORD(&ha->reg->u1.isp4022.intr_mask,
		    SET_RMASK(IMR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->u1.isp4022.intr_mask);
	} else {
		WRT_REG_DWORD(&ha->reg->ctrl_status,
		    SET_RMASK(CSR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->ctrl_status);
	}
	set_bit(AF_INTERRUPTS_ON, &ha->flags);
}

static inline void
__qla4xxx_disable_intrs(scsi_qla_host_t *ha)
{
	if (IS_QLA4022(ha)) {
		WRT_REG_DWORD(&ha->reg->u1.isp4022.intr_mask,
		    CLR_RMASK(IMR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->u1.isp4022.intr_mask);
	} else {
		WRT_REG_DWORD(&ha->reg->ctrl_status,
		    CLR_RMASK(CSR_SCSI_INTR_ENABLE));
		PCI_POSTING(&ha->reg->ctrl_status);
	}
	clear_bit(AF_INTERRUPTS_ON, &ha->flags);
}

static inline void
qla4xxx_enable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__qla4xxx_enable_intrs(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static inline void
qla4xxx_disable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__qla4xxx_disable_intrs(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}
