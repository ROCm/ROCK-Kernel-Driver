/*
 * indy_int.c: Routines for generic manipulation of the INT[23] ASIC
 *             found on INDY workstations..
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Andrew R. Baker (andrewb@uab.edu) 
 *                    - Indigo2 changes
 *                    - Interrupt handling fixes
 */
#include <linux/init.h>

#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/sgi/sgi.h>
#include <asm/sgi/sgihpc.h>
#include <asm/sgi/sgint23.h>
#include <asm/sgialib.h>
#include <asm/gdb-stub.h>

/*
 * Linux has a controller-independent x86 interrupt architecture.
 * every controller has a 'controller-template', that is used
 * by the main code to do the right thing. Each driver-visible
 * interrupt source is transparently wired to the apropriate
 * controller. Thus drivers need not be aware of the
 * interrupt-controller.
 *
 * Various interrupt controllers we handle: 8259 PIC, SMP IO-APIC,
 * PIIX4's internal 8259 PIC and SGI's Visual Workstation Cobalt (IO-)APIC.
 * (IO-APICs assumed to be messaging to Pentium local-APICs)
 *
 * the code is designed to be easily extended with new/different
 * interrupt controllers, without having to do assembly magic.
 */

/* #define DEBUG_SGINT */

struct sgi_int2_regs *sgi_i2regs;
struct sgi_int3_regs *sgi_i3regs;
struct sgi_ioc_ints *ioc_icontrol;
struct sgi_ioc_timers *ioc_timers;
volatile unsigned char *ioc_tclear;

static char lc0msk_to_irqnr[256];
static char lc1msk_to_irqnr[256];
static char lc2msk_to_irqnr[256];
static char lc3msk_to_irqnr[256];

extern asmlinkage void indyIRQ(void);

/* Local IRQ's are layed out logically like this:
 *
 * 0  --> 7   ==   local 0 interrupts
 * 8  --> 15  ==   local 1 interrupts
 * 16 --> 23  ==   vectored level 2 interrupts
 * 24 --> 31  ==   vectored level 3 interrupts (not used)
 */
static void enable_local0_irq(unsigned int irq)
{
	unsigned long flags;

	save_and_cli(flags);
	ioc_icontrol->imask0 |= (1 << (irq - SGINT_LOCAL0));
	restore_flags(flags);
}

static unsigned int startup_local0_irq(unsigned int irq)
{
	enable_local0_irq(irq);

	return 0;		/* Never anything pending  */
}

static void disable_local0_irq(unsigned int irq)
{
	unsigned long flags;

	save_and_cli(flags);
	ioc_icontrol->imask0 &= ~(1 << (irq - SGINT_LOCAL0));
	restore_flags(flags);
}

#define shutdown_local0_irq	disable_local0_irq
#define mask_and_ack_local0_irq	disable_local0_irq

static void end_local0_irq (unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_local0_irq(irq);
}

static struct hw_interrupt_type ip22_local0_irq_type = {
	"IP22 local 0",
	startup_local0_irq,
	shutdown_local0_irq,
	enable_local0_irq,
	disable_local0_irq,
	mask_and_ack_local0_irq,
	end_local0_irq,
	NULL
};

static void enable_local1_irq(unsigned int irq)
{
	unsigned long flags;

	save_and_cli(flags);
	ioc_icontrol->imask1 |= (1 << (irq - SGINT_LOCAL1));
	restore_flags(flags);
}

static unsigned int startup_local1_irq(unsigned int irq)
{
	enable_local1_irq(irq);

	return 0;		/* Never anything pending  */
}

void disable_local1_irq(unsigned int irq)
{
	unsigned long flags;

	save_and_cli(flags);
	ioc_icontrol->imask1 &= ~(1 << (irq- SGINT_LOCAL1));
	restore_flags(flags);
}

#define shutdown_local1_irq	disable_local1_irq
#define mask_and_ack_local1_irq	disable_local1_irq

static void end_local1_irq (unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_local1_irq(irq);
}

static struct hw_interrupt_type ip22_local1_irq_type = {
	"IP22 local 1",
	startup_local1_irq,
	shutdown_local1_irq,
	enable_local1_irq,
	disable_local1_irq,
	mask_and_ack_local1_irq,
	end_local1_irq,
	NULL
};

static void enable_local2_irq(unsigned int irq)
{
	unsigned long flags;

	save_and_cli(flags);
	enable_local0_irq(7);
	ioc_icontrol->cmeimask0 |= (1 << (irq - SGINT_LOCAL2));
	restore_flags(flags);
}

static unsigned int startup_local2_irq(unsigned int irq)
{
	enable_local2_irq(irq);

	return 0;		/* Never anything pending  */
}

void disable_local2_irq(unsigned int irq)
{
	unsigned long flags;

	save_and_cli(flags);
	ioc_icontrol->cmeimask0 &= ~(1 << (irq - SGINT_LOCAL2));
	restore_flags(flags);
}

#define shutdown_local2_irq disable_local2_irq
#define mask_and_ack_local2_irq	disable_local2_irq

