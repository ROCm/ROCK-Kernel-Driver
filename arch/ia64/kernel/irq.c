/*
 *	linux/arch/ia64/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * (mostly architecture independent, will move to kernel/irq.c in 2.5.)
 *
 * IRQs are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>

#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/delay.h>
#include <asm/irq.h>



/*
 * Linux has a controller-independent x86 interrupt architecture.
 * every controller has a 'controller-template', that is used
 * by the main code to do the right thing. Each driver-visible
 * interrupt source is transparently wired to the appropriate
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

/*
 * Controller mappings for all interrupt sources:
 */
irq_desc_t _irq_desc[NR_IRQS] __cacheline_aligned = {
	[0 ... NR_IRQS-1] = {
		.status = IRQ_DISABLED,
		.handler = &no_irq_type,
		.lock = SPIN_LOCK_UNLOCKED
	}
};

#ifdef CONFIG_IA64_GENERIC
irq_desc_t * __ia64_irq_desc (unsigned int irq)
{
	return _irq_desc + irq;
}

ia64_vector __ia64_irq_to_vector (unsigned int irq)
{
	return (ia64_vector) irq;
}

unsigned int __ia64_local_vector_to_irq (ia64_vector vec)
{
	return (unsigned int) vec;
}
#endif

static void register_irq_proc (unsigned int irq);

/*
 * Special irq handlers.
 */

irqreturn_t no_action(int cpl, void *dev_id, struct pt_regs *regs)
{ return IRQ_NONE; }

/*
 * Generic no controller code
 */

static void enable_none(unsigned int irq) { }
static unsigned int startup_none(unsigned int irq) { return 0; }
static void disable_none(unsigned int irq) { }
static void ack_none(unsigned int irq)
{
/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves, it doesn't deserve
 * a generic callback i think.
 */
#ifdef CONFIG_X86
	printk(KERN_ERR "unexpected IRQ trap at vector %02x\n", irq);
#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 */
	ack_APIC_irq();
#endif
#endif
#ifdef CONFIG_IA64
	printk(KERN_ERR "Unexpected irq vector 0x%x on CPU %u!\n", irq, smp_processor_id());
#endif
}

/* startup is the same as "enable", shutdown is same as "disable" */
#define shutdown_none	disable_none
#define end_none	enable_none

struct hw_interrupt_type no_irq_type = {
	"none",
	startup_none,
	shutdown_none,
	enable_none,
	disable_none,
	ack_none,
	end_none
};

atomic_t irq_err_count;
#ifdef CONFIG_X86_IO_APIC
#ifdef APIC_MISMATCH_DEBUG
atomic_t irq_mis_count;
#endif
#endif

/*
 * Generic, controller-independent functions:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i, j;
	struct irqaction * action;
	irq_desc_t *idesc;
	unsigned long flags;

	seq_puts(p, "           ");
	for (j=0; j<NR_CPUS; j++)
		if (cpu_online(j))
			seq_printf(p, "CPU%d       ",j);
	seq_putc(p, '\n');

	for (i = 0 ; i < NR_IRQS ; i++) {
		idesc = irq_descp(i);
		spin_lock_irqsave(&idesc->lock, flags);
		action = idesc->action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		seq_printf(p, " %14s", idesc->handler->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&idesc->lock, flags);
	}
	seq_puts(p, "NMI: ");
	for (j = 0; j < NR_CPUS; j++)
		if (cpu_online(j))
			seq_printf(p, "%10u ", nmi_count(j));
	seq_putc(p, '\n');
#ifdef CONFIG_X86_LOCAL_APIC
	seq_puts(p, "LOC: ");
	for (j = 0; j < NR_CPUS; j++)
		if (cpu_online(j))
			seq_printf(p, "%10u ", irq_stat[j].apic_timer_irqs);
	seq_putc(p, '\n');
#endif
	seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
#ifdef CONFIG_X86_IO_APIC
#ifdef APIC_MISMATCH_DEBUG
	seq_printf(p, "MIS: %10u\n", atomic_read(&irq_mis_count));
#endif
#endif
	return 0;
}

#ifdef CONFIG_SMP
inline void synchronize_irq(unsigned int irq)
{
	while (irq_descp(irq)->status & IRQ_INPROGRESS)
		cpu_relax();
}
#endif

/*
 * This should really return information about whether
 * we should do bottom half handling etc. Right now we
 * end up _always_ checking the bottom half, which is a
 * waste of time and is not what some drivers would
 * prefer.
 */
