/*
 * BK Id: SCCS/s.delay.h 1.7 05/17/01 18:14:24 cort
 */
#ifdef __KERNEL__
#ifndef _PPC_DELAY_H
#define _PPC_DELAY_H

#include <asm/param.h>

/*
 * Copyright 1996, Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

extern unsigned long loops_per_jiffy;

/* maximum permitted argument to udelay */
#define __MAX_UDELAY	1000000

extern void __delay(unsigned int loops);

/* N.B. the `secs' parameter here is a fixed-point number with
   the binary point to the left of the most-significant bit. */
extern __inline__ void __const_udelay(unsigned int secs)
{
	unsigned int loops;

	__asm__("mulhwu %0,%1,%2" : "=r" (loops) :
		"r" (secs), "r" (loops_per_jiffy));
	__delay(loops * HZ);
}

/*
 * note that 4294 == 2^32 / 10^6, multiplying by 4294 converts from
 * microseconds to a 32-bit fixed-point number of seconds.
 */
extern __inline__ void __udelay(unsigned int usecs)
{
	__const_udelay(usecs * 4294);
}

extern void __bad_udelay(void);		/* deliberately undefined */

#define udelay(n) (__builtin_constant_p(n)? \
		   ((n) > __MAX_UDELAY? __bad_udelay(): __const_udelay((n) * 4294u)) : \
		   __udelay(n))

#endif /* defined(_PPC_DELAY_H) */
#endif /* __KERNEL__ */
