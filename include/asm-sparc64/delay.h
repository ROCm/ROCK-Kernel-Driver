/* $Id: delay.h,v 1.11 2001/01/02 08:15:32 davem Exp $
 * delay.h: Linux delay routines on the V9.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu).
 */

#ifndef __SPARC64_DELAY_H
#define __SPARC64_DELAY_H

#include <linux/config.h>
#include <linux/param.h>
#ifdef CONFIG_SMP
#include <linux/sched.h>
#include <asm/smp.h>
#endif 

extern __inline__ void __delay(unsigned long loops)
{
	__asm__ __volatile__("
	b,pt	%%xcc, 1f
	 cmp	%0, 0
	.align	32
1:
	bne,pt	%%xcc, 1b
	 subcc	%0, 1, %0
"	: "=&r" (loops)
	: "0" (loops)
	: "cc");
}

extern __inline__ void __udelay(unsigned long usecs, unsigned long lps)
{
	usecs *= 0x00000000000010c6UL;		/* 2**32 / 1000000 */

	__asm__ __volatile__("
	mulx	%1, %2, %0
	srlx	%0, 32, %0
"	: "=r" (usecs)
	: "r" (usecs), "r" (lps));

	__delay(usecs * HZ);
}

#ifdef CONFIG_SMP
#define __udelay_val cpu_data[smp_processor_id()].udelay_val
#else
#define __udelay_val loops_per_jiffy
#endif

#define udelay(usecs) __udelay((usecs),__udelay_val)

#endif /* defined(__SPARC64_DELAY_H) */
