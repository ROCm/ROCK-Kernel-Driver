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

#include "qla_os.h"
#include "qla_def.h"

static uint16_t qla2x00_nvram_request(scsi_qla_host_t *, uint32_t);
static void qla2x00_nv_deselect(scsi_qla_host_t *);
static void qla2x00_nv_write(scsi_qla_host_t *, uint16_t);

uint8_t qla2x00_read_flash_byte(scsi_qla_host_t *, uint32_t);
static void qla2x00_write_flash_byte(scsi_qla_host_t *, uint32_t, uint8_t);
static uint8_t qla2x00_poll_flash(scsi_qla_host_t *ha,
		uint32_t addr, uint8_t poll_data, uint8_t mid);
static uint8_t qla2x00_program_flash_address(scsi_qla_host_t *ha,
		uint32_t addr, uint8_t data, uint8_t mid);
static uint8_t qla2x00_erase_flash_sector(scsi_qla_host_t *ha,
		uint32_t addr, uint32_t sec_mask, uint8_t mid);

uint8_t qla2x00_get_flash_manufacturer(scsi_qla_host_t *ha);
uint16_t qla2x00_get_flash_version(scsi_qla_host_t *);
uint16_t qla2x00_get_flash_image(scsi_qla_host_t *ha, uint8_t *image);
uint16_t qla2x00_set_flash_image(scsi_qla_host_t *ha, uint8_t *image);


/*
 * NVRAM support routines
 */

/**
 * qla2x00_lock_nvram_access() - 
 * @ha: HA context
 */
void
qla2x00_lock_nvram_access(scsi_qla_host_t *ha)
{
	uint16_t data;
	device_reg_t *reg;

	reg = ha->iobase;

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
}

/**
 * qla2x00_unlock_nvram_access() - 
 * @ha: HA context
 */
void
qla2x00_unlock_nvram_access(scsi_qla_host_t *ha)
{
	device_reg_t *reg;

	reg = ha->iobase;

	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha))
		WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0);
}

/**
 * qla2x00_get_nvram_word() - Calculates word position in NVRAM and calls the
 *	request routine to get the word from NVRAM.
 * @ha: HA context
 * @addr: Address in NVRAM to read
 *
 * Returns the word read from nvram @addr.
 */
uint16_t
qla2x00_get_nvram_word(scsi_qla_host_t *ha, uint32_t addr)
{
	uint16_t	data;
	uint32_t	nv_cmd;

	nv_cmd = addr << 16;
	nv_cmd |= NV_READ_OP;
	data = qla2x00_nvram_request(ha, nv_cmd);

	return (data);
}

/**
 * qla2x00_write_nvram_word() - Write NVRAM data.
 * @ha: HA context
 * @addr: Address in NVRAM to write
 * @data: word to program
 */