static void end_local2_irq (unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_local2_irq(irq);
}

static struct hw_interrupt_type ip22_local2_irq_type = {
	"IP22 local 2",
	startup_local2_irq,
	shutdown_local2_irq,
	enable_local2_irq,
	disable_local2_irq,
	mask_and_ack_local2_irq,
	end_local2_irq,
	NULL
};

static void enable_local3_irq(unsigned int irq)
{
	unsigned long flags;

	save_and_cli(flags);
	printk("Yeeee, got passed irq_nr %d at enable_local3_irq\n", irq);
	panic("INVALID IRQ level!");
	restore_flags(flags);
}

static unsigned int startup_local3_irq(unsigned int irq)
{
	enable_local3_irq(irq);

	return 0;		/* Never anything pending  */
}

void disable_local3_irq(unsigned int irq)
{
	unsigned long flags;

	save_and_cli(flags);
	/*
	 * This way we'll see if anyone would ever want vectored level 3
	 * interrupts.  Highly unlikely.
	 */
	printk("Yeeee, got passed irq_nr %d at disable_local3_irq\n", irq);
	panic("INVALID IRQ level!");
	restore_flags(flags);
}

#define shutdown_local3_irq disable_local3_irq
#define mask_and_ack_local3_irq	disable_local3_irq

static void end_local3_irq (unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_local3_irq(irq);
}

static struct hw_interrupt_type ip22_local3_irq_type = {
	"IP22 local 3",
	startup_local3_irq,
	shutdown_local3_irq,
	enable_local3_irq,
	disable_local3_irq,
	mask_and_ack_local3_irq,
	end_local3_irq,
	NULL
};

void enable_gio_irq(unsigned int irq)
{
	/* XXX TODO XXX */
}

static unsigned int startup_gio_irq(unsigned int irq)
{
	enable_gio_irq(irq);

	return 0;		/* Never anything pending  */
}

void disable_gio_irq(unsigned int irq)
{
	/* XXX TODO XXX */
}

#define shutdown_gio_irq	disable_gio_irq
#define mask_and_ack_gio_irq	disable_gio_irq

static void end_gio_irq (unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_gio_irq(irq);
}

static struct hw_interrupt_type ip22_gio_irq_type = {
	"IP22 GIO",
	startup_gio_irq,
	shutdown_gio_irq,
	enable_gio_irq,
	disable_gio_irq,
	mask_and_ack_gio_irq,
	end_gio_irq,
	NULL
};

void enable_hpcdma_irq(unsigned int irq)
{
	/* XXX TODO XXX */
}

static unsigned int startup_hpcdma_irq(unsigned int irq)
{
	enable_hpcdma_irq(irq);

	return 0;		/* Never anything pending  */
}

void disable_hpcdma_irq(unsigned int irq)
{
	/* XXX TODO XXX */
}

#define shutdown_hpcdma_irq	disable_hpcdma_irq
#define mask_and_ack_hpcdma_irq	disable_hpcdma_irq

static void end_hpcdma_irq (unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_hpcdma_irq(irq);
}

static struct hw_interrupt_type ip22_hpcdma_irq_type = {
	"IP22 HPC DMA",
	startup_hpcdma_irq,
	shutdown_hpcdma_irq,
	enable_hpcdma_irq,
	disable_hpcdma_irq,
	mask_and_ack_hpcdma_irq,
	end_hpcdma_irq,
	NULL
};

static struct irqaction r4ktimer_action = {
	NULL, 0, 0, "R4000 timer/counter", NULL, NULL,
};

static struct irqaction indy_berr_action = {
	NULL, 0, 0, "IP22 Bus Error", NULL, NULL,
};

static struct irqaction *irq_action[16] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, &indy_berr_action, &r4ktimer_action,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
};

void indy_local0_irqdispatch(struct pt_regs *regs)
{
	unsigned char mask = ioc_icontrol->istat0;
	unsigned char mask2 = 0;
	int irq;

	mask &= ioc_icontrol->imask0;
	if (mask & ISTAT0_LIO2) {
		mask2 = ioc_icontrol->vmeistat;
		mask2 &= ioc_icontrol->cmeimask0;
		irq = lc2msk_to_irqnr[mask2];
	} else {
		irq = lc0msk_to_irqnr[mask];
	}

	/* if irq == 0, then the interrupt has already been cleared */
	if (irq == 0)
		goto end;

	do_IRQ(irq, regs);
	goto end;

no_handler:
	printk("No handler for local0 irq: %i\n", irq);

end:	
	return;
}

