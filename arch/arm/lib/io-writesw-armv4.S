/*
 *  linux/arch/arm/lib/io-writesw-armv4.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.macro	outword, rd
#ifndef __ARMEB__
		strh	\rd, [r0]
		mov	\rd, \rd, lsr #16
		strh	\rd, [r0]
#else
		mov	lr, \rd, lsr #16
		strh	lr, [r0]
		strh	\rd, [r0]
#endif
		.endm

.outsw_bad_alignment:
		adr	r0, .outsw_bad_align_msg
		mov	r2, lr
		b	panic
.outsw_bad_align_msg:
		.asciz	"outsw: bad buffer alignment (0x%p, lr=0x%08lX)\n"
		.align

.outsw_align:	tst	r1, #1
		bne	.outsw_bad_alignment

		ldrh	r3, [r1], #2
		strh	r3, [r0]

		subs	r2, r2, #1
		RETINSTR(moveq, pc, lr)

ENTRY(__raw_writesw)
		teq	r2, #0		@ do we have to check for the zero len?
		moveq	pc, lr
		tst	r1, #3
		bne	.outsw_align

		stmfd	sp!, {r4, r5, lr}

		subs	r2, r2, #8
		bmi	.no_outsw_8

.outsw_8_lp:	ldmia	r1!, {r3, r4, r5, ip}
		outword	r3
		outword	r4
		outword	r5
		outword	ip
		subs	r2, r2, #8
		bpl	.outsw_8_lp

		tst	r2, #7
		LOADREGS(eqfd, sp!, {r4, r5, pc})

.no_outsw_8:	tst	r2, #4
		beq	.no_outsw_4

		ldmia	r1!, {r3, ip}
		outword	r3
		outword	ip

.no_outsw_4:	tst	r2, #2
		beq	.no_outsw_2

		ldr	r3, [r1], #4
		outword	r3

.no_outsw_2:	tst	r2, #1
		ldrneh	r3, [r1]
		strneh	r3, [r0]

		LOADREGS(fd, sp!, {r4, r5, pc})
