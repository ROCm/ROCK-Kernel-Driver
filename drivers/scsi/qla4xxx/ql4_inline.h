/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

/*
 *
 * qla4xxx_lookup_ddb_by_fw_index
 *	This routine locates a device handle given the firmware device
 *	database index.	 If device doesn't exist, returns NULL.
 *
 * Input:
 *	ha - Pointer to host adapter structure.
 *	fw_ddb_index - Firmware's device database index
 *
 * Returns:
 *	Pointer to the corresponding internal device database structure
 */
static inline struct ddb_entry *
qla4xxx_lookup_ddb_by_fw_index(struct scsi_qla_host *ha, uint32_t fw_ddb_index)
{
	struct ddb_entry *ddb_entry = NULL;

	if ((fw_ddb_index < MAX_DDB_ENTRIES) &&
	    (ha->fw_ddb_index_map[fw_ddb_index] !=
		(struct ddb_entry *) INVALID_ENTRY)) {
		ddb_entry = ha->fw_ddb_index_map[fw_ddb_index];
	}

	DEBUG3(printk("scsi%d: %s: index [%d], ddb_entry = %p\n",
	    ha->host_no, __func__, fw_ddb_index, ddb_entry));

	return ddb_entry;
}

/*
 * The MBOX_CMD_CLEAR_DATABASE_ENTRY (0x31) mailbox command does not
 * result in an AEN, so we need to process it seperately.
 */
static inline void qla4xxx_check_for_clear_ddb(struct scsi_qla_host *ha,
						uint32_t *mbox_cmd)
{
	uint32_t fw_ddb_index;
	struct ddb_entry *ddb_entry = NULL;

	if (mbox_cmd[0] == MBOX_CMD_CLEAR_DATABASE_ENTRY) {

		fw_ddb_index = mbox_cmd[1];

		if (fw_ddb_index < MAX_DDB_ENTRIES)
			ddb_entry = ha->fw_ddb_index_map[fw_ddb_index];

		if (ddb_entry) {
			dev_info(&ha->pdev->dev, "%s: ddb[%d] os[%d] freed\n",
				__func__, ddb_entry->fw_ddb_index,
				ddb_entry->os_target_id);
			set_bit(DF_REMOVE, &ddb_entry->flags);
			set_bit(DPC_REMOVE_DEVICE, &ha->dpc_flags);
			queue_work(ha->dpc_thread, &ha->dpc_work);
		}
	}
}

static inline void
__qla4xxx_enable_intrs(struct scsi_qla_host *ha)
{
	if (is_qla4022(ha) | is_qla4032(ha)) {
		writel(set_rmask(IMR_SCSI_INTR_ENABLE),
		       &ha->reg->u1.isp4022.intr_mask);
		readl(&ha->reg->u1.isp4022.intr_mask);
	} else {
		writel(set_rmask(CSR_SCSI_INTR_ENABLE), &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
	}
	set_bit(AF_INTERRUPTS_ON, &ha->flags);
}

static inline void
__qla4xxx_disable_intrs(struct scsi_qla_host *ha)
{
	if (is_qla4022(ha) | is_qla4032(ha)) {
		writel(clr_rmask(IMR_SCSI_INTR_ENABLE),
		       &ha->reg->u1.isp4022.intr_mask);
		readl(&ha->reg->u1.isp4022.intr_mask);
	} else {
		writel(clr_rmask(CSR_SCSI_INTR_ENABLE), &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
	}
	clear_bit(AF_INTERRUPTS_ON, &ha->flags);
}

static inline void
qla4xxx_enable_intrs(struct scsi_qla_host *ha)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__qla4xxx_enable_intrs(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static inline void
qla4xxx_disable_intrs(struct scsi_qla_host *ha)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__qla4xxx_disable_intrs(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static inline void
qla4xxx_remove_device(struct scsi_qla_host *ha)
{
	struct ddb_entry *ddb_entry, *dtemp;

	if (test_and_clear_bit(DPC_REMOVE_DEVICE, &ha->dpc_flags)) {
		list_for_each_entry_safe(ddb_entry, dtemp,
			&ha->ddb_list, list) {
			if (test_and_clear_bit(DF_REMOVE, &ddb_entry->flags)) {
				dev_info(&ha->pdev->dev,
					"%s: ddb[%d] os[%d] - removed\n",
					__func__, ddb_entry->fw_ddb_index,
					ddb_entry->os_target_id);
				qla4xxx_free_ddb(ha, ddb_entry);
			}
		}
	}
}

static void
ql4_get_aen_log(struct scsi_qla_host *ha, struct ql4_aen_log *aenl)
{
        if (aenl) {
                memcpy(aenl, &ha->aen_log, sizeof (ha->aen_log));
                ha->aen_log.count = 0;
        }
}

static inline int
qla4xxx_ioctl_init(struct scsi_qla_host *ha)
{
        ha->ql4mbx = qla4xxx_mailbox_command;
        ha->ql4cmd = qla4xxx_send_command_to_isp;
        ha->ql4getaenlog = ql4_get_aen_log;
        return 0;
}

static inline void
qla4xxx_ioctl_exit(struct scsi_qla_host *ha)
{
        return;
}
