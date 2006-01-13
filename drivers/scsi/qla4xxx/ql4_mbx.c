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

#include <linux/delay.h>

/*
 * externals
 */
extern int ql4xportdownretrycount;
extern int ql4xdiscoverywait;
extern void qla4xxx_isns_build_entity_id(scsi_qla_host_t *);
extern int qla4xxx_eh_wait_for_active_target_commands(scsi_qla_host_t *, int,
    int lun);

/**************************************************************************
 * qla4xxx_mailbox_command
 *	This routine sssue mailbox commands and waits for completion.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	inCount	 - number of mailbox registers to load.
 *      outCount - number of mailbox registers to return.
 *      mbx_cmd  - data pointer for mailbox in registers.
 *      mbx_sts  - data pointer for mailbox out registers.
 *
 * Output:
 *      mbx_sts - returned mailbox out data.
 *
 * Remarks:
 *	If outCount is 0, this routine completes successfully WITHOUT waiting
 *	for the mailbox command to complete.
 *
 * Returns:
 *	QLA_SUCCESS - Mailbox command completed successfully
 *	QLA_ERROR   - Mailbox command competed in error.
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_mailbox_command(scsi_qla_host_t * ha, uint8_t inCount,
    uint8_t outCount, uint32_t * mbx_cmd, uint32_t * mbx_sts)
{
	int status = QLA_ERROR;
	uint8_t i;
	u_long wait_count;
	uint32_t intr_status;
	unsigned long flags = 0;
	DECLARE_WAITQUEUE(wait, current);

	down(&ha->mbox_sem);

	/* Mailbox code active */
	set_bit(AF_MBOX_COMMAND, &ha->flags);

	/* Make sure that pointers are valid */
	if (!mbx_cmd || !mbx_sts) {
		DEBUG2(printk("scsi%ld: %s: Invalid mbx_cmd or mbx_sts "
		    "pointer\n", ha->host_no, __func__));
		goto mbox_exit;
	}

	/* To prevent overwriting mailbox registers for a command that has
	 * not yet been serviced, check to see if a previously issued
	 * mailbox command is interrupting.
	 * -----------------------------------------------------------------
	 */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	intr_status = RD_REG_DWORD(&ha->reg->ctrl_status);
	if (intr_status & CSR_SCSI_PROCESSOR_INTR) {
		/* Service existing interrupt */
		qla4xxx_interrupt_service_routine(ha, intr_status);
		clear_bit(AF_MBOX_COMMAND_DONE, &ha->flags);
	}

	/* Send the mailbox command to the firmware */
	ha->mbox_status_count = outCount;
	for (i = 0; i < outCount; i++)
		ha->mbox_status[i] = 0;

	/* Load all mailbox registers, except mailbox 0. */
	for (i = 1; i < inCount; i++)
		WRT_REG_DWORD(&ha->reg->mailbox[i], mbx_cmd[i]);

	/* Wakeup firmware  */
	WRT_REG_DWORD(&ha->reg->mailbox[0], mbx_cmd[0]);
	PCI_POSTING(&ha->reg->mailbox[0]);
	WRT_REG_DWORD(&ha->reg->ctrl_status, SET_RMASK(CSR_INTR_RISC));
	PCI_POSTING(&ha->reg->ctrl_status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Wait for completion */
	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&ha->mailbox_wait_queue, &wait);

	/*
	 * If we don't want status, don't wait for the mailbox command to
	 * complete.  For example, MBOX_CMD_RESET_FW doesn't return status,
	 * you must poll the inbound Interrupt Mask for completion.
	 */
	if (outCount == 0) {
		status = QLA_SUCCESS;
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&ha->mailbox_wait_queue, &wait);
		goto mbox_exit;
	}

	/* Wait for command to complete */
	wait_count = jiffies + MBOX_TOV * HZ;
	while (test_bit(AF_MBOX_COMMAND_DONE, &ha->flags) == 0) {
		if (wait_count <= jiffies)
			break;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		intr_status = RD_REG_DWORD(&ha->reg->ctrl_status);
		if (intr_status & INTR_PENDING) {
			/*
			 * Service the interrupt.
			 * The ISR will save the mailbox status registers
			 * to a temporary storage location in the adapter
			 * structure.
			 */
			ha->mbox_status_count = outCount;
			qla4xxx_interrupt_service_routine(ha, intr_status);
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/*
		 * Delay for 10 microseconds
		 * NOTE: Interrupt_handler may be called here,
		 *       if interrupts are enabled
		 */
#if 0
		udelay(10);

#else				/*  */
		schedule_timeout(10);

#endif				/*  */
	}			/* wait loop */
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ha->mailbox_wait_queue, &wait);

	/* Check for mailbox timeout. */
	if (!test_bit(AF_MBOX_COMMAND_DONE, &ha->flags)) {
		DEBUG2(printk("scsi%ld: Mailbox Cmd 0x%08X timed out ...,"
		    " Scheduling Adapter Reset\n", ha->host_no, mbx_cmd[0]));
		ha->mailbox_timeout_count++;
		mbx_sts[0] = (-1);
		set_bit(DPC_RESET_HA, &ha->dpc_flags);
		goto mbox_exit;
	}

	/*
	 * Copy the mailbox out registers to the caller's mailbox in/out
	 * structure.
	 */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 0; i < outCount; i++)
		mbx_sts[i] = ha->mbox_status[i];

	/* Set return status and error flags (if applicable). */
	switch (ha->mbox_status[0]) {
	case MBOX_STS_COMMAND_COMPLETE:
		status = QLA_SUCCESS;
		break;

	case MBOX_STS_INTERMEDIATE_COMPLETION:
		status = QLA_SUCCESS;
		break;

	case MBOX_STS_BUSY:
		DEBUG2( printk("scsi%ld: %s: Cmd = %08X, ISP BUSY\n",
		    ha->host_no, __func__, mbx_cmd[0]));
		ha->mailbox_timeout_count++;
		break;

	default:
		DEBUG2(printk("scsi%ld: %s: **** FAILED, cmd = %08X, "
		    "sts = %08X ****\n", ha->host_no, __func__, mbx_cmd[0],
		    mbx_sts[0]));
		break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

mbox_exit:
	clear_bit(AF_MBOX_COMMAND, &ha->flags);
	clear_bit(AF_MBOX_COMMAND_DONE, &ha->flags);
	up(&ha->mbox_sem);

	return status;
}

