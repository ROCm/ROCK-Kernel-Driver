/* $Id: irq.c,v 1.8 2000/02/08 02:01:17 grundler Exp $
 *
 * Code to handle x86 style IRQs plus some generic interrupt stuff.
 *
 * This is not in any way SMP-clean.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 Ralf Baechle
 * Copyright (C) 1999 SuSE GmbH (Author: Philipp Rumpf, prumpf@tux.org)
 * Copyright (C) 2000 Hewlett Packard Corp (Co-Author: Grant Grundler, grundler@cup.hp.com)
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/config.h>
#include <linux/bitops.h>
#include <asm/bitops.h>
#include <asm/pdc.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/cache.h>

#undef DEBUG_IRQ

extern void timer_interrupt(int, void *, struct pt_regs *);
extern void ipi_interrupt(int, void *, struct pt_regs *);

#ifdef DEBUG_IRQ
#define DBG_IRQ(x...)   printk(x)
#else /* DEBUG_IRQ */
#define DBG_IRQ(x...)
#endif /* DEBUG_IRQ */

#define EIEM_MASK(irq) (1L<<(MAX_CPU_IRQ-IRQ_OFFSET(irq)))
#define CLEAR_EIEM_BIT(irq) set_eiem(get_eiem() & ~EIEM_MASK(irq))
#define SET_EIEM_BIT(irq) set_eiem(get_eiem() | EIEM_MASK(irq))

static void disable_cpu_irq(void *unused, int irq)
{
	CLEAR_EIEM_BIT(irq);
}

static void enable_cpu_irq(void *unused, int irq)
{
	unsigned long mask = EIEM_MASK(irq);

	mtctl(mask, 23);
	SET_EIEM_BIT(irq);
}

static struct irqaction cpu_irq_actions[IRQ_PER_REGION] = {
	[IRQ_OFFSET(TIMER_IRQ)] { timer_interrupt, 0, 0, "timer", NULL, NULL },
	[IRQ_OFFSET(IPI_IRQ)]	{ ipi_interrupt, 0, 0, "IPI", NULL, NULL },
};

struct irq_region cpu_irq_region = {
	{ disable_cpu_irq, enable_cpu_irq, NULL, NULL },
	{ &cpu_data[0], "PA-PIC", IRQ_REG_MASK|IRQ_REG_DIS, IRQ_FROM_REGION(CPU_IRQ_REGION)},
	cpu_irq_actions
};

struct irq_region *irq_region[NR_IRQ_REGS] = {
	[ 0 ] NULL,		/* abuse will data page fault (aka code 15) */
	[ CPU_IRQ_REGION ] &cpu_irq_region,
};



/* we special-case the real IRQs here, which feels right given the relatively
 * high cost of indirect calls.  If anyone is bored enough to benchmark this
 * and find out whether I am right, feel free to.   prumpf */

static inline void mask_irq(int irq)
{
	struct irq_region *region;
	
#ifdef DEBUG_IRQ
	if (irq != TIMER_IRQ)
#endif
	DBG_IRQ("mask_irq(%d) %d+%d\n", irq, IRQ_REGION(irq), IRQ_OFFSET(irq));

	if(IRQ_REGION(irq) != CPU_IRQ_REGION) {
		region = irq_region[IRQ_REGION(irq)];
		if(region->data.flags & IRQ_REG_MASK)
			region->ops.mask_irq(region->data.dev, IRQ_OFFSET(irq));
	} else {
		CLEAR_EIEM_BIT(irq);
	}
}

static inline void unmask_irq(int irq)
{
	struct irq_region *region;

#ifdef DEBUG_IRQ
	if (irq != TIMER_IRQ)
#endif
	DBG_IRQ("unmask_irq(%d) %d+%d\n", irq, IRQ_REGION(irq), IRQ_OFFSET(irq));

	if(IRQ_REGION(irq) != CPU_IRQ_REGION) {
		region = irq_region[IRQ_REGION(irq)];
		if(region->data.flags & IRQ_REG_MASK)
			region->ops.unmask_irq(region->data.dev, IRQ_OFFSET(irq));
	} else {
		SET_EIEM_BIT(irq);
	}
}

void disable_irq(int irq)
{
	struct irq_region *region;

#ifdef DEBUG_IRQ
	if (irq != TIMER_IRQ)
#endif
	DBG_IRQ("disable_irq(%d) %d+%d\n", irq, IRQ_REGION(irq), IRQ_OFFSET(irq));
	region = irq_region[IRQ_REGION(irq)];

	if(region->data.flags & IRQ_REG_DIS)
		region->ops.disable_irq(region->data.dev, IRQ_OFFSET(irq));
	else
		BUG();
}

void enable_irq(int irq) 
{
	struct irq_region *region;

#ifdef DEBUG_IRQ
	if (irq != TIMER_IRQ)
#endif
	DBG_IRQ("enable_irq(%d) %d+%d\n", irq, IRQ_REGION(irq), IRQ_OFFSET(irq));
	region = irq_region[IRQ_REGION(irq)];

	if(region->data.flags & IRQ_REG_DIS)
		region->ops.enable_irq(region->data.dev, IRQ_OFFSET(irq));
	else
		BUG();
}

