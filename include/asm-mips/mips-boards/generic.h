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

#include <asm/addrspace.h>
#include <asm/byteorder.h>

/*
 * Display register base.
 */
#define ASCII_DISPLAY_WORD_BASE    (KSEG1ADDR(0x1f000410))
#define ASCII_DISPLAY_POS_BASE     (KSEG1ADDR(0x1f000418))


/*
 * Yamon Prom print address.
 */
#define YAMON_PROM_PRINT_ADDR      (KSEG1ADDR(0x1fc00504))


/*
 * Reset register.
 */
#define SOFTRES_REG       (KSEG1ADDR(0x1f000500))
#define GORESET           0x42


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

#endif  /* !(_MIPS_GENERIC_H) */
