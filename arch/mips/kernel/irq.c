/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Code to handle x86 style IRQs plus some generic interrupt stuff.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994 - 2000 Ralf Baechle
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/sni.h>
#include <asm/nile4.h>

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

/*
 * This contains the irq mask for both 8259A irq controllers, it's an
 * int so we can deal with the third PIC in some systems like the RM300.
 * (XXX This is broken for big endian.)
 */
static unsigned int cached_irq_mask = 0xffff;

#define __byte(x,y) (((unsigned char *)&(y))[x])
#define __word(x,y) (((unsigned short *)&(y))[x])
#define __long(x,y) (((unsigned int *)&(y))[x])

#define cached_21       (__byte(0,cached_irq_mask))
#define cached_A1       (__byte(1,cached_irq_mask))

unsigned long spurious_count = 0;

/*
 * (un)mask_irq, disable_irq() and enable_irq() only handle (E)ISA and
 * PCI devices.  Other onboard hardware needs specific routines.
 */
static inline void mask_irq(unsigned int irq)
{
	cached_irq_mask |= 1 << irq;
	if (irq & 8) {
		outb(cached_A1, 0xa1);
	} else {
		outb(cached_21, 0x21);
	}
}

static inline void unmask_irq(unsigned int irq)
{
	cached_irq_mask &= ~(1 << irq);
	if (irq & 8) {
		outb(cached_A1, 0xa1);
	} else {
		outb(cached_21, 0x21);
	}
}

void i8259_disable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	save_and_cli(flags);
	mask_irq(irq_nr);
	restore_flags(flags);
}

void i8259_enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	save_and_cli(flags);
	unmask_irq(irq_nr);
	restore_flags(flags);
}

static struct irqaction *irq_action[NR_IRQS] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

int get_irq_list(char *buf)
{
	int i, len = 0;
	struct irqaction * action;

	for (i = 0 ; i < 32 ; i++) {
		action = irq_action[i];
		if (!action) 
			continue;
		len += sprintf(buf+len, "%2d: %8d %c %s",
			i, kstat.irqs[0][i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			len += sprintf(buf+len, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		len += sprintf(buf+len, "\n");
	}
	return len;
}

static inline void i8259_mask_and_ack_irq(int irq)
{
	cached_irq_mask |= 1 << irq;

	if (irq & 8) {
		inb(0xa1);
		outb(cached_A1, 0xa1);
		outb(0x62, 0x20);		/* Specific EOI to cascade */
                outb(0x20, 0xa0);
        } else {
		inb(0x21);
		outb(cached_21, 0x21);
		outb(0x20, 0x20);
        }
}

asmlinkage void i8259_do_irq(int irq, struct pt_regs *regs)
{
	struct irqaction *action;
	int do_random, cpu;

	cpu = smp_processor_id();
	irq_enter(cpu);

	if (irq >= 16)
		goto out;

	i8259_mask_and_ack_irq(irq);

	kstat.irqs[cpu][irq]++;

	action = *(irq + irq_action);
	if (!action)
		goto out;

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
	unmask_irq (irq);

out:
	irq_exit(cpu);
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
	kstat.irqs[cpu][irq]++;

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

	if (softirq_active(cpu)&softirq_mask(cpu))
		do_softirq();

	/* unmasking and bottom half handling is done magically for us. */
}

int i8259_setup_irq(int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	p = irq_action + irq;
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

	save_and_cli(flags);
	*p = new;

	if (!shared) {
		if (is_i8259_irq(irq))
		    unmask_irq(irq);
#if CONFIG_DDB5074 /* This has no business here  */
		else
		    nile4_enable_irq(irq_to_nile4(irq));
#endif
	}
	restore_flags(flags);
	return 0;
}

/*
 * Request_interrupt and free_interrupt ``sort of'' handle interrupts of
 * non i8259 devices.  They will have to be replaced by architecture
 * specific variants.  For now we still use this as broken as it is because
 * it used to work ...
 */
int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, const char * devname, void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq >= 32)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = i8259_setup_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq > 31) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_and_cli(flags);
		*p = action->next;
		if (!irq[irq_action])
			mask_irq(irq);
		restore_flags(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0;
	unsigned long delay;

	/* first, enable any unassigned (E)ISA irqs */
	for (i = 15; i > 0; i--) {
		if (!irq_action[i]) {
			i8259_enable_irq(i);
			irqs |= (1 << i);
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + HZ/10; time_before(jiffies, delay); )
		/* about 100ms delay */;

	/* now filter out any obviously spurious interrupts */
	return irqs & ~cached_irq_mask;
}

int probe_irq_off (unsigned long irqs)
{
	unsigned int i;

#ifdef DEBUG
	printk("probe_irq_off: irqs=0x%04x irqmask=0x%04x\n", irqs, irqmask);
#endif
	irqs &= cached_irq_mask;
	if (!irqs)
		return 0;
	i = ffz(~irqs);
	if (irqs != (irqs & (1 << i)))
		i = -i;
	return i;
}

int (*irq_cannonicalize)(int irq);

static int i8259_irq_cannonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}

void __init i8259_init(void)
{
	/* Init master interrupt controller */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x00, 0x21); /* Vector base */
	outb(0x04, 0x21); /* edge tiggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0x21); /* Select 8086 mode */
	outb(0xff, 0x21); /* Mask all */
        
	/* Init slave interrupt controller */
	outb(0x11, 0xa0); /* Start init sequence */
	outb(0x08, 0xa1); /* Vector base */
	outb(0x02, 0xa1); /* edge triggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0xa1); /* Select 8086 mode */
	outb(0xff, 0xa1); /* Mask all */

	outb(cached_A1, 0xa1);
	outb(cached_21, 0x21);
}

void __init init_IRQ(void)
{
	irq_cannonicalize = i8259_irq_cannonicalize;
	/* i8259_init(); */
	irq_setup();
}

EXPORT_SYMBOL(irq_cannonicalize);
