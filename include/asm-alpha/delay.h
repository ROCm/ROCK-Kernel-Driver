#ifndef __ALPHA_DELAY_H
#define __ALPHA_DELAY_H

#include <linux/config.h>
#include <asm/param.h>
#include <asm/smp.h>

/*
 * Copyright (C) 1993, 2000 Linus Torvalds
 *
 * Delay routines, using a pre-computed "loops_per_jiffy" value.
 */

/*
 * Use only for very small delays (< 1 msec). 
 *
 * The active part of our cycle counter is only 32-bits wide, and
 * we're treating the difference between two marks as signed.  On
 * a 1GHz box, that's about 2 seconds.
 */

extern __inline__ void
__delay(int loops)
{
	int tmp;
	__asm__ __volatile__(
		"	rpcc %0\n"
		"	addl %1,%0,%1\n"
		"1:	rpcc %0\n"
		"	subl %1,%0,%0\n"
		"	bgt %0,1b"
		: "=&r" (tmp), "=r" (loops) : "1"(loops));
}

extern __inline__ void
__udelay(unsigned long usecs, unsigned long lpj)
{
	usecs *= (((unsigned long)HZ << 32) / 1000000) * lpj;
	__delay((long)usecs >> 32);
}

#ifdef CONFIG_SMP
#define udelay(u)  __udelay((u), cpu_data[smp_processor_id()].loops_per_jiffy)
#else
#define udelay(u)  __udelay((u), loops_per_jiffy)
#endif

#endif /* defined(__ALPHA_DELAY_H) */
