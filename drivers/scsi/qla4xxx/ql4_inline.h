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
	    (ha->fw_ddb_index_map[fw_ddb_index] != NULL)) {
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
			set_bit(DF_DELETED, &ddb_entry->flags);
			set_bit(DPC_DELETE_DEVICE, &ha->dpc_flags);
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

static inline int qla4xxx_get_req_pkt(struct scsi_qla_host *ha,
			struct queue_entry **queue_entry)
{
	uint16_t request_in;
	uint8_t status = QLA_SUCCESS;

	*queue_entry = ha->request_ptr;

	/* get the latest request_in and request_out index */
	request_in = ha->request_in;
	ha->request_out = (uint16_t) le32_to_cpu(ha->shadow_regs->req_q_out);

	/* Advance request queue pointer and check for queue full */
	if (request_in == (REQUEST_QUEUE_DEPTH - 1)) {
		request_in = 0;
		ha->request_ptr = ha->request_ring;
	} else {
		request_in++;
		ha->request_ptr++;
	}

	/* request queue is full, try again later */
	if ((ha->iocb_cnt + 1) >= ha->iocb_hiwat) {
		/* restore request pointer */
		ha->request_ptr = *queue_entry;
		status = QLA_ERROR;
	} else {
		ha->request_in = request_in;
		memset(*queue_entry, 0, sizeof(**queue_entry));
	}

	return status;
}

/**
 * qla4xxx_send_marker_iocb - issues marker iocb to HBA
 * @ha: Pointer to host adapter structure.
 * @ddb_entry: Pointer to device database entry
 * @lun: SCSI LUN
 * @marker_type: marker identifier
 *
 * This routine issues a marker IOCB.
 **/
static inline int qla4xxx_send_marker_iocb(struct scsi_qla_host *ha,
			     struct ddb_entry *ddb_entry, int lun)
{
	struct marker_entry *marker_entry;
	unsigned long flags = 0;
	uint8_t status = QLA_SUCCESS;

	/* Acquire hardware specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Get pointer to the queue entry for the marker */
	if (qla4xxx_get_req_pkt(ha, (struct queue_entry **) &marker_entry) !=
	    QLA_SUCCESS) {
		status = QLA_ERROR;
		goto exit_send_marker;
	}

	/* Put the marker in the request queue */
	marker_entry->hdr.entryType = ET_MARKER;
	marker_entry->hdr.entryCount = 1;
	marker_entry->target = cpu_to_le16(ddb_entry->fw_ddb_index);
	marker_entry->modifier = cpu_to_le16(MM_LUN_RESET);
	int_to_scsilun(lun, &marker_entry->lun);
	wmb();

	/* Tell ISP it's got a new I/O request */
	writel(ha->request_in, &ha->reg->req_q_in);
	readl(&ha->reg->req_q_in);

exit_send_marker:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return status;
}

static inline struct continuation_t1_entry* qla4xxx_alloc_cont_entry(
	struct scsi_qla_host *ha)
{
	struct continuation_t1_entry *cont_entry;

	cont_entry = (struct continuation_t1_entry *)ha->request_ptr;

	/* Advance request queue pointer */
	if (ha->request_in == (REQUEST_QUEUE_DEPTH - 1)) {
		ha->request_in = 0;
		ha->request_ptr = ha->request_ring;
	} else {
		ha->request_in++;
		ha->request_ptr++;
	}

	/* Load packet defaults */
	cont_entry->hdr.entryType = ET_CONTINUE;
	cont_entry->hdr.entryCount = 1;
	cont_entry->hdr.systemDefined = (uint8_t) cpu_to_le16(ha->request_in);

	return cont_entry;
}

static inline uint16_t qla4xxx_calc_request_entries(uint16_t dsds)
{
	uint16_t iocbs;

	iocbs = 1;
	if (dsds > COMMAND_SEG) {
		iocbs += (dsds - COMMAND_SEG) / CONTINUE_SEG;
		if ((dsds - COMMAND_SEG) % CONTINUE_SEG)
			iocbs++;
	}
	return iocbs;
}

