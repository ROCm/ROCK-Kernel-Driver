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

/*
 * qla4xxx_print_srb_cmd
 *	This routine displays the SRB command
 */
static void qla4xxx_print_srb_info(srb_t * srb)
{
	printk("%s: srb = 0x%p, flags=0x%02x\n", __func__, srb, srb->flags);
	printk("%s: entry_count = 0x%02x, active_array_index=0x%04x\n",
	       __func__, srb->iocb_cnt, srb->active_array_index);
	printk("%s: cmd = 0x%p, saved_dma_handle = 0x%lx\n",
	       __func__, srb->cmd, (unsigned long) srb->dma_handle);
	printk("%s: fw_ddb_index = %d, lun = %d\n",
	       __func__, srb->fw_ddb_index, srb->cmd->device->lun);
	printk("%s: iocb_tov = %d\n",
	       __func__, srb->iocb_tov);
	printk("%s: cc_stat = 0x%x, r_start = 0x%lx, u_start = 0x%lx\n\n",
	       __func__, srb->cc_stat, srb->r_start, srb->u_start);
}

/*
 * qla4xxx_print_scsi_cmd
 *	This routine displays the SCSI command
 */
void qla4xxx_print_scsi_cmd(struct scsi_cmnd *cmd)
{
	int i;
	printk("SCSI Command = 0x%p, Handle=0x%p\n", cmd, cmd->host_scribble);
	printk("  b=%d, t=%02xh, l=%02xh, cmd_len = %02xh\n",
	       cmd->device->channel, cmd->device->id, cmd->device->lun,
	       cmd->cmd_len);
	printk("  CDB = ");
	for (i = 0; i < cmd->cmd_len; i++)
		printk("%02x ", cmd->cmnd[i]);
	printk("  seg_cnt = %d\n", cmd->use_sg);
	printk("  request buffer = 0x%p, request buffer len = 0x%x\n",
	       cmd->request_buffer, cmd->request_bufflen);
	if (cmd->use_sg) {
		struct scatterlist *sg;
		sg = (struct scatterlist *)cmd->request_buffer;
		printk("  SG buffer: \n");
		qla4xxx_dump_buffer((caddr_t) sg,
				    (cmd->use_sg * sizeof(struct scatterlist)));
	}
	printk("  tag = %d, transfersize = 0x%x \n", cmd->tag,
	       cmd->transfersize);
	printk("  Pid = %d, SP = 0x%p\n", (int)cmd->pid, CMD_SP(cmd));
	printk("  underflow size = 0x%x, direction=0x%x\n", cmd->underflow,
	       cmd->sc_data_direction);
	printk("  Current time (jiffies) = 0x%lx, "
	       "timeout expires = 0x%lx\n", jiffies, cmd->eh_timeout.expires);
	qla4xxx_print_srb_info((srb_t *) CMD_SP(cmd));
}

void __dump_registers(scsi_qla_host_t * ha)
{
	uint8_t i;
	for (i = 0; i < MBOX_REG_COUNT; i++) {
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
		       (uint8_t) offsetof(isp_reg_t,
					  u2.isp4010.port_err_status),
		       RD_REG_DWORD(&ha->reg->u2.isp4010.port_err_status));
	}

	else if (IS_QLA4022(ha)) {
		printk(KERN_INFO "Page 0 Registers:\n");
		printk(KERN_INFO "0x%02X ext_hw_conf     = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t,
					  u2.isp4022.p0.ext_hw_conf),
		       RD_REG_DWORD(&ha->reg->u2.isp4022.p0.ext_hw_conf));
		printk(KERN_INFO "0x%02X port_ctrl       = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t,
					  u2.isp4022.p0.port_ctrl),
		       RD_REG_DWORD(&ha->reg->u2.isp4022.p0.port_ctrl));
		printk(KERN_INFO "0x%02X port_status     = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t,
					  u2.isp4022.p0.port_status),
		       RD_REG_DWORD(&ha->reg->u2.isp4022.p0.port_status));
		printk(KERN_INFO "0x%02X gp_out          = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4022.p0.gp_out),
		       RD_REG_DWORD(&ha->reg->u2.isp4022.p0.gp_out));
		printk(KERN_INFO "0x%02X gp_in           = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t, u2.isp4022.p0.gp_in),
		       RD_REG_DWORD(&ha->reg->u2.isp4022.p0.gp_in));
		printk(KERN_INFO "0x%02X port_err_status = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t,
					  u2.isp4022.p0.port_err_status),
		       RD_REG_DWORD(&ha->reg->u2.isp4022.p0.port_err_status));
		printk(KERN_INFO "Page 1 Registers:\n");
		WRT_REG_DWORD(&ha->reg->ctrl_status,
			      HOST_MEM_CFG_PAGE &
			      SET_RMASK(CSR_SCSI_PAGE_SELECT));
		printk(KERN_INFO "0x%02X req_q_out       = 0x%08X\n",
		       (uint8_t) offsetof(isp_reg_t,
					  u2.isp4022.p1.req_q_out),
		       RD_REG_DWORD(&ha->reg->u2.isp4022.p1.req_q_out));
		WRT_REG_DWORD(&ha->reg->ctrl_status,
			      PORT_CTRL_STAT_PAGE &
			      SET_RMASK(CSR_SCSI_PAGE_SELECT));
	}
}

/*
 * qla4xxx_dump_registers
 *	This routine displays ISP registers
 *
 * Input:
 *	ha       - adapter structure pointer
 *
 */
void qla4xxx_dump_mbox_registers(scsi_qla_host_t * ha)
{
	unsigned long flags = 0;
	int i = 0;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 1; i < MBOX_REG_COUNT; i++)
		printk(KERN_INFO "  Mailbox[%d] = %08x\n", i,
		       RD_REG_DWORD(&ha->reg->mailbox[i]));
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void qla4xxx_dump_registers(scsi_qla_host_t * ha)
{
	unsigned long flags = 0;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	__dump_registers(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void qla4xxx_dump_buffer(uint8_t * b, uint32_t size)
{
	uint32_t cnt;
	uint8_t c;
	printk(" 0   1   2   3   4   5   6   7   8   9  Ah  Bh  Ch  Dh  Eh  "
	    "Fh\n");
	printk("------------------------------------------------------------"
	    "--\n");
	for (cnt = 0; cnt < size;) {
		c = *b++;
		printk("%02x", (uint32_t) c);
		cnt++;
		if (!(cnt % 16))
			printk("\n");

		else
			printk("  ");
	}
	if (cnt % 16)
		printk("\n");
}
