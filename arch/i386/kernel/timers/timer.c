#include <linux/kernel.h>
#include <asm/timer.h>

/* list of externed timers */
/* eg: extern struct timer_opts timer_XXX*/;

/* list of timers, ordered by preference */
struct timer_opts* timers[] = {
	/* eg: &timer_XXX */
};

#define NR_TIMERS (sizeof(timers)/sizeof(timers[0]))

/* iterates through the list of timers, returning the first 
 * one that initializes successfully.
 */
struct timer_opts* select_timer(void)
{
	int i;
	/* find most preferred working timer */
	for(i=0; i < NR_TIMERS; i++)
		if(timers[i]->init())
			return timers[i];
	panic("select_timer: Cannot find a suitable timer\n");
	return 0;
}