int handle_IRQ_event(unsigned int irq,
		struct pt_regs *regs, struct irqaction *action)
{
	int status = 1;	/* Force the "do bottom halves" bit */
	int retval = 0;

	if (!(action->flags & SA_INTERRUPT))
		local_irq_enable();

	do {
		status |= action->flags;
		retval |= action->handler(irq, action->dev_id, regs);
		action = action->next;
	} while (action);
	if (status & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	local_irq_disable();
	return retval;
}

static void __report_bad_irq(int irq, irq_desc_t *desc, irqreturn_t action_ret)
{
	struct irqaction *action;

	if (action_ret != IRQ_HANDLED && action_ret != IRQ_NONE) {
		printk(KERN_ERR "irq event %d: bogus return value %x\n",
				irq, action_ret);
	} else {
		printk(KERN_ERR "irq %d: nobody cared!\n", irq);
	}
	dump_stack();
	printk(KERN_ERR "handlers:\n");
	action = desc->action;
	do {
		printk(KERN_ERR "[<%p>]", action->handler);
		print_symbol(" (%s)",
			(unsigned long)action->handler);
		printk("\n");
		action = action->next;
	} while (action);
}

static void report_bad_irq(int irq, irq_desc_t *desc, irqreturn_t action_ret)
{
	static int count = 100;

	if (count) {
		count--;
		__report_bad_irq(irq, desc, action_ret);
	}
}

static int noirqdebug;

static int __init noirqdebug_setup(char *str)
{
	noirqdebug = 1;
	printk("IRQ lockup detection disabled\n");
	return 1;
}

__setup("noirqdebug", noirqdebug_setup);

/*
 * If 99,900 of the previous 100,000 interrupts have not been handled then
 * assume that the IRQ is stuck in some manner.  Drop a diagnostic and try to
 * turn the IRQ off.
 *
 * (The other 100-of-100,000 interrupts may have been a correctly-functioning
 *  device sharing an IRQ with the failing one)
 *
 * Called under desc->lock
 */
static void note_interrupt(int irq, irq_desc_t *desc, irqreturn_t action_ret)
{
	if (action_ret != IRQ_HANDLED) {
		desc->irqs_unhandled++;
		if (action_ret != IRQ_NONE)
			report_bad_irq(irq, desc, action_ret);
	}

	desc->irq_count++;
	if (desc->irq_count < 100000)
		return;

	desc->irq_count = 0;
	if (desc->irqs_unhandled > 99900) {
		/*
		 * The interrupt is stuck
		 */
		__report_bad_irq(irq, desc, action_ret);
		/*
		 * Now kill the IRQ
		 */
		printk(KERN_EMERG "Disabling IRQ #%d\n", irq);
		desc->status |= IRQ_DISABLED;
		desc->handler->disable(irq);
	}
	desc->irqs_unhandled = 0;
}

/*
 * Generic enable/disable code: this just calls
 * down into the PIC-specific version for the actual
 * hardware disable after having gotten the irq
 * controller lock.
 */

/**
 *	disable_irq_nosync - disable an irq without waiting
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line.  Disables and Enables are
 *	nested.
 *	Unlike disable_irq(), this function does not ensure existing
 *	instances of the IRQ handler have completed before returning.
 *
 *	This function may be called from IRQ context.
 */

inline void disable_irq_nosync(unsigned int irq)
{
	irq_desc_t *desc = irq_descp(irq);
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	if (!desc->depth++) {
		desc->status |= IRQ_DISABLED;
		desc->handler->disable(irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

/**
 *	disable_irq - disable an irq and wait for completion
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line.  Enables and Disables are
 *	nested.
 *	This function waits for any pending IRQ handlers for this interrupt
 *	to complete before returning. If you use this function while
 *	holding a resource the IRQ handler may need you will deadlock.
 *
 *	This function may be called - with care - from IRQ context.
 */

void disable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_descp(irq);

	disable_irq_nosync(irq);
	if (desc->action)
		synchronize_irq(irq);
}

/**
 *	enable_irq - enable handling of an irq
 *	@irq: Interrupt to enable
 *
 *	Undoes the effect of one call to disable_irq().  If this
 *	matches the last disable, processing of interrupts on this
 *	IRQ line is re-enabled.
 *
 *	This function may be called from IRQ context.
 */

void enable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_descp(irq);
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	switch (desc->depth) {
	case 1: {
		unsigned int status = desc->status & ~IRQ_DISABLED;
		desc->status = status;
		if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
			desc->status = status | IRQ_REPLAY;
			hw_resend_irq(desc->handler,irq);
		}
		desc->handler->enable(irq);
		/* fall-through */
	}
	default:
		desc->depth--;
		break;
	case 0:
		printk(KERN_ERR "enable_irq(%u) unbalanced from %p\n",
		       irq, (void *) __builtin_return_address(0));
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
unsigned int do_IRQ(unsigned long irq, struct pt_regs *regs)
{
	/*
	 * We ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 *
	 * 0 return value means that this irq is already being
	 * handled by some other CPU. (or is disabled)
	 */
	irq_desc_t *desc = irq_descp(irq);
	struct irqaction * action;
	irqreturn_t action_ret;
	unsigned int status;
	int cpu;

	irq_enter();
	cpu = smp_processor_id(); /* for CONFIG_PREEMPT, this must come after irq_enter()! */

	kstat_cpu(cpu).irqs[irq]++;

	if (desc->status & IRQ_PER_CPU) {
		/* no locking required for CPU-local interrupts: */
		desc->handler->ack(irq);
		action_ret = handle_IRQ_event(irq, regs, desc->action);
		desc->handler->end(irq);
		if (!noirqdebug)
			note_interrupt(irq, desc, action_ret);
	} else {
		spin_lock(&desc->lock);
		desc->handler->ack(irq);
		/*
		 * REPLAY is when Linux resends an IRQ that was dropped earlier
		 * WAITING is used by probe to mark irqs that are being tested
		 */
		status = desc->status & ~(IRQ_REPLAY | IRQ_WAITING);
		status |= IRQ_PENDING; /* we _want_ to handle it */

		/*
		 * If the IRQ is disabled for whatever reason, we cannot
		 * use the action we have.
		 */
		action = NULL;
		if (likely(!(status & (IRQ_DISABLED | IRQ_INPROGRESS)))) {
			action = desc->action;
			status &= ~IRQ_PENDING; /* we commit to handling */
			status |= IRQ_INPROGRESS; /* we are handling it */
		}
		desc->status = status;

		/*
		 * If there is no IRQ handler or it was disabled, exit early.
		 * Since we set PENDING, if another processor is handling
		 * a different instance of this same irq, the other processor
		 * will take care of it.
		 */
		if (unlikely(!action))
			goto out;

		/*
		 * Edge triggered interrupts need to remember
		 * pending events.
		 * This applies to any hw interrupts that allow a second
		 * instance of the same irq to arrive while we are in do_IRQ
		 * or in the handler. But the code here only handles the _second_
		 * instance of the irq, not the third or fourth. So it is mostly
		 * useful for irq hardware that does not mask cleanly in an
		 * SMP environment.
		 */
		for (;;) {
			spin_unlock(&desc->lock);
			action_ret = handle_IRQ_event(irq, regs, action);
			spin_lock(&desc->lock);
			if (!noirqdebug)
				note_interrupt(irq, desc, action_ret);
			if (!(desc->status & IRQ_PENDING))
				break;
			desc->status &= ~IRQ_PENDING;
		}
		desc->status &= ~IRQ_INPROGRESS;
	  out:
		/*
		 * The ->end() handler has to deal with interrupts which got
		 * disabled while the handler was running.
		 */
		desc->handler->end(irq);
		spin_unlock(&desc->lock);
	}
	irq_exit();
	return 1;
}

/**
 *	request_irq - allocate an interrupt line
 *	@irq: Interrupt line to allocate
 *	@handler: Function to be called when the IRQ occurs
 *	@irqflags: Interrupt type flags
 *	@devname: An ascii name for the claiming device
 *	@dev_id: A cookie passed back to the handler function
 *
 *	This call allocates interrupt resources and enables the
 *	interrupt line and IRQ handling. From the point this
 *	call is made your handler function may be invoked. Since
 *	your handler function must clear any interrupt the board 
 *	raises, you must take care both to initialise your hardware
 *	and to set up the interrupt handler in the right order.
 *
 *	Dev_id must be globally unique. Normally the address of the
 *	device data structure is used as the cookie. Since the handler
 *	receives this value it makes sense to use it.
 *
 *	If your interrupt is shared you must pass a non NULL dev_id
 *	as this is required when freeing the interrupt.
 *
 *	Flags:
 *
 *	SA_SHIRQ		Interrupt is shared
 *
 *	SA_INTERRUPT		Disable local interrupts while processing
 *
 *	SA_SAMPLE_RANDOM	The interrupt can be used for entropy
 *
 */

int request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags,
		const char * devname,
		void *dev_id)
{
	int retval;
	struct irqaction * action;

#if 1
	/*
	 * Sanity-check: shared interrupts should REALLY pass in
	 * a real dev-ID, otherwise we'll have trouble later trying
	 * to figure out which interrupt is which (messes up the
	 * interrupt freeing logic etc).
	 */
	if (irqflags & SA_SHIRQ) {
		if (!dev_id)
			printk(KERN_ERR "Bad boy: %s called us without a dev_id!\n", devname);
	}
#endif

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)
			kmalloc(sizeof(struct irqaction), GFP_ATOMIC);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
	return retval;
}

EXPORT_SYMBOL(request_irq);

/**
 *	free_irq - free an interrupt
 *	@irq: Interrupt line to free
 *	@dev_id: Device identity to free
 *
 *	Remove an interrupt handler. The handler is removed and if the
 *	interrupt line is no longer in use by any driver it is disabled.
 *	On a shared IRQ the caller must ensure the interrupt is disabled
 *	on the card it drives before calling this function. The function
 *	does not return until any executing interrupts for this IRQ
 *	have completed.
 *
 *	This function must not be called from interrupt context.
 */

void free_irq(unsigned int irq, void *dev_id)
{
	irq_desc_t *desc;
	struct irqaction **p;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return;

	desc = irq_descp(irq);
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	for (;;) {
		struct irqaction * action = *p;
		if (action) {
			struct irqaction **pp = p;
			p = &action->next;
			if (action->dev_id != dev_id)
				continue;

			/* Found it - now remove it from the list of entries */
			*pp = action->next;
			if (!desc->action) {
				desc->status |= IRQ_DISABLED;
				desc->handler->shutdown(irq);
			}
			spin_unlock_irqrestore(&desc->lock,flags);

			/* Wait to make sure it's not being used on another CPU */
			synchronize_irq(irq);
			kfree(action);
			return;
		}
		printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&desc->lock,flags);
		return;
	}
}

