/*
 * Copyright (C) 1999 Hewlett-Packard (Frank Rowand)
 * Copyright (C) 1999 Philipp Rumpf <prumpf@tux.org>
 * Copyright (C) 1999 SuSE GmbH
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _PARISC_ASSEMBLY_H
#define _PARISC_ASSEMBLY_H

#if defined(__LP64__) && defined(__ASSEMBLY__)
/* the 64-bit pa gnu assembler unfortunately defaults to .level 1.1 or 2.0 so
 * work around that for now... */
	.level 2.0w
#endif

#include <asm/offset.h>
#include <asm/page.h>

#include <asm/asmregs.h>

	sp	=	30
	gp	=	27
	ipsw	=	22

#if __PAGE_OFFSET == 0xc0000000
	.macro	tophys	gr
	zdep	\gr, 31, 30, \gr
	.endm
	
	.macro	tovirt	gr
	depi	3,1,2,\gr
	.endm
#else
#error	unknown __PAGE_OFFSET
#endif

	.macro delay value
	ldil	L%\value, 1
	ldo	R%\value(1), 1
	addib,UV,n -1,1,.
	addib,NUV,n -1,1,.+8
	nop
	.endm

	.macro	debug value
	.endm

#ifdef __LP64__
# define LDIL_FIXUP(reg) depdi 0,31,32,reg
#else
# define LDIL_FIXUP(reg)
#endif

	/* load 32-bit 'value' into 'reg' compensating for the ldil
	 * sign-extension when running in wide mode.
	 * WARNING!! neither 'value' nor 'reg' can be expressions
	 * containing '.'!!!! */
	.macro	load32 value, reg
	ldil	L%\value, \reg
	ldo	R%\value(\reg), \reg
	LDIL_FIXUP(\reg)
	.endm

#ifdef __LP64__
#define LDREG   ldd
#define STREG   std
#define RP_OFFSET	16
#else
#define LDREG   ldw
#define STREG   stw
#define RP_OFFSET	20
#endif

	.macro loadgp
#ifdef __LP64__
	ldil		L%__gp, %r27
	ldo		R%__gp(%r27), %r27
	LDIL_FIXUP(%r27)
#else
	ldil		L%$global$, %r27
	ldo		R%$global$(%r27), %r27
#endif
	.endm

