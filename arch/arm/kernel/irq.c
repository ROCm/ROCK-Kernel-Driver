/*
 *  linux/arch/arm/kernel/irq.c
 *
 *  Copyright (C) 1992 Linus Torvalds
 *  Modifications for ARM processor Copyright (C) 1995-1998 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the code used by various IRQ handling routines:
 *  asking for different IRQ's should be done through these routines
 *  instead of just grabbing them. Thus setups with different IRQ numbers
 *  shouldn't result in any weird surprises, and installing new handlers
 *  should be easier.
 *
 *  IRQ's are in fact implemented a bit like signal handlers for the kernel.
 *  Naturally it's not a 1:1 relation, but there are similarities.
 */
#include <linux/config.h>
#include <linux/ptrace.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/system.h>

/*
 * Maximum IRQ count.  Currently, this is arbitary.  However, it should
 * not be set too low to prevent false triggering.  Conversely, if it
 * is set too high, then you could miss a stuck IRQ.
 *
 * Maybe we ought to set a timer and re-enable the IRQ at a later time?
 */
#define MAX_IRQ_CNT	100000

spinlock_t irq_controller_lock;

int setup_arm_irq(int, struct irqaction *);
extern int get_fiq_list(char *);
extern void init_FIQ(void);

struct irqdesc {
	unsigned int	 nomask   : 1;		/* IRQ does not mask in IRQ   */
	unsigned int	 enabled  : 1;		/* IRQ is currently enabled   */
	unsigned int	 triggered: 1;		/* IRQ has occurred	      */
	unsigned int	 probing  : 1;		/* IRQ in use for a probe     */
	unsigned int	 probe_ok : 1;		/* IRQ can be used for probe  */
	unsigned int	 valid    : 1;		/* IRQ claimable	      */
	unsigned int	 noautoenable : 1;	/* don't automatically enable IRQ */
	unsigned int	 unused   :25;
	void (*mask_ack)(unsigned int irq);	/* Mask and acknowledge IRQ   */
	void (*mask)(unsigned int irq);		/* Mask IRQ		      */
	void (*unmask)(unsigned int irq);	/* Unmask IRQ		      */
	struct irqaction *action;
	/*
	 * IRQ lock detection
	 */
	unsigned int	 lck_cnt;
	unsigned int	 lck_pc;
	unsigned int	 lck_jif;
};

static struct irqdesc irq_desc[NR_IRQS];
static volatile unsigned long irq_err_count;

/*
 * Get architecture specific interrupt handlers
 * and interrupt initialisation.
 */
#include <asm/arch/irq.h>

/*
 * Dummy mask/unmask handler
 */
static void dummy_mask_unmask_irq(unsigned int irq)
{
}

void disable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	irq_desc[irq].enabled = 0;
	irq_desc[irq].mask(irq);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

void enable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	irq_desc[irq].probing = 0;
	irq_desc[irq].triggered = 0;
	irq_desc[irq].enabled = 1;
	irq_desc[irq].unmask(irq);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

int get_irq_list(char *buf)
{
	int i;
	struct irqaction * action;
	char *p = buf;

	for (i = 0 ; i < NR_IRQS ; i++) {
	    	action = irq_desc[i].action;
		if (!action)
			continue;
		p += sprintf(p, "%3d: %10u ", i, kstat_irqs(i));
		p += sprintf(p, "  %s", action->name);
		for (action = action->next; action; action = action->next) {
			p += sprintf(p, ", %s", action->name);
		}
		*p++ = '\n';
	}

#ifdef CONFIG_ARCH_ACORN
	p += get_fiq_list(p);
#endif
	p += sprintf(p, "Err: %10lu\n", irq_err_count);
	return p - buf;
}

/*
 * IRQ lock detection.
 *
 * Hopefully, this should get us out of a few locked situations.
 * However, it may take a while for this to happen, since we need
 * a large number if IRQs to appear in the same jiffie with the
 * same instruction pointer (or within 2 instructions).
 */
static void check_irq_lock(struct irqdesc *desc, int irq, struct pt_regs *regs)
{
	unsigned long instr_ptr = instruction_pointer(regs);

	if (desc->lck_jif == jiffies &&
	    desc->lck_pc >= instr_ptr && desc->lck_pc < instr_ptr + 8) {
		desc->lck_cnt += 1;

		if (desc->lck_cnt > MAX_IRQ_CNT) {
			printk(KERN_ERR "IRQ LOCK: IRQ%d is locking the system, disabled\n", irq);
			disable_irq(irq);
		}
	} else {
		desc->lck_cnt = 0;
		desc->lck_pc  = instruction_pointer(regs);
		desc->lck_jif = jiffies;
	}
}

/*
 * do_IRQ handles all normal device IRQ's
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqdesc * desc;
	struct irqaction * action;
	int cpu;

	irq = fixup_irq(irq);

	/*
	 * Some hardware gives randomly wrong interrupts.  Rather
	 * than crashing, do something sensible.
	 */
	if (irq >= NR_IRQS)
		goto bad_irq;

	desc = irq_desc + irq;

	spin_lock(&irq_controller_lock);
	desc->mask_ack(irq);
	spin_unlock(&irq_controller_lock);

	cpu = smp_processor_id();
	irq_enter(cpu, irq);
	kstat.irqs[cpu][irq]++;
	desc->triggered = 1;

	/* Return with this interrupt masked if no action */
	action = desc->action;

	if (action) {
		int status = 0;

		if (desc->nomask) {
			spin_lock(&irq_controller_lock);
			desc->unmask(irq);
			spin_unlock(&irq_controller_lock);
		}

		if (!(action->flags & SA_INTERRUPT))
			__sti();

		do {
			status |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);

		if (status & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();

		if (!desc->nomask && desc->enabled) {
			spin_lock(&irq_controller_lock);
			desc->unmask(irq);
			spin_unlock(&irq_controller_lock);
		}
	}

	/*
	 * Debug measure - hopefully we can continue if an
	 * IRQ lockup problem occurs...
	 */
	check_irq_lock(desc, irq, regs);

	irq_exit(cpu, irq);

	if (softirq_active(cpu) & softirq_mask(cpu))
		do_softirq();
	return;

bad_irq:
	irq_err_count += 1;
	printk(KERN_ERR "IRQ: spurious interrupt %d\n", irq);
	return;
}

