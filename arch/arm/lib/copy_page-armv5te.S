/*
 *  linux/arch/arm/lib/copypage-armv5te.S
 *
 *  Copyright (C) 2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/constants.h>

/*
 * General note:
 *  We don't really want write-allocate cache behaviour for these functions
 *  since that will just eat through 8K of the cache.
 */

	.text
	.align	5
/*
 * ARMv5TE optimised copy_user_page
 *  r0 = destination
 *  r1 = source
 *  r2 = virtual user address of ultimate destination page
 *
 * The source page may have some clean entries in the cache already, but we
 * can safely ignore them - break_cow() will flush them out of the cache
 * if we eventually end up using our copied page.
 *
 * What we could do is use the mini-cache to buffer reads from the source
 * page.  We rely on the mini-cache being smaller than one page, so we'll
 * cycle through the complete cache anyway.
 */
ENTRY(armv5te_copy_user_page)
	stmfd	sp!, {r4, r5, lr}
	mov	r5, r0
	mov	r0, r1
	bl	map_page_minicache
	mov	r1, r5
	mov	lr, #PAGE_SZ/32

1:	mov	ip, r1
	ldrd	r2, [r0], #8
	ldrd	r4, [r0], #8
	strd	r2, [r1], #8
	ldrd	r2, [r0], #8
	strd	r4, [r1], #8
	ldrd	r4, [r0], #8
	strd	r2, [r1], #8
	strd	r4, [r1], #8
	mcr	p15, 0, ip, c7, c10, 1		@ clean D line
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate D line
	subs	lr, lr, #1
	bne	1b

	ldmfd	sp!, {r4, r5, pc}

	.align	5
/*
 * ARMv5TE optimised clear_user_page
 *  r0 = destination
 *  r1 = virtual user address of ultimate destination page
 */
ENTRY(armv5te_clear_user_page)
	str	lr, [sp, #-4]!
	mov	r1, #PAGE_SZ/32
	mov	r2, #0
	mov	r3, #0
1:	mov	ip, r0
	strd	r2, [r0], #8
	strd	r2, [r0], #8
	strd	r2, [r0], #8
	strd	r2, [r0], #8
	mcr	p15, 0, ip, c7, c10, 1		@ clean D line
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate D line
	subs	r1, r1, #1
	bne	1b
	ldr	pc, [sp], #4
