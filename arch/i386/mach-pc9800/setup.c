/*
 *	Machine specific setup for pc9800
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/apm_bios.h>
#include <asm/setup.h>
#include <asm/arch_hooks.h>

struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};

/**
 * pre_intr_init_hook - initialisation prior to setting up interrupt vectors
 *
 * Description:
 *	Perform any necessary interrupt initialisation prior to setting up
 *	the "ordinary" interrupt call gates.  For legacy reasons, the ISA
 *	interrupts should be initialised here if the machine emulates a PC
 *	in any way.
 **/
void __init pre_intr_init_hook(void)
{
	init_ISA_irqs();
}

/*
 * IRQ7 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq7 = { no_action, 0, 0, "cascade", NULL, NULL};

/**
 * intr_init_hook - post gate setup interrupt initialisation
 *
 * Description:
 *	Fill in any interrupts that may have been left out by the general
 *	init_IRQ() routine.  interrupts having to do with the machine rather
 *	than the devices on the I/O bus (like APIC interrupts in intel MP
 *	systems) are started here.
 **/
void __init intr_init_hook(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	apic_intr_init();
#endif

	setup_irq(7, &irq7);
}

/**
 * pre_setup_arch_hook - hook called prior to any setup_arch() execution
 *
 * Description:
 *	generally used to activate any machine specific identification
 *	routines that may be needed before setup_arch() runs.  On VISWS
 *	this is used to get the board revision and type.
 **/
void __init pre_setup_arch_hook(void)
{
	SYS_DESC_TABLE.length = 0;
	MCA_bus = 0;
	/* In PC-9800, APM BIOS version is written in BCD...?? */
	APM_BIOS_INFO.version = (APM_BIOS_INFO.version & 0xff00)
				| ((APM_BIOS_INFO.version & 0x00f0) >> 4);
}

/**
 * trap_init_hook - initialise system specific traps
 *
 * Description:
 *	Called as the final act of trap_init().  Used in VISWS to initialise
 *	the various board specific APIC traps.
 **/
void __init trap_init_hook(void)
{
}

static struct irqaction irq0  = { timer_interrupt, SA_INTERRUPT, 0, "timer", NULL, NULL};

/**
 * time_init_hook - do any specific initialisations for the system timer.
 *
 * Description:
 *	Must plug the system timer interrupt source at HZ into the IRQ listed
 *	in irq_vectors.h:TIMER_IRQ
 **/
void __init time_init_hook(void)
{
	setup_irq(0, &irq0);
}

#ifdef CONFIG_MCA
/**
 * mca_nmi_hook - hook into MCA specific NMI chain
 *
 * Description:
 *	The MCA (Microchannel Architecture) has an NMI chain for NMI sources
 *	along the MCA bus.  Use this to hook into that chain if you will need
 *	it.
 **/
void __init mca_nmi_hook(void)
{
	/* If I recall correctly, there's a whole bunch of other things that
	 * we can do to check for NMI problems, but that's all I know about
	 * at the moment.
	 */

	printk("NMI generated from unknown source!\n");
}
#endif