#ifdef CONFIG_ARCH_ACORN
void do_ecard_IRQ(int irq, struct pt_regs *regs)
{
	struct irqdesc * desc;
	struct irqaction * action;
	int cpu;

	desc = irq_desc + irq;

	cpu = smp_processor_id();
	kstat.irqs[cpu][irq]++;
	desc->triggered = 1;

	action = desc->action;

	if (action) {
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
	} else {
		spin_lock(&irq_controller_lock);
		desc->mask(irq);
		spin_unlock(&irq_controller_lock);
	}
}
#endif

int setup_arm_irq(int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
	        rand_initialize_irq(irq);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&irq_controller_lock, flags);
	p = &irq_desc[irq].action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&irq_controller_lock, flags);
			return -EBUSY;
		}

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		irq_desc[irq].nomask = (new->flags & SA_IRQNOMASK) ? 1 : 0;
		irq_desc[irq].probing = 0;
		if (!irq_desc[irq].noautoenable) {
			irq_desc[irq].enabled = 1;
			irq_desc[irq].unmask(irq);
		}
	}

	spin_unlock_irqrestore(&irq_controller_lock, flags);
	return 0;
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		 unsigned long irq_flags, const char * devname, void *dev_id)
{
	unsigned long retval;
	struct irqaction *action;

	if (irq >= NR_IRQS || !irq_desc[irq].valid || !handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irq_flags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_arm_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= NR_IRQS || !irq_desc[irq].valid) {
		printk(KERN_ERR "Trying to free IRQ%d\n",irq);
#ifdef CONFIG_DEBUG_ERRORS
		__backtrace();
#endif
		return;
	}

	spin_lock_irqsave(&irq_controller_lock, flags);
	for (p = &irq_desc[irq].action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

	    	/* Found it - now free it */
		*p = action->next;
		kfree(action);
		goto out;
	}
	printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
#ifdef CONFIG_DEBUG_ERRORS
	__backtrace();
#endif
out:
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/* Start the interrupt probing.  Unlike other architectures,
 * we don't return a mask of interrupts from probe_irq_on,
 * but return the number of interrupts enabled for the probe.
 * The interrupts which have been enabled for probing is
 * instead recorded in the irq_desc structure.
 */
unsigned long probe_irq_on(void)
{
	unsigned int i, irqs = 0;
	unsigned long delay;

	/*
	 * first snaffle up any unassigned but
	 * probe-able interrupts
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i = 0; i < NR_IRQS; i++) {
		if (!irq_desc[i].valid ||
		    !irq_desc[i].probe_ok ||
		    irq_desc[i].action)
			continue;

		irq_desc[i].probing = 1;
		irq_desc[i].triggered = 0;
		irq_desc[i].unmask(i);
		irqs += 1;
	}
	spin_unlock_irq(&irq_controller_lock);

	/*
	 * wait for spurious interrupts to mask themselves out again
	 */
	for (delay = jiffies + HZ/10; time_before(jiffies, delay); )
		/* min 100ms delay */;

	/*
	 * now filter out any obviously spurious interrupts
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i = 0; i < NR_IRQS; i++) {
		if (irq_desc[i].probing &&
		    irq_desc[i].triggered) {
			irq_desc[i].probing = 0;
			irqs -= 1;
		}
	}
	spin_unlock_irq(&irq_controller_lock);

	/* now filter out any obviously spurious interrupts */
	return irqs;
}

/*
 * Possible return values:
 *  >= 0 - interrupt number
 *    -1 - no interrupt/many interrupts
 */
int probe_irq_off(unsigned long irqs)
{
	unsigned int i;
	int irq_found = NO_IRQ;

	/*
	 * look at the interrupts, and find exactly one
	 * that we were probing has been triggered
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i = 0; i < NR_IRQS; i++) {
		if (irq_desc[i].probing &&
		    irq_desc[i].triggered) {
			if (irq_found != NO_IRQ) {
				irq_found = NO_IRQ;
				goto out;
			}
			irq_found = i;
		}
	}

	if (irq_found == -1)
		irq_found = NO_IRQ;
out:
	spin_unlock_irq(&irq_controller_lock);

	return irq_found;
}

void __init init_irq_proc(void)
{
}

void __init init_IRQ(void)
{
	extern void init_dma(void);
	int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		irq_desc[irq].probe_ok = 0;
		irq_desc[irq].valid    = 0;
		irq_desc[irq].noautoenable = 0;
		irq_desc[irq].mask_ack = dummy_mask_unmask_irq;
		irq_desc[irq].mask     = dummy_mask_unmask_irq;
		irq_desc[irq].unmask   = dummy_mask_unmask_irq;
	}

	irq_init_irq();
	init_dma();
}
