/*
 *  arch/s390/kernel/delay.c
 *    Precise Delay Loops for S390
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 *  Derived from "arch/i386/lib/delay.c"
 *    Copyright (C) 1993 Linus Torvalds
 *    Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/delay.h>

#ifdef CONFIG_SMP
#include <asm/smp.h>
#endif

void __delay(unsigned long loops)
{
	__asm__ __volatile__(
                "0: ahi  %0,-1\n"
                "   jnm  0b"
                : /* no outputs */ : "r" (loops) );
}

inline void __const_udelay(unsigned long xloops)
{

	__asm__("LR    3,%1\n\t"
		"MR    2,%2\n\t"
		"LR    %0,2\n\t"
		: "=r" (xloops)
		: "r" (xloops) , "r"  (loops_per_sec)
		: "2" , "3");
        __delay(xloops);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c6);  /* 2**32 / 1000000 */
}
