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
 * Copyright (C) 2000  Maciej W. Rozycki
 *
 * These are addresses which have to be known early in the boot process.
 * For other addresses refer to tc.h ioasic_addrs.h and friends.
 */
#ifndef __ASM_MIPS_DEC_KN03_H 
#define __ASM_MIPS_DEC_KN03_H 

#include <asm/addrspace.h>

/*
 * Some port addresses...
 * FIXME: these addresses are incomplete and need tidying up!
 */
#define KN03_IOASIC_BASE	KSEG1ADDR(0x1f840000)	/* I/O ASIC */
#define KN03_RTC_BASE		KSEG1ADDR(0x1fa00000)	/* RTC */
#define KN03_MCR_BASE		KSEG1ADDR(0x1fac0000)	/* MCR */

#define KN03_MCR_BNK32M		(1<<10)			/* 32M stride */
#define KN03_MCR_ECCEN		(1<<13)			/* ECC enabled */

#define KN03_IOASIC_REG(r)	(KN03_IOASIC_BASE+(r))

#endif /* __ASM_MIPS_DEC_KN03_H */
