/*
 * arch/ppc/platforms/subzero.h   Platform definitions for the IBM 
 *				Subzero card, based on beech.h by Bishop Brock
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Copyright (C) 2002, International Business Machines Corporation
 * All Rights Reserved.
 *
 * David Gibson
 * IBM OzLabs, Canberra, Australia
 * <arctic@gibson.dropbear.id.au>
 *
 * Ken Inoue 
 * IBM Thomas J. Watson Research Center
 * <keninoue@us.ibm.com>
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_SUBZERO_CORE_H__
#define __ASM_SUBZERO_CORE_H__

#include <platforms/4xx/ibm405lp.h>

#ifndef __ASSEMBLY__

#include <linux/types.h>

/*
 * Data structure defining board information maintained by the standard boot
 * ROM on the IBM Subzero card. An effort has been made to
 * keep the field names consistent with the 8xx 'bd_t' board info
 * structures.
 * 
 * Original Beech BIOS Definition:
 * 
 * typedef struct board_cfg_data {
 *    unsigned char     usr_config_ver[4];
 *    unsigned long     timerclk_freq;
 *    unsigned char     rom_sw_ver[30];
 *    unsigned int      mem_size;
 *    unsigned long     sysclock_period;
 *    unsigned long     sys_speed;
 *    unsigned long     cpu_speed;
 *    unsigned long     vco_speed;
 *    unsigned long     plb_speed;
 *    unsigned long     opb_speed;
 *    unsigned long     ebc_speed;
 *  } bd_t;
 */

typedef struct board_info {
	unsigned char     bi_s_version[4];  /* Version of this structure */
	unsigned long     bi_tbfreq;        /* Frequency of SysTmrClk */
	unsigned char     bi_r_version[30]; /* Version of the IBM ROM */
	unsigned int      bi_memsize;       /* DRAM installed, in bytes */
	unsigned long     sysclock_period;  /* SysClk period in ns */
	unsigned long     sys_speed;        /* SysCLk frequency in Hz */
	unsigned long     bi_intfreq;       /* Processor speed, in Hz */
	unsigned long     vco_speed;        /* PLL VCO speed, in Hz */
	unsigned long     bi_busfreq;       /* PLB Bus speed, in Hz */
	unsigned long     bi_opb_busfreq;   /* OPB Bus speed, in Hz */
	unsigned long     bi_ebc_busfreq;   /* EBC Bus speed, in Hz */
	int		  bi_iic_fast[1];   /* Use fast i2c mode */
} bd_t;

/* EBC Bank 0 controls the boot flash
 *
 * FIXME? these values assume that there is 16MB of flash on the
 * personality card, in addition to the 16MB on the subzero card
 * itself */
#define SUBZERO_BANK0_PADDR      ((uint)0xfe000000)
#define SUBZERO_BANK0_EBC_SIZE   EBC0_BnCR_BS_32MB

#define SUBZERO_BOOTFLASH_PADDR  (SUBZERO_BANK0_PADDR)
#define SUBZERO_BOOTFLASH_SIZE   ((uint)(32 * 1024 * 1024))

#define PCI_DRAM_OFFSET		0

void *beech_sram_alloc(size_t size);
int beech_sram_free(void *p);

void subzero_core_ebc_setup(void);

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_SUBZERO_CORE_H__ */
#endif /* __KERNEL__ */