#if 0
int qla4xxx_send_noop(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_NOP;
	if (qla4xxx_mailbox_command(ha, 1, 1, &mbox_cmd[0], &mbox_sts[0])
	    != QLA_SUCCESS) {
		DEBUG2( printk(KERN_INFO "scsi%d: NOP failed\n", ha->host_no));
		return (QLA_ERROR);
	}

	else {
		return (QLA_SUCCESS);
	}
}

int qla4xxx_mbx_test(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int i;
	int status;
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_REGISTER_TEST;
	mbox_cmd[1] = 0x11111111;
	mbox_cmd[2] = 0x22222222;
	mbox_cmd[3] = 0x33333333;
	mbox_cmd[4] = 0x44444444;
	mbox_cmd[5] = 0x55555555;
	mbox_cmd[6] = 0x66666666;
	mbox_cmd[7] = 0x77777777;
	if (qla4xxx_mailbox_command(ha, 8, 8, &mbox_cmd[0], &mbox_sts[0])
	    != QLA_SUCCESS) {
		return (QLA_ERROR);
	}
	if (mbox_sts[1] != 0x11111111 || mbox_sts[2] != 0x22222222
	    || mbox_sts[3] != 0x33333333 || mbox_sts[4] != 0x44444444
	    || mbox_sts[5] != 0x55555555 || mbox_sts[6] != 0x66666666
	    || mbox_sts[7] != 0x77777777) {
		status = QLA_ERROR;
	}

	else {
		status = QLA_SUCCESS;
	}
	return (status);
}

#endif				/*  */

