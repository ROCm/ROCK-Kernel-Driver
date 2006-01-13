/*
 * Copyright (c)  2003-2005 QLogic Corporation
 * QLogic Linux iSCSI Driver
 *
 * This program includes a device driver for Linux 2.6 that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software
 * Foundation (version 2 or a later version) and/or under the
 * following terms, as applicable:
 *
 * 	1. Redistribution of source code must retain the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer.
 *
 * 	2. Redistribution in binary form must reproduce the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer in the documentation and/or other
 * 	   materials provided with the distribution.
 *
 * 	3. The name of QLogic Corporation may not be used to
 * 	   endorse or promote products derived from this software
 * 	   without specific prior written permission.
 *
 * You may redistribute the hardware specific firmware binary file
 * under the following terms:
 *
 * 	1. Redistribution of source code (only if applicable),
 * 	   must retain the above copyright notice, this list of
 * 	   conditions and the following disclaimer.
 *
 * 	2. Redistribution in binary form must reproduce the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer in the documentation and/or other
 * 	   materials provided with the distribution.
 *
 * 	3. The name of QLogic Corporation may not be used to
 * 	   endorse or promote products derived from this software
 * 	   without specific prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT
 * CREATE OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR
 * OTHERWISE IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT,
 * TRADE SECRET, MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN
 * ANY OTHER QLOGIC HARDWARE OR SOFTWARE EITHER SOLELY OR IN
 * COMBINATION WITH THIS PROGRAM.
 */

#include "ql4_def.h"

#include <scsi/scsi_tcq.h>

/**************************************************************************
 * qla4xxx_get_req_pkt
 *	This routine performs the following tasks:
 *	- returns the current request_in pointer (if queue not full)
 *	- advances the request_in pointer
 *	- checks for queue full
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	queue_entry - Pointer to pointer to queue entry structure
 *
 * Output:
 *	queue_entry - Return pointer to next available request packet
 *
 * Returns:
 *	QLA_SUCCESS - Successfully retrieved request packet
 *	QLA_ERROR   - Failed to retrieve request packet
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_get_req_pkt(scsi_qla_host_t *ha, QUEUE_ENTRY **queue_entry)
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

/**************************************************************************
 * qla4xxx_send_marker_iocb
 *	This routine issues a marker IOCB.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	ddb_entry - Pointer to device database entry
 *	lun - SCSI LUN
 *	marker_type - marker identifier
 *
 * Returns:
 *	QLA_SUCCESS - Successfully sent marker IOCB
 *	QLA_ERROR   - Failed to send marker IOCB
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_send_marker_iocb(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry, int lun)
{
	MARKER_ENTRY *marker_entry;
	unsigned long flags = 0;
	uint8_t status = QLA_SUCCESS;

	/* Acquire hardware specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Get pointer to the queue entry for the marker */
	if (qla4xxx_get_req_pkt(ha, (QUEUE_ENTRY **) &marker_entry) !=
	    QLA_SUCCESS) {
		status = QLA_ERROR;
		goto exit_send_marker;
	}

	/* Put the marker in the request queue */
	marker_entry->hdr.entryType = ET_MARKER;
	marker_entry->hdr.entryCount = 1;
	marker_entry->target = cpu_to_le16(ddb_entry->fw_ddb_index);
	marker_entry->modifier = cpu_to_le16(MM_LUN_RESET);
	marker_entry->lun[1] = LSB(lun);	/*SAMII compliant lun */
	marker_entry->lun[2] = MSB(lun);
	wmb();

	/* Tell ISP it's got a new I/O request */
	WRT_REG_DWORD(&ha->reg->req_q_in, ha->request_in);
	PCI_POSTING(&ha->reg->req_q_in);

exit_send_marker:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return status;
}

PDU_ENTRY *
qla4xxx_get_pdu(scsi_qla_host_t * ha, uint32_t length)
{
	PDU_ENTRY *pdu;
	PDU_ENTRY *free_pdu_top;
	PDU_ENTRY *free_pdu_bottom;
	uint16_t pdu_active;

	if (ha->free_pdu_top == NULL)
		return NULL;

	/* Save current state */
	free_pdu_top = ha->free_pdu_top;
	free_pdu_bottom = ha->free_pdu_bottom;
	pdu_active = ha->pdu_active + 1;

	/* get next available pdu */
	pdu = free_pdu_top;
	free_pdu_top = pdu->Next;
	if (free_pdu_top == NULL)
		free_pdu_bottom = NULL;

	/* round up to nearest page */
	length = (length + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);

	/* Allocate pdu buffer PDU */
	pdu->Buff = dma_alloc_coherent(&ha->pdev->dev, length, &pdu->DmaBuff,
	    GFP_KERNEL);
	if (pdu->Buff == NULL)
		return NULL;

	memset(pdu->Buff, 0, length);

	/* Fill in remainder of PDU */
	pdu->BuffLen = length;
	pdu->SendBuffLen = 0;
	pdu->RecvBuffLen = 0;
	pdu->Next = NULL;
	ha->free_pdu_top = free_pdu_top;
	ha->free_pdu_bottom = free_pdu_bottom;
	ha->pdu_active = pdu_active;
	return pdu;
}

