/*
 *  arch/ppc/kernel/irq.c
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/kallsyms.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/iSeries/LparData.h>
#include <asm/machdep.h>
#include <asm/paca.h>

#ifdef CONFIG_SMP
extern void iSeries_smp_message_recv( struct pt_regs * );
#endif

volatile unsigned char *chrp_int_ack_special;
static void register_irq_proc (unsigned int irq);

irq_desc_t irq_desc[NR_IRQS] __cacheline_aligned = {
	[0 ... NR_IRQS-1] = {
		.lock = SPIN_LOCK_UNLOCKED
	}
};
	
int ppc_spurious_interrupts = 0;
unsigned long lpEvent_count = 0;

int
setup_irq(unsigned int irq, struct irqaction * new)
{
	int shared = 0;
	unsigned long flags;
	struct irqaction *old, **p;
	irq_desc_t *desc = irq_desc + irq;

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
		unmask_irq(irq);
	}
	spin_unlock_irqrestore(&desc->lock,flags);

	register_irq_proc(irq);
	return 0;
}

#ifdef CONFIG_SMP

inline void synchronize_irq(unsigned int irq)
{
	while (irq_desc[irq].status & IRQ_INPROGRESS)
		cpu_relax();
}

EXPORT_SYMBOL(synchronize_irq);

#endif /* CONFIG_SMP */

/* XXX Make this into free_irq() - Anton */

/* This could be promoted to a real free_irq() ... */
static int
do_free_irq(int irq, void* dev_id)
{
	irq_desc_t *desc;
	struct irqaction **p;
	unsigned long flags;

	desc = irq_desc + irq;
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
				mask_irq(irq);
			}
			spin_unlock_irqrestore(&desc->lock,flags);

			/* Wait to make sure it's not being used on another CPU */
			synchronize_irq(irq);
			kfree(action);
			return 0;
		}
		printk("Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&desc->lock,flags);
		break;
	}
	return -ENOENT;
}

int request_irq(unsigned int irq,
	irqreturn_t (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction *action;
	int retval;

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
		/* We could implement really free_irq() instead of that... */
		return do_free_irq(irq, dev_id);
	
	action = (struct irqaction *)
		kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action) {
		printk(KERN_ERR "kmalloc() failed for irq %d !\n", irq);
		return -ENOMEM;
	}
	
	action->handler = handler;
	action->flags = irqflags;					
	action->mask = 0;
	action->name = devname;
	action->dev_id = dev_id;
	action->next = NULL;
	
	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
		
	return 0;
}

EXPORT_SYMBOL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	request_irq(irq, NULL, 0, NULL, dev_id);
}

EXPORT_SYMBOL(free_irq);

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
 *	Disable the selected interrupt line. Disables of an interrupt
 *	stack. Unlike disable_irq(), this function does not ensure existing
 *	instances of the IRQ handler have completed before returning.
 *
 *	This function may be called from IRQ context.
 */
 
inline void disable_irq_nosync(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	if (!desc->depth++) {
		if (!(desc->status & IRQ_PER_CPU))
			desc->status |= IRQ_DISABLED;
		mask_irq(irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(disable_irq_nosync);

/**
 *	disable_irq - disable an irq and wait for completion
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line. Disables of an interrupt
 *	stack. That is for two disables you need two enables. This
 *	function waits for any pending IRQ handlers for this interrupt
 *	to complete before returning. If you use this function while
 *	holding a resource the IRQ handler may need you will deadlock.
 *
 *	This function may be called - with care - from IRQ context.
 */
 
void disable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	disable_irq_nosync(irq);
	if (desc->action)
		synchronize_irq(irq);
}

EXPORT_SYMBOL(disable_irq);

/**
 *	enable_irq - enable interrupt handling on an irq
 *	@irq: Interrupt to enable
 *
 *	Re-enables the processing of interrupts on this IRQ line
 *	providing no disable_irq calls are now in effect.
 *
 *	This function may be called from IRQ context.
 */
 
void enable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	switch (desc->depth) {
	case 1: {
		unsigned int status = desc->status & ~(IRQ_DISABLED | IRQ_INPROGRESS);
		desc->status = status;
		if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
			desc->status = status | IRQ_REPLAY;
			hw_resend_irq(desc->handler,irq);
		}
		unmask_irq(irq);
		/* fall-through */
	}
	default:
		desc->depth--;
		break;
	case 0:
		printk("enable_irq(%u) unbalanced from %p\n", irq,
		       __builtin_return_address(0));
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(enable_irq);

int show_interrupts(struct seq_file *p, void *v)
{
	int i, j;
	struct irqaction * action;
	unsigned long flags;

	seq_printf(p, "           ");
	for (j=0; j<NR_CPUS; j++) {
		if (cpu_online(j))
			seq_printf(p, "CPU%d       ",j);
	}
	seq_putc(p, '\n');

	for (i = 0 ; i < NR_IRQS ; i++) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action || !action->handler)
			goto skip;
		seq_printf(p, "%3d: ", i);		
#ifdef CONFIG_SMP
		for (j = 0; j < NR_CPUS; j++) {
			if (cpu_online(j))
				seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
		}
#else		
		seq_printf(p, "%10u ", kstat_irqs(i));
#endif /* CONFIG_SMP */
		if (irq_desc[i].handler)		
			seq_printf(p, " %s ", irq_desc[i].handler->typename );
		else
			seq_printf(p, "  None      ");
		seq_printf(p, "%s", (irq_desc[i].status & IRQ_LEVEL) ? "Level " : "Edge  ");
		seq_printf(p, "    %s",action->name);
		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}
	seq_printf(p, "BAD: %10u\n", ppc_spurious_interrupts);
	return 0;
}

