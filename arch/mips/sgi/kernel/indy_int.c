/* $Id: indy_int.c,v 1.18 2000/03/02 02:36:50 ralf Exp $
 *
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
#include <linux/malloc.h>
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

unsigned long spurious_count = 0;

/* Local IRQ's are layed out logically like this:
 *
 * 0  --> 7   ==   local 0 interrupts
 * 8  --> 15  ==   local 1 interrupts
 * 16 --> 23  ==   vectored level 2 interrupts
 * 24 --> 31  ==   vectored level 3 interrupts (not used)
 */
void disable_local_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	switch(irq_nr) {
	case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
		ioc_icontrol->imask0 &= ~(1 << irq_nr);
		break;

	case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
		ioc_icontrol->imask1 &= ~(1 << (irq_nr - 8));
		break;

	case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
		ioc_icontrol->cmeimask0 &= ~(1 << (irq_nr - 16));
		break;

	default:
		/* This way we'll see if anyone would ever want vectored
		 * level 3 interrupts.  Highly unlikely.
		 */
		printk("Yeeee, got passed irq_nr %d at disable_irq\n", irq_nr);
		panic("INVALID IRQ level!");
	};
	restore_flags(flags);
}

void enable_local_irq(unsigned int irq_nr)
{
	unsigned long flags;
	save_and_cli(flags);
	switch(irq_nr) {
	case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
		ioc_icontrol->imask0 |= (1 << irq_nr);
		break;

	case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
		ioc_icontrol->imask1 |= (1 << (irq_nr - 8));
		break;

	case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
		enable_local_irq(7);
		ioc_icontrol->cmeimask0 |= (1 << (irq_nr - 16));
		break;

	default:
		printk("Yeeee, got passed irq_nr %d at disable_irq\n", irq_nr);
		panic("INVALID IRQ level!");
	};
	restore_flags(flags);
}

void disable_gio_irq(unsigned int irq_nr)
{
	/* XXX TODO XXX */
}

void enable_gio_irq(unsigned int irq_nr)
{
	/* XXX TODO XXX */
}

void disable_hpcdma_irq(unsigned int irq_nr)
{
	/* XXX TODO XXX */
}

void enable_hpcdma_irq(unsigned int irq_nr)
{
	/* XXX TODO XXX */
}

void disable_irq(unsigned int irq_nr)
{
	unsigned int n = irq_nr;
	if(n >= SGINT_END) {
		printk("whee, invalid irq_nr %d\n", irq_nr);
		panic("IRQ, you lose...");
	}
	if(n >= SGINT_LOCAL0 && n < SGINT_GIO) {
		disable_local_irq(n - SGINT_LOCAL0);
	} else if(n >= SGINT_GIO && n < SGINT_HPCDMA) {
		disable_gio_irq(n - SGINT_GIO);
	} else if(n >= SGINT_HPCDMA && n < SGINT_END) {
		disable_hpcdma_irq(n - SGINT_HPCDMA);
	} else {
		panic("how did I get here?");
	}
}

void enable_irq(unsigned int irq_nr)
{
	unsigned int n = irq_nr;
	if(n >= SGINT_END) {
		printk("whee, invalid irq_nr %d\n", irq_nr);
		panic("IRQ, you lose...");
	}
	if(n >= SGINT_LOCAL0 && n < SGINT_GIO) {
		enable_local_irq(n - SGINT_LOCAL0);
	} else if(n >= SGINT_GIO && n < SGINT_HPCDMA) {
		enable_gio_irq(n - SGINT_GIO);
	} else if(n >= SGINT_HPCDMA && n < SGINT_END) {
		enable_hpcdma_irq(n - SGINT_HPCDMA);
	} else {
		panic("how did I get here?");
	}
}

#if 0
/*
 * Currently unused.
 */
static void local_unex(int irq, void *data, struct pt_regs *regs)
{
	printk("Whee: unexpected local IRQ at %08lx\n",
	       (unsigned long) regs->cp0_epc);
	printk("DUMP: stat0<%x> stat1<%x> vmeistat<%x>\n",
	       ioc_icontrol->istat0, ioc_icontrol->istat1,
	       ioc_icontrol->vmeistat);
}
#endif