void
qla2x00_write_nvram_word(scsi_qla_host_t *ha, uint32_t addr, uint16_t data)
{
	int count;
	uint16_t word;
	uint32_t nv_cmd;
	device_reg_t *reg = ha->iobase;

	qla2x00_nv_write(ha, NVR_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Erase Location */
	nv_cmd = (addr << 16) | NV_ERASE_OP;
	nv_cmd <<= 5;
	for (count = 0; count < 11; count++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);

		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for Erase to Finish */
	WRT_REG_WORD(&reg->nvram, NVR_SELECT);
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NVR_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Write data */
	nv_cmd = (addr << 16) | NV_WRITE_OP;
	nv_cmd |= data;
	nv_cmd <<= 5;
	for (count = 0; count < 27; count++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);

		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready */
	WRT_REG_WORD(&reg->nvram, NVR_SELECT);
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NVR_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Disable writes */
	qla2x00_nv_write(ha, NVR_DATA_OUT);
	for (count = 0; count < 10; count++)
		qla2x00_nv_write(ha, 0);

	qla2x00_nv_deselect(ha);
}

/**
 * qla2x00_nvram_request() - Sends read command to NVRAM and gets data from
 *	NVRAM.
 * @ha: HA context
 * @nv_cmd: NVRAM command
 *
 * Bit definitions for NVRAM command:
 *
 *	Bit 26     = start bit
 *	Bit 25, 24 = opcode
 *	Bit 23-16  = address
 *	Bit 15-0   = write data
 *
 * Returns the word read from nvram @addr.
 */
static uint16_t
qla2x00_nvram_request(scsi_qla_host_t *ha, uint32_t nv_cmd)
{
	uint8_t		cnt;
	device_reg_t	*reg = ha->iobase;
	uint16_t	data = 0;
	uint16_t	reg_data;

	/* Send command to NVRAM. */
	nv_cmd <<= 5;
	for (cnt = 0; cnt < 11; cnt++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	/* Read data from NVRAM. */
	for (cnt = 0; cnt < 16; cnt++) {
		WRT_REG_WORD(&reg->nvram, NVR_SELECT | NVR_CLOCK);
		NVRAM_DELAY();
		data <<= 1;
		reg_data = RD_REG_WORD(&reg->nvram);
		if (reg_data & NVR_DATA_IN)
			data |= BIT_0;
		WRT_REG_WORD(&reg->nvram, NVR_SELECT);
		NVRAM_DELAY();
		RD_REG_WORD(&reg->nvram);	/* PCI Posting. */
	}

	/* Deselect chip. */
	WRT_REG_WORD(&reg->nvram, NVR_DESELECT);
	NVRAM_DELAY();
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */

	return (data);
}

/**
 * qla2x00_nv_write() - Clean NVRAM operations.
 * @ha: HA context
 */
void
qla2x00_nv_deselect(scsi_qla_host_t *ha)
{
	device_reg_t *reg = ha->iobase;

	WRT_REG_WORD(&reg->nvram, NVR_DESELECT);
	NVRAM_DELAY();
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
}

/**
 * qla2x00_nv_write() - Prepare for NVRAM read/write operation.
 * @ha: HA context
 * @data: Serial interface selector
 */
void
qla2x00_nv_write(scsi_qla_host_t *ha, uint16_t data)
{
	device_reg_t *reg = ha->iobase;

	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT);
	NVRAM_DELAY();
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT | NVR_CLOCK);
	NVRAM_DELAY();
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT);
	NVRAM_DELAY();
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
}

/*
 * Flash support routines
 */

/**
 * qla2x00_flash_enable() - Setup flash for reading and writing.
 * @ha: HA context
 */
void
qla2x00_flash_enable(scsi_qla_host_t *ha)
{
	uint16_t	data;
	device_reg_t	*reg = ha->iobase;

	data = RD_REG_WORD(&reg->ctrl_status);
	data |= CSR_FLASH_ENABLE;
	WRT_REG_WORD(&reg->ctrl_status, data);
	RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
}

/**
 * qla2x00_flash_disable() - Disable flash and allow RISC to run.
 * @ha: HA context
 */
void
qla2x00_flash_disable(scsi_qla_host_t *ha)
{
	uint16_t	data;
	device_reg_t	*reg = ha->iobase;

	data = RD_REG_WORD(&reg->ctrl_status);
	data &= ~(CSR_FLASH_ENABLE);
	WRT_REG_WORD(&reg->ctrl_status, data);
	RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
}

/**
 * qla2x00_read_flash_byte() - Reads a byte from flash
 * @ha: HA context
 * @addr: Address in flash to read
 *
 * A word is read from the chip, but, only the lower byte is valid.
 *
 * Returns the byte read from flash @addr.
 */
