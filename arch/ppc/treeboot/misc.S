/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "../kernel/ppc_asm.tmpl"

	.text

/*
 * Flush the dcache and invalidate the icache for a range of addresses.
 *
 * flush_cache(addr, len)
 */
	.global	flush_cache
flush_cache:
	mfpvr	r5			# Get processor version register
	extrwi	r5,r5,16,0		# Get the version bits
	cmpwi	cr0,r5,0x0020		# Is this a 403-based processor?
	beq	1f			# Yes, it is
	li	r5,32			# It is not a 403, set to 32 bytes
	addi	r4,r4,32-1		# len += line_size - 1
	srwi.	r4,r4,5			# Convert from bytes to lines
	b	2f
1:	li	r5,16			# It is a 403, set to 16 bytes
	addi	r4,r4,16-1		# len += line_size - 1
	srwi.	r4,r4,4			# Convert from bytes to lines
2:	mtctr	r4			# Set-up the counter register
	beqlr				# If it is 0, we are done
3:	dcbf	r0,r3			# Flush and invalidate the data line
	icbi	r0,r3			# Invalidate the instruction line
	add	r3,r3,r5		# Move to the next line
	bdnz	3b			# Are we done yet?
	sync
	isync
	blr				# Return to the caller