EXPORT_SYMBOL(free_irq);

/*
 * IRQ autodetection code..
 *
 * This depends on the fact that any interrupt that
 * comes in on to an unassigned handler will get stuck
 * with "IRQ_WAITING" cleared and the interrupt
 * disabled.
 */

static DECLARE_MUTEX(probe_sem);

/**
 *	probe_irq_on	- begin an interrupt autodetect
 *
 *	Commence probing for an interrupt. The interrupts are scanned
 *	and a mask of potential interrupt lines is returned.
 *
 */

unsigned long probe_irq_on(void)
{
	unsigned int i;
	irq_desc_t *desc;
	unsigned long val;
	unsigned long delay;

	down(&probe_sem);
	/*
	 * something may have generated an irq long ago and we want to
	 * flush such a longstanding irq before considering it as spurious.
	 */
	for (i = NR_IRQS-1; i > 0; i--)  {
		desc = irq_descp(i);

		spin_lock_irq(&desc->lock);
		if (!desc->action)
			desc->handler->startup(i);
		spin_unlock_irq(&desc->lock);
	}

	/* Wait for longstanding interrupts to trigger. */
	for (delay = jiffies + HZ/50; time_after(delay, jiffies); )
		/* about 20ms delay */ barrier();

	/*
	 * enable any unassigned irqs
	 * (we must startup again here because if a longstanding irq
	 * happened in the previous stage, it may have masked itself)
	 */
	for (i = NR_IRQS-1; i > 0; i--) {
		desc = irq_descp(i);

		spin_lock_irq(&desc->lock);
		if (!desc->action) {
			desc->status |= IRQ_AUTODETECT | IRQ_WAITING;
			if (desc->handler->startup(i))
				desc->status |= IRQ_PENDING;
		}
		spin_unlock_irq(&desc->lock);
	}

	/*
	 * Wait for spurious interrupts to trigger
	 */
	for (delay = jiffies + HZ/10; time_after(delay, jiffies); )
		/* about 100ms delay */ barrier();

	/*
	 * Now filter out any obviously spurious interrupts
	 */
	val = 0;
	for (i = 0; i < NR_IRQS; i++) {
		irq_desc_t *desc = irq_descp(i);
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			/* It triggered already - consider it spurious. */
			if (!(status & IRQ_WAITING)) {
				desc->status = status & ~IRQ_AUTODETECT;
				desc->handler->shutdown(i);
			} else
				if (i < 32)
					val |= 1 << i;
		}
		spin_unlock_irq(&desc->lock);
	}

	return val;
}

