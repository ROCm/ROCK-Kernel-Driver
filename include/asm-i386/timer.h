#ifndef _ASMi386_TIMER_H
#define _ASMi386_TIMER_H

struct timer_opts{
	/* probes and initializes timer. returns 1 on sucess, 0 on failure */
	int (*init)(void);
	/* called by the timer interrupt */
	void (*mark_offset)(void);
	/* called by gettimeofday. returns # ms since the last timer interrupt */
	unsigned long (*get_offset)(void);
};
#define TICK_SIZE (tick_nsec / 1000)
struct timer_opts* select_timer(void);
#endif
