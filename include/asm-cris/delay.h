/* $Id: delay.h,v 1.4 2001/05/31 06:40:53 markusl Exp $ */

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
	__asm__ __volatile__ (
			      "move.d %0,r0\n\t"
			      "1:\n\t"
			      "cmpq 0,r0\n\t"
			      "beq 2f\n\t"
			      "nop\n\t"
			      "subq 1,r0\n\t"
			      "ba 1b\n\t"
			      "nop\n\t"
			      "2:\n\t"
			      : : "r" (loops) : "r0");
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

#endif /* defined(_CRIS_DELAY_H) */