void
qla4xxx_free_pdu(scsi_qla_host_t * ha, PDU_ENTRY * pdu)
{
	if (ha->free_pdu_bottom == NULL) {
		ha->free_pdu_top = pdu;
		ha->free_pdu_bottom = pdu;
	} else {
		ha->free_pdu_bottom->Next = pdu;
		ha->free_pdu_bottom = pdu;
	}
	dma_free_coherent(&ha->pdev->dev, pdu->BuffLen, pdu->Buff,
	    pdu->DmaBuff);
	ha->pdu_active--;

	/* Clear PDU */
	pdu->Buff = NULL;
	pdu->BuffLen = 0;
	pdu->SendBuffLen = 0;
	pdu->RecvBuffLen = 0;
	pdu->Next = NULL;
	pdu->DmaBuff = 0;
}

/**************************************************************************
 * qla4xxx_send_passthru0_iocb
 *	This routine issues a passthru0 IOCB.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Remarks: hardware_lock acquired upon entry, interrupt context
 *
 * Returns:
 *	QLA_SUCCESS - Successfully sent marker IOCB
 *	QLA_ERROR   - Failed to send marker IOCB
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_send_passthru0_iocb(scsi_qla_host_t * ha, uint16_t fw_ddb_index,
    uint16_t connection_id, dma_addr_t pdu_dma_data, uint32_t send_len,
    uint32_t recv_len, uint16_t control_flags, uint32_t handle)
{
	PASSTHRU0_ENTRY *passthru_entry;
	uint8_t status = QLA_SUCCESS;

	/* Get pointer to the queue entry for the marker */
	if (qla4xxx_get_req_pkt(ha, (QUEUE_ENTRY **) &passthru_entry) !=
	    QLA_SUCCESS) {
		status = QLA_ERROR;
		goto exit_send_pt0;
	}

	/* Fill in the request queue */
	passthru_entry->hdr.entryType = ET_PASSTHRU0;
	passthru_entry->hdr.entryCount = 1;
	passthru_entry->handle = cpu_to_le32(handle);
	passthru_entry->target = cpu_to_le16(fw_ddb_index);
	passthru_entry->connectionID = cpu_to_le16(connection_id);
	passthru_entry->timeout = __constant_cpu_to_le16(PT_DEFAULT_TIMEOUT);
	if (send_len) {
		control_flags |= PT_FLAG_SEND_BUFFER;
		passthru_entry->outDataSeg64.base.addrHigh =
		    cpu_to_le32(MSDW(pdu_dma_data));
		passthru_entry->outDataSeg64.base.addrLow =
		    cpu_to_le32(LSDW(pdu_dma_data));
		passthru_entry->outDataSeg64.count = cpu_to_le32(send_len);
	}
	if (recv_len) {
		passthru_entry->inDataSeg64.base.addrHigh =
		    cpu_to_le32(MSDW(pdu_dma_data));
		passthru_entry->inDataSeg64.base.addrLow =
		    cpu_to_le32(LSDW(pdu_dma_data));
		passthru_entry->inDataSeg64.count = cpu_to_le32(recv_len);
	}
	passthru_entry->controlFlags = cpu_to_le16(control_flags);
	wmb();

	/* Tell ISP it's got a new I/O request */
	WRT_REG_DWORD(&ha->reg->req_q_in, ha->request_in);
	PCI_POSTING(&ha->reg->req_q_in);

exit_send_pt0:
	return status;
}