void indy_local1_irqdispatch(struct pt_regs *regs)
{
	unsigned char mask = ioc_icontrol->istat1;
	unsigned char mask2 = 0;
	int irq;

	mask &= ioc_icontrol->imask1;
	if (mask & ISTAT1_LIO3) {
		printk("WHee: Got an LIO3 irq, winging it...\n");
		mask2 = ioc_icontrol->vmeistat;
		mask2 &= ioc_icontrol->cmeimask1;
		irq = lc3msk_to_irqnr[ioc_icontrol->vmeistat];
	} else {
		irq = lc1msk_to_irqnr[mask];
	}

	/* if irq == 0, then the interrupt has already been cleared */
	/* not sure if it is needed here, but it is needed for local0 */
	if (irq == 0)
		goto end;

	do_IRQ(irq, regs);
	goto end;
	
no_handler:
	printk("No handler for local1 irq: %i\n", irq);

end:	
	return;	
}

void indy_buserror_irq(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	int irq = 6;

	irq_enter(cpu, irq);
	kstat.irqs[0][irq]++;
	printk("Got a bus error IRQ, shouldn't happen yet\n");
	show_regs(regs);
	printk("Spinning...\n");
	while(1);
	irq_exit(cpu, irq);
}

void __init init_IRQ(void)
{
	int i;

	sgi_i2regs = (struct sgi_int2_regs *) (KSEG1 + SGI_INT2_BASE);
	sgi_i3regs = (struct sgi_int3_regs *) (KSEG1 + SGI_INT3_BASE);

	/* Init local mask --> irq tables. */
	for (i = 0; i < 256; i++) {
		if (i & 0x80) {
			lc0msk_to_irqnr[i] = 7;
			lc1msk_to_irqnr[i] = 15;
			lc2msk_to_irqnr[i] = 23;
			lc3msk_to_irqnr[i] = 31;
		} else if (i & 0x40) {
			lc0msk_to_irqnr[i] = 6;
			lc1msk_to_irqnr[i] = 14;
			lc2msk_to_irqnr[i] = 22;
			lc3msk_to_irqnr[i] = 30;
		} else if (i & 0x20) {
			lc0msk_to_irqnr[i] = 5;
			lc1msk_to_irqnr[i] = 13;
			lc2msk_to_irqnr[i] = 21;
			lc3msk_to_irqnr[i] = 29;
		} else if (i & 0x10) {
			lc0msk_to_irqnr[i] = 4;
			lc1msk_to_irqnr[i] = 12;
			lc2msk_to_irqnr[i] = 20;
			lc3msk_to_irqnr[i] = 28;
		} else if (i & 0x08) {
			lc0msk_to_irqnr[i] = 3;
			lc1msk_to_irqnr[i] = 11;
			lc2msk_to_irqnr[i] = 19;
			lc3msk_to_irqnr[i] = 27;
		} else if (i & 0x04) {
			lc0msk_to_irqnr[i] = 2;
			lc1msk_to_irqnr[i] = 10;
			lc2msk_to_irqnr[i] = 18;
			lc3msk_to_irqnr[i] = 26;
		} else if (i & 0x02) {
			lc0msk_to_irqnr[i] = 1;
			lc1msk_to_irqnr[i] = 9;
			lc2msk_to_irqnr[i] = 17;
			lc3msk_to_irqnr[i] = 25;
		} else if (i & 0x01) {
			lc0msk_to_irqnr[i] = 0;
			lc1msk_to_irqnr[i] = 8;
			lc2msk_to_irqnr[i] = 16;
			lc3msk_to_irqnr[i] = 24;
		} else {
			lc0msk_to_irqnr[i] = 0;
			lc1msk_to_irqnr[i] = 0;
			lc2msk_to_irqnr[i] = 0;
			lc3msk_to_irqnr[i] = 0;
		}
	}

	/* Indy uses an INT3, Indigo2 uses an INT2 */
	if (sgi_guiness) {
		ioc_icontrol = &sgi_i3regs->ints;
		ioc_timers = &sgi_i3regs->timers;
		ioc_tclear = &sgi_i3regs->tclear;
	} else {
		ioc_icontrol = &sgi_i2regs->ints;
		ioc_timers = &sgi_i2regs->timers;
		ioc_tclear = &sgi_i2regs->tclear;
	}

	/* Mask out all interrupts. */
	ioc_icontrol->imask0 = 0;
	ioc_icontrol->imask1 = 0;
	ioc_icontrol->cmeimask0 = 0;
	ioc_icontrol->cmeimask1 = 0;

	set_except_vector(0, indyIRQ);

	init_generic_irq();

	for (i = SGINT_LOCAL0; i < SGINT_END; i++) {
		hw_irq_controller *handler;

		if (i < SGINT_LOCAL1)
			handler		= &ip22_local0_irq_type;
		else if (i < SGINT_LOCAL2)
			handler		= &ip22_local1_irq_type;
		else if (i < SGINT_LOCAL3)
			handler		= &ip22_local2_irq_type;
		else if (i < SGINT_GIO)
			handler		= &ip22_local3_irq_type;
		else if (i < SGINT_HPCDMA)
			handler		= &ip22_gio_irq_type;
		else if (i < SGINT_END)
			handler		= &ip22_hpcdma_irq_type;

		irq_desc[i].status	= IRQ_DISABLED;
		irq_desc[i].action	= 0;
		irq_desc[i].depth	= 1;
		irq_desc[i].handler	= handler;
	}
}