static struct irqaction *local_irq_action[24] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
};

int setup_indy_irq(int irq, struct irqaction * new)
{
	printk("setup_indy_irq: Yeee, don't know how to setup irq<%d> for %s  %p\n",
	       irq, new->name, new->handler);
	return 0;
}

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

int get_irq_list(char *buf)
{
	int i, len = 0;
	int num = 0;
	struct irqaction * action;

	for (i = 0 ; i < 16 ; i++, num++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s",
			num, kstat.irqs[0][num],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, " [on-chip]\n");
	}
	for (i = 0 ; i < 24 ; i++, num++) {
		action = local_irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s",
			num, kstat.irqs[0][num],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, " [local]\n");
	}
	return len;
}

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction *action;
	int do_random, cpu;

	cpu = smp_processor_id();
	irq_enter(cpu);
	kstat.irqs[0][irq]++;

	printk("Got irq %d, press a key.", irq);
	prom_getchar();
	romvec->imode();

	/*
	 * mask and ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 *
	 * Commented out because we've already done this in the
	 * machinespecific part of the handler.  It's reasonable to
	 * do this here in a highlevel language though because that way
	 * we could get rid of a good part of duplicated code ...
	 */
        /* mask_and_ack_irq(irq); */

	action = *(irq + irq_action);
	if (action) {
		if (!(action->flags & SA_INTERRUPT))
			__sti();
		action = *(irq + irq_action);
		do_random = 0;
		do {
			do_random |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
		if (do_random & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();
	}
	irq_exit(cpu);

	/* unmasking and bottom half handling is done magically for us. */
}

int request_local_irq(unsigned int lirq, void (*func)(int, void *, struct pt_regs *),
		      unsigned long iflags, const char *dname, void *devid)
{
	struct irqaction *action;

	lirq -= SGINT_LOCAL0;
	if(lirq >= 24 || !func)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if(!action)
		return -ENOMEM;

	action->handler = func;
	action->flags = iflags;
	action->mask = 0;
	action->name = dname;
	action->dev_id = devid;
	action->next = 0;
	local_irq_action[lirq] = action;
	enable_irq(lirq + SGINT_LOCAL0);
	return 0;
}

void free_local_irq(unsigned int lirq, void *dev_id)
{
	struct irqaction *action;

	lirq -= SGINT_LOCAL0;
	if(lirq >= 24) {
		printk("Aieee: trying to free bogus local irq %d\n",
		       lirq + SGINT_LOCAL0);
		return;
	}
	action = local_irq_action[lirq];
	local_irq_action[lirq] = NULL;
	disable_irq(lirq + SGINT_LOCAL0);
	kfree(action);
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq >= SGINT_END)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	if((irq >= SGINT_LOCAL0) && (irq < SGINT_GIO))
		return request_local_irq(irq, handler, irqflags, devname, dev_id);

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_indy_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= SGINT_END) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	if((irq >= SGINT_LOCAL0) && (irq < SGINT_GIO)) {
		free_local_irq(irq, dev_id);
		return;
	}
	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_and_cli(flags);
		*p = action->next;
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

int (*irq_cannonicalize)(int irq);

static int indy_irq_cannonicalize(int irq)
{
	return irq;	/* Sane hardware, sane code ... */
}

void __init init_IRQ(void)
{
	irq_cannonicalize = indy_irq_cannonicalize;
	irq_setup();
}

void indy_local0_irqdispatch(struct pt_regs *regs)
{
	struct irqaction *action;
	unsigned char mask = ioc_icontrol->istat0;
	unsigned char mask2 = 0;
	int irq, cpu = smp_processor_id();;

	mask &= ioc_icontrol->imask0;
	if(mask & ISTAT0_LIO2) {
		mask2 = ioc_icontrol->vmeistat;
		mask2 &= ioc_icontrol->cmeimask0;
		irq = lc2msk_to_irqnr[mask2];
		action = local_irq_action[irq];
	} else {
		irq = lc0msk_to_irqnr[mask];
		action = local_irq_action[irq];
	}

	/* if irq == 0, then the interrupt has already been cleared */
	if ( irq == 0 ) { goto end; }
	/* if action == NULL, then we do have a handler for the irq */
	if ( action == NULL ) { goto no_handler; }
	
	irq_enter(cpu);
	kstat.irqs[0][irq + 16]++;
	action->handler(irq, action->dev_id, regs);
	irq_exit(cpu);
	goto end;

no_handler:
	printk("No handler for local0 irq: %i\n", irq);

end:	
	return;	
	
}

