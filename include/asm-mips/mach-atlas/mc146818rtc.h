/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2003 by Ralf Baechle
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#ifndef __ASM_MACH_ATLAS_MC146818RTC_H
#define __ASM_MACH_ATLAS_MC146818RTC_H

#include <asm/io.h>
#include <asm/mips-boards/atlas.h>
#include <asm/mips-boards/atlasint.h>


#define RTC_PORT(x)	(ATLAS_RTC_ADR_REG + (x)*8)
#define RTC_IOMAPPED	1
#define RTC_EXTENT	16
#define RTC_IRQ		ATLASINT_RTC

#if CONFIG_CPU_LITTLE_ENDIAN
#define ATLAS_RTC_PORT(x) (RTC_PORT(x) + 0)
#else
#define ATLAS_RTC_PORT(x) (RTC_PORT(x) + 3)
#endif

static inline unsigned char CMOS_READ(unsigned long addr)
{
	outb(addr, ATLAS_RTC_PORT(0));

	return inb(ATLAS_RTC_PORT(1));
}

static inline void CMOS_WRITE(unsigned char data, unsigned long addr)
{
	outb(addr, ATLAS_RTC_PORT(0));
	outb(data, ATLAS_RTC_PORT(1));
}

#define RTC_ALWAYS_BCD	0

#define mc146818_decode_year(year) ((year) < 70 ? (year) + 2000 : (year) + 1970)

#endif /* __ASM_MACH_ATLAS_MC146818RTC_H */
