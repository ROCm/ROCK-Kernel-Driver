/*
 * linux/include/asm-cris/timex.h
 *
 * CRIS architecture timex specifications
 */
#ifndef _ASM_CRIS_TIMEX_H
#define _ASM_CRIS_TIMEX_H

#define CLOCK_TICK_RATE 9600 /* Underlying frequency of the HZ timer */

/*
 * We don't have a cycle-counter.. but we do not support SMP anyway where this is
 * used so it does not matter.
 */

typedef unsigned int cycles_t;

static inline cycles_t get_cycles(void)
{
        return 0;
}

#endif