EXPORT_SYMBOL(probe_irq_on);

/**
 *	probe_irq_mask - scan a bitmap of interrupt lines
 *	@val:	mask of interrupts to consider
 *
 *	Scan the ISA bus interrupt lines and return a bitmap of
 *	active interrupts. The interrupt probe logic state is then
 *	returned to its previous value.
 *
 *	Note: we need to scan all the irq's even though we will
 *	only return ISA irq numbers - just so that we reset them
 *	all to a known state.
 */
unsigned int probe_irq_mask(unsigned long val)
{
	int i;
	unsigned int mask;

	mask = 0;
	for (i = 0; i < 16; i++) {
		irq_desc_t *desc = irq_descp(i);
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			if (!(status & IRQ_WAITING))
				mask |= 1 << i;

			desc->status = status & ~IRQ_AUTODETECT;
			desc->handler->shutdown(i);
		}
		spin_unlock_irq(&desc->lock);
	}
	up(&probe_sem);

	return mask & val;
}

/**
 *	probe_irq_off	- end an interrupt autodetect
 *	@val: mask of potential interrupts (unused)
 *
 *	Scans the unused interrupt lines and returns the line which
 *	appears to have triggered the interrupt. If no interrupt was
 *	found then zero is returned. If more than one interrupt is
 *	found then minus the first candidate is returned to indicate
 *	their is doubt.
 *
 *	The interrupt probe logic state is returned to its previous
 *	value.
 *
 *	BUGS: When used in a module (which arguably shouldn't happen)
 *	nothing prevents two IRQ probe callers from overlapping. The
 *	results of this are non-optimal.
 */

