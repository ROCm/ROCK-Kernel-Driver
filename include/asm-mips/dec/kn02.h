/*
 * Hardware info about DEC DECstation 5000/2xx systems (otherwise known
 * as 3max or kn02.
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
#ifndef __ASM_MIPS_DEC_KN02_H 
#define __ASM_MIPS_DEC_KN02_H 

#include <asm/addrspace.h>

/*
 * Motherboard regs (kseg1 addresses)
 */
#define KN02_CSR_ADDR	KSEG1ADDR(0x1ff00000)	/* system control & status reg */

/*
 * Some port addresses...
 * FIXME: these addresses are incomplete and need tidying up!
 */
#define KN02_RTC_BASE	KSEG1ADDR(0x1fe80000)
#define KN02_DZ11_BASE	KSEG1ADDR(0x1fe00000)

#define KN02_CSR_BNK32M	(1<<10)			/* 32M stride */

/*
 * Interrupt enable Bits
 */
#define KN02_SLOT0	(1<<16)
#define KN02_SLOT1	(1<<17)
#define KN02_SLOT2	(1<<18)
#define KN02_SLOT5	(1<<21)
#define KN02_SLOT6	(1<<22)
#define KN02_SLOT7	(1<<23)

#endif /* __ASM_MIPS_DEC_KN02_H */
