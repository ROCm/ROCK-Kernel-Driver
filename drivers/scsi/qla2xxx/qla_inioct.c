/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

#include "qla_def.h"

#include <linux/delay.h>
#include <asm/uaccess.h>

#include "exioct.h"
#include "inioct.h"

static int qla2x00_read_option_rom_ext(scsi_qla_host_t *, EXT_IOCTL *, int);
static int qla2x00_update_option_rom_ext(scsi_qla_host_t *, EXT_IOCTL *, int);
static void qla2x00_get_option_rom_table(scsi_qla_host_t *,
    INT_OPT_ROM_REGION **, unsigned long *);

/* Option ROM definitions. */
INT_OPT_ROM_REGION OptionRomTable6312[] = // 128k x20000
{
    {INT_OPT_ROM_REGION_ALL,	INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_PHBIOS,	0xC400,
	    0, 0xC400-1},
    {INT_OPT_ROM_REGION_BCFW,	INT_OPT_ROM_SIZE_2312-0xC400-0x200,
	    0xC400, INT_OPT_ROM_SIZE_2312-0x200-1},
    {INT_OPT_ROM_REGION_VPD,	0x200,
	    INT_OPT_ROM_SIZE_2312-0x200, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_NONE,	0,	0,	0}
};
// ISP2312 Version 3 Chip.
INT_OPT_ROM_REGION OptionRomTable6826A[] = // 128k x20000
{
    {INT_OPT_ROM_REGION_ALL,	INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_PHEFI_PHECFW_PHVPD,	INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_NONE,	0,	0,	0}
};

INT_OPT_ROM_REGION OptionRomTable2312[] = // 128k x20000
{
    {INT_OPT_ROM_REGION_ALL,	INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_PHBIOS_FCODE_EFI_FW,	INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_NONE,	0,	0,	0}
};

INT_OPT_ROM_REGION OptionRomTable2322[] = // 1 M x100000
{
    {INT_OPT_ROM_REGION_ALL, INT_OPT_ROM_SIZE_2322,
	    0, INT_OPT_ROM_SIZE_2322-1},
    {INT_OPT_ROM_REGION_PHBIOS_PHFCODE_PHEFI, 0x40000,
	    0, 0x40000-1},
    {INT_OPT_ROM_REGION_VPD,	0x200,
	    0x40000, 0x40000+0x200-1},
    {INT_OPT_ROM_REGION_FW1,	0x80000,
	    0x80000, INT_OPT_ROM_SIZE_2322-1},
    {INT_OPT_ROM_REGION_NONE,	0,	0,	0}
};

/* ========================================================================= */

int
qla2x00_read_nvram(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int	ret = 0;
	char	*ptmp_buf;
	uint16_t cnt;
 	uint16_t *wptr;
	uint32_t transfer_size;

	DEBUG9(printk("qla2x00_read_nvram: entered.\n");)

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&ptmp_buf,
	    sizeof(nvram_t))) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%d.\n",
		    __func__, ha->host_no, ha->instance,
		    sizeof(nvram_t));)
		return (ret);
	}

	if (pext->ResponseLen < sizeof(nvram_t))
		transfer_size = pext->ResponseLen / 2;
	else
		transfer_size = sizeof(nvram_t) / 2;

	/* Dump NVRAM. */
	qla2x00_lock_nvram_access(ha);

 	wptr = (uint16_t *)ptmp_buf;
 	for (cnt = 0; cnt < transfer_size; cnt++) {
		*wptr = cpu_to_le16(qla2x00_get_nvram_word(ha,
		    cnt+ha->nvram_base));
		wptr++;
 	}
	qla2x00_unlock_nvram_access(ha);

	ret = copy_to_user((uint8_t *)pext->ResponseAdr, ptmp_buf,
	    transfer_size * 2);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s(%ld): inst=%ld ERROR copy rsp buffer.\n",
		    __func__, ha->host_no, ha->instance);)
		qla2x00_free_ioctl_scrap_mem(ha);
		return (ret);
	}

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("qla2x00_read_nvram: exiting.\n");)

	return (ret);
}

