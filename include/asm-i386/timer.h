#ifndef _ASMi386_TIMER_H
#define _ASMi386_TIMER_H

/**
 * struct timer_ops - used to define a timer source
 *
 * @init: Probes and initializes the timer. Takes clock= override 
 *  string as an argument. Returns 0 on success, anything else on failure.
 * @mark_offset: called by the timer interrupt
 * @get_offset: called by gettimeofday().  Returns the number of ms since the
 *	last timer intruupt.
 */
struct timer_opts{
	int (*init)(char *override);
	void (*mark_offset)(void);
	unsigned long (*get_offset)(void);
	unsigned long long (*monotonic_clock)(void);
	void (*delay)(unsigned long);
};

#define TICK_SIZE (tick_nsec / 1000)

extern struct timer_opts* select_timer(void);
extern void clock_fallback(void);

/* Modifiers for buggy PIT handling */

extern int pit_latch_buggy;

extern struct timer_opts *cur_timer;
extern int timer_ack;

/* list of externed timers */
extern struct timer_opts timer_none;
extern struct timer_opts timer_pit;
extern struct timer_opts timer_tsc;
#ifdef CONFIG_X86_CYCLONE_TIMER
extern struct timer_opts timer_cyclone;
#endif

extern unsigned long calibrate_tsc(void);
#ifdef CONFIG_HPET_TIMER
extern struct timer_opts timer_hpet;
extern unsigned long calibrate_tsc_hpet(unsigned long *tsc_hpet_quotient_ptr);
#endif

#endif
