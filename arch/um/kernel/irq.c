/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 * Derived (i.e. mostly copied) from arch/i386/kernel/irq.c:
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 */

#include "linux/config.h"
#include "linux/kernel.h"
#include "linux/module.h"
#include "linux/smp.h"
#include "linux/irq.h"
#include "linux/kernel_stat.h"
#include "linux/interrupt.h"
#include "linux/random.h"
#include "linux/slab.h"
#include "linux/file.h"
#include "linux/proc_fs.h"
#include "linux/init.h"
#include "linux/seq_file.h"
#include "asm/irq.h"
#include "asm/hw_irq.h"
#include "asm/hardirq.h"
#include "asm/atomic.h"
#include "asm/signal.h"
#include "asm/system.h"
#include "asm/errno.h"
#include "asm/uaccess.h"
#include "user_util.h"
#include "kern_util.h"
#include "irq_user.h"

static void register_irq_proc (unsigned int irq);

irq_desc_t irq_desc[NR_IRQS] __cacheline_aligned = {
	[0 ... NR_IRQS-1] = {
		.handler = &no_irq_type,
		.lock = SPIN_LOCK_UNLOCKED
	}
};

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

/* Not changed */
volatile unsigned long irq_err_count;

/*
 * Generic, controller-independent functions:
 */

int get_irq_list(char *buf)
{
	int i, j;
	unsigned long flags;
	struct irqaction * action;
	char *p = buf;

	p += sprintf(p, "           ");
	for (j=0; j<num_online_cpus(); j++)
		p += sprintf(p, "CPU%d       ",j);
	*p++ = '\n';

	for (i = 0 ; i < NR_IRQS ; i++) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action) 
			goto end;
		p += sprintf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		p += sprintf(p, "%10u ", kstat_irqs(i));
#else
		for (j = 0; j < num_online_cpus(); j++)
			p += sprintf(p, "%10u ",
				kstat_cpu(cpu_logical_map(j)).irqs[i]);
#endif
		p += sprintf(p, " %14s", irq_desc[i].handler->typename);
		p += sprintf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			p += sprintf(p, ", %s", action->name);
		*p++ = '\n';
	end:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}
	p += sprintf(p, "\n");
#ifdef notdef
#ifdef CONFIG_SMP
	p += sprintf(p, "LOC: ");
	for (j = 0; j < num_online_cpus(); j++)
		p += sprintf(p, "%10u ",
			apic_timer_irqs[cpu_logical_map(j)]);
	p += sprintf(p, "\n");
#endif
#endif
	p += sprintf(p, "ERR: %10lu\n", irq_err_count);
	return p - buf;
}


int show_interrupts(struct seq_file *p, void *v)
{
	return(0);
}

/*
 * This should really return information about whether
 * we should do bottom half handling etc. Right now we
 * end up _always_ checking the bottom half, which is a
 * waste of time and is not what some drivers would
 * prefer.
 */
int handle_IRQ_event(unsigned int irq, struct pt_regs * regs, 
		     struct irqaction * action)
{
	int status = 1;	/* Force the "do bottom halves" bit */

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

	return status;
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
		desc->status |= IRQ_DISABLED;
		desc->handler->disable(irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

#ifdef CONFIG_SMP
inline void synchronize_irq(unsigned int irq)
{
	/* is there anything to synchronize with? */
	if (!irq_desc[irq].action)
		return;
 
	while (irq_desc[irq].status & IRQ_INPROGRESS)
		cpu_relax();
}
#endif

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
	disable_irq_nosync(irq);
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
		desc->handler->enable(irq);
		/* fall-through */
	}
	default:
		desc->depth--;
		break;
	case 0:
		printk(KERN_ERR "enable_irq() unbalanced from %p\n",
		       __builtin_return_address(0));
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
unsigned int do_IRQ(int irq, union uml_pt_regs *regs)
{	
	/* 
	 * 0 return value means that this irq is already being
	 * handled by some other CPU. (or is disabled)
	 */
	int cpu = smp_processor_id();
	irq_desc_t *desc = irq_desc + irq;
	struct irqaction * action;
	unsigned int status;

	irq_enter();
	kstat_cpu(cpu).irqs[irq]++;
	spin_lock(&desc->lock);
	desc->handler->ack(irq);
	/*
	   REPLAY is when Linux resends an IRQ that was dropped earlier
	   WAITING is used by probe to mark irqs that are being tested
	   */
	status = desc->status & ~(IRQ_REPLAY | IRQ_WAITING);
	status |= IRQ_PENDING; /* we _want_ to handle it */

	/*
	 * If the IRQ is disabled for whatever reason, we cannot
	 * use the action we have.
	 */
	action = NULL;
	if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
		status &= ~IRQ_PENDING; /* we commit to handling */
		status |= IRQ_INPROGRESS; /* we are handling it */
	}
	desc->status = status;

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	   Since we set PENDING, if another processor is handling
	   a different instance of this same irq, the other processor
	   will take care of it.
	 */
	if (!action)
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
		handle_IRQ_event(irq, (struct pt_regs *) regs, action);
		spin_lock(&desc->lock);
		
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
		void (*handler)(int, void *, struct pt_regs *),
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
			printk(KERN_ERR "Bad boy: %s (at 0x%x) called us "
			       "without a dev_id!\n", devname, (&irq)[-1]);
	}
#endif

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)
			kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
	return retval;
}

EXPORT_SYMBOL(request_irq);

int um_request_irq(unsigned int irq, int fd, int type,
		   void (*handler)(int, void *, struct pt_regs *),
		   unsigned long irqflags, const char * devname,
		   void *dev_id)
{
	int retval;

	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if(retval) return(retval);
	return(activate_fd(irq, fd, type, dev_id));
}

