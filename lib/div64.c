/*
 * Copyright (C) 2003 Bernardo Innocenti <bernie@develer.com>
 *
 * Based on former do_div() implementation from asm-parisc/div64.h:
 *	Copyright (C) 1999 Hewlett-Packard Co
 *	Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 *
 * Generic C version of 64bit/32bit division and modulo, with
 * 64bit result and 32bit remainder.
 *
 * The fast case for (n>>32 == 0) is handled inline by do_div(). 
 *
 * Code generated for this function might be very inefficient
 * for some CPUs. __div64_32() can be overridden by linking arch-specific
 * assembly versions such as arch/ppc/lib/div64.S and arch/sh/lib/div64.S.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <asm/div64.h>

/* Not needed on 64bit architectures */
#if BITS_PER_LONG == 32

uint32_t __div64_32(uint64_t *n, uint32_t base)
{
	uint32_t low, low2, high, rem;

	low   = *n   & 0xffffffff;
	high  = *n  >> 32;
	rem   = high % (uint32_t)base;
	high  = high / (uint32_t)base;
	low2  = low >> 16;
	low2 += rem << 16;
	rem   = low2 % (uint32_t)base;
	low2  = low2 / (uint32_t)base;
	low   = low  & 0xffff;
	low  += rem << 16;
	rem   = low  % (uint32_t)base;
	low   = low  / (uint32_t)base;

	*n = low +
		((uint64_t)low2 << 16) +
		((uint64_t)high << 32);

	return rem;
}

EXPORT_SYMBOL(__div64_32);

#endif /* BITS_PER_LONG == 32 */
