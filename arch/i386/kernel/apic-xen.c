/*
 *	Local APIC handling stubs
 */

#include <linux/init.h>

#include <asm/hw_irq.h>

/*
 * Debug level, exported for io_apic.c
 */
int apic_verbosity;

static int __init apic_set_verbosity(char *str)
{
	if (strcmp("debug", str) == 0)
		apic_verbosity = APIC_DEBUG;
	else if (strcmp("verbose", str) == 0)
		apic_verbosity = APIC_VERBOSE;
	return 1;
}

__setup("apic=", apic_set_verbosity);

#ifdef CONFIG_X86_64
/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ trap at irq %02x\n", irq);
}
#endif

int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

/*
 * This initializes the IO-APIC and APIC hardware if this is
 * a UP kernel.
 */
int __init APIC_init_uniprocessor (void)
{
#ifdef CONFIG_X86_IO_APIC
	if (smp_found_config)
		if (!skip_ioapic_setup && nr_ioapics)
			setup_IO_APIC();
#endif

	return 0;
}
