#include <linux/kernel.h>
#include <asm/timer.h>

/* list of externed timers */
extern struct timer_opts timer_pit;
extern struct timer_opts timer_tsc;

/* list of timers, ordered by preference, NULL terminated */
static struct timer_opts* timers[] = {
	&timer_tsc,
	&timer_pit,
	NULL,
};


/* iterates through the list of timers, returning the first 
 * one that initializes successfully.
 */
struct timer_opts* select_timer(void)
{
	int i = 0;
	
	/* find most preferred working timer */
	while (timers[i]) {
		if (timers[i]->init)
			if (timers[i]->init() == 0)
				return timers[i];
		++i;
	}
		
	panic("select_timer: Cannot find a suitable timer\n");
	return NULL;
}
