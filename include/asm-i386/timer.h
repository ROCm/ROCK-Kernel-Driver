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
	void (*delay)(unsigned long);
};

#define TICK_SIZE (tick_nsec / 1000)

extern struct timer_opts* select_timer(void);

/* Modifiers for buggy PIT handling */

extern int pit_latch_buggy;
#endif