/*
 * qla4xxx_issue_iocb
 *	Issue IOCB using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	buffer = buffer pointer.
 *	phys_addr = physical address of buffer.
 *	size = size of buffer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla4xxx_issue_iocb(scsi_qla_host_t * ha, void *buffer, dma_addr_t phys_addr,
    size_t size)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_EXECUTE_IOCB_A64;
	mbox_cmd[1] = 0;
	mbox_cmd[2] = LSDW(phys_addr);
	mbox_cmd[3] = MSDW(phys_addr);
	status = qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]);
	return status;
}

int
qla4xxx_conn_close_sess_logout(scsi_qla_host_t * ha, uint16_t fw_ddb_index,
    uint16_t connection_id, uint16_t option)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_CONN_CLOSE_SESS_LOGOUT;
	mbox_cmd[1] = fw_ddb_index;
	mbox_cmd[2] = connection_id;
	mbox_cmd[3] = LOGOUT_OPTION_RELOGIN;
	if (qla4xxx_mailbox_command(ha, 4, 2, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_CONN_CLOSE_SESS_LOGOUT "
		    "option %04x failed sts %04X %04X", ha->host_no, __func__,
		    option, mbox_sts[0], mbox_sts[1]));
		if (mbox_sts[0] == 0x4005)
			DEBUG2(printk("%s reason %04X\n", __func__,
			    mbox_sts[1]));
	}
	return QLA_SUCCESS;
}

int
qla4xxx_clear_database_entry(scsi_qla_host_t * ha, uint16_t fw_ddb_index)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_CLEAR_DATABASE_ENTRY;
	mbox_cmd[1] = fw_ddb_index;
	if (qla4xxx_mailbox_command(ha, 2, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		return QLA_ERROR;
	}
	return QLA_SUCCESS;
}

/**************************************************************************
 * qla4xxx_initialize_fw_cb
 *	This routine initializes the firmware control block for the
 *	specified adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully initialized firmware ctrl block
 *	QLA_ERROR   - Failed to initialize firmware ctrl block
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_initialize_fw_cb(scsi_qla_host_t * ha)
{
	INIT_FW_CTRL_BLK *init_fw_cb;
	dma_addr_t init_fw_cb_dma;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_ERROR;

	init_fw_cb = dma_alloc_coherent(&ha->pdev->dev,
	    sizeof(INIT_FW_CTRL_BLK), &init_fw_cb_dma, GFP_KERNEL);
	if (init_fw_cb == NULL) {
		DEBUG2(printk("scsi%ld: %s: Unable to alloc init_cb\n",
		    ha->host_no, __func__));
		    return 10;
	}
	memset(init_fw_cb, 0, sizeof(INIT_FW_CTRL_BLK));

	/* Get Initialize Firmware Control Block. */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_INIT_FW_CTRL_BLOCK;
	mbox_cmd[2] = LSDW(init_fw_cb_dma);
	mbox_cmd[3] = MSDW(init_fw_cb_dma);
	if (qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		dma_free_coherent(&ha->pdev->dev, sizeof(INIT_FW_CTRL_BLK),
		    init_fw_cb, init_fw_cb_dma);
		return status;
	}

	/* Initialize request and response queues. */
	qla4xxx_init_rings(ha);

	/* Fill in the request and response queue information. */
	init_fw_cb->ReqQConsumerIndex = cpu_to_le16(ha->request_out);
	init_fw_cb->ComplQProducerIndex = cpu_to_le16(ha->response_in);
	init_fw_cb->ReqQLen = __constant_cpu_to_le16(REQUEST_QUEUE_DEPTH);
	init_fw_cb->ComplQLen = __constant_cpu_to_le16(RESPONSE_QUEUE_DEPTH);
	init_fw_cb->ReqQAddrLo = cpu_to_le32(LSDW(ha->request_dma));
	init_fw_cb->ReqQAddrHi = cpu_to_le32(MSDW(ha->request_dma));
	init_fw_cb->ComplQAddrLo = cpu_to_le32(LSDW(ha->response_dma));
	init_fw_cb->ComplQAddrHi = cpu_to_le32(MSDW(ha->response_dma));
	init_fw_cb->ShadowRegBufAddrLo = cpu_to_le32(LSDW(ha->shadow_regs_dma));
	init_fw_cb->ShadowRegBufAddrHi = cpu_to_le32(MSDW(ha->shadow_regs_dma));

	/* Set up required options. */
	init_fw_cb->FwOptions |=
	    __constant_cpu_to_le16(FWOPT_SESSION_MODE | FWOPT_INITIATOR_MODE);
	init_fw_cb->FwOptions &= __constant_cpu_to_le16(~FWOPT_TARGET_MODE);

	/* Save some info in adapter structure. */
	ha->firmware_options = le16_to_cpu(init_fw_cb->FwOptions);
	ha->tcp_options = le16_to_cpu(init_fw_cb->TCPOptions);
	ha->heartbeat_interval = init_fw_cb->HeartbeatInterval;
	ha->isns_server_port_number =
	    le16_to_cpu(init_fw_cb->iSNSServerPortNumber);
	memcpy(ha->ip_address, init_fw_cb->IPAddr,
	    min(sizeof(ha->ip_address), sizeof(init_fw_cb->IPAddr)));
	memcpy(ha->subnet_mask, init_fw_cb->SubnetMask,
	    min(sizeof(ha->subnet_mask), sizeof(init_fw_cb->SubnetMask)));
	memcpy(ha->isns_ip_address, init_fw_cb->iSNSIPAddr,
	    min(sizeof(ha->isns_ip_address), sizeof(init_fw_cb->iSNSIPAddr)));
	memcpy(ha->name_string, init_fw_cb->iSCSINameString,
	    min(sizeof(ha->name_string), sizeof(init_fw_cb->iSCSINameString)));
	memcpy(ha->alias, init_fw_cb->Alias,
	    min(sizeof(ha->alias), sizeof(init_fw_cb->Alias)));

	/* Save Command Line Paramater info */
	ha->port_down_retry_count = le16_to_cpu(init_fw_cb->KeepAliveTimeout);
	ha->discovery_wait = ql4xdiscoverywait;

	/* Send Initialize Firmware Control Block. */
	mbox_cmd[0] = MBOX_CMD_INITIALIZE_FIRMWARE;
	mbox_cmd[1] = 0;
	mbox_cmd[2] = LSDW(init_fw_cb_dma);
	mbox_cmd[3] = MSDW(init_fw_cb_dma);
	if (qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_SUCCESS) {
		status = QLA_SUCCESS;
	} else {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_INITIALIZE_FIRMWARE "
		    "failed w/ status %04X\n", ha->host_no, __func__,
		    mbox_sts[0]));
	}
	dma_free_coherent(&ha->pdev->dev, sizeof(INIT_FW_CTRL_BLK), init_fw_cb,
	    init_fw_cb_dma);

	return status;
}

