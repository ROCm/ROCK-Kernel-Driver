/*
 *  linux/arch/arm/lib/csumpartialcopy.S
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

/* Function: __u32 csum_partial_copy_nocheck(const char *src, char *dst, int len, __u32 sum)
 * Params  : r0 = src, r1 = dst, r2 = len, r3 = checksum
 * Returns : r0 = new checksum
 */

		.macro	save_regs
		stmfd	sp!, {r4 - r8, fp, ip, lr, pc}
		.endm

		.macro	load_regs,flags
		LOADREGS(\flags,fp,{r4 - r8, fp, sp, pc})
		.endm

		.macro	load1b, reg1
		ldrb	\reg1, [r0], #1
		.endm

		.macro	load2b, reg1, reg2
		ldrb	\reg1, [r0], #1
		ldrb	\reg2, [r0], #1
		.endm

		.macro	load1l, reg1
		ldr	\reg1, [r0], #4
		.endm

		.macro	load2l, reg1, reg2
		ldr	\reg1, [r0], #4
		ldr	\reg2, [r0], #4
		.endm

		.macro	load4l, reg1, reg2, reg3, reg4
		ldmia	r0!, {\reg1, \reg2, \reg3, \reg4}
		.endm

ENTRY(csum_partial_copy_nocheck)
		mov	ip, sp
		save_regs
		sub	fp, ip, #4
		cmp	r2, #4
		blt	.too_small
		tst	r1, #2			@ Test destination alignment
		beq	.dst_aligned
		load2b	ip, r8
		subs	r2, r2, #2		@ We do not know if SRC is aligned...
		orr	ip, ip, r8, lsl #8
		adds	r3, r3, ip
		adcs	r3, r3, #0
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1		@ Destination now aligned
.dst_aligned:	tst	r0, #3
		bne	.src_not_aligned
		adds	r3, r3, #0
		bics	ip, r2, #15		@ Routine for src & dst aligned
		beq	2f
1:		load4l	r4, r5, r6, r7
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r4, r5
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		tst	ip, #4
		beq	4f
3:		load1l	r4
		str	r4, [r1], #4
		adcs	r3, r3, r4
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		load_regs	eqea
		load1l	r4
		tst	r2, #2
		beq	.exit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
.exit:		tst	r2, #1
		strneb	r4, [r1], #1
		andne	r4, r4, #255
		adcnes	r3, r3, r4
		adcs	r0, r3, #0
		load_regs	ea

.too_small:	teq	r2, #0
		load_regs	eqea
		cmp	r2, #2
		blt	.too_small1
		load2b	ip, r8
		orr	ip, ip, r8, lsl #8
		adds	r3, r3, ip
		strb	ip, [r1], #1
		strb	r8, [r1], #1
		tst	r2, #1
.too_small1:				@ C = 0
		beq	.csum_exit
		load1b	ip
		strb	ip, [r1], #1
		adcs	r3, r3, ip
.csum_exit:	adc	r0, r3, #0
		load_regs	ea

.src_not_aligned:
		cmp	r2, #4
		blt	.too_small
		and	ip, r0, #3
		bic	r0, r0, #3
		load1l	r4
		cmp	ip, #2
		beq	.src2_aligned
		bhi	.src3_aligned
		mov	r4, r4, lsr #8
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		mov	r6, r6, lsr #8
		orr	r6, r6, r7, lsl #24
		mov	r7, r7, lsr #8
		orr	r7, r7, r8, lsl #24
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #8
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lsl #24
		mov	r5, r5, lsr #8
		orr	r5, r5, r6, lsl #24
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #8
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lsl #24
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #8
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		load_regs	eqea
		tst	r2, #2
		beq	.exit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		b	.exit

.src2_aligned:	mov	r4, r4, lsr #16
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		mov	r6, r6, lsr #16
		orr	r6, r6, r7, lsl #16
		mov	r7, r7, lsr #16
		orr	r7, r7, r8, lsl #16
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #16
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lsl #16
		mov	r5, r5, lsr #16
		orr	r5, r5, r6, lsl #16
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #16
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lsl #16
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #16
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		load_regs	eqea
		tst	r2, #2
		beq	.exit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		mov	r4, r4, lsr #8
		strb	r4, [r1], #1
		tst	r2, #1
		adceq	r0, r3, #0
		load_regs	eqea
		load1b	r4
		b	.exit

.src3_aligned:	mov	r4, r4, lsr #24
		adds	r3, r3, #0
		bics	ip, r2, #15
		beq	2f
1:		load4l	r5, r6, r7, r8
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		mov	r6, r6, lsr #24
		orr	r6, r6, r7, lsl #8
		mov	r7, r7, lsr #24
		orr	r7, r7, r8, lsl #8
		stmia	r1!, {r4, r5, r6, r7}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		adcs	r3, r3, r6
		adcs	r3, r3, r7
		mov	r4, r8, lsr #24
		sub	ip, ip, #16
		teq	ip, #0
		bne	1b
2:		ands	ip, r2, #12
		beq	4f
		tst	ip, #8
		beq	3f
		load2l	r5, r6
		orr	r4, r4, r5, lsl #8
		mov	r5, r5, lsr #24
		orr	r5, r5, r6, lsl #8
		stmia	r1!, {r4, r5}
		adcs	r3, r3, r4
		adcs	r3, r3, r5
		mov	r4, r6, lsr #24
		tst	ip, #4
		beq	4f
3:		load1l	r5
		orr	r4, r4, r5, lsl #8
		str	r4, [r1], #4
		adcs	r3, r3, r4
		mov	r4, r5, lsr #24
4:		ands	r2, r2, #3
		adceq	r0, r3, #0
		load_regs	eqea
		tst	r2, #2
		beq	.exit
		adcs	r3, r3, r4, lsl #16
		strb	r4, [r1], #1
		load1l	r4
		strb	r4, [r1], #1
		adcs	r3, r3, r4, lsl #24
		mov	r4, r4, lsr #8
		b	.exit
