#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/timer.h>
#include <asm/hpet.h>

#ifdef CONFIG_HPET_TIMER
/*
 * HPET memory read is slower than tsc reads, but is more dependable as it
 * always runs at constant frequency and reduces complexity due to
 * cpufreq. So, we prefer HPET timer to tsc based one. Also, we cannot use
 * timer_pit when HPET is active. So, we default to timer_tsc.
 */
#endif
/* list of timers, ordered by preference, NULL terminated */
static struct init_timer_opts* __initdata timers[] = {
#ifdef CONFIG_X86_CYCLONE_TIMER
	&timer_cyclone_init,
#endif
#ifdef CONFIG_HPET_TIMER
	&timer_hpet_init,
#endif
#ifdef CONFIG_X86_PM_TIMER
	&timer_pmtmr_init,
#endif
	&timer_tsc_init,
	&timer_pit_init,
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

/*
 * Make an educated guess if the TSC is trustworthy and synchronized
 * over all CPUs.
 */
__cpuinit int unsynchronized_tsc(void)
{
#ifdef CONFIG_SMP
	if (oem_force_hpet_timer())
		return 1;
 	/* Intel systems are normally all synchronized. Exceptions
 	   are handled in the OEM check above. */
 	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
 		return 0;
#endif
 	/* Assume multi socket systems are not synchronized */
 	return num_present_cpus() > 1;
}

/* iterates through the list of timers, returning the first 
 * one that initializes successfully.
 */
struct timer_opts* __init select_timer(void)
{
	int i = 0;
	
	/* Prefer TSC if possible because it's fastest */
	if (clock_override[0] == 0 && !unsynchronized_tsc()) { 
		if (timer_tsc_init.init(clock_override) == 0)
			return timer_tsc_init.opts;
	}

	/* find most preferred working timer */
	while (timers[i]) {
		if (timers[i]->init)
			if (timers[i]->init(clock_override) == 0)
				return timers[i]->opts;
		++i;
	}
		
	panic("select_timer: Cannot find a suitable timer\n");
	return NULL;
}

int read_current_timer(unsigned long *timer_val)
{
	if (cur_timer->read_timer) {
		*timer_val = cur_timer->read_timer();
		return 0;
	}
	return -1;
}
