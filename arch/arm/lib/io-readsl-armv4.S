/*
 *  linux/arch/arm/lib/io-readsl-armv4.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Note that some reads can be aligned on half-word boundaries.
 */
ENTRY(__raw_readsl)
		teq	r2, #0		@ do we have to check for the zero len?
		moveq	pc, lr
		ands	ip, r1, #3
		bne	2f

		subs	r2, r2, #4
		bmi	1001f
		stmfd	sp!, {r4, lr}
1000:		ldr	r3, [r0, #0]
		ldr	r4, [r0, #0]
		ldr	ip, [r0, #0]
		ldr	lr, [r0, #0]
		subs	r2, r2, #4
		stmia	r1!, {r3, r4, ip, lr}
		bpl	1000b
		ldmfd	sp!, {r4, lr}
1001:		tst	r2, #2
		ldrne	r3, [r0, #0]
		ldrne	ip, [r0, #0]
		stmneia	r1!, {r3, ip}
		tst	r2, #1
		ldrne	r3, [r0, #0]
		strne	r3, [r1, #0]
		mov	pc, lr

2:		cmp	ip, #2
		ldr	ip, [r0]
		blt	4f
		bgt	6f

#ifndef	__ARMEB__

		/* little endian code */

		strh	ip, [r1], #2
		mov	ip, ip, lsr #16
3:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #16
		strne	ip, [r1], #4
		movne	ip, r3, lsr #16
		bne	3b
		strh	ip, [r1], #2
		mov	pc, lr

4:		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strh	ip, [r1], #2
		mov	ip, ip, lsr #16
5:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #8
		strne	ip, [r1], #4
		movne	ip, r3, lsr #24
		bne	5b
		strb	ip, [r1], #1
		mov	pc, lr

6:		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
7:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #24
		strne	ip, [r1], #4
		movne	ip, r3, lsr #8
		bne	7b
		strh	ip, [r1], #2
		mov	ip, ip, lsr #16
		strb	ip, [r1]
		mov	pc, lr

#else

		/* big endian code */


		mov	r3, ip, lsr #16
		strh	r3, [r1], #2
3:		mov	r3, ip, lsl #16
		subs	r2, r2, #1
		ldrne	ip, [r0]
		orrne	r3, r3, ip, lsr #16
		strne	r3, [r1], #4
		bne	3b
 		strh	ip, [r1], #2
 		mov	pc, lr

4:		mov	r3, ip, lsr #24
		strb	r3, [r1], #1
		mov	r3, ip, lsr #8
		strh	r3, [r1], #2
5:		mov	r3, ip, lsl #24
		subs	r2, r2, #1
		ldrne	ip, [r0]
		orrne	r3, r3, ip, lsr #8
		strne	r3, [r1], #4
		bne	5b
		strb	ip, [r1], #1
		mov	pc, lr

6:		mov	r3, ip, lsr #24
		strb	r3, [r1], #1
7:		mov	r3, ip, lsl #8
		subs	r2, r2, #1
		ldrne	ip, [r0]
		orrne	r3, r3, ip, lsr #24
		strne	r3, [r1], #4
		bne	7b
		mov	r3, ip, lsr #8
		strh	r3, [r1], #2
		strb	ip, [r1], #1
		mov	pc, lr

#endif


