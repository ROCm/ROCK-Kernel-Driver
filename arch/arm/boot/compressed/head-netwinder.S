/*
 *  linux/arch/arm/boot/compressed/head-netwinder.S
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define K(a,b,c)	((a) << 24 | (b) << 12 | (c))

		.section	".start", #alloc, #execinstr

		/*
		 * check to see if we are running from the correct address.
		 * If not, we move ourselves in a two stage process.  Firstly,
		 * we copy the start of the kernel (which includes this code)
		 * to 0x8000, and then jump to this code to continue with the
		 * rest (since this code will get overwritten).
		 */
		adr	r2, 1f
		ldmdb	r2, {r9, r10}
		and	r3, r2, #0xc000
		teq	r3, #0x8000	@ correctly located?
		beq	2f		@ skip this code
		bic	r3, r2, #0xc000
		orr	r3, r3, #0x8000
		mov	r0, r3		@ new address if '1'
		mov	r4, #64		@ number of bytes to copy
		sub	r5, r10, r9	@ total number of bytes to copy
		b	1f

		.word	_start
		.word	__bss_start

1:
		.rept	4
		ldmia	r2!, {r6, r9, r10, r11}
		stmia	r3!, {r6, r9, r10, r11}
		.endr
		subs	r4, r4, #64
		bcs	1b
		movs	r4, r5		@ remaining length
		mov	r5, #0		@ no more to copy
		movne	pc, r0		@ jump back to 1 (in the newly copied
					@ code)
		mov	r7, #5		@ only here to fix NeTTroms which dont
		mov	r8, #2 << 24		@ scheduled for removal in 2.5.xx
		orr	r8, r8, #5 << 12
2:
