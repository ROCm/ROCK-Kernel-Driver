/*
 * arch/ppc/kernel/ppc_asm.h
 *
 * Definitions used by various bits of low-level assembly code on PowerPC.
 *
 * Copyright (C) 1995-1999 Gary Thomas, Paul Mackerras, Cort Dougan.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>

#include "ppc_asm.tmpl"
#include "ppc_defs.h"

/*
 * Macros for storing registers into and loading registers from
 * exception frames.
 */
#define SAVE_GPR(n, base)	stw	n,GPR0+4*(n)(base)
#define SAVE_2GPRS(n, base)	SAVE_GPR(n, base); SAVE_GPR(n+1, base)
#define SAVE_4GPRS(n, base)	SAVE_2GPRS(n, base); SAVE_2GPRS(n+2, base)
#define SAVE_8GPRS(n, base)	SAVE_4GPRS(n, base); SAVE_4GPRS(n+4, base)
#define SAVE_10GPRS(n, base)	SAVE_8GPRS(n, base); SAVE_2GPRS(n+8, base)
#define REST_GPR(n, base)	lwz	n,GPR0+4*(n)(base)
#define REST_2GPRS(n, base)	REST_GPR(n, base); REST_GPR(n+1, base)
#define REST_4GPRS(n, base)	REST_2GPRS(n, base); REST_2GPRS(n+2, base)
#define REST_8GPRS(n, base)	REST_4GPRS(n, base); REST_4GPRS(n+4, base)
#define REST_10GPRS(n, base)	REST_8GPRS(n, base); REST_2GPRS(n+8, base)

#define SAVE_FPR(n, base)	stfd	n,THREAD_FPR0+8*(n)(base)
#define SAVE_2FPRS(n, base)	SAVE_FPR(n, base); SAVE_FPR(n+1, base)
#define SAVE_4FPRS(n, base)	SAVE_2FPRS(n, base); SAVE_2FPRS(n+2, base)
#define SAVE_8FPRS(n, base)	SAVE_4FPRS(n, base); SAVE_4FPRS(n+4, base)
#define SAVE_16FPRS(n, base)	SAVE_8FPRS(n, base); SAVE_8FPRS(n+8, base)
#define SAVE_32FPRS(n, base)	SAVE_16FPRS(n, base); SAVE_16FPRS(n+16, base)
#define REST_FPR(n, base)	lfd	n,THREAD_FPR0+8*(n)(base)
#define REST_2FPRS(n, base)	REST_FPR(n, base); REST_FPR(n+1, base)
#define REST_4FPRS(n, base)	REST_2FPRS(n, base); REST_2FPRS(n+2, base)
#define REST_8FPRS(n, base)	REST_4FPRS(n, base); REST_4FPRS(n+4, base)
#define REST_16FPRS(n, base)	REST_8FPRS(n, base); REST_8FPRS(n+8, base)
#define REST_32FPRS(n, base)	REST_16FPRS(n, base); REST_16FPRS(n+16, base)

/*
 * Once a version of gas that understands the AltiVec instructions
 * is freely available, we can do this the normal way...  - paulus
 */
#define LVX(r,a,b)	.long	(31<<26)+((r)<<21)+((a)<<16)+((b)<<11)+(103<<1)
#define STVX(r,a,b)	.long	(31<<26)+((r)<<21)+((a)<<16)+((b)<<11)+(231<<1)
#define MFVSCR(r)	.long	(4<<26)+((r)<<21)+(1540<<1)
#define MTVSCR(r)	.long	(4<<26)+((r)<<11)+(802<<1)

#define SAVE_VR(n,b,base)	li b,THREAD_VR0+(16*(n)); STVX(n,b,base)
#define SAVE_2VR(n,b,base)	SAVE_VR(n,b,base); SAVE_VR(n+1,b,base) 
#define SAVE_4VR(n,b,base)	SAVE_2VR(n,b,base); SAVE_2VR(n+2,b,base) 
#define SAVE_8VR(n,b,base)	SAVE_4VR(n,b,base); SAVE_4VR(n+4,b,base) 
#define SAVE_16VR(n,b,base)	SAVE_8VR(n,b,base); SAVE_8VR(n+8,b,base)
#define SAVE_32VR(n,b,base)	SAVE_16VR(n,b,base); SAVE_16VR(n+16,b,base)
#define REST_VR(n,b,base)	li b,THREAD_VR0+(16*(n)); LVX(n,b,base)
#define REST_2VR(n,b,base)	REST_VR(n,b,base); REST_VR(n+1,b,base) 
#define REST_4VR(n,b,base)	REST_2VR(n,b,base); REST_2VR(n+2,b,base) 
#define REST_8VR(n,b,base)	REST_4VR(n,b,base); REST_4VR(n+4,b,base) 
#define REST_16VR(n,b,base)	REST_8VR(n,b,base); REST_8VR(n+8,b,base) 
#define REST_32VR(n,b,base)	REST_16VR(n,b,base); REST_16VR(n+16,b,base)

#define SYNC \
	sync; \
	isync

/*
 * This instruction is not implemented on the PPC 603 or 601; however, on
 * the 403GCX and 405GP tlbia IS defined and tlbie is not.
 * All of these instructions exist in the 8xx, they have magical powers,
 * and they must be used.
 */

#if !defined(CONFIG_4xx) && !defined(CONFIG_8xx)
#define tlbia					\
	li	r4,1024;			\
	mtctr	r4;				\
	lis	r4,KERNELBASE@h;		\
0:	tlbie	r4;				\
	addi	r4,r4,0x1000;			\
	bdnz	0b
#endif

/*
 * On APUS (Amiga PowerPC cpu upgrade board), we don't know the
 * physical base address of RAM at compile time.
 */
#define tophys(rd,rs)				\
0:	addis	rd,rs,-KERNELBASE@h;		\
	.section ".vtop_fixup","aw";		\
	.align  1;				\
	.long   0b;				\
	.previous

#define tovirt(rd,rs)				\
0:	addis	rd,rs,KERNELBASE@h;		\
	.section ".ptov_fixup","aw";		\
	.align  1;				\
	.long   0b;				\
	.previous

/*
 * On 64-bit cpus, we use the rfid instruction instead of rfi, but
 * we then have to make sure we preserve the top 32 bits except for
 * the 64-bit mode bit, which we clear.
 */
#ifdef CONFIG_PPC64BRIDGE
#define	FIX_SRR1(ra, rb)	\
	mr	rb,ra;		\
	mfmsr	ra;		\
	clrldi	ra,ra,1;		/* turn off 64-bit mode */ \
	rldimi	ra,rb,0,32
#define	RFI		.long	0x4c000024	/* rfid instruction */
#define MTMSRD(r)	.long	(0x7c000164 + ((r) << 21))	/* mtmsrd */
#define CLR_TOP32(r)	rlwinm	(r),(r),0,0,31	/* clear top 32 bits */

#else
#define FIX_SRR1(ra, rb)
#define	RFI		rfi
#define MTMSRD(r)	mtmsr	r
#define CLR_TOP32(r)
#endif /* CONFIG_PPC64BRIDGE */
