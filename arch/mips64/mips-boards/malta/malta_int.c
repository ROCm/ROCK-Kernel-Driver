/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Routines for generic manipulation of the interrupts found on the MIPS 
 * Malta board.
 * The interrupt controller is located in the South Bridge a PIIX4 device 
 * with two internal 82C95 interrupt controllers.
 *
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/random.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mips-boards/malta.h>
#include <asm/mips-boards/maltaint.h>
#include <asm/mips-boards/piix4.h>
#include <asm/mips-boards/gt64120.h>
#include <asm/mips-boards/generic.h>

extern asmlinkage void mipsIRQ(void);

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
unsigned long spurious_count = 0;

static struct irqaction *hw0_irq_action[MALTAINT_END] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL
};

static struct irqaction r4ktimer_action = {
	NULL, 0, 0, "R4000 timer/counter", NULL, NULL,
};

static struct irqaction *irq_action[8] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, &r4ktimer_action
};

#if 0
#define DEBUG_INT(x...) printk(x)
#else
#define DEBUG_INT(x...)
#endif

/*
 * This contains the interrupt mask for both 82C59 interrupt controllers.
 */
static unsigned int cached_int_mask = 0xffff;


void disable_irq(unsigned int irq_nr)
{
        unsigned long flags;

	if(irq_nr >= MALTAINT_END) {
		printk("whee, invalid irq_nr %d\n", irq_nr);
		panic("IRQ, you lose...");
	}

	save_and_cli(flags);
	cached_int_mask |= (1 << irq_nr);
	if (irq_nr & 8) {
	        outb((cached_int_mask >> 8) & 0xff, PIIX4_ICTLR2_OCW1);
	} else {
		outb(cached_int_mask & 0xff, PIIX4_ICTLR1_OCW1);
	}
	restore_flags(flags);
}


void enable_irq(unsigned int irq_nr)
{
        unsigned long flags;

	if(irq_nr >= MALTAINT_END) {
		printk("whee, invalid irq_nr %d\n", irq_nr);
		panic("IRQ, you lose...");
	}

	save_and_cli(flags);
	cached_int_mask &= ~(1 << irq_nr);
	if (irq_nr & 8) {
		outb((cached_int_mask >> 8) & 0xff, PIIX4_ICTLR2_OCW1);

		/* Enable irq 2 (cascade interrupt). */
	        cached_int_mask &= ~(1 << 2); 
		outb(cached_int_mask & 0xff, PIIX4_ICTLR1_OCW1);
	} else {
		outb(cached_int_mask & 0xff, PIIX4_ICTLR1_OCW1);
	}	
	restore_flags(flags);
}


int get_irq_list(char *buf)
{
	int i, len = 0;
	int num = 0;
	struct irqaction *action;

	for (i = 0; i < 8; i++, num++) {
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
	for (i = 0; i < MALTAINT_END; i++, num++) {
		action = hw0_irq_action[i];
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
		len += sprintf(buf+len, " [hw0]\n");
	}
	return len;
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{  
        struct irqaction *action;
	int retval;

	DEBUG_INT("request_irq: irq=%d, devname = %s\n", irq, devname);

        if (irq >= MALTAINT_END)
	        return -EINVAL;
	if (!handler)
	        return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if(!action)
	        return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->dev_id = dev_id;
	action->next = 0;

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);

	return retval;	
}


void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction *action, **p;

	if (irq >= MALTAINT_END) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	
	for (p = &hw0_irq_action[irq]; (action = *p) != NULL; 
	     p = &action->next) 
	{
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		*p = action->next;
		kfree(action);
		if (!hw0_irq_action[irq])
			disable_irq(irq);
		return;
	}
	printk("Trying to free IRQ%d\n",irq);
}

void __init init_IRQ(void)
{
	irq_setup();
}

static int setup_irq(unsigned int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;

	p = &hw0_irq_action[irq];
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ))
			return -EBUSY;

		/* Can't share interrupts unless both are same type */
		if ((old->flags ^ new->flags) & SA_INTERRUPT)
			return -EBUSY;

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	if (new->flags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	*p = new;
	if (!shared)
		enable_irq(irq);

	return 0;
}

static inline int get_int(int *irq)
{
	/*  
	 * Determine highest priority pending interrupt by performing
         * a PCI Interrupt Acknowledge cycle.
	 */
	GT_READ(GT_PCI0_IACK_OFS, *irq);
	*irq &= 0xFF;

	/*  
	 * IRQ7 is used to detect spurious interrupts.
	 * The interrupt acknowledge cycle returns IRQ7, if no 
	 * interrupts is requested.
	 * We can differentiate between this situation and a
	 * "Normal" IRQ7 by reading the ISR.
	 */
	if (*irq == 7) 
	{
		outb(PIIX4_OCW3_SEL | PIIX4_OCW3_ISR, PIIX4_ICTLR1_OCW3);
		if (!(inb(PIIX4_ICTLR1_OCW3) & (1 << 7)))
		        return -1;    /* Spurious interrupt. */
	}

	return 0;
}

static inline void ack_int(int irq)
{
	if (irq & 8) {
	        /* Specific EOI to cascade */
		outb(PIIX4_OCW2_SEL | PIIX4_OCW2_NSEOI | PIIX4_OCW2_ILS_2, 
		     PIIX4_ICTLR1_OCW2);

	        /* Non specific EOI to cascade */
		outb(PIIX4_OCW2_SEL | PIIX4_OCW2_NSEOI, PIIX4_ICTLR2_OCW2);
	} else {
	        /* Non specific EOI to cascade */
		outb(PIIX4_OCW2_SEL | PIIX4_OCW2_NSEOI, PIIX4_ICTLR1_OCW2);
	}
}

void malta_hw0_irqdispatch(struct pt_regs *regs)
{
        struct irqaction *action;
	int irq=0, cpu = smp_processor_id();

	DEBUG_INT("malta_hw0_irqdispatch\n");
	
	if (get_int(&irq))
	        return;  /* interrupt has already been cleared */

	disable_irq(irq);
	ack_int(irq);

	DEBUG_INT("malta_hw0_irqdispatch: irq=%d\n", irq);
	action = hw0_irq_action[irq];

	/* 
	 * if action == NULL, then we don't have a handler 
	 * for the irq 
	 */
	if ( action == NULL )
		return;

	irq_enter(cpu, irq);
	kstat.irqs[0][irq + 8]++;
	do {
	        action->handler(irq, action->dev_id, regs);
		action = action->next;
	} while (action);

	enable_irq(irq);
	irq_exit(cpu, irq);
}


unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0;
	unsigned long delay;

	/* first, enable any unassigned irqs */
	for (i = MALTAINT_END-1; i > 0; i--) {
		if (!hw0_irq_action[i]) {
			enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + HZ/10; time_before(jiffies, delay); )
		/* about 100ms delay */;

	/* now filter out any obviously spurious interrupts */
	return irqs & ~cached_int_mask;
}


int probe_irq_off (unsigned long irqs)
{
	unsigned int i;

	irqs &= cached_int_mask;
	if (!irqs)
		return 0;
	i = ffz(~irqs);
	if (irqs != (irqs & (1 << i)))
		i = -i;

	return i;
}


void __init maltaint_init(void)
{
        /* 
	 * Mask out all interrupt by writing "1" to all bit position in 
	 * the IMR register. 
	 */
	outb(cached_int_mask & 0xff, PIIX4_ICTLR1_OCW1);
	outb((cached_int_mask >> 8) & 0xff, PIIX4_ICTLR2_OCW1);

	/* Now safe to set the exception vector. */
	set_except_vector(0, mipsIRQ);
}
