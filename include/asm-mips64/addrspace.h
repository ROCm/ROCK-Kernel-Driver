/* $Id: addrspace.h,v 1.5 2000/02/01 00:32:01 kanoj Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1999 by Ralf Baechle
 * Copyright (C) 1990, 1999 by Silicon Graphics, Inc.
 */
#ifndef _ASM_ADDRSPACE_H
#define _ASM_ADDRSPACE_H

#include <linux/config.h>

/*
 * Memory segments (32bit kernel mode addresses)
 */
#define KUSEG                   0x0000000000000000
#define KSEG0                   0xffffffff80000000
#define KSEG1                   0xffffffffa0000000
#define KSEG2                   0xffffffffc0000000
#define KSEG3                   0xffffffffe0000000

/*
 * Returns the kernel segment base of a given address
 */
#define KSEGX(a)                (((unsigned long)(a)) & 0xe0000000)

/*
 * Returns the physical address of a KSEG0/KSEG1 address
 */
#define CPHYSADDR(a)		(((unsigned long)(a)) & 0x000000001fffffffUL)
#define PHYSADDR(a)		(((unsigned long)(a)) & 0x000000ffffffffffUL)

/*
 * Map an address to a certain kernel segment
 */
#define KSEG0ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x000000ffffffffffUL) | KSEG0))
#define KSEG1ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x000000ffffffffffUL) | KSEG1))
#define KSEG2ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x000000ffffffffffUL) | KSEG2))
#define KSEG3ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x000000ffffffffffUL) | KSEG3))

/*
 * Memory segments (64bit kernel mode addresses)
 */
#define XKUSEG                  0x0000000000000000
#define XKSSEG                  0x4000000000000000
#define XKPHYS                  0x8000000000000000
#define XKSEG                   0xc000000000000000
#define CKSEG0                  0xffffffff80000000
#define CKSEG1                  0xffffffffa0000000
#define CKSSEG                  0xffffffffc0000000
#define CKSEG3                  0xffffffffe0000000

#if defined (CONFIG_CPU_R4300)						\
    || defined (CONFIG_CPU_R4X00)					\
    || defined (CONFIG_CPU_R5000)					\
    || defined (CONFIG_CPU_NEVADA)
#define	KUSIZE		0x0000010000000000		/* 2^^40 */
#define	KUSIZE_64	0x0000010000000000		/* 2^^40 */
#define	K0SIZE		0x0000001000000000		/* 2^^36 */
#define	K1SIZE		0x0000001000000000		/* 2^^36 */
#define	K2SIZE		0x000000ff80000000
#define	KSEGSIZE	0x000000ff80000000		/* max syssegsz */
#define TO_PHYS_MASK	0x0000000fffffffff		/* 2^^36 - 1 */
#endif

#if defined (CONFIG_CPU_R8000)
/* We keep KUSIZE consistent with R4000 for now (2^^40) instead of (2^^48) */
#define	KUSIZE		0x0000010000000000		/* 2^^40 */
#define	KUSIZE_64	0x0000010000000000		/* 2^^40 */
#define	K0SIZE		0x0000010000000000		/* 2^^40 */
#define	K1SIZE		0x0000010000000000		/* 2^^40 */
#define	K2SIZE		0x0001000000000000
#define	KSEGSIZE	0x0000010000000000		/* max syssegsz */
#define TO_PHYS_MASK	0x000000ffffffffff		/* 2^^40 - 1 */
#endif

#if defined (CONFIG_CPU_R10000)
#define	KUSIZE		0x0000010000000000		/* 2^^40 */
#define	KUSIZE_64	0x0000010000000000		/* 2^^40 */
#define	K0SIZE		0x0000010000000000		/* 2^^40 */
#define	K1SIZE		0x0000010000000000		/* 2^^40 */
#define	K2SIZE		0x00000fff80000000
#define	KSEGSIZE	0x00000fff80000000		/* max syssegsz */
#define TO_PHYS_MASK	0x000000ffffffffff		/* 2^^40 - 1 */
#endif

/*
 * Further names for SGI source compatibility.  These are stolen from
 * IRIX's <sys/mips_addrspace.h>.
 */
#define KUBASE		0
#define KUSIZE_32	0x0000000080000000	/* KUSIZE for a 32 bit proc */
#define K0BASE		0xa800000000000000
#define K0BASE_EXL_WR	K0BASE			/* exclusive on write */
#define K0BASE_NONCOH	0x9800000000000000	/* noncoherent */
#define K0BASE_EXL	0xa000000000000000	/* exclusive */

#ifdef CONFIG_SGI_IP27
#define K1BASE		0x9600000000000000	/* Uncached attr 3, uncac */
#else
#define K1BASE		0x9000000000000000
#endif
#define K2BASE		0xc000000000000000

#if !defined (CONFIG_CPU_R8000)
#define COMPAT_K1BASE32		0xffffffffa0000000
#define PHYS_TO_COMPATK1(x)	((x) | COMPAT_K1BASE32) /* 32-bit compat k1 */
#endif

#define KDM_TO_PHYS(x)	((unsigned long)(x) & TO_PHYS_MASK)
#define PHYS_TO_K0(x)	((unsigned long)(x) | K0BASE)

#endif /* _ASM_ADDRSPACE_H */
