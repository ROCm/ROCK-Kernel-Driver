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
 *      qla4xxx_get_req_pkt
 *      qla4xxx_send_marker_iocb
 *	qla4xxx_get_pdu
 *	qla4xxx_free_pdu
 *	qla4xxx_send_passthru0_iocb
 ****************************************************************************/

#include "ql4_def.h"

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
uint8_t
qla4xxx_get_req_pkt(scsi_qla_host_t *ha, QUEUE_ENTRY **queue_entry)
{
	uint16_t  request_in;
	uint8_t   status = QLA_SUCCESS;

	ENTER("qla4xxx_get_req_pkt");

	*queue_entry = ha->request_ptr;

	/* get the latest request_in and request_out index */
	request_in = ha->request_in;
	ha->request_out =
	    (uint16_t) le32_to_cpu(ha->shadow_regs->req_q_out);

	/* Advance request queue pointer and check for queue full */
	if (request_in == (REQUEST_QUEUE_DEPTH - 1)) {
		request_in = 0;
		ha->request_ptr = ha->request_ring;
		QL4PRINT(QLP10, printk("scsi%d: %s: wraparound -- new "
		    "request_in = %04x, new request_ptr = %p\n", ha->host_no,
		    __func__, request_in, ha->request_ptr));
	} else {
		request_in++;
		ha->request_ptr++;
		QL4PRINT(QLP10, printk("scsi%d: %s: new request_in = %04x, new "
		    "request_ptr = %p\n", ha->host_no, __func__, request_in,
		    ha->request_ptr));
	}

	/* request queue is full, try again later */
	if ((ha->iocb_cnt + 1) >= ha->iocb_hiwat) {
		QL4PRINT(QLP2, printk("scsi%d: %s: request queue is full, "
		    "iocb_cnt=%d, iocb_hiwat=%d\n", ha->host_no, __func__,
		    ha->iocb_cnt, ha->iocb_hiwat));

		/* restore request pointer */
		ha->request_ptr = *queue_entry;
		QL4PRINT(QLP2, printk("scsi%d: %s: restore request_ptr = %p, "
		    "request_in = %04x, request_out = %04x\n", ha->host_no,
		    __func__, ha->request_ptr, ha->request_in,
		    ha->request_out));
		status = QLA_ERROR;
	} else {
		ha->request_in = request_in;
		memset(*queue_entry, 0, sizeof(**queue_entry));
	}

	LEAVE("qla4xxx_get_req_pkt");

	return (status);
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
uint8_t
qla4xxx_send_marker_iocb(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry,
    fc_lun_t *lun_entry)
{
	MARKER_ENTRY *marker_entry;
	unsigned long flags = 0;
	uint8_t       status = QLA_SUCCESS;

	ENTER("qla4xxx_send_marker_iocb");

	/* Acquire hardware specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Get pointer to the queue entry for the marker */
	if (qla4xxx_get_req_pkt(ha, (QUEUE_ENTRY **) &marker_entry)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s: request queue full, try "
		    "again later\n", ha->host_no, __func__));

		status = QLA_ERROR;
		goto exit_send_marker;
	}

	/* Put the marker in the request queue */
	marker_entry->hdr.entryType = ET_MARKER;
	marker_entry->hdr.entryCount = 1;
	marker_entry->target = cpu_to_le16(ddb_entry->fw_ddb_index);
	marker_entry->modifier = cpu_to_le16(MM_LUN_RESET);
	marker_entry->lun[1] = LSB(lun_entry->lun);	 /*SAMII compliant lun*/
	marker_entry->lun[2] = MSB(lun_entry->lun);
	wmb();

	QL4PRINT(QLP3, printk(KERN_INFO
	    "scsi%d:%d:%d:%d: LUN_RESET Marker sent\n", ha->host_no,
	    ddb_entry->bus, ddb_entry->target, lun_entry->lun));

	/* Tell ISP it's got a new I/O request */
	WRT_REG_DWORD(&ha->reg->req_q_in, ha->request_in);
	PCI_POSTING(&ha->reg->req_q_in);

exit_send_marker:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	LEAVE("qla4xxx_send_marker_iocb");

	return (status);
}