/**************************************************************************
 * qla4xxx_get_dhcp_ip_address
 *	This routine retrieves the IP Address obtained via DHCP for the
 *	specified adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully obtained DHCP IP Address
 *	QLA_ERROR   - Failed to obtained
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_get_dhcp_ip_address(scsi_qla_host_t * ha)
{
	INIT_FW_CTRL_BLK *init_fw_cb;
	dma_addr_t init_fw_cb_dma;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	init_fw_cb = dma_alloc_coherent(&ha->pdev->dev,
	    sizeof(INIT_FW_CTRL_BLK), &init_fw_cb_dma, GFP_KERNEL);
	if (init_fw_cb == NULL) {
		printk("scsi%ld: %s: Unable to alloc init_cb\n", ha->host_no,
		    __func__);
		return 10;
	}

	/* Get Initialize Firmware Control Block. */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	memset(init_fw_cb, 0, sizeof(INIT_FW_CTRL_BLK));
	mbox_cmd[0] = MBOX_CMD_GET_INIT_FW_CTRL_BLOCK;
	mbox_cmd[2] = LSDW(init_fw_cb_dma);
	mbox_cmd[3] = MSDW(init_fw_cb_dma);

	if (qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: Failed to get init_fw_ctrl_blk\n",
		    ha->host_no, __func__));
		dma_free_coherent(&ha->pdev->dev, sizeof(INIT_FW_CTRL_BLK),
		    init_fw_cb, init_fw_cb_dma);
		return QLA_ERROR;
	}

	/* Save IP Address. */
	memcpy(ha->ip_address, init_fw_cb->IPAddr,
	    min(sizeof(ha->ip_address), sizeof(init_fw_cb->IPAddr)));
	memcpy(ha->subnet_mask, init_fw_cb->SubnetMask,
	    min(sizeof(ha->subnet_mask), sizeof(init_fw_cb->SubnetMask)));

	dma_free_coherent(&ha->pdev->dev, sizeof(INIT_FW_CTRL_BLK), init_fw_cb,
	    init_fw_cb_dma);

	return QLA_SUCCESS;
}

/**************************************************************************
 * qla4xxx_get_firmware_state
 *	This routine retrieves the firmware state for the specified adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully retrieved firmware state
 *	QLA_ERROR   - Failed to retrieve firmware state
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_get_firmware_state(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Get firmware version */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_FW_STATE;
	if (qla4xxx_mailbox_command(ha, 1, 4, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_GET_FW_STATE failed w/ "
		    "status %04X\n", ha->host_no, __func__, mbox_sts[0]));
		return QLA_ERROR;
	}
	ha->firmware_state = mbox_sts[1];
	ha->board_id = mbox_sts[2];
	ha->addl_fw_state = mbox_sts[3];
	DEBUG2(printk("%s firmware_state=0x%x\n", __func__,
	    ha->firmware_state));

	return QLA_SUCCESS;
}