/*
 * qla2x00_update_nvram
 *	Write data to NVRAM.
 *
 * Input:
 *	ha = adapter block pointer.
 *	pext = pointer to driver internal IOCTL structure.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_update_nvram(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	device_reg_t	*reg = ha->iobase;
	uint8_t i, cnt;
	uint8_t *usr_tmp, *kernel_tmp;
	nvram_t *pnew_nv;
	uint16_t *wptr;
	uint16_t data;
	uint32_t transfer_size;
	uint8_t chksum = 0;
	int ret = 0;

	DEBUG9(printk("qla2x00_update_nvram: entered.\n");)

	if (pext->RequestLen < sizeof(nvram_t))
		transfer_size = pext->RequestLen;
	else
		transfer_size = sizeof(nvram_t);

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&pnew_nv,
	    sizeof(nvram_t))) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(nvram_t));)
		return (ret);
	}

	/* Read from user buffer */
	kernel_tmp = (uint8_t *)pnew_nv;
	usr_tmp = (uint8_t *)pext->RequestAdr;

	ret = copy_from_user(kernel_tmp, usr_tmp, transfer_size);
	if (ret) {
		DEBUG9_10(printk(
		    "qla2x00_update_nvram: ERROR in buffer copy READ. "
		    "RequestAdr=%p\n", pext->RequestAdr);)
		qla2x00_free_ioctl_scrap_mem(ha);
		return ret;
	}

	kernel_tmp = (uint8_t *)pnew_nv;

	/* we need to checksum the nvram */
	for (i = 0; i < sizeof(nvram_t) - 1; i++) {
		chksum += *kernel_tmp;
		kernel_tmp++;
	}

	chksum = ~chksum + 1;

	*kernel_tmp = chksum;

	/* Write to NVRAM */
	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha)) {
		data = RD_REG_WORD(&reg->nvram);
		while (data & NVR_BUSY) {
			udelay(100);
			data = RD_REG_WORD(&reg->nvram);
		}

		/* Lock resource */
		WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0x1);
		udelay(5);
		data = RD_REG_WORD(&reg->u.isp2300.host_semaphore);
		while ((data & BIT_0) == 0) {
			/* Lock failed */
			udelay(100);
			WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0x1);
			udelay(5);
			data = RD_REG_WORD(&reg->u.isp2300.host_semaphore);
		}
	}

	wptr = (uint16_t *)pnew_nv;
	for (cnt = 0; cnt < transfer_size / 2; cnt++) {
		data = cpu_to_le16(*wptr++);
		qla2x00_write_nvram_word(ha, cnt+ha->nvram_base, data);
	}

	/* Unlock resource */
	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha))
		WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0);

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("qla2x00_update_nvram: exiting.\n");)

	/* Schedule DPC to restart the RISC */
	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	up(ha->dpc_wait);

	return qla2x00_wait_for_hba_online(ha);
}

static int
qla2x00_loopback_test(scsi_qla_host_t *ha, INT_LOOPBACK_REQ *req,
    uint16_t *ret_mb)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	DEBUG11(printk("qla2x00_send_loopback: req.Options=%x iterations=%x "
	    "MAILBOX_CNT=%d.\n", req->Options, req->IterationCount,
	    MAILBOX_REGISTER_COUNT);)

	memset(mcp->mb, 0 , sizeof(mcp->mb));

	mcp->mb[0] = MBC_DIAGNOSTIC_LOOP_BACK;
	mcp->mb[1] = req->Options | MBX_6;
	mcp->mb[10] = LSW(req->TransferCount);
	mcp->mb[11] = MSW(req->TransferCount);
	mcp->mb[14] = LSW(ha->ioctl_mem_phys); /* send data address */
	mcp->mb[15] = MSW(ha->ioctl_mem_phys);
	mcp->mb[20] = LSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[21] = MSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[16] = LSW(ha->ioctl_mem_phys); /* rcv data address */
	mcp->mb[17] = MSW(ha->ioctl_mem_phys);
	mcp->mb[6] = LSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[7] = MSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[18] = LSW(req->IterationCount); /* iteration count lsb */
	mcp->mb[19] = MSW(req->IterationCount); /* iteration count msb */
	mcp->out_mb = MBX_21|MBX_20|MBX_19|MBX_18|MBX_17|MBX_16|MBX_15|
	    MBX_14|MBX_13|MBX_12|MBX_11|MBX_10|MBX_7|MBX_6|MBX_1|MBX_0;
	mcp->in_mb = MBX_19|MBX_18|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->buf_size = req->TransferCount;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
	mcp->tov = 30;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Always copy back return mailbox values. */
	memcpy((void *)ret_mb, (void *)mcp->mb, sizeof(mcp->mb));

	if (rval != QLA_SUCCESS) {
		/* Empty. */
		DEBUG2_3_11(printk(
		    "qla2x00_loopback_test(%ld): mailbox command FAILED=%x.\n",
		    ha->host_no, mcp->mb[0]);)
	} else {
		/* Empty. */
		DEBUG11(printk(
		    "qla2x00_loopback_test(%ld): done.\n", ha->host_no);)
	}

	return rval;
}