CONTINUE_ENTRY *
qla4xxx_alloc_cont_entry(scsi_qla_host_t * ha)
{
	CONTINUE_ENTRY *cont_entry;

	cont_entry = (CONTINUE_ENTRY *)ha->request_ptr;

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

uint16_t
qla4xxx_calc_request_entries(uint16_t dsds)
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

void
qla4xxx_build_scsi_iocbs(srb_t *srb, COMMAND_ENTRY *cmd_entry,
    uint16_t tot_dsds)
{
	scsi_qla_host_t *ha;
	uint16_t avail_dsds;
	DATA_SEG_A64 *cur_dsd;
	struct scsi_cmnd *cmd;

	cmd = srb->cmd;
	ha = srb->ha;

	if (cmd->request_bufflen == 0 || cmd->sc_data_direction == DMA_NONE) {
		/* No data being transferred */
		cmd_entry->ttlByteCnt = __constant_cpu_to_le32(0);
		return;
	}

	avail_dsds = COMMAND_SEG;
	cur_dsd = (DATA_SEG_A64 *) & (cmd_entry->dataseg[0]);

	/* Load data segments */
	if (cmd->use_sg) {
		struct scatterlist *cur_seg;
		struct scatterlist *end_seg;

		cur_seg = (struct scatterlist *)cmd->request_buffer;
		end_seg = cur_seg + tot_dsds;
		while (cur_seg < end_seg) {
			dma_addr_t sle_dma;

			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				CONTINUE_ENTRY *cont_entry;

				cont_entry = qla4xxx_alloc_cont_entry(ha);
				cur_dsd =
				    (DATA_SEG_A64 *) &cont_entry->dataseg[0];
				avail_dsds = CONTINUE_SEG;
			}

			sle_dma = sg_dma_address(cur_seg);
			cur_dsd->base.addrLow = cpu_to_le32(LSDW(sle_dma));
			cur_dsd->base.addrHigh = cpu_to_le32(MSDW(sle_dma));
			cur_dsd->count = cpu_to_le32(sg_dma_len(cur_seg));
			avail_dsds--;

			cur_dsd++;
			cur_seg++;
		}
	} else {
		cur_dsd->base.addrLow = cpu_to_le32(LSDW(srb->dma_handle));
		cur_dsd->base.addrHigh = cpu_to_le32(MSDW(srb->dma_handle));
		cur_dsd->count = cpu_to_le32(cmd->request_bufflen);
	}
}

/**************************************************************************
 * qla4xxx_send_command_to_isp
 *      This routine is called by qla4xxx_queuecommand to build an ISP
 *      command and pass it to the ISP for execution.
 *
 * Input:
 *      ha - Pointer to host adapter structure.
 *      srb - pointer to SCSI Request Block to be sent to ISP
 *
 * Output:
 *      None
 *
 * Remarks:
 *      None
 *
 * Returns:
 *      QLA_SUCCESS - Successfully sent command to ISP
 *      QLA_ERROR   - Failed to send command to ISP
 *
 * Context:
 *      Kernel context.
 **************************************************************************/