int probe_irq_off(unsigned long val)
{
	int i, irq_found, nr_irqs;

	nr_irqs = 0;
	irq_found = 0;
	for (i = 0; i < NR_IRQS; i++) {
		irq_desc_t *desc = irq_descp(i);
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			if (!(status & IRQ_WAITING)) {
				if (!nr_irqs)
					irq_found = i;
				nr_irqs++;
			}
			desc->status = status & ~IRQ_AUTODETECT;
			desc->handler->shutdown(i);
		}
		spin_unlock_irq(&desc->lock);
	}
	up(&probe_sem);

	if (nr_irqs > 1)
		irq_found = -irq_found;
	return irq_found;
}

EXPORT_SYMBOL(probe_irq_off);

int setup_irq(unsigned int irq, struct irqaction * new)
{
	int shared = 0;
	unsigned long flags;
	struct irqaction *old, **p;
	irq_desc_t *desc = irq_descp(irq);

	if (desc->handler == &no_irq_type)
		return -ENOSYS;
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

	if (new->flags & SA_PERCPU_IRQ) {
		desc->status |= IRQ_PER_CPU;
		desc->handler = &irq_type_ia64_lsapic;
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&desc->lock,flags);
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
		desc->depth = 0;
		desc->status &= ~(IRQ_DISABLED | IRQ_AUTODETECT | IRQ_WAITING | IRQ_INPROGRESS);
		desc->handler->startup(irq);
	}
	spin_unlock_irqrestore(&desc->lock,flags);

	register_irq_proc(irq);
	return 0;
}