static int
qla2x00_echo_test(scsi_qla_host_t *ha, INT_LOOPBACK_REQ *req, uint16_t *ret_mb)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	memset(mcp->mb, 0 , sizeof(mcp->mb));

	mcp->mb[0] = MBC_DIAGNOSTIC_ECHO;
	mcp->mb[1] = BIT_6; /* use 64bit DMA addr */
	mcp->mb[10] = req->TransferCount;
	mcp->mb[14] = LSW(ha->ioctl_mem_phys); /* send data address */
	mcp->mb[15] = MSW(ha->ioctl_mem_phys);
	mcp->mb[20] = LSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[21] = MSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[16] = LSW(ha->ioctl_mem_phys); /* rcv data address */
	mcp->mb[17] = MSW(ha->ioctl_mem_phys);
	mcp->mb[6] = LSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[7] = MSW(MSD(ha->ioctl_mem_phys));
	mcp->out_mb = MBX_21|MBX_20|MBX_17|MBX_16|MBX_15|MBX_14|MBX_10|
	    MBX_7|MBX_6|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->buf_size = req->TransferCount;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
	mcp->tov = 30;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Always copy back return mailbox values. */
	memcpy((void *)ret_mb, (void *)mcp->mb, sizeof(mcp->mb));

	if (rval != QLA_SUCCESS) {
		/* Empty. */
		DEBUG2_3_11(printk(
		    "%s(%ld): mailbox command FAILED=%x.\n", __func__,
		    ha->host_no, mcp->mb[0]);)
	} else {
		/* Empty. */
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

	return rval;
}

