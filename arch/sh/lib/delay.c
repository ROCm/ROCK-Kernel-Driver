/*
 *	Precise Delay Loops for SuperH
 *
 *	Copyright (C) 1999 Niibe Yutaka & Kaz Kojima
 */

#include <linux/sched.h>
#include <linux/delay.h>

void __delay(unsigned long loops)
{
	__asm__ __volatile__(
		"tst	%0, %0\n\t"
		"1:\t"
		"bf/s	1b\n\t"
		" dt	%0"
		: "=r" (loops)
		: "0" (loops)
		: "t");
}

inline void __const_udelay(unsigned long xloops)
{
	xloops *= current_cpu_data.loops_per_jiffy;
	__delay(xloops * HZ);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c6);  /* 2**32 / 1000000 */
}
