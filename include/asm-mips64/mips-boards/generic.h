/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Defines of the MIPS boards specific address-MAP, registers, etc.
 *
 */
#ifndef _MIPS_GENERIC_H
#define _MIPS_GENERIC_H

#include <linux/config.h>
#include <asm/addrspace.h>
#include <asm/byteorder.h>
#include <asm/mips-boards/bonito64.h>

/*
 * Display register base.
 */
#if defined(CONFIG_MIPS_SEAD)
#define ASCII_DISPLAY_POS_BASE     (KSEG1ADDR(0x1f0005c0))
#else
#define ASCII_DISPLAY_WORD_BASE    (KSEG1ADDR(0x1f000410))
#define ASCII_DISPLAY_POS_BASE     (KSEG1ADDR(0x1f000418))
#endif


/*
 * Yamon Prom print address.
 */
#define YAMON_PROM_PRINT_ADDR      (KSEG1ADDR(0x1fc00504))


/*
 * Reset register.
 */
#if defined(CONFIG_MIPS_SEAD)
#define SOFTRES_REG       (KSEG1ADDR(0x1e800050))
#define GORESET           0x4d
#else
#define SOFTRES_REG       (KSEG1ADDR(0x1f000500))
#define GORESET           0x42
#endif

/*
 * Revision register.
 */
#define MIPS_REVISION_REG                  (KSEG1ADDR(0x1fc00010))
#define MIPS_REVISION_CORID_QED_RM5261     0
#define MIPS_REVISION_CORID_CORE_LV        1
#define MIPS_REVISION_CORID_BONITO64       2
#define MIPS_REVISION_CORID_CORE_20K       3
#define MIPS_REVISION_CORID_CORE_FPGA      4
#define MIPS_REVISION_CORID_CORE_MSC       5

#define MIPS_REVISION_CORID (((*(volatile u32 *)(MIPS_REVISION_REG)) >> 10) & 0x3f)

extern unsigned int mips_revision_corid;


/*
 * Galileo GT64120 system controller register base.
 */
#define MIPS_GT_BASE    (KSEG1ADDR(0x1be00000))

/*
 * Because of the way the internal register works on the Galileo chip,
 * we need to swap the bytes when running bigendian.
 */
#define GT_WRITE(ofs, data)  \
             *(volatile u32 *)(MIPS_GT_BASE+ofs) = cpu_to_le32(data)
#define GT_READ(ofs, data)   \
             data = le32_to_cpu(*(volatile u32 *)(MIPS_GT_BASE+ofs))

#define GT_PCI_WRITE(ofs, data)  \
	*(volatile u32 *)(MIPS_GT_BASE+ofs) = data
#define GT_PCI_READ(ofs, data)   \
	data = *(volatile u32 *)(MIPS_GT_BASE+ofs)

/*
 * Algorithmics Bonito64 system controller register base.
 */
static char * const _bonito = (char *)KSEG1ADDR(BONITO_REG_BASE);

/*
 * MIPS System controller PCI register base.
 */
#define MSC01_PCI_REG_BASE  (KSEG1ADDR(0x1bd00000))

#define MSC_WRITE(reg, data)  \
	*(volatile u32 *)(reg) = data
#define MSC_READ(reg, data)   \
	data = *(volatile u32 *)(reg)

#endif  /* !(_MIPS_GENERIC_H) */