int
qla2x00_send_loopback(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		status;
	uint16_t	ret_mb[MAILBOX_REGISTER_COUNT];
	INT_LOOPBACK_REQ req;
	INT_LOOPBACK_RSP rsp;

	DEBUG9(printk("qla2x00_send_loopback: entered.\n");)


	if (pext->RequestLen != sizeof(INT_LOOPBACK_REQ)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG9_10(printk(
		    "qla2x00_send_loopback: invalid RequestLen =%d.\n",
		    pext->RequestLen);)
		return pext->Status;
	}

	if (pext->ResponseLen != sizeof(INT_LOOPBACK_RSP)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG9_10(printk(
		    "qla2x00_send_loopback: invalid ResponseLen =%d.\n",
		    pext->ResponseLen);)
		return pext->Status;
	}

	status = copy_from_user(&req, pext->RequestAdr, pext->RequestLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR copy read of "
		    "request buffer.\n");)
		return pext->Status;
	}

	status = copy_from_user(&rsp, pext->ResponseAdr, pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR copy read of "
		    "response buffer.\n");)
		return pext->Status;
	}

	if (req.TransferCount > req.BufferLength ||
	    req.TransferCount > rsp.BufferLength) {

		/* Buffer lengths not large enough. */
		pext->Status = EXT_STATUS_INVALID_PARAM;

		DEBUG9_10(printk(
		    "qla2x00_send_loopback: invalid TransferCount =%d. "
		    "req BufferLength =%d rspBufferLength =%d.\n",
		    req.TransferCount, req.BufferLength, rsp.BufferLength);)

		return pext->Status;
	}

	status = copy_from_user(ha->ioctl_mem, req.BufferAddress,
	    req.TransferCount);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR copy read of "
		    "user loopback data buffer.\n");)
		return pext->Status;
	}


	DEBUG9(printk("qla2x00_send_loopback: req -- bufadr=%p, buflen=%x, "
	    "xfrcnt=%x, rsp -- bufadr=%p, buflen=%x.\n",
	    req.BufferAddress, req.BufferLength, req.TransferCount,
	    rsp.BufferAddress, rsp.BufferLength);)

	/*
	 * AV - the caller of this IOCTL expects the FW to handle
	 * a loopdown situation and return a good status for the
	 * call function and a LOOPDOWN status for the test operations
	 */
	/*if (atomic_read(&ha->loop_state) != LOOP_READY || */
	if (test_bit(CFG_ACTIVE, &ha->cfg_flags) ||
	    test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags) ||
	    test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) || ha->dpc_active) {

		pext->Status = EXT_STATUS_BUSY;
		DEBUG9_10(printk("qla2x00_send_loopback(%ld): "
		    "loop not ready.\n", ha->host_no);)
		return pext->Status;
	}

	if (ha->current_topology == ISP_CFG_F) {
		if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
			pext->Status = EXT_STATUS_INVALID_REQUEST ;
			DEBUG9_10(printk("qla2x00_send_loopback: ERROR "
			    "command only supported for QLA23xx.\n");)
			return 0;
		}
		status = qla2x00_echo_test(ha, &req, ret_mb);
	} else {
		status = qla2x00_loopback_test(ha, &req, ret_mb);
	}

	if (status) {
		if (status == QLA_FUNCTION_TIMEOUT ) {
			pext->Status = EXT_STATUS_BUSY;
			DEBUG9_10(printk("qla2x00_send_loopback: ERROR "
			    "command timed out.\n");)
			return 0;
		} else {
			/* EMPTY. Just proceed to copy back mailbox reg
			 * values for users to interpret.
			 */
			DEBUG10(printk("qla2x00_send_loopback: ERROR "
			    "loopback command failed 0x%x.\n", ret_mb[0]);)
		}
	}

	DEBUG9(printk("qla2x00_send_loopback: loopback mbx cmd ok. "
	    "copying data.\n");)

	/* put loopback return data in user buffer */
	status = copy_to_user(rsp.BufferAddress, ha->ioctl_mem,
	    req.TransferCount);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR copy "
		    "write of return data buffer.\n");)
		return status;
	}

	rsp.CompletionStatus = ret_mb[0];
	if (ha->current_topology == ISP_CFG_F) {
		rsp.CommandSent = INT_DEF_LB_ECHO_CMD;
	} else {
		if (rsp.CompletionStatus == INT_DEF_LB_COMPLETE ||
		    rsp.CompletionStatus == INT_DEF_LB_CMD_ERROR) {
			rsp.CrcErrorCount = ret_mb[1];
			rsp.DisparityErrorCount = ret_mb[2];
			rsp.FrameLengthErrorCount = ret_mb[3];
			rsp.IterationCountLastError =
			    (ret_mb[19] << 16) | ret_mb[18];
		}
	}

	status = copy_to_user(pext->ResponseAdr, &rsp, pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR copy "
		    "write of response buffer.\n");)
		return pext->Status;
	}


	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG9(printk("qla2x00_send_loopback: exiting.\n");)

	return pext->Status;
}

int
qla2x00_read_option_rom(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	if (pext->SubCode)
		return qla2x00_read_option_rom_ext(ha, pext, mode);

	DEBUG9(printk("%s: entered.\n", __func__);)

	/* The ISP2312 v2 chip cannot access the FLASH registers via MMIO. */
	if (IS_QLA2312(ha) && ha->product_id[3] == 0x2 && !ha->pio_address) {
		pext->Status = EXT_STATUS_INVALID_REQUEST;
		return (1);
	}

	if (pext->ResponseLen != FLASH_IMAGE_SIZE) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		return (1);
	}

	/* Dump FLASH. */
 	qla2x00_update_or_read_flash(ha, (uint8_t *)pext->ResponseAdr, 0,
	    FLASH_IMAGE_SIZE, QLA2X00_READ); 

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return 0;
}

