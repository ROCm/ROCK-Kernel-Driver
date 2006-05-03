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
 *	qla4xxx_get_debug_level
 *	qla4xxx_set_debug_level
 *	printchar
 *	qla4xxx_dump_bytes
 *	qla4xxx_dump_words
 *	qla4xxx_dump_dwords
 *	qla4xxx_print_scsi_cmd
 *	qla4xxx_print_srb_info
 *	qla4xxx_print_iocb_passthru
 *	__dump_dwords
 *	__dump_words
 *	__dump_registers
 *	qla4xxx_dump_registers
 *	__dump_mailbox_registers
 ****************************************************************************/

#include "ql4_def.h"

//#define QLP1	0x00000002  // Unrecoverable error messages
//#define QLP2	0x00000004  // Unexpected completion path error messages
//#define QLP3	0x00000008  // Function trace messages
//#define QLP4	0x00000010  // IOCTL trace messages
//#define QLP5	0x00000020  // I/O & Request/Response queue trace messages
//#define QLP6	0x00000040  // Watchdog messages (current state)
//#define QLP7	0x00000080  // Initialization
//#define QLP8	0x00000100  // Internal command queue traces
//#define QLP9	0x00000200  // Unused
//#define QLP10	0x00000400  // Extra Debug messages (dump buffers)
//#define QLP11	0x00000800  // Mailbox & ISR Details
//#define QLP12	0x00001000  // Enter/Leave routine messages
//#define QLP13 0x00002000  // Display data for Inquiry, TUR, ReqSense, RptLuns
//#define QLP14 0x00004000
//#define QLP15 0x00008000  // Display jiffies for IOCTL calls
//#define QLP16 0x00010000  // Extended proc print statements (srb info)
//#define QLP17 0x00020000  // Display NVRAM Accesses
//#define QLP18 0x00040000  // unused
//#define QLP19	0x00080000  // PDU info
//#define QLP20 0x00100000  // iSNS info
//#define QLP24 0x01000000  // Scatter/Gather info

uint32_t ql_dbg_level = 0;

/**************************************************************************
 * qla4xxx_get_debug_level
 *	This routine retrieves the driver's debug print level.
 *
 * Input:
 *	dbg_level - driver's debug print level
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS - always
 **************************************************************************/
uint8_t
qla4xxx_get_debug_level(uint32_t *dbg_level)
{
	*dbg_level = ql_dbg_level;
	barrier();
	return(QLA_SUCCESS);
}

/**************************************************************************
 * qla4xxx_set_debug_level
 *	This routine sets the driver's debug print level.
 *
 * Input:
 *	dbg_level - driver's debug print level
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS - always
 **************************************************************************/
uint8_t
qla4xxx_set_debug_level(uint32_t dbg_level)
{
	ql_dbg_level = dbg_level;
	barrier();
	return(QLA_SUCCESS);
}

/****************************************************************************/
/*                      Debug Print Routines                          	    */
/****************************************************************************/

void printchar(char ch)
{
	if (ch>=32)
		printk("%c", ch);
	else
		printk(".");
}

/**************************************************************************
 * qla4xxx_dump_bytes
 *	This routine displays bytes in hex format
 *
 * Input:
 *	dbg_mask - this call's debug print mask
 *	buffer   - data buffer to display
 *	size     - number of bytes to display
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 **************************************************************************/
void
qla4xxx_dump_bytes(uint32_t dbg_mask, void *buffer, uint32_t size)
{
	uint32_t i;
	uint8_t  *data = (uint8_t *)buffer;

	if ((ql_dbg_level & dbg_mask) != 0) {
		#if 0
		  printk("        0  1  2  3  4  5  6  7 -  8  9  A  B  C  D  E  F\n");
		  printk("---------------------------------------------------------\n");
		#endif

		for (i = 0; i < size; i++, data++) {
			if (i % 0x10 == 0) {
				printk("%04X:  %02X", i, *data);
			}
			else if (i % 0x10 == 0x08) {
				printk(" - %02X", *data);
			}
			else if (i % 0x10 == 0xF) {
				printk(" %02X:  ", *data);
				printchar(*(data-15));
				printchar(*(data-14));
				printchar(*(data-13));
				printchar(*(data-12));
				printchar(*(data-11));
				printchar(*(data-10));
				printchar(*(data-9));
				printchar(*(data-8));
				printchar(*(data-7));
				printchar(*(data-6));
				printchar(*(data-5));
				printchar(*(data-4));
				printchar(*(data-3));
				printchar(*(data-2));
				printchar(*(data-1));
				printchar(*data);
				printk("\n");
			}
			else {
				printk(" %02X", *data);
			}
		}

		if ((i != 0) && (i % 0x10)) {
			printk("\n");
		}
		printk("\n");
	}
}