/**************************************************************************
 * qla4xxx_get_firmware_status
 *	This routine retrieves the firmware status for the specified adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully retrieved firmware status
 *	QLA_ERROR   - Failed to retrieve firmware status
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_get_firmware_status(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Get firmware version */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_FW_STATUS;
	if (qla4xxx_mailbox_command(ha, 1, 3, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_GET_FW_STATUS failed w/ "
		    "status %04X\n", ha->host_no, __func__, mbox_sts[0]));
		return QLA_ERROR;
	}

	/* High-water mark of IOCBs */
	ha->iocb_hiwat = mbox_sts[2];
	if (ha->iocb_hiwat > IOCB_HIWAT_CUSHION)
		ha->iocb_hiwat -= IOCB_HIWAT_CUSHION;
	else
		ql4_printk(KERN_INFO, ha, "WARNING!!!  You have less than %d "
		    "firmare IOCBs available (%d).\n", IOCB_HIWAT_CUSHION,
		    ha->iocb_hiwat);

	return QLA_SUCCESS;
}

/**************************************************************************
 * qla4xxx_get_fwddb_entry
 *	This routine retrieves the firmware's device database entry.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *      fw_ddb_index - Firmware's device database index
 *      fw_ddb_entry - Pointer to firmware's device database entry structure
 *      num_valid_ddb_entries - Pointer to number of valid ddb entries
 *      next_ddb_index - Pointer to next valid device database index
 *      fw_ddb_device_state - Pointer to device state
 *
 * Output:
 *      fw_ddb_entry - Fills in structure if pointer is supplied
 *      num_valid_ddb_entries - Fills in if pointer is supplied
 *      next_ddb_index - Fills in if pointer is supplied
 *      fw_ddb_device_state - Fills in if pointer is supplied
 *
 * Returns:
 *	QLA_SUCCESS - Successfully retrieved ddb info from firmware
 *	QLA_ERROR   - Failed to retrieve ddb info from firmware
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_get_fwddb_entry(scsi_qla_host_t *ha, uint16_t fw_ddb_index,
    DEV_DB_ENTRY *fw_ddb_entry, dma_addr_t fw_ddb_entry_dma,
    uint32_t *num_valid_ddb_entries, uint32_t *next_ddb_index,
    uint32_t *fw_ddb_device_state, uint32_t *conn_err_detail,
    uint16_t *tcp_source_port_num, uint16_t *connection_id)
{
	int status = QLA_ERROR;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Make sure the device index is valid */
	if (fw_ddb_index >= MAX_DDB_ENTRIES) {
		DEBUG2(printk("scsi%ld: %s: index [%d] out of range.\n",
		    ha->host_no, __func__, fw_ddb_index));
		goto exit_get_fwddb;
	}
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_DATABASE_ENTRY;
	mbox_cmd[1] = (uint32_t) fw_ddb_index;
	mbox_cmd[2] = LSDW(fw_ddb_entry_dma);
	mbox_cmd[3] = MSDW(fw_ddb_entry_dma);
	if (qla4xxx_mailbox_command(ha, 4, 7, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_GET_DATABASE_ENTRY failed "
		    "with status 0x%04X\n", ha->host_no, __func__,
		    mbox_sts[0]));
		goto exit_get_fwddb;
	}
	if (fw_ddb_index != mbox_sts[1]) {
		DEBUG2(printk("scsi%ld: %s: index mismatch [%d] != [%d].\n",
		    ha->host_no, __func__, fw_ddb_index, mbox_sts[1]));
		goto exit_get_fwddb;
	}
	if (fw_ddb_entry) {
		ql4_printk(KERN_INFO, ha, "DDB[%d] MB0 %04x Tot %d Next %d "
		    "State %04x ConnErr %08x %d.%d.%d.%d:%04d \"%s\"\n",
		    fw_ddb_index, mbox_sts[0], mbox_sts[2], mbox_sts[3],
		    mbox_sts[4], mbox_sts[5], fw_ddb_entry->ipAddr[0],
		    fw_ddb_entry->ipAddr[1], fw_ddb_entry->ipAddr[2],
		    fw_ddb_entry->ipAddr[3],
		    le16_to_cpu(fw_ddb_entry->portNumber),
		    fw_ddb_entry->iscsiName);
	}
	if (num_valid_ddb_entries)
		*num_valid_ddb_entries = mbox_sts[2];
	if (next_ddb_index)
		*next_ddb_index = mbox_sts[3];
	if (fw_ddb_device_state)
		*fw_ddb_device_state = mbox_sts[4];

	/*
	 * RA: This mailbox has been changed to pass connection error and
	 * details.  Its true for ISP4010 as per Version E - Not sure when it
	 * was changed.  Get the time2wait from the fw_dd_entry field :
	 * default_time2wait which we call it as minTime2Wait DEV_DB_ENTRY
	 * struct.
	 */
	if (conn_err_detail)
		*conn_err_detail = mbox_sts[5];
	if (tcp_source_port_num)
		*tcp_source_port_num = (uint16_t) mbox_sts[6] >> 16;
	if (connection_id)
		*connection_id = (uint16_t) mbox_sts[6] & 0x00FF;
	status = QLA_SUCCESS;

