/*
 *  linux/include/asm-arm/arch-ebsa110/uncompress.h
 *
 *  Copyright (C) 1996,1997,1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This does not append a newline
 */
static void puts(const char *s)
{
	__asm__ __volatile__("
	ldrb	%0, [%2], #1
	teq	%0, #0
	beq	3f
1:	strb	%0, [%3]
2:	ldrb	%1, [%3, #0x14]
	and	%1, %1, #0x60
	teq	%1, #0x60
	bne	2b
	teq	%0, #'\n'
	moveq	%0, #'\r'
	beq	1b
	ldrb	%0, [%2], #1
	teq	%0, #0
	bne	1b
3:	ldrb	%1, [%3, #0x14]
	and	%1, %1, #0x60
	teq	%1, #0x60
	bne	3b
	" : : "r" (0), "r" (0), "r" (s), "r" (0xf0000be0) : "cc");
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