#define SAVE_SP(r, where) mfsp r, %r1 ! STREG %r1, where
#define REST_SP(r, where) LDREG where, %r1 ! mtsp %r1, r
#define SAVE_CR(r, where) mfctl r, %r1 ! STREG %r1, where
#define REST_CR(r, where) LDREG where, %r1 ! mtctl %r1, r

	.macro	save_general	regs
	STREG %r2, PT_GR2 (\regs)
	STREG %r3, PT_GR3 (\regs)
	STREG %r4, PT_GR4 (\regs)
	STREG %r5, PT_GR5 (\regs)
	STREG %r6, PT_GR6 (\regs)
	STREG %r7, PT_GR7 (\regs)
	STREG %r8, PT_GR8 (\regs)
	STREG %r9, PT_GR9 (\regs)
	STREG %r10, PT_GR10(\regs)
	STREG %r11, PT_GR11(\regs)
	STREG %r12, PT_GR12(\regs)
	STREG %r13, PT_GR13(\regs)
	STREG %r14, PT_GR14(\regs)
	STREG %r15, PT_GR15(\regs)
	STREG %r16, PT_GR16(\regs)
	STREG %r17, PT_GR17(\regs)
	STREG %r18, PT_GR18(\regs)
	STREG %r19, PT_GR19(\regs)
	STREG %r20, PT_GR20(\regs)
	STREG %r21, PT_GR21(\regs)
	STREG %r22, PT_GR22(\regs)
	STREG %r23, PT_GR23(\regs)
	STREG %r24, PT_GR24(\regs)
	STREG %r25, PT_GR25(\regs)
	/* r26 is clobbered by cr19 and assumed to be saved before hand */
	STREG %r27, PT_GR27(\regs)
	STREG %r28, PT_GR28(\regs)
	/* r29 is already saved and points to PT_xxx struct */
	/* r30 stack pointer saved in get_stack */
	STREG %r31, PT_GR31(\regs)
	.endm

	.macro	rest_general	regs
	LDREG PT_GR2 (\regs), %r2
	LDREG PT_GR3 (\regs), %r3
	LDREG PT_GR4 (\regs), %r4
	LDREG PT_GR5 (\regs), %r5
	LDREG PT_GR6 (\regs), %r6
	LDREG PT_GR7 (\regs), %r7
	LDREG PT_GR8 (\regs), %r8
	LDREG PT_GR9 (\regs), %r9
	LDREG PT_GR10(\regs), %r10
	LDREG PT_GR11(\regs), %r11
	LDREG PT_GR12(\regs), %r12
	LDREG PT_GR13(\regs), %r13
	LDREG PT_GR14(\regs), %r14
	LDREG PT_GR15(\regs), %r15
	LDREG PT_GR16(\regs), %r16
	LDREG PT_GR17(\regs), %r17
	LDREG PT_GR18(\regs), %r18
	LDREG PT_GR19(\regs), %r19
	LDREG PT_GR20(\regs), %r20
	LDREG PT_GR21(\regs), %r21
	LDREG PT_GR22(\regs), %r22
	LDREG PT_GR23(\regs), %r23
	LDREG PT_GR24(\regs), %r24
	LDREG PT_GR25(\regs), %r25
	LDREG PT_GR26(\regs), %r26
	LDREG PT_GR27(\regs), %r27
	LDREG PT_GR28(\regs), %r28
	/* r30 stack pointer restored in rest_stack */
	LDREG PT_GR31(\regs), %r31
	.endm

	.macro	save_fp 	regs
	fstd,ma  %fr0, 8(\regs)
	fstd,ma	 %fr1, 8(\regs)
	fstd,ma	 %fr2, 8(\regs)
	fstd,ma	 %fr3, 8(\regs)
	fstd,ma	 %fr4, 8(\regs)
	fstd,ma	 %fr5, 8(\regs)
	fstd,ma	 %fr6, 8(\regs)
	fstd,ma	 %fr7, 8(\regs)
	fstd,ma	 %fr8, 8(\regs)
	fstd,ma	 %fr9, 8(\regs)
	fstd,ma	%fr10, 8(\regs)
	fstd,ma	%fr11, 8(\regs)
	fstd,ma	%fr12, 8(\regs)
	fstd,ma	%fr13, 8(\regs)
	fstd,ma	%fr14, 8(\regs)
	fstd,ma	%fr15, 8(\regs)
	fstd,ma	%fr16, 8(\regs)
	fstd,ma	%fr17, 8(\regs)
	fstd,ma	%fr18, 8(\regs)
	fstd,ma	%fr19, 8(\regs)
	fstd,ma	%fr20, 8(\regs)
	fstd,ma	%fr21, 8(\regs)
	fstd,ma	%fr22, 8(\regs)
	fstd,ma	%fr23, 8(\regs)
	fstd,ma	%fr24, 8(\regs)
	fstd,ma	%fr25, 8(\regs)
	fstd,ma	%fr26, 8(\regs)
	fstd,ma	%fr27, 8(\regs)
	fstd,ma	%fr28, 8(\regs)
	fstd,ma	%fr29, 8(\regs)
	fstd,ma	%fr30, 8(\regs)
	fstd	%fr31, 0(\regs)
	.endm

	.macro	rest_fp 	regs
	fldd	0(\regs),	 %fr31
	fldd,mb	-8(\regs),       %fr30
	fldd,mb	-8(\regs),       %fr29
	fldd,mb	-8(\regs),       %fr28
	fldd,mb	-8(\regs),       %fr27
	fldd,mb	-8(\regs),       %fr26
	fldd,mb	-8(\regs),       %fr25
	fldd,mb	-8(\regs),       %fr24
	fldd,mb	-8(\regs),       %fr23
	fldd,mb	-8(\regs),       %fr22
	fldd,mb	-8(\regs),       %fr21
	fldd,mb	-8(\regs),       %fr20
	fldd,mb	-8(\regs),       %fr19
	fldd,mb	-8(\regs),       %fr18
	fldd,mb	-8(\regs),       %fr17
	fldd,mb	-8(\regs),       %fr16
	fldd,mb	-8(\regs),       %fr15
	fldd,mb	-8(\regs),       %fr14
	fldd,mb	-8(\regs),       %fr13
	fldd,mb	-8(\regs),       %fr12
	fldd,mb	-8(\regs),       %fr11
	fldd,mb	-8(\regs),       %fr10
	fldd,mb	-8(\regs),       %fr9
	fldd,mb	-8(\regs),       %fr8
	fldd,mb	-8(\regs),       %fr7
	fldd,mb	-8(\regs),       %fr6
	fldd,mb	-8(\regs),       %fr5
	fldd,mb	-8(\regs),       %fr4
	fldd,mb	-8(\regs),       %fr3
	fldd,mb	-8(\regs),       %fr2
	fldd,mb	-8(\regs),       %fr1
	fldd,mb	-8(\regs),       %fr0
	.endm