uint8_t
qla2x00_read_flash_byte(scsi_qla_host_t *ha, uint32_t addr)
{
	uint16_t	data;
	uint16_t	bank_select;
	device_reg_t	*reg = ha->iobase;

	/* Setup bit 16 of flash address. */
	bank_select = RD_REG_WORD(&reg->ctrl_status);
	if ((addr & BIT_16) && ((bank_select & CSR_FLASH_64K_BANK) == 0)) {
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */
	} else if (((addr & BIT_16) == 0) &&
	    (bank_select & CSR_FLASH_64K_BANK)) {
		bank_select &= ~(CSR_FLASH_64K_BANK);
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */
	}

	/* The ISP2312 v2 chip cannot access the FLASH registers via MMIO. */
	if (IS_QLA2312(ha) && ha->product_id[3] == 0x2 && ha->pio_address) {
		uint16_t data2;

		reg = (device_reg_t *)ha->pio_address;
		outw((uint16_t)addr, (unsigned long)(&reg->flash_address));
		do {
			data = inw((unsigned long)(&reg->flash_data));
			barrier();
			cpu_relax();
			data2 = inw((unsigned long)(&reg->flash_data));
		} while (data != data2);
	} else {
		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		data = qla2x00_debounce_register(&reg->flash_data);
	}

	return ((uint8_t)data);
}

/**
 * qla2x00_write_flash_byte() - Write a byte to flash
 * @ha: HA context
 * @addr: Address in flash to write
 * @data: Data to write
 */
static void
qla2x00_write_flash_byte(scsi_qla_host_t *ha, uint32_t addr, uint8_t data)
{
	uint16_t	bank_select;
	device_reg_t	*reg = ha->iobase;

	/* Setup bit 16 of flash address. */
	bank_select = RD_REG_WORD(&reg->ctrl_status);
	if ((addr & BIT_16) && ((bank_select & CSR_FLASH_64K_BANK) == 0)) {
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */
	} else if (((addr & BIT_16) == 0) &&
	    (bank_select & CSR_FLASH_64K_BANK)) {
		bank_select &= ~(CSR_FLASH_64K_BANK);
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */
	}

	/* The ISP2312 v2 chip cannot access the FLASH registers via MMIO. */
	if (IS_QLA2312(ha) && ha->product_id[3] == 0x2 && ha->pio_address) {
		reg = (device_reg_t *)ha->pio_address;
		outw((uint16_t)addr, (unsigned long)(&reg->flash_address));
		outw((uint16_t)data, (unsigned long)(&reg->flash_data));
	} else {
		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
		WRT_REG_WORD(&reg->flash_data, (uint16_t)data);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
	}
}

/**
 * qla2x00_poll_flash() - Polls flash for completion.
 * @ha: HA context
 * @addr: Address in flash to poll
 * @poll_data: Data to be polled
 * @mid: Flash manufacturer ID
 *
 * This function polls the device until bit 7 of what is read matches data
 * bit 7 or until data bit 5 becomes a 1.  If that hapens, the flash ROM timed
 * out (a fatal error).  The flash book recommeds reading bit 7 again after
 * reading bit 5 as a 1.
 *
 * Returns 0 on success, else non-zero.
 */
static uint8_t
qla2x00_poll_flash(scsi_qla_host_t *ha,
    uint32_t addr, uint8_t poll_data, uint8_t mid)
{
	uint8_t		status;
	uint8_t		flash_data;
	uint32_t	cnt;
	int		failed_pass;

	status = 1;
	failed_pass = 1;

	/* Wait for 30 seconds for command to finish. */
	poll_data &= BIT_7;
	for (cnt = 3000000; cnt; cnt--) {
		flash_data = qla2x00_read_flash_byte(ha, addr);
		if ((flash_data & BIT_7) == poll_data) {
			status = 0;
			break;
		}

		if (mid != 0x40 && mid != 0xda) {
			if (flash_data & BIT_5)
				failed_pass--;
			if (failed_pass < 0)
				break;
		}
		udelay(10);
		barrier();
	}
	return (status);
}

/**
 * qla2x00_program_flash_address() - Programs a flash address
 * @ha: HA context
 * @addr: Address in flash to program
 * @data: Data to be written in flash
 * @mid: Flash manufacturer ID
 *
 * Returns 0 on success, else non-zero.
 */