exit_get_fwddb:
	return status;
}

/**************************************************************************
 * qla4xxx_set_fwddb_entry
 *	This routine initializes or updates the adapter's device database
 *	entry for the specified device.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *      fw_ddb_index - Firmware's device database index
 *      fw_ddb_entry - Pointer to firmware's device database entry
 *		       structure, or NULL.
 *
 * Output:
 *	None
 *
 * Remarks:
 *	This routine also triggers a login for the specified device.
 *	Therefore, it may also be used as a secondary login routine when
 *	a NULL pointer is specified for the fw_ddb_entry.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully set ddb_entry in firmware
 *	QLA_ERROR   - Failed to set ddb_entry in firmware
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_set_ddb_entry(scsi_qla_host_t * ha, uint16_t fw_ddb_index,
    DEV_DB_ENTRY * fw_ddb_entry, dma_addr_t fw_ddb_entry_dma)
{
	int status = QLA_ERROR;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Do not wait for completion. The firmware will send us an
	 * ASTS_DATABASE_CHANGED (0x8014) to notify us of the login status.
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_SET_DATABASE_ENTRY;
	mbox_cmd[1] = (uint32_t) fw_ddb_index;
	mbox_cmd[2] = LSDW(fw_ddb_entry_dma);
	mbox_cmd[3] = MSDW(fw_ddb_entry_dma);
	status = qla4xxx_mailbox_command(ha, 4, 1, &mbox_cmd[0], &mbox_sts[0]);
	return status;
}

int
qla4xxx_conn_open_session_login(scsi_qla_host_t * ha, uint16_t fw_ddb_index)
{
	int status = QLA_ERROR;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Do not wait for completion. The firmware will send us an
	 * ASTS_DATABASE_CHANGED (0x8014) to notify us of the login status.
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_CONN_OPEN_SESS_LOGIN;
	mbox_cmd[1] = (uint32_t) fw_ddb_index;
	mbox_cmd[2] = 0;
	mbox_cmd[3] = 0;
	mbox_cmd[4] = 0;
	status = qla4xxx_mailbox_command(ha, 4, 0, &mbox_cmd[0], &mbox_sts[0]);
	DEBUG2(printk("%s fw_ddb_index=%d status=%d mbx0_1=0x%x :0x%x\n",
	    __func__, fw_ddb_index, status, mbox_sts[0], mbox_sts[1]);)

	return status;
}

/**************************************************************************
 * qla4xxx_get_crash_record
 *	This routine retrieves a crash record from the QLA4010 after an
 *	8002h aen.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	None
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
void
qla4xxx_get_crash_record(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	CRASH_RECORD *crash_record = NULL;
	dma_addr_t crash_record_dma = 0;
	uint32_t crash_record_size = 0;
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_cmd));

	/* Get size of crash record. */
	mbox_cmd[0] = MBOX_CMD_GET_CRASH_RECORD;
	if (qla4xxx_mailbox_command(ha, 5, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: ERROR: Unable to retrieve size!\n",
		    ha->host_no, __func__));
		goto exit_get_crash_record;
	}
	crash_record_size = mbox_sts[4];
	if (crash_record_size == 0) {
		DEBUG2(printk("scsi%ld: %s: ERROR: Crash record size is 0!\n",
		    ha->host_no, __func__));
		goto exit_get_crash_record;
	}

	/* Alloc Memory for Crash Record. */
	crash_record = dma_alloc_coherent(&ha->pdev->dev, crash_record_size,
	    &crash_record_dma, GFP_KERNEL);
	if (crash_record == NULL)
		goto exit_get_crash_record;

	/* Get Crash Record. */
	mbox_cmd[0] = MBOX_CMD_GET_CRASH_RECORD;
	mbox_cmd[2] = LSDW(crash_record_dma);
	mbox_cmd[3] = MSDW(crash_record_dma);
	mbox_cmd[4] = crash_record_size;
	if (qla4xxx_mailbox_command(ha, 5, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS)
		goto exit_get_crash_record;

	/* Dump Crash Record. */

exit_get_crash_record:
	if (crash_record)
		dma_free_coherent(&ha->pdev->dev, crash_record_size,
		    crash_record, crash_record_dma);
}