static inline int handle_irq_event(int irq, struct pt_regs *regs,
				   struct irqaction *action)
{
	int status = 0;
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
 * Eventually, this should take an array of interrupts and an array size
 * so it can dispatch multiple interrupts.
 */
void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq)
{
	int status;
	struct irqaction *action;
	int cpu = smp_processor_id();
	irq_desc_t *desc = irq_desc + irq;

	kstat_cpu(cpu).irqs[irq]++;
	spin_lock(&desc->lock);
	ack_irq(irq);	
	/*
	   REPLAY is when Linux resends an IRQ that was dropped earlier
	   WAITING is used by probe to mark irqs that are being tested
	   */
	status = desc->status & ~(IRQ_REPLAY | IRQ_WAITING);
	if (!(status & IRQ_PER_CPU))
		status |= IRQ_PENDING; /* we _want_ to handle it */

	/*
	 * If the IRQ is disabled for whatever reason, we cannot
	 * use the action we have.
	 */
	action = NULL;
	if (likely(!(status & (IRQ_DISABLED | IRQ_INPROGRESS)))) {
		action = desc->action;
		if (!action || !action->handler) {
			ppc_spurious_interrupts++;
			printk(KERN_DEBUG "Unhandled interrupt %x, disabled\n", irq);
			/* We can't call disable_irq here, it would deadlock */
			if (!desc->depth)
				desc->depth = 1;
			desc->status |= IRQ_DISABLED;
			/* This is not a real spurrious interrupt, we
			 * have to eoi it, so we jump to out
			 */
			mask_irq(irq);
			goto out;
		}
		status &= ~IRQ_PENDING; /* we commit to handling */
		if (!(status & IRQ_PER_CPU))
			status |= IRQ_INPROGRESS; /* we are handling it */
	}
	desc->status = status;

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	   Since we set PENDING, if another processor is handling
	   a different instance of this same irq, the other processor
	   will take care of it.
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
		irqreturn_t action_ret;

		spin_unlock(&desc->lock);
		action_ret = handle_irq_event(irq, regs, action);
		spin_lock(&desc->lock);
		if (!noirqdebug)
			note_interrupt(irq, desc, action_ret);
		if (likely(!(desc->status & IRQ_PENDING)))
			break;
		desc->status &= ~IRQ_PENDING;
	}
out:
	desc->status &= ~IRQ_INPROGRESS;
	/*
	 * The ->end() handler has to deal with interrupts which got
	 * disabled while the handler was running.
	 */
	if (irq_desc[irq].handler) {
		if (irq_desc[irq].handler->end)
			irq_desc[irq].handler->end(irq);
		else if (irq_desc[irq].handler->enable)
			irq_desc[irq].handler->enable(irq);
	}
	spin_unlock(&desc->lock);
}

int do_IRQ(struct pt_regs *regs)
{
	int irq, first = 1;
#ifdef CONFIG_PPC_ISERIES
	struct paca_struct *lpaca;
	struct ItLpQueue *lpq;
#endif

	irq_enter();

#ifdef CONFIG_PPC_ISERIES
	lpaca = get_paca();
#ifdef CONFIG_SMP
	if (lpaca->xLpPaca.xIntDword.xFields.xIpiCnt) {
		lpaca->xLpPaca.xIntDword.xFields.xIpiCnt = 0;
		iSeries_smp_message_recv(regs);
	}
#endif /* CONFIG_SMP */
	lpq = lpaca->lpQueuePtr;
	if (lpq && ItLpQueue_isLpIntPending(lpq))
		lpEvent_count += ItLpQueue_process(lpq, regs);
#else
	/*
	 * Every arch is required to implement ppc_md.get_irq.
	 * This function will either return an irq number or -1 to
	 * indicate there are no more pending.  But the first time
	 * through the loop this means there wasn't an IRQ pending.
	 * The value -2 is for buggy hardware and means that this IRQ
	 * has already been handled. -- Tom
	 */
	while ((irq = ppc_md.get_irq(regs)) >= 0) {
		ppc_irq_dispatch_handler(regs, irq);
		first = 0;
	}
	if (irq != -2 && first)
		/* That's not SMP safe ... but who cares ? */
		ppc_spurious_interrupts++;
#endif

	irq_exit();

#ifdef CONFIG_PPC_ISERIES
	if (lpaca->xLpPaca.xIntDword.xFields.xDecrInt) {
		lpaca->xLpPaca.xIntDword.xFields.xDecrInt = 0;
		/* Signal a fake decrementer interrupt */
		timer_interrupt(regs);
	}
#endif

	return 1; /* lets ret_from_int know we can do checks */
}