int
qla2x00_read_option_rom_ext(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		iter, found;
	uint32_t	saddr, length;

	DEBUG9(printk("%s: entered.\n", __func__);)

	found = 0;
	saddr = length = 0;
	/* Retrieve region or raw starting address. */
	if (pext->SubCode == 0xFFFF) {
		saddr = pext->Reserved1;
		length = pext->RequestLen;
		found++;
	} else {
		INT_OPT_ROM_REGION *OptionRomTable = NULL;
		unsigned long OptionRomTableSize;
		/* Pick the right OptionRom table based on device id */
		qla2x00_get_option_rom_table(ha, &OptionRomTable,
		    &OptionRomTableSize);
		for (iter = 0; OptionRomTable != NULL && iter <
		    (OptionRomTableSize / sizeof(INT_OPT_ROM_REGION));
		    iter++) {
			if (OptionRomTable[iter].Region == pext->SubCode) {
				saddr = OptionRomTable[iter].Beg;
				length = OptionRomTable[iter].Size;
				found++;
				break;
			}
		}
	}
	if (!found) {
		pext->Status = EXT_STATUS_ERR;
		return 1;
	}
	if (pext->ResponseLen < length) {
		pext->Status = EXT_STATUS_COPY_ERR;
		return 1;
	}

	/* Dump FLASH. */
 	qla2x00_update_or_read_flash(ha, (uint8_t *)pext->ResponseAdr, saddr,
	    length, QLA2X00_READ); 

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return 0;
}

int
qla2x00_update_option_rom(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		ret;
	uint8_t		*usr_tmp;
	uint8_t		*kern_tmp;
	uint16_t	status;

	if (pext->SubCode)
		return qla2x00_update_option_rom_ext(ha, pext, mode);

	DEBUG9(printk("%s: entered.\n", __func__);)

	/* The ISP2312 v2 chip cannot access the FLASH registers via MMIO. */
	if (IS_QLA2312(ha) && ha->product_id[3] == 0x2 && !ha->pio_address) {
		pext->Status = EXT_STATUS_INVALID_REQUEST;
		return (1);
	}

	if (pext->RequestLen != FLASH_IMAGE_SIZE) {
		pext->Status = EXT_STATUS_COPY_ERR;
		return (1);
	}

	/* Read from user buffer */
	kern_tmp = kmalloc(FLASH_IMAGE_SIZE, GFP_KERNEL);
	if (kern_tmp == NULL) {
		pext->Status = EXT_STATUS_COPY_ERR;
		printk(KERN_WARNING
			"%s: ERROR in flash allocation.\n", __func__);
		return (1);
	}

	usr_tmp = (uint8_t *)pext->RequestAdr;

	ret = copy_from_user(kern_tmp, usr_tmp, FLASH_IMAGE_SIZE);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR in buffer copy READ. "
				"RequestAdr=%p\n",
				__func__, pext->RequestAdr);)
		return (ret);
	}

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	/* Go with update */
	status = qla2x00_update_or_read_flash(ha, kern_tmp, 0, FLASH_IMAGE_SIZE,
	    QLA2X00_WRITE); 

	kfree(kern_tmp);

	if (status) {
		ret = 1;
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR updating flash.\n", __func__);)
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return ret;
}


int
qla2x00_update_option_rom_ext(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		ret;
	uint8_t		*usr_tmp;
	uint8_t		*kern_tmp;
	uint16_t	status;
	int		iter, found;
	uint32_t	saddr, length;

	DEBUG9(printk("%s: entered.\n", __func__);)

	found = 0;
	saddr = length = 0;
	/* Retrieve region or raw starting address. */
	if (pext->SubCode == 0xFFFF) {
		saddr = pext->Reserved1;
		length = pext->RequestLen;
		found++;
	} else {
		INT_OPT_ROM_REGION *OptionRomTable = NULL;
		unsigned long  OptionRomTableSize;
		/* Pick the right OptionRom table based on device id */
		qla2x00_get_option_rom_table(ha, &OptionRomTable,
		    &OptionRomTableSize);
		for (iter = 0; OptionRomTable != NULL && iter <
		    (OptionRomTableSize / sizeof(INT_OPT_ROM_REGION));
		    iter++) {
			if (OptionRomTable[iter].Region == pext->SubCode) {
				saddr = OptionRomTable[iter].Beg;
				length = OptionRomTable[iter].Size;
				found++;
				break;
			}
		}
	}
	if (!found) {
		pext->Status = EXT_STATUS_ERR;
		return 1;
	}
	if (pext->RequestLen < length) {
		pext->Status = EXT_STATUS_COPY_ERR;
		return 1;
	}

	/* Read from user buffer */
	usr_tmp = (uint8_t *)pext->RequestAdr;

	kern_tmp = kmalloc(length, GFP_KERNEL);
	if (kern_tmp == NULL) {
		pext->Status = EXT_STATUS_COPY_ERR;
		printk(KERN_WARNING
		    "%s: ERROR in flash allocation.\n", __func__);
		return 1;
	}
	ret = copy_from_user(kern_tmp, usr_tmp, length);
	if (ret) {
		kfree(kern_tmp);
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR in buffer copy READ. "
		    "RequestAdr=%p\n", __func__, pext->RequestAdr));
		return (ret);
	}

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	/* Go with update */
	status = qla2x00_update_or_read_flash(ha, kern_tmp, saddr, length,
	    QLA2X00_WRITE);

	kfree(kern_tmp);

	if (status) {
		ret = 1;
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR updating flash.\n", __func__);)
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return ret;
}