/**************************************************************************
 * qla4xxx_get_conn_event_log
 *	This routine retrieves the connection event log
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Returns:
 *	None
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
void
qla4xxx_get_conn_event_log(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	CONN_EVENT_LOG_ENTRY *event_log = NULL;
	dma_addr_t event_log_dma = 0;
	uint32_t event_log_size = 0;
	uint32_t num_valid_entries;
	/*uint32_t      oldest_entry = 0; */

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_cmd));

	/* Get size of crash record. */
	mbox_cmd[0] = MBOX_CMD_GET_CONN_EVENT_LOG;
	if (qla4xxx_mailbox_command(ha, 4, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS)
		goto exit_get_event_log;

	event_log_size = mbox_sts[4];
	if (event_log_size == 0)
		goto exit_get_event_log;

	/* Alloc Memory for Crash Record. */
	event_log = dma_alloc_coherent(&ha->pdev->dev, event_log_size,
	    &event_log_dma, GFP_KERNEL);
	if (event_log == NULL)
		goto exit_get_event_log;

	/* Get Crash Record. */
	mbox_cmd[0] = MBOX_CMD_GET_CONN_EVENT_LOG;
	mbox_cmd[2] = LSDW(event_log_dma);
	mbox_cmd[3] = MSDW(event_log_dma);
	if (qla4xxx_mailbox_command(ha, 4, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: ERROR: Unable to retrieve event "
		    "log!\n", ha->host_no, __func__));
		goto exit_get_event_log;
	}

	/* Dump Event Log. */
	num_valid_entries = mbox_sts[1];

	//FIXME:  Code wraparound entries later ...
	//if (num_valid_entries > MAX_CONN_EVENT_LOG_ENTRIES)
	//      oldest_entry = num_valid_entries % MAX_CONN_EVENT_LOG_ENTRIES;
exit_get_event_log:
	if (event_log)
		dma_free_coherent(&ha->pdev->dev, event_log_size, event_log,
		    event_log_dma);
}

/**************************************************************************
 * qla4xxx_reset_lun
 *	This routine performs a LUN RESET on the specified target/lun.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	ddb_entry - Pointer to device database entry
 *	lun_entry - Pointer to lun entry structure
 *
 * Remarks:
 *	The caller must ensure that the ddb_entry and lun_entry pointers
 *	are valid before calling this routine.
 *
 * Returns:
 *	QLA_SUCCESS - lun reset completed successfully
 *	QLA_ERROR   - lun reset failed
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_reset_lun(scsi_qla_host_t * ha, ddb_entry_t * ddb_entry, int lun)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_SUCCESS;

	DEBUG2(printk("scsi%ld:%d:%d: lun reset issued\n", ha->host_no,
	    ddb_entry->os_target_id, lun));

	/*
	 * Send lun reset command to ISP, so that the ISP will return all
	 * outstanding requests with RESET status
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_LUN_RESET;
	mbox_cmd[1] = ddb_entry->fw_ddb_index;
	mbox_cmd[2] = lun << 8;
	mbox_cmd[5] = 0x01;	/* Immediate Command Enable */
	qla4xxx_mailbox_command(ha, 6, 1, &mbox_cmd[0], &mbox_sts[0]);
	if (mbox_sts[0] != MBOX_STS_COMMAND_COMPLETE &&
	    mbox_sts[0] != MBOX_STS_COMMAND_ERROR)
		status = QLA_ERROR;
/*FIXME*/
	/*spin_lock_irq(ha->host->host_lock);*/
	return status;
}

