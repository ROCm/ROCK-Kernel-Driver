/*
 * Hardware info about DEC DECstation 5000/2x0 systems (otherwise known
 * as 3max+ or kn03.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995,1996 by Paul M. Antoine, some code and definitions
 * are by curteousy of Chris Fraser.
 *
 * These are addresses which have to be known early in the boot process.
 * For other addresses refer to tc.h ioasic_addrs.h and friends.
 */
#ifndef __ASM_MIPS_DEC_KN03_H 
#define __ASM_MIPS_DEC_KN03_H 

#include <asm/addrspace.h>

/*
 * Motherboard regs (kseg1 addresses)
 */
#define KN03_SSR_ADDR	KSEG1ADDR(0x1f840100)	/* system control & status reg */
#define KN03_SIR_ADDR	KSEG1ADDR(0x1f840110)	/* system interrupt reg */
#define KN03_SIRM_ADDR	KSEG1ADDR(0x1f840120)	/* system interrupt mask reg */

/*
 * Some port addresses...
 * FIXME: these addresses are incomplete and need tidying up!
 */
#define KN03_RTC_BASE	(KSEG1ADDR(0x1f800000 + 0x200000)) /* ASIC + SL8 */

#endif /* __ASM_MIPS_DEC_KN03_H */
