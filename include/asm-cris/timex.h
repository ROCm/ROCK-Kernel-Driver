/*
 * linux/include/asm-cris/timex.h
 *
 * CRIS architecture timex specifications
 */
#ifndef _ASM_CRIS_TIMEX_H
#define _ASM_CRIS_TIMEX_H

#define CLOCK_TICK_RATE 19200 /* Underlying frequency of the HZ timer */

/* The timer0 values gives ~52.1us resolution (1/19200) but interrupts at HZ*/
#define TIMER0_FREQ (CLOCK_TICK_RATE)
#define TIMER0_CLKSEL c19k2Hz
#define TIMER0_DIV (TIMER0_FREQ/(HZ))
/* This is the slow one: */
/*
#define GET_JIFFIES_USEC() \
  ( (*R_TIMER0_DATA - TIMER0_DIV) * (1000000/HZ)/TIMER0_DIV )
*/
/* This is the fast version: */
extern unsigned short cris_timer0_value_us[TIMER0_DIV+1]; /* in kernel/time.c */
#define GET_JIFFIES_USEC() (cris_timer0_value_us[*R_TIMER0_DATA])

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