static uint8_t
qla2x00_program_flash_address(scsi_qla_host_t *ha,
    uint32_t addr, uint8_t data, uint8_t mid)
{
	/* Write Program Command Sequence */
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0xa0);
	qla2x00_write_flash_byte(ha, addr, data);

	/* Wait for write to complete. */
	return (qla2x00_poll_flash(ha, addr, data, mid));
}

/**
 * qla2x00_erase_flash_sector() - Erase a flash sector.
 * @ha: HA context
 * @addr: Flash sector to erase
 * @sec_mask: Sector address mask
 * @mid: Flash manufacturer ID
 *
 * Returns 0 on success, else non-zero.
 */
static uint8_t
qla2x00_erase_flash_sector(scsi_qla_host_t *ha,
    uint32_t addr, uint32_t sec_mask, uint8_t mid)
{
	/* Individual Sector Erase Command Sequence */
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0x80);
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);

	if (mid == 0xda)
		qla2x00_write_flash_byte(ha, addr & sec_mask, 0x10);
	else
		qla2x00_write_flash_byte(ha, addr & sec_mask, 0x30);

	udelay(150);

	/* Wait for erase to complete. */
	return (qla2x00_poll_flash(ha, addr, 0x80, mid));
}

/**
 * qla2x00_get_flash_manufacturer() - Read manufacturer ID from flash chip.
 * @ha: HA context
 *
 * Returns the manufacturer's ID read from the flash chip.
 */
uint8_t
qla2x00_get_flash_manufacturer(scsi_qla_host_t *ha)
{
	uint8_t	manuf_id;

	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0x90);
	manuf_id = qla2x00_read_flash_byte(ha, 0x0001);

	return (manuf_id);
}

/**
 * qla2x00_get_flash_version() - Read version information from flash.
 * @ha: HA context
 *
 * Returns QLA_SUCCESS on successful retrieval of flash version.
 */
uint16_t
qla2x00_get_flash_version(scsi_qla_host_t *ha)
{
	uint16_t	ret = QLA_SUCCESS;
	uint32_t	loop_cnt = 1;  /* this is for error exit only */
	uint32_t	pcir_adr;

	/* The ISP2312 v2 chip cannot access the FLASH registers via MMIO. */
	if (IS_QLA2312(ha) && ha->product_id[3] == 0x2 && !ha->pio_address)
		ret = QLA_FUNCTION_FAILED;

	qla2x00_flash_enable(ha);
	do {	/* Loop once to provide quick error exit */
		/* Match signature */
		if (!(qla2x00_read_flash_byte(ha, 0) == 0x55 &&
		    qla2x00_read_flash_byte(ha, 1) == 0xaa)) {
			/* No signature */
			DEBUG2(printk("scsi(%ld): No matching FLASH "
			    "signature.\n", ha->host_no));
			ret = QLA_FUNCTION_FAILED;
			break;
		}

		pcir_adr = qla2x00_read_flash_byte(ha, 0x18) & 0xff;

		/* validate signature of PCI data structure */
		if ((qla2x00_read_flash_byte(ha, pcir_adr)) == 'P' &&
		    (qla2x00_read_flash_byte(ha, pcir_adr + 1)) == 'C' &&
		    (qla2x00_read_flash_byte(ha, pcir_adr + 2)) == 'I' &&
		    (qla2x00_read_flash_byte(ha, pcir_adr + 3)) == 'R') {

			/* Read version */
			ha->optrom_minor =
			    qla2x00_read_flash_byte(ha, pcir_adr + 0x12);
			ha->optrom_major =
			    qla2x00_read_flash_byte(ha, pcir_adr + 0x13);
			DEBUG3(printk("%s(): got %d.%d.\n",
			    __func__, ha->optrom_major, ha->optrom_minor));
		} else {
			/* error */
			DEBUG2(printk("%s(): PCI data struct not found. "
			    "pcir_adr=%x.\n",
			    __func__, pcir_adr));
			ret = QLA_FUNCTION_FAILED;
			break;
		}

	} while (--loop_cnt);
	qla2x00_flash_disable(ha);

	return (ret);
}