int
qla2x00_get_option_rom_layout(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int	ret, iter;
	INT_OPT_ROM_REGION *OptionRomTable = NULL;
	INT_OPT_ROM_LAYOUT *optrom_layout;	
	unsigned long  OptionRomTableSize; 

	DEBUG9(printk("%s: entered.\n", __func__);)

	/* Pick the right OptionRom table based on device id */
	qla2x00_get_option_rom_table(ha, &OptionRomTable, &OptionRomTableSize);

	if (OptionRomTable == NULL) {
		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		DEBUG9_10(printk("%s(%ld) Option Rom Table for device_id=0x%x "
		    "not defined\n", __func__, ha->host_no, ha->pdev->device));
		return pext->Status;
	}

	if (pext->ResponseLen < OptionRomTableSize) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		DEBUG9_10(printk("%s(%ld) buffer too small: response_len = %d "
		    "optrom_table_len=%ld.\n", __func__, ha->host_no,
		    pext->ResponseLen, OptionRomTableSize));
		return pext->Status;
	}
	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&optrom_layout,
	    OptionRomTableSize)) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%ld.\n", __func__, ha->host_no,
		    ha->instance, OptionRomTableSize));
		return pext->Status;
	}

	// Dont Count the NULL Entry.
	optrom_layout->NoOfRegions = (UINT32)
	    (OptionRomTableSize / sizeof(INT_OPT_ROM_REGION) - 1);

	for (iter = 0; iter < optrom_layout->NoOfRegions; iter++) {
		optrom_layout->Region[iter].Region =
		    OptionRomTable[iter].Region;
		optrom_layout->Region[iter].Size =
		    OptionRomTable[iter].Size;

		if (OptionRomTable[iter].Region == INT_OPT_ROM_REGION_ALL)
			optrom_layout->Size = OptionRomTable[iter].Size;
	}

	ret = copy_to_user(pext->ResponseAdr, optrom_layout,
	    OptionRomTableSize);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s(%ld): inst=%ld ERROR copy rsp buffer.\n",
		    __func__, ha->host_no, ha->instance));
		qla2x00_free_ioctl_scrap_mem(ha);
		return ret;
	}

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("%s: exiting.\n", __func__));

	return ret;
}

static void
qla2x00_get_option_rom_table(scsi_qla_host_t *ha,
    INT_OPT_ROM_REGION **pOptionRomTable, unsigned long  *OptionRomTableSize)
{
	DEBUG9(printk("%s: entered.\n", __func__));

	switch (ha->pdev->device) {
	case PCI_DEVICE_ID_QLOGIC_ISP6312:
		*pOptionRomTable = OptionRomTable6312;
		*OptionRomTableSize = sizeof(OptionRomTable6312);
		break;		       	
	case PCI_DEVICE_ID_QLOGIC_ISP2312:
		/* HBA Model 6826A - is 2312 V3 Chip */
		if (ha->pdev->subsystem_vendor == 0x103C &&
		    ha->pdev->subsystem_device == 0x12BA) {
			*pOptionRomTable = OptionRomTable6826A;
			*OptionRomTableSize = sizeof(OptionRomTable6826A);
		} else {
			*pOptionRomTable = OptionRomTable2312;
			*OptionRomTableSize = sizeof(OptionRomTable2312);
		}
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2322:
	case PCI_DEVICE_ID_QLOGIC_ISP6322:
		*pOptionRomTable = OptionRomTable2322;
		*OptionRomTableSize = sizeof(OptionRomTable2322);
		break;
	default: 
		DEBUG9_10(printk("%s(%ld) Option Rom Table for device_id=0x%x "
		    "not defined\n", __func__, ha->host_no, ha->pdev->device));
		break;
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)
}
