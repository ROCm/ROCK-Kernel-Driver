/*
 *  linux/arch/arm/lib/csumpartial.S
 *
 *  Copyright (C) 1995-1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text

/* Function: __u32 csum_partial(const char *src, int len, __u32)
 * Params  : r0 = buffer, r1 = len, r2 = checksum
 * Returns : r0 = new checksum
 */

ENTRY(csum_partial)
		tst	r0, #2
		beq	1f
		subs	r1, r1, #2
		addmi	r1, r1, #2
		bmi	3f
		bic	r0, r0, #3
		ldr	r3, [r0], #4
		adds	r2, r2, r3, lsr #16
		adcs	r2, r2, #0
1:		adds	r2, r2, #0
		bics	ip, r1, #31
		beq	3f
		stmfd	sp!, {r4 - r6}
2:		ldmia	r0!, {r3 - r6}
		adcs	r2, r2, r3
		adcs	r2, r2, r4
		adcs	r2, r2, r5
		adcs	r2, r2, r6
		ldmia	r0!, {r3 - r6}
		adcs	r2, r2, r3
		adcs	r2, r2, r4
		adcs	r2, r2, r5
		adcs	r2, r2, r6
		sub	ip, ip, #32
		teq	ip, #0
		bne	2b
		adcs	r2, r2, #0
		ldmfd	sp!, {r4 - r6}
3:		ands	ip, r1, #0x1c
		beq	5f
4:		ldr	r3, [r0], #4
		sub	ip, ip, #4
		adcs	r2, r2, r3
		teq	ip, #0
		bne	4b
		adcs	r2, r2, #0
5:		ands	ip, r1, #3
		moveq	r0, r2
		RETINSTR(moveq,pc,lr)
		mov	ip, ip, lsl #3
		ldr	r3, [r0]
		rsb	ip, ip, #32
		mov	r3, r3, lsl ip
		adds	r2, r2, r3, lsr ip
		adc	r0, r2, #0
		RETINSTR(mov,pc,lr)


