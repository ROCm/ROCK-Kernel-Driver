/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic qla4xxx driver for Linux 2.4.x
 * Copyright (C) 2004 Qlogic Corporation
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
 ******************************************************************************
 *             Please see release.txt for revision history.                   *
 *                                                                            *
 ******************************************************************************
 * Function Table of Contents:
 *	FM93C56A_Select
 *	FM93C56A_Cmd
 *	FM93C56A_Deselect
 *	FM93C56A_DataIn
 *	EEPROM_ReadWord
 *	RD_NVRAM_WORD
 ****************************************************************************/

#include "ql4_def.h"

int   eepromSize  = EEPROM_SIZE;
int   addrBits    = EEPROM_NO_ADDR_BITS;
int   dataBits    = EEPROM_NO_DATA_BITS;
int   eepromCmdData = 0;


static int FM93C56A_Select(scsi_qla_host_t *ha)
{
	QL4PRINT(QLP17, printk(KERN_ERR "FM93C56A_Select:\n"));
	eepromCmdData = AUBURN_EEPROM_CS_1 | 0x000f0000;
	WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData);
	PCI_POSTING(ISP_NVRAM(ha));
	return(1);
}

static int FM93C56A_Cmd(scsi_qla_host_t *ha, int cmd, int addr)
{
	int   i;
	int   mask;
	int   dataBit;
	int   previousBit;

	QL4PRINT(QLP17, printk(KERN_ERR "FM93C56A_Cmd(%d, 0x%x)\n", cmd, addr));

	// Clock in a zero, then do the start bit
	WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | AUBURN_EEPROM_DO_1);
	WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | AUBURN_EEPROM_DO_1 | AUBURN_EEPROM_CLK_RISE);
	WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | AUBURN_EEPROM_DO_1 | AUBURN_EEPROM_CLK_FALL);
	PCI_POSTING(ISP_NVRAM(ha));

	mask = 1 << (FM93C56A_CMD_BITS-1);
	// Force the previous data bit to be different
	previousBit = 0xffff;
	for (i = 0; i < FM93C56A_CMD_BITS; i++) {
		dataBit = (cmd & mask) ? AUBURN_EEPROM_DO_1 : AUBURN_EEPROM_DO_0;
		if (previousBit != dataBit) {
			// If the bit changed, then change the DO state to match
			WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | dataBit);
			previousBit = dataBit;
		}
		WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | dataBit | AUBURN_EEPROM_CLK_RISE);
		WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | dataBit | AUBURN_EEPROM_CLK_FALL);
		PCI_POSTING(ISP_NVRAM(ha));
		cmd = cmd << 1;
	}

	mask = 1 << (addrBits-1);
	// Force the previous data bit to be different
	previousBit = 0xffff;
	for (i = 0; i < addrBits; i++) {
		dataBit = (addr & mask) ? AUBURN_EEPROM_DO_1 : AUBURN_EEPROM_DO_0;
		if (previousBit != dataBit) {
			// If the bit changed, then change the DO state to match
			WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | dataBit);
			previousBit = dataBit;
		}
		WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | dataBit | AUBURN_EEPROM_CLK_RISE);
		WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | dataBit | AUBURN_EEPROM_CLK_FALL);
		PCI_POSTING(ISP_NVRAM(ha));
		addr = addr << 1;
	}
	return(1);
}

static int FM93C56A_Deselect(scsi_qla_host_t *ha)
{
	QL4PRINT(QLP17, printk(KERN_ERR "FM93C56A_Deselect:\n"));
	eepromCmdData = AUBURN_EEPROM_CS_0 | 0x000f0000 ;
	WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData);
	PCI_POSTING(ISP_NVRAM(ha));
	return(1);
}

static int FM93C56A_DataIn(scsi_qla_host_t *ha, unsigned short *value)
{
	int   i;
	int   data = 0;
	int   dataBit;

	// Read the data bits
	// The first bit is a dummy.  Clock right over it.
	for (i = 0; i < dataBits; i++) {
		WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | AUBURN_EEPROM_CLK_RISE);
		WRT_REG_DWORD(ISP_NVRAM(ha), eepromCmdData | AUBURN_EEPROM_CLK_FALL);
		dataBit = (RD_REG_DWORD(ISP_NVRAM(ha)) & AUBURN_EEPROM_DI_1) ? 1 : 0;
		data = (data << 1) | dataBit;
	}
	*value = data;
	QL4PRINT(QLP17, printk(KERN_ERR "FM93C56A_DataIn(0x%x)\n", *value));
	return(1);
}

static int
EEPROM_ReadWord(int eepromAddr, u16 *value, scsi_qla_host_t *ha)
{
	QL4PRINT(QLP17, printk(KERN_ERR "EEPROM_Reg addr %p\n", ISP_NVRAM(ha)));
	QL4PRINT(QLP17, printk(KERN_ERR "EEPROM_ReadWord(0x%x)\n", eepromAddr));

	FM93C56A_Select(ha);
	FM93C56A_Cmd(ha, FM93C56A_READ, eepromAddr);
	FM93C56A_DataIn(ha, value);
	FM93C56A_Deselect(ha);
	QL4PRINT(QLP17, printk(KERN_ERR "EEPROM_ReadWord(0x%x, %d)\n",
			       eepromAddr, *value));
	return(1);
}

