#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/timer.h>

#ifdef CONFIG_HPET_TIMER
/*
 * HPET memory read is slower than tsc reads, but is more dependable as it
 * always runs at constant frequency and reduces complexity due to
 * cpufreq. So, we prefer HPET timer to tsc based one. Also, we cannot use
 * timer_pit when HPET is active. So, we default to timer_tsc.
 */
#endif
/* list of timers, ordered by preference, NULL terminated */
static struct timer_opts* timers[] = {
#ifdef CONFIG_X86_CYCLONE_TIMER
	&timer_cyclone,
#endif
#ifdef CONFIG_HPET_TIMER
	&timer_hpet,
#endif
	&timer_tsc,
	&timer_pit,
	NULL,
};

static char clock_override[10] __initdata;

static int __init clock_setup(char* str)
{
	if (str)
		strlcpy(clock_override, str, sizeof(clock_override));
	return 1;
}
__setup("clock=", clock_setup);


/* The chosen timesource has been found to be bad.
 * Fall back to a known good timesource (the PIT)
 */
void clock_fallback(void)
{
	cur_timer = &timer_pit;
}

/* iterates through the list of timers, returning the first 
 * one that initializes successfully.
 */
struct timer_opts* select_timer(void)
{
	int i = 0;
	
	/* find most preferred working timer */
	while (timers[i]) {
		if (timers[i]->init)
			if (timers[i]->init(clock_override) == 0)
				return timers[i];
		++i;
	}
		
	panic("select_timer: Cannot find a suitable timer\n");
	return NULL;
}