unsigned long probe_irq_on (void)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_on);

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_off);

unsigned int probe_irq_mask(unsigned long irqs)
{
	return 0;
}

void __init init_IRQ(void)
{
	static int once = 0;

	if ( once )
		return;
	else
		once++;
	
	ppc_md.init_IRQ();
}

static struct proc_dir_entry * root_irq_dir;
static struct proc_dir_entry * irq_dir [NR_IRQS];
static struct proc_dir_entry * smp_affinity_entry [NR_IRQS];

#ifdef CONFIG_IRQ_ALL_CPUS
cpumask_t irq_affinity [NR_IRQS] = { [0 ... NR_IRQS-1] = CPU_MASK_ALL };
#else  /* CONFIG_IRQ_ALL_CPUS */
cpumask_t irq_affinity [NR_IRQS] = { [0 ... NR_IRQS-1] = CPU_MASK_NONE };
#endif /* CONFIG_IRQ_ALL_CPUS */

#define HEX_DIGITS (2*sizeof(cpumask_t))

static int irq_affinity_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int k, len;
	cpumask_t tmp = irq_affinity[(long)data];

	if (count < HEX_DIGITS+1)
		return -EINVAL;

	for (k = 0; k < sizeof(cpumask_t) / sizeof(u16); ++k) {
		int j = sprintf(page, "%04hx", (u16)cpus_coerce(tmp));
		len += j;
		page += j;
		cpus_shift_right(tmp, tmp, 16);
	}
	len += sprintf(page, "\n");
	return len;
}

static unsigned int parse_hex_value (const char *buffer,
		unsigned long count, cpumask_t *ret)
{
	unsigned char hexnum[HEX_DIGITS];
	cpumask_t value = CPU_MASK_NONE;
	int i;

	if (!count)
		return -EINVAL;
	if (count > HEX_DIGITS)
		count = HEX_DIGITS;
	if (copy_from_user(hexnum, buffer, count))
		return -EFAULT;

	/*
	 * Parse the first HEX_DIGITS characters as a hex string, any non-hex char
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

static int irq_affinity_write_proc (struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	int irq = (long)data, full_count = count, err;
	cpumask_t new_value, tmp;

	if (!irq_desc[irq].handler->set_affinity)
		return -EIO;

	err = parse_hex_value(buffer, count, &new_value);
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

	irq_affinity[irq] = new_value;
	irq_desc[irq].handler->set_affinity(irq, new_value);

	return full_count;
}

static int prof_cpu_mask_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	unsigned long *mask = (unsigned long *) data;
	if (count < HEX_DIGITS+1)
		return -EINVAL;
	return sprintf (page, "%08lx\n", *mask);
}

static int prof_cpu_mask_write_proc (struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	cpumask_t *mask = (cpumask_t *)data;
	unsigned long full_count = count, err;
	cpumask_t new_value;

	err = parse_hex_value(buffer, count, &new_value);
	if (err)
		return err;

	*mask = new_value;

#ifdef CONFIG_PPC_ISERIES
	{
		unsigned i;
		for (i=0; i<NR_CPUS; ++i) {
			if ( paca[i].prof_buffer && (new_value & 1) )
				paca[i].prof_enabled = 1;
			else
				paca[i].prof_enabled = 0;
			new_value >>= 1;
		}
	}
#endif

	return full_count;
}

#define MAX_NAMELEN 10

static void register_irq_proc (unsigned int irq)
{
	struct proc_dir_entry *entry;
	char name [MAX_NAMELEN];

	if (!root_irq_dir || (irq_desc[irq].handler == NULL) || irq_dir[irq])
		return;

	memset(name, 0, MAX_NAMELEN);
	sprintf(name, "%d", irq);

	/* create /proc/irq/1234 */
	irq_dir[irq] = proc_mkdir(name, root_irq_dir);

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

unsigned long prof_cpu_mask = -1;

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
		if (irq_desc[i].handler == NULL)
			continue;
		register_irq_proc(i);
	}
}

irqreturn_t no_action(int irq, void *dev, struct pt_regs *regs)
{
	return IRQ_NONE;
}
