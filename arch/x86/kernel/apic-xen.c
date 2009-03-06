/*
 *	Local APIC handling stubs
 */

#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/smp.h>
#include <asm/proto.h>
#include <asm/apic.h>

/*
 * Debug level, exported for io_apic.c
 */
unsigned int apic_verbosity;

/* Have we found an MP table */
int smp_found_config;

static int __init apic_set_verbosity(char *arg)
{
	if (!arg)  {
#ifdef CONFIG_X86_64
		skip_ioapic_setup = 0;
		return 0;
#endif
		return -EINVAL;
	}

	if (strcmp("debug", arg) == 0)
		apic_verbosity = APIC_DEBUG;
	else if (strcmp("verbose", arg) == 0)
		apic_verbosity = APIC_VERBOSE;
	else {
		pr_warning("APIC Verbosity level %s not recognised"
			" use apic=verbose or apic=debug\n", arg);
		return -EINVAL;
	}

	return 0;
}
early_param("apic", apic_set_verbosity);

int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

int __init APIC_init_uniprocessor(void)
{
#ifdef CONFIG_X86_IO_APIC
	if (smp_found_config && !skip_ioapic_setup && nr_ioapics)
		setup_IO_APIC();
# ifdef CONFIG_X86_64
	else
		nr_ioapics = 0;
# endif
#endif

	return 0;
}
