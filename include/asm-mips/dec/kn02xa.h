/*
 * Hardware info about DEC DECstation 5000/1xx systems (otherwise known
 * as 3min or kn02ba. Apllies to the Personal DECstations 5000/xx (otherwise known
 * as maxine or kn02ca) as well.
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
#ifndef __ASM_MIPS_DEC_KN02XA_H 
#define __ASM_MIPS_DEC_KN02XA_H 

#include <asm/addrspace.h>

/*
 * Some port addresses...
 * FIXME: these addresses are incomplete and need tidying up!
 */
#define KN02XA_IOASIC_BASE	KSEG1ADDR(0x1c040000)	/* I/O ASIC */
#define KN02XA_RTC_BASE		KSEG1ADDR(0x1c200000)	/* RTC */

#define KN02XA_IOASIC_REG(r)	(KN02XA_IOASIC_BASE+(r))

#endif /* __ASM_MIPS_DEC_KN02XA_H */
