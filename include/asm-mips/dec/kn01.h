/*
 * Hardware info about DEC DECstation DS2100/3100 systems (otherwise known
 * as pmax or kn01.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995,1996 by Paul M. Antoine, some code and definitions
 * are by curteousy of Chris Fraser.
 *
 * This file is under construction - you were warned!
 */
#ifndef __ASM_MIPS_DEC_KN01_H 
#define __ASM_MIPS_DEC_KN01_H 

#include <asm/addrspace.h>

/*
 * Some port addresses...
 * FIXME: these addresses are incomplete and need tidying up!
 */

#define KN01_LANCE_BASE (KSEG1ADDR(0x18000000)) /* 0xB8000000 */
#define KN01_DZ11_BASE	(KSEG1ADDR(0x1c000000)) /* 0xBC000000 */
#define KN01_RTC_BASE	(KSEG1ADDR(0x1d000000)) /* 0xBD000000 */

#endif /* __ASM_MIPS_DEC_KN01_H */