/* this was setup_x86_irq but it seems pretty generic */
int setup_irq(unsigned int irq, struct irqaction * new)
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
		desc->status &= ~IRQ_DISABLED;
		desc->handler->startup(irq);
	}
	spin_unlock_irqrestore(&desc->lock,flags);

	register_irq_proc(irq);
	return 0;
}

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
 *	This function may be called from interrupt context. 
 *
 *	Bugs: Attempting to free an irq in a handler for the same irq hangs
 *	      the machine.
 */
 
void free_irq(unsigned int irq, void *dev_id)
{
	irq_desc_t *desc;
	struct irqaction **p;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return;

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
				desc->handler->shutdown(irq);
			}
			free_irq_by_irq_and_dev(irq, dev_id);
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

/* These are initialized by sysctl_init, which is called from init/main.c */
static struct proc_dir_entry * root_irq_dir;
static struct proc_dir_entry * irq_dir [NR_IRQS];
static struct proc_dir_entry * smp_affinity_entry [NR_IRQS];

/* These are read and written as longs, so a read won't see a partial write
 * even during a race.
 */
static cpumask_t irq_affinity [NR_IRQS] = { [0 ... NR_IRQS-1] = CPU_MASK_ALL };

static int irq_affinity_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len = cpumask_scnprintf(page, count, irq_affinity[(long)data]);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static int irq_affinity_write_proc (struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	int irq = (long) data, full_count = count, err;
	cpumask_t new_value, tmp;

	if (!irq_desc[irq].handler->set_affinity)
		return -EIO;

	err = cpumask_parse(buffer, count, new_value);

#ifdef CONFIG_SMP
	/*
	 * Do not allow disabling IRQs completely - it's a too easy
	 * way to make the system unusable accidentally :-) At least
	 * one online CPU still has to be targeted.
	 */
	cpus_and(tmp, new_value, cpu_online_map);
	if (cpus_empty(tmp))
		return -EINVAL;
#endif

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

static int prof_cpu_mask_write_proc (struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	cpumask_t *mask = (cpumask_t *)data, new_value;
	unsigned long full_count = count, err;

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

	if (!root_irq_dir || (irq_desc[irq].handler == &no_irq_type) ||
	    irq_dir[irq])
		return;

	memset(name, 0, MAX_NAMELEN);
	sprintf(name, "%d", irq);

	/* create /proc/irq/1234 */
	irq_dir[irq] = proc_mkdir(name, root_irq_dir);

	/* create /proc/irq/1234/smp_affinity */
	entry = create_proc_entry("smp_affinity", 0600, irq_dir[irq]);

	entry->nlink = 1;
	entry->data = (void *)(long)irq;
	entry->read_proc = irq_affinity_read_proc;
	entry->write_proc = irq_affinity_write_proc;

	smp_affinity_entry[irq] = entry;
}

/* Read and written as a long */
cpumask_t prof_cpu_mask = CPU_MASK_ALL;

void __init init_irq_proc (void)
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
	for (i = 0; i < NR_IRQS; i++)
		register_irq_proc(i);
}

static spinlock_t irq_spinlock = SPIN_LOCK_UNLOCKED;

unsigned long irq_lock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_spinlock, flags);
	return(flags);
}

void irq_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&irq_spinlock, flags);
}

unsigned long probe_irq_on(void)
{
	return(0);
}

EXPORT_SYMBOL(probe_irq_on);

int probe_irq_off(unsigned long val)
{
	return(0);
}

EXPORT_SYMBOL(probe_irq_off);

static unsigned int startup_SIGIO_irq(unsigned int irq)
{
	return(0);
}

static void shutdown_SIGIO_irq(unsigned int irq)
{
}

static void enable_SIGIO_irq(unsigned int irq)
{
}

static void disable_SIGIO_irq(unsigned int irq)
{
}

static void mask_and_ack_SIGIO(unsigned int irq)
{
}

static void end_SIGIO_irq(unsigned int irq)
{
}

static unsigned int startup_SIGVTALRM_irq(unsigned int irq)
{
	return(0);
}

static void shutdown_SIGVTALRM_irq(unsigned int irq)
{
}

static void enable_SIGVTALRM_irq(unsigned int irq)
{
}

static void disable_SIGVTALRM_irq(unsigned int irq)
{
}

static void mask_and_ack_SIGVTALRM(unsigned int irq)
{
}

static void end_SIGVTALRM_irq(unsigned int irq)
{
}

static struct hw_interrupt_type SIGIO_irq_type = {
	"SIGIO",
	startup_SIGIO_irq,
	shutdown_SIGIO_irq,
	enable_SIGIO_irq,
	disable_SIGIO_irq,
	mask_and_ack_SIGIO,
	end_SIGIO_irq,
	NULL
};

static struct hw_interrupt_type SIGVTALRM_irq_type = {
	"SIGVTALRM",
	startup_SIGVTALRM_irq,
	shutdown_SIGVTALRM_irq,
	enable_SIGVTALRM_irq,
	disable_SIGVTALRM_irq,
	mask_and_ack_SIGVTALRM,
	end_SIGVTALRM_irq,
	NULL
};

void __init init_IRQ(void)
{
	int i;

	irq_desc[TIMER_IRQ].status = IRQ_DISABLED;
	irq_desc[TIMER_IRQ].action = 0;
	irq_desc[TIMER_IRQ].depth = 1;
	irq_desc[TIMER_IRQ].handler = &SIGVTALRM_irq_type;
	enable_irq(TIMER_IRQ);
	for(i=1;i<NR_IRQS;i++){
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = 0;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &SIGIO_irq_type;
		enable_irq(i);
	}
	init_irq_signals(0);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
