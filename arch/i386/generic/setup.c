/*
 *	Machine specific setup for generic
 */

#include <linux/config.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/arch_hooks.h>

void __init pre_intr_init_hook(void)
{
	init_ISA_irqs();
}

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2 = { no_action, 0, 0, "cascade", NULL, NULL};

void __init intr_init_hook(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	apic_intr_init();
#endif

	setup_irq(2, &irq2);
}

void __init pre_setup_arch_hook(void)
{
}

void __init trap_init_hook(void)
{
}

static struct irqaction irq0  = { timer_interrupt, SA_INTERRUPT, 0, "timer", NULL, NULL};

void __init time_init_hook(void)
{
	setup_irq(0, &irq0);
}

#ifdef CONFIG_MCA
void __init mca_nmi_hook(void)
{
	/* If I recall correctly, there's a whole bunch of other things that
	 * we can do to check for NMI problems, but that's all I know about
	 * at the moment.
	 */

	printk("NMI generated from unknown source!\n");
}
#endif