static struct proc_dir_entry * root_irq_dir;
static struct proc_dir_entry * irq_dir [NR_IRQS];

#define HEX_DIGITS (2*sizeof(cpumask_t))

static unsigned int parse_hex_value(const char *buffer,
		unsigned long count, cpumask_t *ret)
{
	unsigned char hexnum[HEX_DIGITS];
	cpumask_t value = CPU_MASK_NONE;
	unsigned long i;

	if (!count)
		return -EINVAL;
	if (count > HEX_DIGITS)
		count = HEX_DIGITS;
	if (copy_from_user(hexnum, buffer, count))
		return -EFAULT;

	/*
	 * Parse the first 8 characters as a hex string, any non-hex char
	 * is end-of-string. '00e1', 'e1', '00E1', 'E1' are all the same.
	 */
	for (i = 0; i < count; i++) {
		unsigned int c = hexnum[i];
		int k;

		switch (c) {
			case '0' ... '9': c -= '0'; break;
			case 'a' ... 'f': c -= 'a'-10; break;
			case 'A' ... 'F': c -= 'A'-10; break;
		default:
			goto out;
		}
		cpus_shift_left(value, value, 4);
		for (k = 0; k < 4; ++k)
			if (test_bit(k, (unsigned long *)&c))
				cpu_set(k, value);
	}
out:
	*ret = value;
	return 0;
}

#ifdef CONFIG_SMP

static struct proc_dir_entry * smp_affinity_entry [NR_IRQS];

static cpumask_t irq_affinity [NR_IRQS] = { [0 ... NR_IRQS-1] = CPU_MASK_ALL };

static char irq_redir [NR_IRQS]; // = { [0 ... NR_IRQS-1] = 1 };

void set_irq_affinity_info (unsigned int irq, int hwid, int redir)
{
	cpumask_t mask = CPU_MASK_NONE;

	cpu_set(cpu_logical_id(hwid), mask);

	if (irq < NR_IRQS) {
		irq_affinity[irq] = mask;
		irq_redir[irq] = (char) (redir & 0xff);
	}
}

static int irq_affinity_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int k, len;
	cpumask_t tmp = irq_affinity[(long)data];

	if (count < HEX_DIGITS+1)
		return -EINVAL;

	len = 0;
	for (k = 0; k < sizeof(cpumask_t)/sizeof(u16); ++k) {
		int j = sprintf(page, "%04hx", (u16)cpus_coerce(tmp));
		len += j;
		page += j;
		cpus_shift_right(tmp, tmp, 16);
	}
	len += sprintf(page, "\n");
	return len;
}