void indy_local1_irqdispatch(struct pt_regs *regs)
{
	struct irqaction *action;
	unsigned char mask = ioc_icontrol->istat1;
	unsigned char mask2 = 0;
	int irq, cpu = smp_processor_id();;

	mask &= ioc_icontrol->imask1;
	if(mask & ISTAT1_LIO3) {
		printk("WHee: Got an LIO3 irq, winging it...\n");
		mask2 = ioc_icontrol->vmeistat;
		mask2 &= ioc_icontrol->cmeimask1;
		irq = lc3msk_to_irqnr[ioc_icontrol->vmeistat];
		action = local_irq_action[irq];
	} else {
		irq = lc1msk_to_irqnr[mask];
		action = local_irq_action[irq];
	}
	/* if irq == 0, then the interrupt has already been cleared */
	/* not sure if it is needed here, but it is needed for local0 */
	if ( irq == 0 ) { goto end; }
	/* if action == NULL, then we do have a handler for the irq */
	if ( action == NULL ) { goto no_handler; }
	
	irq_enter(cpu);
	kstat.irqs[0][irq + 24]++;
	action->handler(irq, action->dev_id, regs);
	irq_exit(cpu);
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

	irq_enter(cpu);
	kstat.irqs[0][irq]++;
	printk("Got a bus error IRQ, shouldn't happen yet\n");
	show_regs(regs);
	printk("Spinning...\n");
	while(1);
	irq_exit(cpu);
}

/* Misc. crap just to keep the kernel linking... */
unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

void __init sgint_init(void)
{
	int i;

	sgi_i2regs = (struct sgi_int2_regs *) (KSEG1 + SGI_INT2_BASE);
	sgi_i3regs = (struct sgi_int3_regs *) (KSEG1 + SGI_INT3_BASE);

	/* Init local mask --> irq tables. */
	for(i = 0; i < 256; i++) {
		if(i & 0x80) {
			lc0msk_to_irqnr[i] = 7;
			lc1msk_to_irqnr[i] = 15;
			lc2msk_to_irqnr[i] = 23;
			lc3msk_to_irqnr[i] = 31;
		} else if(i & 0x40) {
			lc0msk_to_irqnr[i] = 6;
			lc1msk_to_irqnr[i] = 14;
			lc2msk_to_irqnr[i] = 22;
			lc3msk_to_irqnr[i] = 30;
		} else if(i & 0x20) {
			lc0msk_to_irqnr[i] = 5;
			lc1msk_to_irqnr[i] = 13;
			lc2msk_to_irqnr[i] = 21;
			lc3msk_to_irqnr[i] = 29;
		} else if(i & 0x10) {
			lc0msk_to_irqnr[i] = 4;
			lc1msk_to_irqnr[i] = 12;
			lc2msk_to_irqnr[i] = 20;
			lc3msk_to_irqnr[i] = 28;
		} else if(i & 0x08) {
			lc0msk_to_irqnr[i] = 3;
			lc1msk_to_irqnr[i] = 11;
			lc2msk_to_irqnr[i] = 19;
			lc3msk_to_irqnr[i] = 27;
		} else if(i & 0x04) {
			lc0msk_to_irqnr[i] = 2;
			lc1msk_to_irqnr[i] = 10;
			lc2msk_to_irqnr[i] = 18;
			lc3msk_to_irqnr[i] = 26;
		} else if(i & 0x02) {
			lc0msk_to_irqnr[i] = 1;
			lc1msk_to_irqnr[i] = 9;
			lc2msk_to_irqnr[i] = 17;
			lc3msk_to_irqnr[i] = 25;
		} else if(i & 0x01) {
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

	/* Now safe to set the exception vector. */
	set_except_vector(0, indyIRQ);
}