#ifdef __LP64__
	.macro	callee_save
	ldo	144(%r30), %r30
	std	  %r3,	-144(%r30)
	std	  %r4,	-136(%r30)
	std	  %r5,	-128(%r30)
	std	  %r6,	-120(%r30)
	std	  %r7,	-112(%r30)
	std	  %r8,	-104(%r30)
	std	  %r9,	 -96(%r30)
	std	 %r10,	 -88(%r30)
	std	 %r11,	 -80(%r30)
	std	 %r12,	 -72(%r30)
	std	 %r13,	 -64(%r30)
	std	 %r14,	 -56(%r30)
	std	 %r15,	 -48(%r30)
	std	 %r16,	 -40(%r30)
	std	 %r17,	 -32(%r30)
	std	 %r18,	 -24(%r30)
	.endm

	.macro	callee_rest
	ldd	 -24(%r30),   %r18
	ldd	 -32(%r30),   %r17
	ldd	 -40(%r30),   %r16
	ldd	 -48(%r30),   %r15
	ldd	 -56(%r30),   %r14
	ldd	 -64(%r30),   %r13
	ldd	 -72(%r30),   %r12
	ldd	 -80(%r30),   %r11
	ldd	 -88(%r30),   %r10
	ldd	 -96(%r30),    %r9
	ldd	-104(%r30),    %r8
	ldd	-112(%r30),    %r7
	ldd	-120(%r30),    %r6
	ldd	-128(%r30),    %r5
	ldd	-136(%r30),    %r4
	ldd	-144(%r30),    %r3
	ldo	-144(%r30),   %r30
	.endm

#else /* __LP64__ */

	.macro	callee_save
	ldo	128(30), 30
	stw	 3,	-128(30)
	stw	 4,	-124(30)
	stw	 5,	-120(30)
	stw	 6,	-116(30)
	stw	 7,	-112(30)
	stw	 8,	-108(30)
	stw	 9,	-104(30)
	stw	 10,	-100(30)
	stw	 11,	 -96(30)
	stw	 12,	 -92(30)
	stw	 13,	 -88(30)
	stw	 14,	 -84(30)
	stw	 15,	 -80(30)
	stw	 16,	 -76(30)
	stw	 17,	 -72(30)
	stw	 18,	 -68(30)
	.endm

	.macro	callee_rest
	ldw	 -68(30),   18
	ldw	 -72(30),   17
	ldw	 -76(30),   16
	ldw	 -80(30),   15
	ldw	 -84(30),   14
	ldw	 -88(30),   13
	ldw	 -92(30),   12
	ldw	 -96(30),   11
	ldw	-100(30),   10
	ldw	-104(30),    9
	ldw	-108(30),    8
	ldw	-112(30),    7
	ldw	-116(30),    6
	ldw	-120(30),    5
	ldw	-124(30),    4
	ldw	-128(30),    3
	ldo	-128(30),   30
	.endm
#endif /* __LP64__ */

	.macro	save_specials	regs

	SAVE_SP  (%sr0, PT_SR0 (\regs))
	SAVE_SP  (%sr1, PT_SR1 (\regs))
	SAVE_SP  (%sr2, PT_SR2 (\regs))
	SAVE_SP  (%sr3, PT_SR3 (\regs))
	SAVE_SP  (%sr4, PT_SR4 (\regs))
	SAVE_SP  (%sr5, PT_SR5 (\regs))
	SAVE_SP  (%sr6, PT_SR6 (\regs))
	SAVE_SP  (%sr7, PT_SR7 (\regs))

	SAVE_CR  (%cr17, PT_IASQ0(\regs))
	mtctl	 %r0,	%cr17
	SAVE_CR  (%cr17, PT_IASQ1(\regs))

	SAVE_CR  (%cr18, PT_IAOQ0(\regs))
	mtctl	 %r0,	%cr18
	SAVE_CR  (%cr18, PT_IAOQ1(\regs))

	SAVE_CR  (%cr11, PT_SAR  (\regs))
	SAVE_CR  (%cr22, PT_PSW  (\regs))
	SAVE_CR  (%cr19, PT_IIR  (\regs))
	SAVE_CR  (%cr28, PT_GR1  (\regs))
	SAVE_CR  (%cr31, PT_GR29 (\regs))

	STREG	%r26,	PT_GR26 (\regs)
	mfctl	%cr29,	%r26
	.endm

	.macro	rest_specials	regs

	REST_SP  (%sr0, PT_SR0 (\regs))
	REST_SP  (%sr1, PT_SR1 (\regs))
	REST_SP  (%sr2, PT_SR2 (\regs))
	REST_SP  (%sr3, PT_SR3 (\regs))
	REST_SP  (%sr4, PT_SR4 (\regs))
	REST_SP  (%sr5, PT_SR5 (\regs))
	REST_SP  (%sr6, PT_SR6 (\regs))
	REST_SP  (%sr7, PT_SR7 (\regs))

	REST_CR	(%cr17, PT_IASQ0(\regs))
	REST_CR	(%cr17, PT_IASQ1(\regs))

	REST_CR	(%cr18, PT_IAOQ0(\regs))
	REST_CR	(%cr18, PT_IAOQ1(\regs))

	REST_CR (%cr11, PT_SAR	(\regs))

	REST_CR	(%cr22, PT_PSW	(\regs))
	.endm

#endif
