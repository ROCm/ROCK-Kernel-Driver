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
 *	FM93C56A_Select
 *	FM93C56A_Cmd
 *	FM93C56A_Deselect
 *	FM93C56A_DataIn
 *	EEPROM_ReadWord
 *	RD_NVRAM_WORD
 ****************************************************************************/

#include "ql4_def.h"

#define EEPROM_SIZE(ha) \
	(IS_QLA4022(ha) ? \
	 FM93C86A_SIZE_16 : \
	 FM93C66A_SIZE_16)
	
#define EEPROM_NO_ADDR_BITS(ha) \
	(IS_QLA4022(ha) ? \
	 FM93C86A_NO_ADDR_BITS_16 : \
	 FM93C56A_NO_ADDR_BITS_16)

#define EEPROM_NO_DATA_BITS(ha) FM93C56A_DATA_BITS_16

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

	mask = 1 << (EEPROM_NO_ADDR_BITS(ha)-1);
	// Force the previous data bit to be different
	previousBit = 0xffff;
	for (i = 0; i < EEPROM_NO_ADDR_BITS(ha); i++) {
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
	for (i = 0; i < EEPROM_NO_DATA_BITS(ha); i++) {
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
	for (index = 0;	index < EEPROM_SIZE(ha); index++) {
		checksum += RD_NVRAM_WORD(ha, index);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (checksum == 0)
		status = QLA_SUCCESS;

	return (status);
}

/*************************************************************************
 *
 *			Hardware Semaphore routines
 *
 *************************************************************************/
int ql4xxx_sem_spinlock(scsi_qla_host_t *ha, u32 sem_mask, u32 sem_bits)
{
    uint32_t      value;

    DEBUG2(printk("scsi%d: Trying to get SEM lock - mask= 0x%x, code = 0x%x\n",
	ha->host_no, sem_mask, sem_bits);)
    while ( 1 ) {
        WRT_REG_DWORD(ISP_SEMAPHORE(ha), (sem_mask | sem_bits));
        value = RD_REG_DWORD(ISP_SEMAPHORE(ha));
        if ((value & (sem_mask >> 16)) == sem_bits) {
    		DEBUG2(printk("scsi%d: Got SEM LOCK - mask= 0x%x, code = 0x%x\n",
		ha->host_no, sem_mask, sem_bits);)
            break;
	}
    }
   return (1);
}

void ql4xxx_sem_unlock(scsi_qla_host_t *ha, u32 sem_mask)
{

    WRT_REG_DWORD(ISP_SEMAPHORE(ha), sem_mask);
    PCI_POSTING(ISP_SEMAPHORE(ha));
    DEBUG2(printk("scsi%d: UNLOCK SEM - mask= 0x%x\n",
	 ha->host_no, sem_mask);)
}

int ql4xxx_sem_lock(scsi_qla_host_t *ha, u32 sem_mask, u32 sem_bits)
{
    uint32_t      value;

    WRT_REG_DWORD(ISP_SEMAPHORE(ha), (sem_mask | sem_bits));
    value = RD_REG_DWORD(ISP_SEMAPHORE(ha));
    if ((value & (sem_mask >> 16)) == sem_bits) {
    	DEBUG2(printk("scsi%d: Got SEM LOCK - mask= 0x%x, code = 0x%x, sema code=0x%x\n",
		ha->host_no, sem_mask, sem_bits, value);)
        return (1);
    } else {
        return (0);
    }
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
