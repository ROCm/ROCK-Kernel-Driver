#ifndef _ASM_IA64_TIMEX_H
#define _ASM_IA64_TIMEX_H

/*
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#define CLOCK_TICK_RATE		1193180	/* Underlying HZ XXX fix me! */

typedef unsigned long cycles_t;
extern cycles_t cacheflush_time;

static inline cycles_t
get_cycles (void)
{
	cycles_t ret;

	__asm__ __volatile__ ("mov %0=ar.itc" : "=r"(ret));
	return ret;
}

#endif /* _ASM_IA64_TIMEX_H */