/**************************************************************************
 * qla4xxx_dump_words
 *	This routine displays words in hex format
 *
 * Input:
 *	dbg_mask - this call's debug print mask
 *	buffer   - data buffer to display
 *	size     - number of bytes to display
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 **************************************************************************/
void
qla4xxx_dump_words(uint32_t dbg_mask, void *buffer, uint32_t size)
{
	if ((ql_dbg_level & dbg_mask) != 0)
		__dump_words(buffer, size);
}

/**************************************************************************
 * qla4xxx_dump_dwords
 *	This routine displays double words in hex format
 *
 * Input:
 *	dbg_mask - this call's debug print mask
 *	buffer   - data buffer to display
 *	size     - number of bytes to display
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 **************************************************************************/
void
qla4xxx_dump_dwords(uint32_t dbg_mask, void *buffer, uint32_t size)
{
	if ((ql_dbg_level & dbg_mask) != 0)
		__dump_dwords(buffer, size);
}

/**************************************************************************
 * qla4xxx_print_scsi_cmd
 *	This routine displays the SCSI command
 *
 * Input:
 *	dbg_mask - this call's debug print mask
 *	cmd      - pointer to Linux kernel command structure
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 **************************************************************************/
void
qla4xxx_print_scsi_cmd(uint32_t dbg_mask, struct scsi_cmnd *cmd)
{
	if ((ql_dbg_level & dbg_mask) != 0) {
		int   i;

		printk("SCSI Command = 0x%p, Handle=0x%p\n",
		       cmd, cmd->host_scribble);

		printk("  b=%d, t=%02xh, l=%02xh, cmd_len = %02xh\n",
		       cmd->device->channel, cmd->device->id, cmd->device->lun,
		       cmd->cmd_len);

		printk("  CDB = ");
		for (i = 0; i < cmd->cmd_len; i++)
			printk("%02x ", cmd->cmnd[i]);

		printk("  seg_cnt = %d\n",cmd->use_sg);
		printk("  request buffer = 0x%p, request buffer len = 0x%x\n",
		       cmd->request_buffer,cmd->request_bufflen);

		if (cmd->use_sg) {
			struct scatterlist *sg;
			sg = (struct scatterlist *) cmd->request_buffer;
			printk("  SG buffer: \n");
			qla4xxx_dump_bytes(dbg_mask, (caddr_t)sg,
					   (cmd->use_sg *
					    sizeof(struct scatterlist)));
		}

		printk("  tag = %d, transfersize = 0x%x \n",
		       cmd->tag, cmd->transfersize);

		printk("  Pid = %d, SP = 0x%p\n", (int)cmd->pid, CMD_SP(cmd));
		printk("  underflow size = 0x%x, direction=0x%x\n",
		       cmd->underflow, cmd->sc_data_direction);

		printk("  Current time (jiffies) = 0x%lx, "
		       "timeout expires = 0x%lx\n",
		       jiffies, cmd->eh_timeout.expires);
	}
}