static int irq_affinity_write_proc (struct file *file, const char *buffer,
				    unsigned long count, void *data)
{
	unsigned int irq = (unsigned long) data;
	int full_count = count, err;
	cpumask_t new_value, tmp;
	const char *buf = buffer;
	irq_desc_t *desc = irq_descp(irq);
	int redir;

	if (!desc->handler->set_affinity)
		return -EIO;

	if (buf[0] == 'r' || buf[0] == 'R') {
		++buf;
		while (*buf == ' ') ++buf;
		redir = 1;
	} else
		redir = 0;

	err = parse_hex_value(buf, count, &new_value);
	if (err)
		return err;

	/*
	 * Do not allow disabling IRQs completely - it's a too easy
	 * way to make the system unusable accidentally :-) At least
	 * one online CPU still has to be targeted.
	 */
	cpus_and(tmp, new_value, cpu_online_map);
	if (cpus_empty(tmp))
		return -EINVAL;

	desc->handler->set_affinity(irq | (redir? IA64_IRQ_REDIRECTED : 0), new_value);
	return full_count;
}

#endif /* CONFIG_SMP */

static int prof_cpu_mask_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	cpumask_t *mask = (cpumask_t *)data;
	int k, len = 0;

	if (count < HEX_DIGITS+1)
		return -EINVAL;

	for (k = 0; k < sizeof(cpumask_t)/sizeof(u16); ++k) {
		int j = sprintf(page, "%04hx", (u16)cpus_coerce(*mask));
		len += j;
		page += j;
		cpus_shift_right(*mask, *mask, 16);
	}
	len += sprintf(page, "\n");
	return len;
}

static int prof_cpu_mask_write_proc (struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	cpumask_t *mask = (cpumask_t *)data;
	unsigned long full_count = count, err;
	cpumask_t new_value;

	err = parse_hex_value(buffer, count, &new_value);
	if (err)
		return err;

	*mask = new_value;
	return full_count;
}

#define MAX_NAMELEN 10

static void register_irq_proc (unsigned int irq)
{
	char name [MAX_NAMELEN];

	if (!root_irq_dir || (irq_descp(irq)->handler == &no_irq_type) || irq_dir[irq])
		return;

	memset(name, 0, MAX_NAMELEN);
	sprintf(name, "%d", irq);

	/* create /proc/irq/1234 */
	irq_dir[irq] = proc_mkdir(name, root_irq_dir);

#ifdef CONFIG_SMP
	{
		struct proc_dir_entry *entry;

		/* create /proc/irq/1234/smp_affinity */
		entry = create_proc_entry("smp_affinity", 0600, irq_dir[irq]);

		if (entry) {
			entry->nlink = 1;
			entry->data = (void *)(long)irq;
			entry->read_proc = irq_affinity_read_proc;
			entry->write_proc = irq_affinity_write_proc;
		}

		smp_affinity_entry[irq] = entry;
	}
#endif
}

cpumask_t prof_cpu_mask = CPU_MASK_ALL;

void init_irq_proc (void)
{
	struct proc_dir_entry *entry;
	int i;

	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", 0);

	/* create /proc/irq/prof_cpu_mask */
	entry = create_proc_entry("prof_cpu_mask", 0600, root_irq_dir);

	if (!entry)
		return;

	entry->nlink = 1;
	entry->data = (void *)&prof_cpu_mask;
	entry->read_proc = prof_cpu_mask_read_proc;
	entry->write_proc = prof_cpu_mask_write_proc;

	/*
	 * Create entries for all existing IRQs.
	 */
	for (i = 0; i < NR_IRQS; i++) {
		if (irq_descp(i)->handler == &no_irq_type)
			continue;
		register_irq_proc(i);
	}
}
