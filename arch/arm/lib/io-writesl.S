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

ENTRY(__arch_writesl)
		ands	ip, r1, #3
		bne	2f

1:		ldr	r3, [r1], #4
		str	r3, [r0]
		subs	r2, r2, #1
		bne	1b
		mov	pc, lr

2:		bic	r1, r1, #3
		cmp	ip, #2
		ldr	ip, [r1], #4
		mov	ip, ip, lsr #16
		blt	4f
		bgt	5f

3:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #16
		str	ip, [r0]
		mov	ip, r3, lsr #16
		subs	r2, r2, #1
		bne	3b
		mov	pc, lr

4:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #8
		str	ip, [r0]
		mov	ip, r3, lsr #24
		subs	r2, r2, #1
		bne	4b
		mov	pc, lr

5:		ldr	r3, [r1], #4
		orr	ip, ip, r3, lsl #24
		str	ip, [r0]
		mov	ip, r3, lsr #8
		subs	r2, r2, #1
		bne	5b
		mov	pc, lr