void
qla4xxx_dump_command(scsi_qla_host_t *ha, struct scsi_cmnd *cmd )
{
	if (host_byte(cmd->result) == DID_OK) {
		switch (cmd->cmnd[0]) {
		case TEST_UNIT_READY:
			QL4PRINT(QLP13,
				 printk("scsi%d:%d:%d:%d: %s: "
					"TEST_UNIT_READY "
					"status = 0x%x\n",
					ha->host_no, cmd->device->channel,
					cmd->device->id, cmd->device->lun,
					__func__, cmd->result & 0xff));

			if (driver_byte(cmd->result) & DRIVER_SENSE) {
				QL4PRINT(QLP13,
					 printk("REQUEST_SENSE data:  "
						"(MAX 0x20 bytes displayed)\n"));

				qla4xxx_dump_bytes(QLP13, cmd->sense_buffer,
						   MIN(0x20, sizeof(cmd->sense_buffer)));
			}
			break;
		case INQUIRY:
			QL4PRINT(QLP13, printk("scsi%d:%d:%d:%d: %s: "
					       "INQUIRY data: "
					       "(MAX 0x30 bytes displayed)\n",
					       ha->host_no,
					       cmd->device->channel,
					       cmd->device->id,
					       cmd->device->lun, __func__));

			qla4xxx_dump_bytes(QLP13, cmd->request_buffer,
					   MIN(0x30, cmd->request_bufflen));

			if (strncmp(cmd->request_buffer,
				    "\7f\00\00\00\7f\00\00\00", 8) == 0) {
				QL4PRINT(QLP2,
					 printk("scsi%d:%d:%d:%d: %s: "
						"Device not present.  "
						"Possible connection "
						"problem with iSCSI router\n",
						ha->host_no,
						cmd->device->channel,
						cmd->device->id,
						cmd->device->lun, __func__));
			}
			break;
		case REQUEST_SENSE:
			QL4PRINT(QLP13,
				 printk("scsi%d:%d:%d:%d: %s: REQUEST_SENSE "
					"data:  (MAX 0x20 bytes displayed)\n",
					ha->host_no, cmd->device->channel,
					cmd->device->id, cmd->device->lun, __func__));

			qla4xxx_dump_bytes(QLP13, cmd->request_buffer,
					   MIN(0x20, cmd->request_bufflen));
			break;
		case REPORT_LUNS:
			QL4PRINT(QLP13,
				 printk("scsi%d:%d:%d:%d: %s: "
					"REPORT_LUNS data: "
					"(MAX 0x40 bytes displayed)\n",
					ha->host_no, cmd->device->channel,
					cmd->device->id, cmd->device->lun,
					__func__));

			qla4xxx_dump_bytes(QLP13, cmd->request_buffer,
					   MIN(0x40, cmd->request_bufflen));
			break;
		}

	}

}

/**************************************************************************
 * qla4xxx_print_srb_info
 *	This routine displays the srb structure
 *
 * Input:
 *	dbg_mask - this call's debug print mask
 *	srb      - pointer to srb structure
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 **************************************************************************/
void
qla4xxx_print_srb_info(uint32_t dbg_mask, srb_t *srb)
{
	if ((ql_dbg_level & dbg_mask) != 0) {
		printk("%s: srb = 0x%p, flags=0x%02x\n",
		       __func__, srb, srb->flags);
		printk("%s: entry_count = 0x%02x\n",
		       __func__, srb->entry_count);
		printk("%s: cmd = 0x%p, saved_dma_handle = 0x%x\n",
		       __func__, srb->cmd, (uint32_t) srb->saved_dma_handle);
		printk("%s: fw_ddb_index = %d, lun = %d\n",
		       __func__, srb->fw_ddb_index, srb->lun);
		printk("%s: os_tov = %d, iocb_tov = %d\n",
		       __func__, srb->os_tov, srb->iocb_tov);
		printk("%s: cc_stat = 0x%x, r_start = 0x%lx, u_start = 0x%lx\n\n",
		       __func__, srb->cc_stat, srb->r_start, srb->u_start);
	}
}

/* hardware_lock taken */
void
__dump_dwords(void *buffer, uint32_t size)
{
	uint32_t *data = (uint32_t *)buffer;
	uint32_t i;

	for (i = 0; i < size; i+=4, data++) {
		if (i % 0x10 == 0) {
			printk("%04X:  %08X", i, *data);
		}
		else if (i % 0x10 == 0x08) {
			printk(" - %08X", *data);
		}
		else if (i % 0x10 == 0x0C) {
			printk(" %08X\n", *data);
		}
		else {
			printk(" %08X", *data);
		}
	}
	if ((i != 0) && (i % 0x10 != 0)) {
		printk("\n");
	}
}

