#ifdef __KERNEL__
#ifndef _PPC_DELAY_H
#define _PPC_DELAY_H

/*
 * Copyright 1996, Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

extern unsigned long loops_per_sec;

extern __inline__ void __delay(unsigned int loops)
{
	if (loops != 0)
		__asm__ __volatile__("mtctr %0; 1: bdnz 1b" : :
				     "r" (loops) : "ctr");
}

extern __inline__ void udelay(unsigned long usecs)
{
	unsigned long loops;

	/* compute (usecs * 2^32 / 10^6) * loops_per_sec / 2^32 */
	usecs *= 0x10c6;		/* 2^32 / 10^6 */
	__asm__("mulhwu %0,%1,%2" : "=r" (loops) :
		"r" (usecs), "r" (loops_per_sec));
	__delay(loops);
}

#endif /* defined(_PPC_DELAY_H) */
#endif /* __KERNEL__ */
