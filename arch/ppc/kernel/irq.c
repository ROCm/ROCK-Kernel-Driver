/*
 *  arch/ppc/kernel/irq.c
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 *
 * The MPC8xx has an interrupt mask in the SIU.  If a bit is set, the
 * interrupt is _enabled_.  As expected, IRQ0 is bit 0 in the 32-bit
 * mask register (of which only 16 are defined), hence the weird shifting
 * and complement of the cached_irq_mask.  I want to be able to stuff
 * this right into the SIU SMASK register.
 * Many of the prep/chrp functions are conditional compiled on CONFIG_8xx
 * to reduce code space and undefined function references.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
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
#include <linux/seq_file.h>
#include <linux/cpumask.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)

extern atomic_t ipi_recv;
extern atomic_t ipi_sent;
void enable_irq(unsigned int irq_nr);
void disable_irq(unsigned int irq_nr);

static void register_irq_proc (unsigned int irq);

#define MAXCOUNT 10000000

irq_desc_t irq_desc[NR_IRQS] __cacheline_aligned = {
	[0 ... NR_IRQS-1] = {
		.lock = SPIN_LOCK_UNLOCKED
	}
};

int ppc_spurious_interrupts = 0;
struct irqaction *ppc_irq_action[NR_IRQS];
unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];
unsigned long ppc_lost_interrupts[NR_MASK_WORDS];
atomic_t ppc_n_lost_interrupts;

/* nasty hack for shared irq's since we need to do kmalloc calls but
 * can't very early in the boot when we need to do a request irq.
 * this needs to be removed.
 * -- Cort
 */
#define IRQ_KMALLOC_ENTRIES 8
static int cache_bitmask = 0;
static struct irqaction malloc_cache[IRQ_KMALLOC_ENTRIES];
extern int mem_init_done;

#if defined(CONFIG_TAU_INT)
extern int tau_interrupts(unsigned long cpu);
extern int tau_initialized;
#endif

void *irq_kmalloc(size_t size, int pri)
{
	unsigned int i;
	if ( mem_init_done )
		return kmalloc(size,pri);
	for ( i = 0; i < IRQ_KMALLOC_ENTRIES ; i++ )
		if ( ! ( cache_bitmask & (1<<i) ) )
		{
			cache_bitmask |= (1<<i);
			return (void *)(&malloc_cache[i]);
		}
	return 0;
}

void irq_kfree(void *ptr)
{
	unsigned int i;
	for ( i = 0 ; i < IRQ_KMALLOC_ENTRIES ; i++ )
		if ( ptr == &malloc_cache[i] )
		{
			cache_bitmask &= ~(1<<i);
			return;
		}
	kfree(ptr);
}

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
		desc->status &= ~(IRQ_DISABLED | IRQ_AUTODETECT | IRQ_WAITING);
		unmask_irq(irq);
	}
	spin_unlock_irqrestore(&desc->lock,flags);

	register_irq_proc(irq);
	return 0;
}

void free_irq(unsigned int irq, void* dev_id)
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

			synchronize_irq(irq);
			irq_kfree(action);
			return;
		}
		printk("Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&desc->lock,flags);
		break;
	}
	return;
}

EXPORT_SYMBOL(free_irq);

int request_irq(unsigned int irq,
	irqreturn_t (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction *action;
	int retval;

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler) {
		printk(KERN_ERR "request_irq called with NULL handler!\n");
		dump_stack();
		return 0;
	}

	action = (struct irqaction *)
		irq_kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action) {
		printk(KERN_ERR "irq_kmalloc() failed for irq %d !\n", irq);
		return -ENOMEM;
	}

	action->handler = handler;
	action->flags = irqflags;			
	action->mask = 0;
	action->name = devname;
	action->dev_id = dev_id;
	action->next = NULL;

	retval = setup_irq(irq, action);
	if (retval) {
		kfree(action);
		return retval;
	}

	return 0;
}

EXPORT_SYMBOL(request_irq);

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