/* hardware_lock taken */
void
__dump_words(void *buffer, uint32_t size)
{
	uint16_t *data = (uint16_t *)buffer;
	uint32_t i;

	for (i = 0; i < size; i+=2, data++) {
		if (i % 0x10 == 0) {
			printk(KERN_INFO "%04X:  %04X", i, *data);
		}
		else if (i % 0x10 == 0x08) {
			printk(KERN_INFO " - %04X", *data);
		}
		else if (i % 0x10 == 0x0E) {
			uint8_t *bdata = (uint8_t *) data;
			printk(KERN_INFO " %04X:  ", *data);
			printchar(*(bdata-13));
			printchar(*(bdata-14));
			printchar(*(bdata-11));
			printchar(*(bdata-12));
			printchar(*(bdata-9));
			printchar(*(bdata-10));
			printchar(*(bdata-7));
			printchar(*(bdata-8));
			printchar(*(bdata-5));
			printchar(*(bdata-6));
			printchar(*(bdata-3));
			printchar(*(bdata-4));
			printchar(*(bdata-1));
			printchar(*(bdata-2));
			printchar(*(bdata+1));
			printchar(*(bdata));
			printk("\n");
		}
		else {
			printk(KERN_INFO " %04X", *data);
		}
	}
	if ((i != 0) && (i % 0x10 != 0)) {
		printk(KERN_INFO "\n");
	}
}

/* hardware_lock taken */
void
__dump_registers(uint32_t dbg_mask, scsi_qla_host_t *ha)
{
	uint8_t  i;

	if ((ql_dbg_level & dbg_mask) == 0)
		return;


	for (i=0; i<MBOX_REG_COUNT; i++) {
		printk(KERN_INFO "0x%02X mailbox[%d]      = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, mailbox[i]), i,
		       RD_REG_DWORD(&ha->reg->mailbox[i]));
	}
	printk(KERN_INFO "0x%02X flash_address   = 0x%08X\n",
	       (uint8_t) offsetof(isp_reg_t, flash_address),
	       RD_REG_DWORD(&ha->reg->flash_address));

	printk(KERN_INFO "0x%02X flash_data      = 0x%08X\n",
	       (uint8_t) offsetof(isp_reg_t, flash_data),
	       RD_REG_DWORD(&ha->reg->flash_data));

	printk(KERN_INFO "0x%02X ctrl_status     = 0x%08X\n",
	       (uint8_t) offsetof(isp_reg_t, ctrl_status),
	       RD_REG_DWORD(&ha->reg->ctrl_status));

	if (IS_QLA4010(ha)) {

		printk(KERN_INFO "0x%02X nvram           = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u1.isp4010.nvram),
		       RD_REG_DWORD(&ha->reg->u1.isp4010.nvram));
	}
	else if (IS_QLA4022(ha)) {

		printk(KERN_INFO "0x%02X intr_mask       = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u1.isp4022.intr_mask),
		    RD_REG_DWORD(&ha->reg->u1.isp4022.intr_mask));
		
		printk(KERN_INFO "0x%02X nvram           = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u1.isp4022.nvram),
		    RD_REG_DWORD(&ha->reg->u1.isp4022.nvram));
		
		printk(KERN_INFO "0x%02X semaphore       = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u1.isp4022.semaphore),
		    RD_REG_DWORD(&ha->reg->u1.isp4022.semaphore));
	}

	printk(KERN_INFO "0x%02X req_q_in        = 0x%08X\n",
	       (uint8_t) offsetof(isp_reg_t, req_q_in),
	       RD_REG_DWORD(&ha->reg->req_q_in));

	printk(KERN_INFO "0x%02X rsp_q_out       = 0x%08X\n",
	       (uint8_t) offsetof(isp_reg_t, rsp_q_out),
	       RD_REG_DWORD(&ha->reg->rsp_q_out));

	if (IS_QLA4010(ha)) {

		printk(KERN_INFO "0x%02X ext_hw_conf     = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4010.ext_hw_conf),
		       RD_REG_DWORD(&ha->reg->u2.isp4010.ext_hw_conf));

		printk(KERN_INFO "0x%02X port_ctrl       = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4010.port_ctrl),
		       RD_REG_DWORD(&ha->reg->u2.isp4010.port_ctrl));

		printk(KERN_INFO "0x%02X port_status     = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4010.port_status),
		       RD_REG_DWORD(&ha->reg->u2.isp4010.port_status));

		printk(KERN_INFO "0x%02X req_q_out       = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4010.req_q_out),
		       RD_REG_DWORD(&ha->reg->u2.isp4010.req_q_out));

		printk(KERN_INFO "0x%02X gp_out          = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4010.gp_out),
		       RD_REG_DWORD(&ha->reg->u2.isp4010.gp_out));

		printk(KERN_INFO "0x%02X gp_in           = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4010.gp_in),
		       RD_REG_DWORD(&ha->reg->u2.isp4010.gp_in));

		printk(KERN_INFO "0x%02X port_err_status = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4010.port_err_status),
		       RD_REG_DWORD(&ha->reg->u2.isp4010.port_err_status));
	}
	else if (IS_QLA4022(ha)) {

		printk(KERN_INFO "Page 0 Registers:\n");
	
		printk(KERN_INFO "0x%02X ext_hw_conf     = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u2.isp4022.p0.ext_hw_conf),
		    RD_REG_DWORD(&ha->reg->u2.isp4022.p0.ext_hw_conf));
		
		printk(KERN_INFO "0x%02X port_ctrl       = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u2.isp4022.p0.port_ctrl),
		    RD_REG_DWORD(&ha->reg->u2.isp4022.p0.port_ctrl));
		
		printk(KERN_INFO "0x%02X port_status     = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u2.isp4022.p0.port_status),
		    RD_REG_DWORD(&ha->reg->u2.isp4022.p0.port_status));
		
		printk(KERN_INFO "0x%02X gp_out          = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u2.isp4022.p0.gp_out),
		    RD_REG_DWORD(&ha->reg->u2.isp4022.p0.gp_out));
		
		printk(KERN_INFO "0x%02X gp_in           = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u2.isp4022.p0.gp_in),
		    RD_REG_DWORD(&ha->reg->u2.isp4022.p0.gp_in));
		
		printk(KERN_INFO "0x%02X port_err_status = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u2.isp4022.p0.port_err_status),
		    RD_REG_DWORD(&ha->reg->u2.isp4022.p0.port_err_status));
		
		printk(KERN_INFO "Page 1 Registers:\n");
		
		WRT_REG_DWORD(&ha->reg->ctrl_status, HOST_MEM_CFG_PAGE &
			   SET_RMASK(CSR_SCSI_PAGE_SELECT));
		
		printk(KERN_INFO "0x%02X req_q_out       = 0x%08X\n",
		    (uint8_t) offsetof(isp_reg_t, u2.isp4022.p1.req_q_out),
		    RD_REG_DWORD(&ha->reg->u2.isp4022.p1.req_q_out));
		
		WRT_REG_DWORD(&ha->reg->ctrl_status, PORT_CTRL_STAT_PAGE &
			   SET_RMASK(CSR_SCSI_PAGE_SELECT));
		
	}
}

