#ifndef _ASMi386_TIMER_H
#define _ASMi386_TIMER_H
#include <linux/init.h>

/**
 * struct timer_ops - used to define a timer source
 *
 * @name: name of the timer.
 * @init: Probes and initializes the timer. Takes clock= override 
 *        string as an argument. Returns 0 on success, anything else
 *        on failure.
 * @mark_offset: called by the timer interrupt.
 * @get_offset:  called by gettimeofday(). Returns the number of microseconds
 *               since the last timer interupt.
 * @monotonic_clock: returns the number of nanoseconds since the init of the
 *                   timer.
 * @delay: delays this many clock cycles.
 */

#define TICK_SIZE (tick_nsec / 1000)

extern void clock_fallback(void);
void setup_pit_timer(void);

/* Modifiers for buggy PIT handling */

extern int pit_latch_buggy;

extern int timer_ack;

/* list of externed timers */
extern unsigned long calibrate_tsc(void);
extern void init_cpu_khz(void);
#ifdef CONFIG_HPET_TIMER
extern unsigned long calibrate_tsc_hpet(unsigned long *tsc_hpet_quotient_ptr);
#endif

#endif