/* Hardware_lock must be set before calling */
u16
RD_NVRAM_WORD(scsi_qla_host_t *ha, int offset)
{
	u16 val;
	/* NOTE: NVRAM uses half-word addresses */
	EEPROM_ReadWord(offset, &val, ha);
	return(val);
}

uint8_t
qla4xxx_is_NVRAM_configuration_valid(scsi_qla_host_t *ha)
{
	uint16_t checksum = 0;
	uint32_t index;
	unsigned long flags;
	uint8_t status = QLA_ERROR;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (index = 0;	index < EEPROM_SIZE; index++) {
		checksum += RD_NVRAM_WORD(ha, index);
	} 
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (checksum == 0)
		status = QLA_SUCCESS;

	return (status);
}

/*************************************************************************
 *
 *			Hardware Semaphore
 *
 *************************************************************************/

isp4xxxSemInfo_t semInfo4010[] = {
	{ SEM_HW_LOCK,     4}
	, { SEM_GPO,         6}
	, { SEM_SDRAM_INIT,  8}
	, { SEM_PHY_GBIC,   10}
	, { SEM_NVRAM,      12}
	, { SEM_FLASH,      14}
};

isp4xxxSemInfo_t semInfo4022[] = {
        { SEM_DRIVER,        1}
        , { SEM_DRAM,  	     4}
	, { SEM_GPO,         7}
	, { SEM_PHY_GBIC,    7}
	, { SEM_NVRAM,      10}
	, { SEM_FLASH,      13}
};

static uint32_t SEM_READ(scsi_qla_host_t *ha, uint32_t semId)
{
	if (IS_QLA4022(ha))
		return ((RD_REG_DWORD(ISP_NVRAM(ha)) >> semInfo4022[semId].semShift) & SEM_MASK);
	else
		return ((RD_REG_DWORD(ISP_NVRAM(ha)) >> semInfo4010[semId].semShift) & SEM_MASK);

}


static void SEM_WRITE(scsi_qla_host_t *ha, uint32_t semId, uint8_t owner)
{
	if (IS_QLA4022(ha))
		WRT_REG_DWORD(ISP_NVRAM(ha), (SEM_MASK << 16 << semInfo4022[semId].semShift) | (owner << semInfo4022[semId].semShift));
	else
		WRT_REG_DWORD(ISP_NVRAM(ha), (SEM_MASK << 16 << semInfo4010[semId].semShift) | (owner << semInfo4010[semId].semShift));
}

/**************************************************************************
 * qla4xxx_take_hw_semaphore
 *	This routine acquires the specified semaphore for the iSCSI
 *	storage driver.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	sem - Indicates which semaphore.
 *	wait_flag - specifies type of wait to acquire semaphore
 *		    SEM_FLG_WAIT_FOREVER = wait indefinitely
 *		    SEM_FLG_TIMED_WAIT = wait for a specified amout of time
 *	            SEM_FLG_NO_WAIT = try once to acquire semaphore
 *
 * Returns:
 *	QLA_SUCCESS - Successfully acquired semaphore
 *	QLA_ERROR   - Failed to acquire semaphore
 *
 * Context:
 *	?? context.
 **************************************************************************/
uint8_t
qla4xxx_take_hw_semaphore(scsi_qla_host_t *ha, uint32_t sem, uint8_t wait_flag)
{
	uint32_t wait_time = SEMAPHORE_TOV;
	unsigned long flags = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	for (SEM_WRITE(ha, sem, SEM_OWNER_STORAGE);
	     (SEM_READ(ha, sem) != SEM_OWNER_STORAGE) && (wait_time--);
	     (SEM_WRITE(ha, sem, SEM_OWNER_STORAGE), PCI_POSTING(ISP_NVRAM(ha)))) {
		if (wait_flag == SEM_FLG_NO_WAIT) {
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			return(QLA_ERROR);
		}

		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1 * HZ);
		spin_lock_irqsave(&ha->hardware_lock, flags);
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (wait_time)
		return(QLA_SUCCESS);
	else
		return(QLA_ERROR);
}

/**************************************************************************
 * qla4xxx_clear_hw_semaphore
 *	This routine restores the specified semaphore to the available
 *	state.
 *
 * Input:
 * 	ha - Pointer to host adapter structure.
 *	sem - Indicates which semaphore.
 *
 * Returns:
 *	QLA_SUCCESS - Successfully restored semaphore
 *	QLA_ERROR   - Failed to restore semaphore
 *
 * Context:
 *	?? context.
 **************************************************************************/
void
qla4xxx_clear_hw_semaphore(scsi_qla_host_t *ha, uint32_t sem)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (SEM_READ(ha, sem) == SEM_OWNER_STORAGE) {
		SEM_WRITE(ha, sem, SEM_AVAILABLE);
		PCI_POSTING(ISP_NVRAM(ha));
	}	
	
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}



/*
 * Overrides for Emacs so that we get a uniform tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