int get_irq_list(char *buf)
{
#ifdef CONFIG_PROC_FS
	char *p = buf;
	int i, j;
	int regnr, irq_no;
	struct irq_region *region;
	struct irqaction *action, *mainaction;

	p += sprintf(p, "           ");
	for (j=0; j<smp_num_cpus; j++)
		p += sprintf(p, "CPU%d       ",j);
	*p++ = '\n';

	for (regnr = 0; regnr < NR_IRQ_REGS; regnr++) {
	    region = irq_region[regnr];
	    if (!region || !region->action)
		continue;
	    
	    mainaction = region->action;

	    for (i = 0; i <= MAX_CPU_IRQ; i++) {
		action = mainaction++;
		if (!action || !action->name)
		    continue;
		
		irq_no = IRQ_FROM_REGION(regnr) + i;
		
		p += sprintf(p, "%3d: ", irq_no);
#ifndef CONFIG_SMP
		p += sprintf(p, "%10u ", kstat_irqs(irq_no));
#else
		for (j = 0; j < smp_num_cpus; j++)
		    p += sprintf(p, "%10u ",
			    kstat.irqs[cpu_logical_map(j)][irq_no]);
#endif
		p += sprintf(p, " %14s", 
			    region->data.name ? region->data.name : "N/A");
		p += sprintf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
		    p += sprintf(p, ", %s", action->name);
		*p++ = '\n';
	    }	    	     
	}  

	p += sprintf(p, "\n");
#if CONFIG_SMP
	p += sprintf(p, "LOC: ");
	for (j = 0; j < smp_num_cpus; j++)
		p += sprintf(p, "%10u ",
			apic_timer_irqs[cpu_logical_map(j)]);
	p += sprintf(p, "\n");
#endif

	return p - buf;

#else	/* CONFIG_PROC_FS */

	return 0;	

#endif	/* CONFIG_PROC_FS */
}



/*
** The following form a "set": Virtual IRQ, Transaction Address, Trans Data.
** Respectively, these map to IRQ region+EIRR, Processor HPA, EIRR bit.
**
** To use txn_XXX() interfaces, get a Virtual IRQ first.
** Then use that to get the Transaction address and data.
*/

int
txn_alloc_irq(void)
{
	int irq;

	/* never return irq 0 cause that's the interval timer */
	for(irq=1; irq<=MAX_CPU_IRQ; irq++) {
		if(cpu_irq_region.action[irq].handler == NULL) {
			return (IRQ_FROM_REGION(CPU_IRQ_REGION) + irq);
		}
	}

	/* unlikely, but be prepared */
	return -1;
}

int
txn_claim_irq(int irq)
{
	if (irq_region[IRQ_REGION(irq)]->action[IRQ_OFFSET(irq)].handler ==NULL)
	{
		return irq;
	}

	/* unlikely, but be prepared */
	return -1;
}

unsigned long
txn_alloc_addr(int virt_irq)
{
	struct cpuinfo_parisc *dev = (struct cpuinfo_parisc *) (irq_region[IRQ_REGION(virt_irq)]->data.dev);

	if (0==dev) {
		printk(KERN_ERR "txn_alloc_addr(0x%x): CPU IRQ region? dev %p\n",
			virt_irq,dev);
		return(0UL);
	}
	return (dev->txn_addr);
}


/*
** The alloc process needs to accept a parameter to accomodate limitations
** of the HW/SW which use these bits:
** Legacy PA I/O (GSC/NIO): 5 bits (architected EIM register)
** V-class (EPIC):          6 bits
** N/L-class/A500:          8 bits (iosapic)
** PCI 2.2 MSI:             16 bits (I think)
** Existing PCI devices:    32-bits (NCR c720/ATM/GigE/HyperFabric)
**
** On the service provider side:
** o PA 1.1 (and PA2.0 narrow mode)     5-bits (width of EIR register)
** o PA 2.0 wide mode                   6-bits (per processor)
** o IA64                               8-bits (0-256 total)
**
** So a Legacy PA I/O device on a PA 2.0 box can't use all
** the bits supported by the processor...and the N/L-class
** I/O subsystem supports more bits than PA2.0 has. The first
** case is the problem.
*/
unsigned int
txn_alloc_data(int virt_irq, unsigned int bits_wide)
{
	/* XXX FIXME : bits_wide indicates how wide the transaction
	** data is allowed to be...we may need a different virt_irq
	** if this one won't work. Another reason to index virtual
	** irq's into a table which can manage CPU/IRQ bit seperately.
	*/
	if (IRQ_OFFSET(virt_irq) > (1 << (bits_wide -1)))
	{
		panic("Sorry -- didn't allocate valid IRQ for this device\n");
	}

	return(IRQ_OFFSET(virt_irq));
}


