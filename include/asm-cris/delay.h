/* $Id: delay.h,v 1.2 2000/08/08 16:36:41 bjornw Exp $ */

#ifndef _CRIS_DELAY_H
#define _CRIS_DELAY_H

/*
 * Copyright (C) 1998, 1999, 2000 Axis Communications AB
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */

#include <linux/config.h>
#include <linux/linkage.h>

#ifdef CONFIG_SMP
#include <asm/smp.h>
#endif 

extern void __do_delay(void);	/* Special register call calling convention */

extern __inline__ void __delay(int loops)
{
	/* need to be a great deal of nops, because Etrax shuts off IRQ's during a branch
	   and we depend on the irq's to measure the time! */
	
	__asm__ __volatile__ (
			      "move.d %0,r0\n"
			      "1:\n\t"
			      "nop\n\t"
			      "nop\n\t"
			      "nop\n\t"
			      "nop\n\t"
			      "nop\n\t"
			      "nop\n\t"
			      "nop\n\t"
			      "subq 1,r0\n\t"
			      "bne 1b\n\t"
			      "nop\n\t"
			      : : "r" (loops) : "r0", "cc");
}


/*
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */

extern unsigned long loops_per_usec; /* arch/cris/mm/init.c */

extern __inline__ void udelay(unsigned long usecs)
{
	__delay(usecs * loops_per_usec);
}

extern __inline__ unsigned long muldiv(unsigned long a, unsigned long b, unsigned long c)
{
	printk("muldiv called!\n");
	return 0;
}

#endif /* defined(_ETRAX_DELAY_H) */