int
qla4xxx_isns_enable(scsi_qla_host_t * ha, uint32_t isns_ip_addr,
    uint16_t isns_server_port_num)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	qla4xxx_isns_build_entity_id(ha);
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_SET_ISNS_SERVICE;
	mbox_cmd[1] = ISNS_ENABLE;
	mbox_cmd[2] = isns_ip_addr;
	mbox_cmd[3] = isns_server_port_num;
	if (qla4xxx_mailbox_command(ha, 4, 6, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS)
		return (QLA_ERROR);

	DEBUG2(printk("scsi%ld: Start iSNS Service %d.%d.%d.%d Port %04d...\n",
	    ha->host_no, (isns_ip_addr & 0x000000FF),
	    (isns_ip_addr & 0x0000FF00) >> 8,
	    (isns_ip_addr & 0x00FF0000) >> 16,
	    (isns_ip_addr & 0xFF000000) >> 24, isns_server_port_num));

	return QLA_SUCCESS;
}

int
qla4xxx_isns_disable(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	if (test_bit(ISNS_FLAG_ISNS_SRV_ENABLED, &ha->isns_flags)) {
		memset(&mbox_cmd, 0, sizeof(mbox_cmd));
		memset(&mbox_sts, 0, sizeof(mbox_sts));
		mbox_cmd[0] = MBOX_CMD_SET_ISNS_SERVICE;
		mbox_cmd[1] = ISNS_DISABLE;
		if (qla4xxx_mailbox_command(ha, 2, 2, &mbox_cmd[0],
		    &mbox_sts[0]) != QLA_SUCCESS) {
			DEBUG2(printk("scsi%ld: %s: MBOX_CMD_SET_ISNS_SERVICE "
			    "failed w/ status %04X %04X\n", ha->host_no,
			    __func__, mbox_sts[0], mbox_sts[1]));
			return QLA_ERROR;
		}
	}
	clear_bit(ISNS_FLAG_ISNS_SRV_ENABLED, &ha->isns_flags);
	ISNS_CLEAR_FLAGS(ha);
	ha->isns_connection_id = 0;

	//ha->isns_scn_conn_id     = 0;
	//ha->isns_esi_conn_id     = 0;
	//ha->isns_nsh_conn_id     = 0;
	ha->isns_remote_port_num = 0;
	ha->isns_scn_port_num = 0;
	ha->isns_esi_port_num = 0;
	ha->isns_nsh_port_num = 0;
	ha->isns_num_discovered_targets = 0;
	memset(ha->isns_entity_id, 0, sizeof(ha->isns_entity_id));

	return QLA_SUCCESS;
}

int
qla4xxx_isns_status(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_SET_ISNS_SERVICE;
	mbox_cmd[1] = ISNS_STATUS;
	if (qla4xxx_mailbox_command(ha, 2, 2, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_SET_ISNS_SERVICE failed "
		    "w/ status %04X %04X\n", ha->host_no, __func__,
		    mbox_sts[0], mbox_sts[1]));
		return QLA_ERROR;
	}
	return QLA_SUCCESS;
}

int
qla4xxx_get_flash(scsi_qla_host_t * ha, dma_addr_t dma_addr, uint32_t offset,
    uint32_t len)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_READ_FLASH;
	mbox_cmd[1] = LSDW(dma_addr);
	mbox_cmd[2] = MSDW(dma_addr);
	mbox_cmd[3] = offset;
	mbox_cmd[4] = len;
	if (qla4xxx_mailbox_command(ha, 5, 2, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_READ_FLASH, failed w/ "
		    "status %04X %04X, offset %08x, len %08x\n", ha->host_no,
		    __func__, mbox_sts[0], mbox_sts[1], offset, len));
		return QLA_ERROR;
	}
	return QLA_SUCCESS;
}

/**************************************************************************
 * qla4xxx_get_fw_version
 *	This routine retrieves the firmware version for the specified adapter.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *
 * Output:
 *	None
 *
 * Remarks:
 *	In QLA4010, mailboxes 2 & 3 may hold an address for data.  Make sure
 *	that we write 0 to those mailboxes, if unused.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully retrieved firmware version
 *	QLA_ERROR   - Failed to retrieve firmware version
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4xxx_get_fw_version(scsi_qla_host_t * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Get firmware version. */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_ABOUT_FW;
	if (qla4xxx_mailbox_command(ha, 4, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_ABOUT_FW failed w/ "
		    "status %04X\n", ha->host_no, __func__, mbox_sts[0]));
		return QLA_ERROR;
	}

	/* Save firmware version information. */
	ha->firmware_version[0] = mbox_sts[1];
	ha->firmware_version[1] = mbox_sts[2];
	ha->patch_number = mbox_sts[3];
	ha->build_number = mbox_sts[4];

	return QLA_SUCCESS;
}