/**
 * qla2x00_get_flash_image() - Read image from flash chip.
 * @ha: HA context
 * @image: Buffer to receive flash image
 *
 * Returns 0 on success, else non-zero.
 */
uint16_t
qla2x00_get_flash_image(scsi_qla_host_t *ha, uint8_t *image)
{
	uint32_t	addr;
	uint32_t	midpoint;
	uint8_t		*data;
	device_reg_t	*reg = ha->iobase;

	midpoint = FLASH_IMAGE_SIZE / 2;

	qla2x00_flash_enable(ha);
	WRT_REG_WORD(&reg->nvram, 0);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	for (addr = 0, data = image; addr < FLASH_IMAGE_SIZE; addr++, data++) {
		if (addr == midpoint)
			WRT_REG_WORD(&reg->nvram, NVR_SELECT);

		*data = qla2x00_read_flash_byte(ha, addr);
	}
	qla2x00_flash_disable(ha);

	return (0);
}

/**
 * qla2x00_set_flash_image() - Write image to flash chip.
 * @ha: HA context
 * @image: Source image to write to flash
 *
 * Returns 0 on success, else non-zero.
 */
uint16_t
qla2x00_set_flash_image(scsi_qla_host_t *ha, uint8_t *image)
{
	uint16_t	status;
	uint32_t	addr;
	uint32_t	midpoint;
	uint32_t	sec_mask;
	uint32_t	rest_addr;
	uint8_t		mid;
	uint8_t		sec_number;
	uint8_t		data;
	device_reg_t	*reg = ha->iobase;

	status = 0;
	sec_number = 0;

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
	RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */

	qla2x00_flash_enable(ha);
	do {	/* Loop once to provide quick error exit */
		/* Structure of flash memory based on manufacturer */
		mid = qla2x00_get_flash_manufacturer(ha);
		if (mid == 0x6d) {
			// Am29LV001 part
			rest_addr = 0x1fff;
			sec_mask = 0x1e000;
		} else if (mid == 0x40) {
			// Mostel v29c51001 part
			rest_addr = 0x1ff;
			sec_mask = 0x1fe00;
		} else if (mid == 0xbf) {
			// SST39sf10 part
			rest_addr = 0xfff;
			sec_mask = 0x1f000;
		} else if (mid == 0xda) {
			// Winbond W29EE011 part
			rest_addr = 0x7f;
			sec_mask = 0x1ff80;
			addr = 0;
			if (qla2x00_erase_flash_sector(ha, addr, sec_mask,
			    mid)) {
				status = 1;
				break;
			}
		} else {
			// Am29F010 part
			rest_addr = 0x3fff;
			sec_mask = 0x1c000;
		}

		midpoint = FLASH_IMAGE_SIZE / 2;
		for (addr = 0; addr < FLASH_IMAGE_SIZE; addr++) {
			data = *image++;
			/* Are we at the beginning of a sector? */
			if (!(addr & rest_addr)) {
				if (addr == midpoint)
					WRT_REG_WORD(&reg->nvram, NVR_SELECT);

				/* Then erase it */
				if (qla2x00_erase_flash_sector(ha, addr,
				    sec_mask, mid)) {
					status = 1;
					break;
				}

				sec_number++;
			}
			if (mid == 0x6d) {
				if (sec_number == 1 &&
				    (addr == (rest_addr - 1))) {
					rest_addr = 0x0fff;
					sec_mask   = 0x1f000;
				} else if (sec_number == 3 && (addr & 0x7ffe)) {
					rest_addr = 0x3fff;
					sec_mask   = 0x1c000;
				}
			}

			if (qla2x00_program_flash_address(ha, addr, data,
			    mid)) {
				status = 1;
				break;
			}
		}
	} while (0);

	qla2x00_flash_disable(ha);

	return (status);
}