int
qla4xxx_send_command_to_isp(scsi_qla_host_t *ha, srb_t * srb)
{
	struct scsi_cmnd *cmd = srb->cmd;
	ddb_entry_t *ddb_entry;
	COMMAND_ENTRY *cmd_entry;
	struct scatterlist *sg;

	uint16_t tot_dsds;
	uint16_t req_cnt;

	unsigned long flags;
	uint16_t cnt;
	uint16_t i;
	uint32_t index;
	char tag[2];

	/* Get real lun and adapter */
	ddb_entry = srb->ddb;

	/* Send marker(s) if needed. */
	if (ha->marker_needed == 1) {
		if (qla4xxx_send_marker_iocb(ha, ddb_entry, cmd->device->lun) !=
		    QLA_SUCCESS) {
			return QLA_ERROR;
		}
		ha->marker_needed = 0;
	}
	tot_dsds = 0;

	/* Acquire hardware specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Check for room in active srb array */
	index = ha->current_active_index;
	for (i = 0; i < MAX_SRBS; i++) {
		index++;
		if (index == MAX_SRBS)
			index = 1;
		if (ha->active_srb_array[index] == 0) {
			ha->current_active_index = index;
			break;
		}
	}
	if (i >= MAX_SRBS) {
		printk(KERN_INFO "scsi%ld: %s: NO more SRB entries used "
		    "iocbs=%d, \n reqs remaining=%d\n", ha->host_no, __func__,
		    ha->iocb_cnt, ha->req_q_count);
		goto queuing_error;
	}

	/* Calculate the number of request entries needed. */
	if (cmd->use_sg) {
		sg = (struct scatterlist *)cmd->request_buffer;
		tot_dsds = pci_map_sg(ha->pdev, sg, cmd->use_sg,
				      cmd->sc_data_direction);
		if (tot_dsds == 0)
			goto queuing_error;
	} else if (cmd->request_bufflen) {
		dma_addr_t      req_dma;

		req_dma = pci_map_single(ha->pdev, cmd->request_buffer,
		    cmd->request_bufflen, cmd->sc_data_direction);
		if (dma_mapping_error(req_dma))
			goto queuing_error;

		srb->dma_handle = req_dma;
		tot_dsds = 1;
	}
	req_cnt = qla4xxx_calc_request_entries(tot_dsds);

	if (ha->req_q_count < (req_cnt + 2)) {
		cnt = (uint16_t) le32_to_cpu(ha->shadow_regs->req_q_out);
		if (ha->request_in < cnt)
			ha->req_q_count = cnt - ha->request_in;
		else
			ha->req_q_count = REQUEST_QUEUE_DEPTH -
			    (ha->request_in - cnt);
	}

	if (ha->req_q_count < (req_cnt + 2))
		goto queuing_error;

	/* total iocbs active */
	if ((ha->iocb_cnt + req_cnt) >= REQUEST_QUEUE_DEPTH)
		goto queuing_error;

	/* Build command packet */
	cmd_entry = (COMMAND_ENTRY *) ha->request_ptr;
	cmd_entry->hdr.entryType = ET_COMMAND;
	cmd_entry->handle = cpu_to_le32(index);
	cmd_entry->target = cpu_to_le16(ddb_entry->fw_ddb_index);
	cmd_entry->connection_id = cpu_to_le16(ddb_entry->connection_id);

	cmd_entry->lun[1] = LSB(cmd->device->lun);	/* SAMII compliant. */
	cmd_entry->lun[2] = MSB(cmd->device->lun);
	cmd_entry->cmdSeqNum = cpu_to_le32(ddb_entry->CmdSn);
	cmd_entry->ttlByteCnt = cpu_to_le32(cmd->request_bufflen);
	memcpy(cmd_entry->cdb, cmd->cmnd, min((unsigned char)MAX_COMMAND_SIZE,
	    cmd->cmd_len));
	cmd_entry->dataSegCnt = cpu_to_le16(tot_dsds);
	cmd_entry->hdr.entryCount = req_cnt;

	/* Set data transfer direction control flags
	 * NOTE: Look at data_direction bits iff there is data to be
	 *       transferred, as the data direction bit is sometimed filled
	 *       in when there is no data to be transferred */
	cmd_entry->control_flags = CF_NO_DATA;
	if (cmd->request_bufflen) {
		if (cmd->sc_data_direction == DMA_TO_DEVICE)
			cmd_entry->control_flags = CF_WRITE;
		else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
			cmd_entry->control_flags = CF_READ;
	}

	/* Set tagged queueing control flags */
	cmd_entry->control_flags |= CF_SIMPLE_TAG;
	if (scsi_populate_tag_msg(cmd, tag)) {
		switch (tag[0]) {
		case MSG_HEAD_TAG:
			cmd_entry->control_flags |= CF_HEAD_TAG;
			break;
		case MSG_ORDERED_TAG:
			cmd_entry->control_flags |= CF_ORDERED_TAG;
			break;
		}
	}

	/* Advance request queue pointer */
	ha->request_in++;
	if (ha->request_in == REQUEST_QUEUE_DEPTH) {
		ha->request_in = 0;
		ha->request_ptr = ha->request_ring;
	} else {
		ha->request_ptr++;
	}

	qla4xxx_build_scsi_iocbs(srb, cmd_entry, tot_dsds);
	wmb();

	/* put command in active array */
	ha->active_srb_array[index] = srb;
	srb->cmd->host_scribble = (unsigned char *)(unsigned long)index;
	//srb->active_array_index = index;

	/* update counters */
	ha->active_srb_count++;
	srb->state = SRB_ACTIVE_STATE;
	srb->flags |= SRB_DMA_VALID;

	/* Track IOCB used */
	ha->iocb_cnt += req_cnt;
	srb->iocb_cnt = req_cnt;
	ha->req_q_count -= req_cnt;

	/* Debug print statements */
	WRT_REG_DWORD(&ha->reg->req_q_in, ha->request_in);
	PCI_POSTING(&ha->reg->req_q_in);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_SUCCESS;

queuing_error:
	if (cmd->use_sg && tot_dsds) {
		sg = (struct scatterlist *) cmd->request_buffer;
		pci_unmap_sg(ha->pdev, sg, cmd->use_sg, cmd->sc_data_direction);
	} else if (tot_dsds) {
		pci_unmap_single(ha->pdev, srb->dma_handle,
		    cmd->request_bufflen, cmd->sc_data_direction);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_ERROR;
}