/* FIXME: SMP, flags, bottom halves, rest */
void do_irq(struct irqaction *action, int irq, struct pt_regs * regs)
{
	int cpu = smp_processor_id();

	irq_enter(cpu, irq);

#ifdef DEBUG_IRQ
	if (irq != TIMER_IRQ)
#endif
	DBG_IRQ("do_irq(%d) %d+%d\n", irq, IRQ_REGION(irq), IRQ_OFFSET(irq));
	if (action->handler == NULL)
		printk(KERN_ERR "No handler for interrupt %d !\n", irq);

	for(; action && action->handler; action = action->next) {
		action->handler(irq, action->dev_id, regs);
	}
	
	irq_exit(cpu, irq);

	/* don't need to care about unmasking and stuff */
	do_softirq();
}

void do_irq_mask(unsigned long mask, struct irq_region *region, struct pt_regs *regs)
{
	unsigned long bit;
	int irq;
	int cpu = smp_processor_id();

#ifdef DEBUG_IRQ
	if (mask != (1L << MAX_CPU_IRQ))
	    printk("do_irq_mask %08lx %p %p\n", mask, region, regs);
#endif

	for(bit=(1L<<MAX_CPU_IRQ), irq = 0; mask && bit; bit>>=1, irq++) {
		int irq_num;
		if(!(bit&mask))
			continue;

		irq_num = region->data.irqbase + irq;

		++kstat.irqs[cpu][IRQ_FROM_REGION(CPU_IRQ_REGION) | irq];
		if (IRQ_REGION(irq_num) != CPU_IRQ_REGION)
		    ++kstat.irqs[cpu][irq_num];

		mask_irq(irq_num);
		do_irq(&region->action[irq], irq_num, regs);
		unmask_irq(irq_num);
	}
}

static inline int alloc_irqregion(void)
{
	int irqreg;

	for(irqreg=1; irqreg<=(NR_IRQ_REGS); irqreg++) {
		if(irq_region[irqreg] == NULL)
			return irqreg;
	}

	return 0;
}

struct irq_region *alloc_irq_region(
	int count, struct irq_region_ops *ops, unsigned long flags,
	const char *name, void *dev)
{
	struct irq_region *region;
	int index;

	index = alloc_irqregion();

	if((IRQ_REGION(count-1)))
		return NULL;
	
	if (count < IRQ_PER_REGION) {
	    DBG_IRQ("alloc_irq_region() using minimum of %d irq lines for %s (%d)\n", 
			IRQ_PER_REGION, name, count);
	    count = IRQ_PER_REGION;
	}

	if(flags & IRQ_REG_MASK)
		if(!(ops->mask_irq && ops->unmask_irq))
			return NULL;
			
	if(flags & IRQ_REG_DIS)
		if(!(ops->disable_irq && ops->enable_irq))
			return NULL;

	if((irq_region[index]))
		return NULL;

	region = kmalloc(sizeof *region, GFP_ATOMIC);
	if(!region)
		return NULL;

	region->action = kmalloc(sizeof *region->action * count, GFP_ATOMIC);
	if(!region->action) {
		kfree(region);
		return NULL;
	}
	memset(region->action, 0, sizeof *region->action * count);

	region->ops = *ops;
	region->data.irqbase = IRQ_FROM_REGION(index);
	region->data.flags = flags;
	region->data.name = name;
	region->data.dev = dev;

	irq_region[index] = region;

	return irq_region[index];
}
	

	
/* FIXME: SMP, flags, bottom halves, rest */

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	struct irqaction * action;

#if 0
	printk(KERN_INFO "request_irq(%d, %p, 0x%lx, %s, %p)\n",irq, handler, irqflags, devname, dev_id);
#endif
	if(!handler) {
		printk(KERN_ERR "request_irq(%d,...): Augh! No handler for irq!\n",
			irq);
		return -EINVAL;
	}

	if ((IRQ_REGION(irq) == 0) || irq_region[IRQ_REGION(irq)] == NULL) {
		/*
		** Bug catcher for drivers which use "char" or u8 for
		** the IRQ number. They lose the region number which
		** is in pcidev->irq (an int).
		*/
		printk(KERN_ERR "%p (%s?) called request_irq with an invalid irq %d\n",
			__builtin_return_address(0), devname, irq);
		return -EINVAL;
	}

	action = &irq_region[IRQ_REGION(irq)]->action[IRQ_OFFSET(irq)];

	if(action->handler) {
		while(action->next)
			action = action->next;

		action->next = kmalloc(sizeof *action, GFP_ATOMIC);

		action = action->next;
	}			

	if(!action) {
		printk(KERN_ERR "request_irq():Augh! No action!\n") ;
		return -ENOMEM;
	}

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	enable_irq(irq);
	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction *action, **p;

	action = &irq_region[IRQ_REGION(irq)]->action[IRQ_OFFSET(irq)];

	if(action->dev_id == dev_id) {
		if(action->next == NULL)
			action->handler = NULL;
		else
			memcpy(action, action->next, sizeof *action);

		return;
	}

	p = &action->next;
	action = action->next;

	for (; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		*p = action->next;
		kfree(action);

		return;
	}

	printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
}

unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}


void __init init_IRQ(void)
{
}

void init_irq_proc(void)
{
}