/**************************************************************************
 * qla4xxx_dump_registers
 *	This routine displays ISP registers
 *
 * Input:
 *	dbg_mask - this call's debug print mask
 *	ha       - adapter structure pointer
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 **************************************************************************/
void
qla4xxx_dump_registers(uint32_t dbg_mask, scsi_qla_host_t *ha)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	__dump_registers(dbg_mask, ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
__dump_mailbox_registers(uint32_t dbg_mask, scsi_qla_host_t *ha)
{
	int i =  0;

	if ((ql_dbg_level & dbg_mask) == 0)
		return;

	for (i = 1; i < MBOX_REG_COUNT; i++)
		printk(KERN_INFO "  Mailbox[%d] = %08x\n", i,
			RD_REG_DWORD(&ha->reg->mailbox[i]));
}

void
qla4xxx_dump_buffer(uint8_t * b, uint32_t size)
{
	uint32_t cnt;
	uint8_t c;

	printk(" 0   1   2   3   4   5   6   7   8   9  "
	    "Ah  Bh  Ch  Dh  Eh  Fh\n");
	printk("----------------------------------------"
	    "----------------------\n");

	for (cnt = 0; cnt < size;) {
		c = *b++;
		printk("%02x",(uint32_t) c);
		cnt++;
		if (!(cnt % 16))
			printk("\n");
		else
			printk("  ");
	}
	if (cnt % 16)
		printk("\n");
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


