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

	QL4PRINT(QLP3, printk(
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

	if (ha->free_pdu_top == NULL) {
		QL4PRINT(QLP2|QLP19,
			 printk("scsi%d: %s: Out of PDUs!\n",
				ha->host_no, __func__));
		return(NULL);
	}

	/* Save current state */
	free_pdu_top    = ha->free_pdu_top;
	free_pdu_bottom = ha->free_pdu_bottom;
	pdu_active = ha->pdu_active + 1;

	/* get next available pdu */
	pdu = free_pdu_top;
	free_pdu_top = pdu->Next;

	if (free_pdu_top == NULL)
		free_pdu_bottom = NULL;


	/* round up to nearest page */
	length = (length + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);


	/* Allocate pdu buffer PDU */
	pdu->Buff = pci_alloc_consistent(ha->pdev, length, &pdu->DmaBuff);
	if (pdu->Buff == NULL) {
		QL4PRINT(QLP2|QLP19,
			 printk("scsi%d: %s: Unable to allocate memory "
				"for PDU buffer\n",
				ha->host_no, __func__));
		return(NULL);
	}
	
	memset(pdu->Buff, 0, length);
	
	/* Fill in remainder of PDU */
	pdu->BuffLen = length;
	pdu->SendBuffLen = 0;
	pdu->RecvBuffLen = 0;
	pdu->Next = NULL;

	ha->free_pdu_top = free_pdu_top;
	ha->free_pdu_bottom = free_pdu_bottom;
	ha->pdu_active = pdu_active;

	QL4PRINT(QLP19,
		 printk("scsi%d: %s: Get PDU SUCCEEDED!  "
			"Top %p Bot %p PDU %p Buf %p DmaBuf %lx Length %x "
			"Active %d\n", ha->host_no, __func__, free_pdu_top,
			free_pdu_bottom, pdu, pdu->Buff,
			(unsigned long)pdu->DmaBuff, pdu->BuffLen,
			pdu_active));
	return(pdu);
}

void qla4xxx_free_pdu(scsi_qla_host_t *ha, PDU_ENTRY *pdu)
{
	if (ha->free_pdu_bottom == NULL) {
		ha->free_pdu_top = pdu;
		ha->free_pdu_bottom = pdu;
	}
	else {
		ha->free_pdu_bottom->Next = pdu;
		ha->free_pdu_bottom = pdu;
	}

	pci_free_consistent(ha->pdev, pdu->BuffLen, pdu->Buff, pdu->DmaBuff);
	ha->pdu_active--;

	QL4PRINT(QLP19,
		 printk("scsi%d: %s: Top %p Bot %p PDU %p Buf %p DmaBuf %lx, "
			"Length %x Active %d\n", ha->host_no, __func__,
			ha->free_pdu_top,  ha->free_pdu_bottom, pdu, pdu->Buff,
			(unsigned long) pdu->DmaBuff, pdu->BuffLen,
			ha->pdu_active));

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
uint8_t
qla4xxx_send_passthru0_iocb(scsi_qla_host_t *ha,
			    uint16_t fw_ddb_index,
			    uint16_t connection_id,
			    dma_addr_t pdu_dma_data,
			    uint32_t send_len,
			    uint32_t recv_len,
			    uint16_t control_flags,
			    uint32_t handle)
{
	PASSTHRU0_ENTRY *passthru_entry;
	uint8_t         status = QLA_SUCCESS;

	ENTER("qla4xxx_send_passthru0_iocb");

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

	QL4PRINT(QLP19, printk("scsi%d: Passthru0 IOCB type %x count %x In (%x) pt0 %p handle %x\n",
			       ha->host_no, passthru_entry->hdr.entryType,
			       passthru_entry->hdr.entryCount, ha->request_in, passthru_entry, handle));
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