PDU_ENTRY *
qla4xxx_get_pdu(scsi_qla_host_t *ha, uint32_t length)
{
	PDU_ENTRY       *pdu;
	PDU_ENTRY       *free_pdu_top;
	PDU_ENTRY       *free_pdu_bottom;
	uint16_t        pdu_active;

	uint16_t        i, j;
	uint16_t        num_pages;
	uint16_t        first_page;
	uint16_t        free_pages;
	uint8_t         pages_available;

	if (ha->free_pdu_top == NULL) {
		QL4PRINT(QLP2|QLP19,
			 printk("scsi%d: %s: Out of PDUs!\n",
				ha->host_no, __func__));
		return(NULL);
	}

	/* Save current state */
	free_pdu_top    = ha->free_pdu_top;
	free_pdu_bottom = ha->free_pdu_bottom;

	pdu = free_pdu_top;
	free_pdu_top = pdu->Next;

	if (free_pdu_top == NULL)
		free_pdu_bottom = NULL;

	pdu_active = ha->pdu_active + 1;

	QL4PRINT(QLP19,
		 printk("scsi%d: %s: Get PDU queue SUCCEEDED!  "
			"Top %p Bot %p PDU %p Active %d\n",
			ha->host_no, __func__,
			free_pdu_top, free_pdu_bottom, pdu, pdu_active));

	length = (length + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
	num_pages = (uint16_t) (length / PAGE_SIZE);
	free_pages = 0;
	first_page = (uint16_t) (-1);
	pages_available = 0;

	/* Try to allocate contiguous free pages */
	for (i=0; i+num_pages <= MAX_PDU_ENTRIES; i++) {
		if (!ha->pdu_buf_used[i]) {
			first_page = i;
			pages_available = 1;
			for (j=i+1; j<num_pages; j++) {
				if (ha->pdu_buf_used[j]) {
					first_page = (uint16_t) (-1);
					pages_available = 0;
					i = j;
					break;
				}
			}
			if (pages_available)
				break;
		}
	}
	if (!pages_available) {
		QL4PRINT(QLP2|QLP19,
			 printk("scsi%d: %s: No contiguous pages available! "
				"Top %p Bot %p PDU %p Active %d\n",
				ha->host_no, __func__,
				free_pdu_top, free_pdu_bottom, pdu, pdu_active));
		return(NULL);
	}

	for (i=0; i<num_pages; i++)
		ha->pdu_buf_used[first_page+i] = 1;

	ha->free_pdu_top = free_pdu_top;
	ha->free_pdu_bottom = free_pdu_bottom;
	ha->pdu_active = pdu_active;

	/* Fill in PDU */
	pdu->Buff = (uint8_t *) (&(ha->pdu_buffsv) + (first_page * PAGE_SIZE));
	pdu->BuffLen = length;
	pdu->SendBuffLen = 0;
	pdu->RecvBuffLen = 0;
	pdu->Next = NULL;

	QL4PRINT(QLP19,
		 printk("scsi%d: %s: Get PDU buffers SUCCEEDED!  "
			"Top %p Bot %p PDU %p Buf %p Length %x Active %d\n",
			ha->host_no, __func__, free_pdu_top, free_pdu_bottom,
			pdu, pdu->Buff, pdu->BuffLen, pdu_active));
	return(pdu);
}

void qla4xxx_free_pdu(scsi_qla_host_t *ha, PDU_ENTRY *pdu)
{
	uint16_t first_page;
	uint16_t num_pages;
	uint16_t i;

	if (ha->free_pdu_bottom == NULL) {
		ha->free_pdu_top = pdu;
		ha->free_pdu_bottom = pdu;
	}
	else {
		ha->free_pdu_bottom->Next = pdu;
		ha->free_pdu_bottom = pdu;
	}

	/* Free buffer resources */
	first_page = (unsigned long) pdu->Buff - ((unsigned long) &ha->pdu_buffsv / PAGE_SIZE);
	num_pages = (uint16_t) (pdu->BuffLen / PAGE_SIZE);
	for (i=0; i<num_pages; i++)
		ha->pdu_buf_used[first_page+i] = 0;
	ha->pdu_active--;

	QL4PRINT(QLP19,
		 printk("scsi%d: %s: Top %p Bot %p PDU %p Buf %p Length %x Active %d\n",
			ha->host_no, __func__, ha->free_pdu_top, ha->free_pdu_bottom,
			pdu, pdu->Buff, pdu->BuffLen, ha->pdu_active));

	/* Clear PDU */
	pdu->Buff = NULL;
	pdu->BuffLen = 0;
	pdu->SendBuffLen = 0;
	pdu->RecvBuffLen = 0;
	pdu->Next = NULL;
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
uint8_t
qla4xxx_send_passthru0_iocb(scsi_qla_host_t *ha,
			    uint16_t fw_ddb_index,
			    uint16_t connection_id,
			    uint8_t *pdu_data,
			    uint32_t send_len,
			    uint32_t recv_len,
			    uint16_t control_flags,
			    uint32_t handle)
{
	PASSTHRU0_ENTRY *passthru_entry;
	uint8_t         status = QLA_SUCCESS;
	dma_addr_t      pdu_dma_data;

	ENTER("qla4xxx_send_passthru0_iocb");

	if (send_len) {
		QL4PRINT(QLP19, printk("PDU (0x%p) ->\n", pdu_data));
		qla4xxx_dump_bytes(QLP19, pdu_data, send_len);
	}

	/* Get pointer to the queue entry for the marker */
	if (qla4xxx_get_req_pkt(ha, (QUEUE_ENTRY **) &passthru_entry)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP5|QLP2|QLP19,
			 printk("scsi%d: %s: request queue full, try again later\n",
				ha->host_no, __func__));

		status = QLA_ERROR;
		goto exit_send_pt0;
	}

	/* Fill in the request queue */
	passthru_entry->hdr.entryType  = ET_PASSTHRU0;
	passthru_entry->hdr.entryCount = 1;
	passthru_entry->handle         = cpu_to_le32(handle);
	passthru_entry->target         = cpu_to_le16(fw_ddb_index);
	passthru_entry->connectionID   = cpu_to_le16(connection_id);
	passthru_entry->timeout        = __constant_cpu_to_le16(PT_DEFAULT_TIMEOUT);

	pdu_dma_data = ha->pdu_buffsp;
	
	/* FIXMEdg: What is this ????  */
	pdu_dma_data += ((unsigned long) pdu_data - ha->pdu_buffsp);

	if (send_len) {
		control_flags |= PT_FLAG_SEND_BUFFER;
		passthru_entry->outDataSeg64.base.addrHigh  =
		cpu_to_le32(MSDW(pdu_dma_data));
		passthru_entry->outDataSeg64.base.addrLow   =
		cpu_to_le32(LSDW(pdu_dma_data));
		passthru_entry->outDataSeg64.count          =
		cpu_to_le32(send_len);
		QL4PRINT(QLP19,
			 printk("scsi%d: %s: sending 0x%X bytes, "
				"pdu_dma_data = %lx\n",
				ha->host_no, __func__, send_len,
				(unsigned long)pdu_dma_data));
	}

	if (recv_len) {
		passthru_entry->inDataSeg64.base.addrHigh = cpu_to_le32(MSDW(pdu_dma_data));
		passthru_entry->inDataSeg64.base.addrLow  = cpu_to_le32(LSDW(pdu_dma_data));
		passthru_entry->inDataSeg64.count         = cpu_to_le32(recv_len);
		QL4PRINT(QLP19, printk("scsi%d: %s: receiving  0x%X bytes, pdu_dma_data = %lx\n",
				       ha->host_no, __func__, recv_len, (unsigned long)pdu_dma_data));
	}

	passthru_entry->controlFlags   = cpu_to_le16(control_flags);

	wmb();

	QL4PRINT(QLP19, printk(KERN_INFO "scsi%d: Passthru0 IOCB type %x count %x In (%x) %p\n",
			       ha->host_no, passthru_entry->hdr.entryType,
			       passthru_entry->hdr.entryCount, ha->request_in, passthru_entry));
	qla4xxx_dump_bytes(QLP10, passthru_entry, sizeof(*passthru_entry));


	/* Tell ISP it's got a new I/O request */
	WRT_REG_DWORD(&ha->reg->req_q_in, ha->request_in);
	PCI_POSTING(&ha->reg->req_q_in);

	exit_send_pt0:
	LEAVE("qla4xxx_send_passthru0_iocb");
	return(status);
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