void disable_irq_nosync(unsigned int irq)
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
		unsigned int status = desc->status & ~IRQ_DISABLED;
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
		printk("enable_irq(%u) unbalanced\n", irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_puts(p, "           ");
		for (j=0; j<NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "CPU%d       ", j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if ( !action || !action->handler )
			goto skip;
		seq_printf(p, "%3d: ", i);
#ifdef CONFIG_SMP
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "%10u ",
					   kstat_cpu(j).irqs[i]);
#else
		seq_printf(p, "%10u ", kstat_irqs(i));
#endif /* CONFIG_SMP */
		if (irq_desc[i].handler)
			seq_printf(p, " %s ", irq_desc[i].handler->typename);
		else
			seq_puts(p, "  None      ");
		seq_printf(p, "%s", (irq_desc[i].status & IRQ_LEVEL) ? "Level " : "Edge  ");
		seq_printf(p, "    %s", action->name);
		for (action = action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
#ifdef CONFIG_TAU_INT
		if (tau_initialized){
			seq_puts(p, "TAU: ");
			for (j = 0; j < NR_CPUS; j++)
				if (cpu_online(j))
					seq_printf(p, "%10u ", tau_interrupts(j));
			seq_puts(p, "  PowerPC             Thermal Assist (cpu temp)\n");
		}
#endif
#ifdef CONFIG_SMP
		/* should this be per processor send/receive? */
		seq_printf(p, "IPI (recv/sent): %10u/%u\n",
				atomic_read(&ipi_recv), atomic_read(&ipi_sent));
#endif
		seq_printf(p, "BAD: %10u\n", ppc_spurious_interrupts);
	}
	return 0;
}

static inline void
handle_irq_event(int irq, struct pt_regs *regs, struct irqaction *action)
{
	int status = 0;

	if (!(action->flags & SA_INTERRUPT))
		local_irq_enable();

	do {
		status |= action->flags;
		action->handler(irq, action->dev_id, regs);
		action = action->next;
	} while (action);
	if (status & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	local_irq_disable();
}

/*
 * Eventually, this should take an array of interrupts and an array size
 * so it can dispatch multiple interrupts.
 */
void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq)
{
	int status;
	struct irqaction *action;
	irq_desc_t *desc = irq_desc + irq;

	kstat_this_cpu.irqs[irq]++;
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
			++desc->depth;
			desc->status |= IRQ_DISABLED;
			mask_irq(irq);
			/* This is a real interrupt, we have to eoi it,
			   so we jump to out */
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
		spin_unlock(&desc->lock);
		handle_irq_event(irq, regs, action);
		spin_lock(&desc->lock);

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

void do_IRQ(struct pt_regs *regs)
{
	int irq, first = 1;
        irq_enter();

	/*
	 * Every platform is required to implement ppc_md.get_irq.
	 * This function will either return an irq number or -1 to
	 * indicate there are no more pending.  But the first time
	 * through the loop this means there wasn't and IRQ pending.
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
        irq_exit();
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

#ifdef CONFIG_SMP
void synchronize_irq(unsigned int irq)
{
	while (irq_desc[irq].status & IRQ_INPROGRESS)
		barrier();
}
#endif /* CONFIG_SMP */

static struct proc_dir_entry *root_irq_dir;
static struct proc_dir_entry *irq_dir[NR_IRQS];
static struct proc_dir_entry *smp_affinity_entry[NR_IRQS];

#ifdef CONFIG_IRQ_ALL_CPUS
#define DEFAULT_CPU_AFFINITY CPU_MASK_ALL
#else
#define DEFAULT_CPU_AFFINITY cpumask_of_cpu(0)
#endif

cpumask_t irq_affinity [NR_IRQS];

static int irq_affinity_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len = cpumask_scnprintf(page, count, irq_affinity[(long)data]);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static int irq_affinity_write_proc (struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	int irq = (int) data, full_count = count, err;
	cpumask_t new_value, tmp;

	if (!irq_desc[irq].handler->set_affinity)
		return -EIO;

	err = cpumask_parse(buffer, count, new_value);

	/*
	 * Do not allow disabling IRQs completely - it's a too easy
	 * way to make the system unusable accidentally :-) At least
	 * one online CPU still has to be targeted.
	 *
	 * We assume a 1-1 logical<->physical cpu mapping here.  If
	 * we assume that the cpu indices in /proc/irq/../smp_affinity
	 * are actually logical cpu #'s then we have no problem.
	 *  -- Cort <cort@fsmlabs.com>
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
	int len = cpumask_scnprintf(page, count, *(cpumask_t *)data);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static int prof_cpu_mask_write_proc (struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	int err;
	int full_count = count;
	cpumask_t *mask = (cpumask_t *)data;
	cpumask_t new_value;

	err = cpumask_parse(buffer, count, new_value);
	if (err)
		return err;

	*mask = new_value;
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

	entry->nlink = 1;
	entry->data = (void *)irq;
	entry->read_proc = irq_affinity_read_proc;
	entry->write_proc = irq_affinity_write_proc;

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

void __init init_IRQ(void)
{
	int i;

	for (i = 0; i < NR_IRQS; ++i)
		irq_affinity[i] = DEFAULT_CPU_AFFINITY;

	ppc_md.init_IRQ();
}
