/*
 *  linux/arch/arm/lib/delay.S
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

LC0:		.word	SYMBOL_NAME(loops_per_sec)

ENTRY(udelay)
		mov	r2, #0x1000
		orr	r2, r2, #0x00c6
		mul	r1, r0, r2
		ldr	r2, LC0
		ldr	r2, [r2]
		mov	r1, r1, lsr #11
		mov	r2, r2, lsr #11
		mul	r0, r1, r2
		movs	r0, r0, lsr #10
		RETINSTR(moveq,pc,lr)

@ Delay routine
ENTRY(__delay)
		subs	r0, r0, #1
		RETINSTR(movcc,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movcc,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movcc,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movcc,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movcc,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movcc,pc,lr)
		subs	r0, r0, #1
		RETINSTR(movcc,pc,lr)
		subs	r0, r0, #1
		bcs	SYMBOL_NAME(__delay)
		RETINSTR(mov,pc,lr)
