/*
 *  linux/arch/arm/lib/io-readsb.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>

.outsw_bad_alignment:
		adr	r0, .outsw_bad_align_msg
		mov	r2, lr
		b	SYMBOL_NAME(panic)
.outsw_bad_align_msg:
		.asciz	"outsw: bad buffer alignment (0x%p, lr=0x%08lX)\n"
		.align

.outsw_align:	tst	r1, #1
		bne	.outsw_bad_alignment

		ldrh	r3, [r1], #2
		strh	r3, [r0]

		subs	r2, r2, #1
		RETINSTR(moveq, pc, lr)

ENTRY(__arch_writesw)
		tst	r1, #3
		bne	.outsw_align

		subs	r2, r2, #8
		bmi	.no_outsw_8

		stmfd	sp!, {r4, r5, lr}

.outsw_8_lp:	ldmia	r1!, {r3, r4, r5, ip}

		strh	r3, [r0]
		mov	r3, r3, lsr #16
		strh	r3, [r0]
		strh	r4, [r0]
		mov	r4, r4, lsr #16
		strh	r4, [r0]
		strh	r5, [r0]
		mov	r5, r5, lsr #16
		strh	r5, [r0]
		strh	ip, [r0]
		mov	ip, ip, lsr #16
		strh	ip, [r0]

		subs	r2, r2, #8
		bpl	.outsw_8_lp
		ldmfd	sp!, {r4, r5, lr}
		tst	r2, #7
		RETINSTR(moveq, pc, lr)

.no_outsw_8:	tst	r2, #4
		ldmneia	r1!, {r3, ip}
		strneh	r3, [r0]
		movne	r3, r3, lsr #16
		strneh	r3, [r0]
		strneh	ip, [r0]
		movne	ip, ip, lsr #16
		strneh	ip, [r0]
		tst	r2, #2
		ldrne	r3, [r1], #4
		strneh	r3, [r0]
		movne	r3, r3, lsr #16
		strneh	r3, [r0]
		tst	r2, #1
		ldrneh	r3, [r1]
		strneh	r3, [r0]
		RETINSTR(mov, pc, lr)


