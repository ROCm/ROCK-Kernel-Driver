/*
 * linux/arch/arm/mach-iop3xx/iq80310-irq.c
 *
 * IRQ hadling/demuxing for IQ80310 board
 *
 * Author:  Nicolas Pitre
 * Copyright:   (C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 2.4.7-rmk1-iop310.1
 *     Moved demux from asm to C - DS
 *     Fixes for various revision boards - DS
 */
#include <linux/init.h>
#include <linux/list.h>

#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/hardware.h>
#include <asm/system.h>

#include <asm/mach-types.h>

extern void iop310_init_irq(void);
extern void iop310_irq_demux(unsigned int, struct irqdesc *, struct pt_regs *);

static void iq80310_irq_mask(unsigned int irq)
{
	*(volatile char *)IQ80310_INT_MASK |= (1 << (irq - IQ80310_IRQ_OFS));
}

static void iq80310_irq_unmask(unsigned int irq)
{
	*(volatile char *)IQ80310_INT_MASK &= ~(1 << (irq - IQ80310_IRQ_OFS));
}

static struct irqchip iq80310_irq_chip = {
	.ack	= iq80310_irq_mask,
	.mask	= iq80310_irq_mask,
	.unmask = iq80310_irq_unmask,
};

extern struct irqchip ext_chip;

static void
iq80310_cpld_irq_handler(unsigned int irq, struct irqdesc *desc,
			 struct pt_regs *regs)
{
	unsigned int irq_stat = *(volatile u8*)IQ80310_INT_STAT;
	unsigned int irq_mask = *(volatile u8*)IQ80310_INT_MASK;
	unsigned int i, handled = 0;
	struct irqdesc *d;

	desc->chip->ack(irq);

	/*
	 * Mask out the interrupts which aren't enabled.
	 */
	irq_stat &= 0x1f & ~irq_mask;

	/*
	 * Test each IQ80310 CPLD interrupt
	 */
	for (i = IRQ_IQ80310_TIMER, d = irq_desc + IRQ_IQ80310_TIMER;
	     irq_stat; i++, d++, irq_stat >>= 1)
		if (irq_stat & 1) {
			d->handle(i, d, regs);
			handled++;
		}

	/*
	 * If running on a board later than REV D.1, we can
	 * decode the PCI interrupt status.
	 */
	if (system_rev) {
		irq_stat = *((volatile u8*)IQ80310_PCI_INT_STAT) & 7;

		for (i = IRQ_IQ80310_INTA, d = irq_desc + IRQ_IQ80310_INTA;
		     irq_stat; i++, d++, irq_stat >>= 1)
			if (irq_stat & 0x1) {
				d->handle(i, d, regs);
				handled++;
			}
	}

	/*
	 * If on a REV D.1 or lower board, we just assumed INTA
	 * since PCI is not routed, and it may actually be an
	 * on-chip interrupt.
	 *
	 * Note that we're giving on-chip interrupts slightly
	 * higher priority than PCI by handling them first.
	 *
	 * On boards later than REV D.1, if we didn't read a
	 * CPLD interrupt, we assume it's from a device on the
	 * chipset itself.
	 */
	if (system_rev == 0 || handled == 0)
		iop310_irq_demux(irq, desc, regs);

	desc->chip->unmask(irq);
}

void __init iq80310_init_irq(void)
{
	volatile char *mask = (volatile char *)IQ80310_INT_MASK;
	unsigned int i;

	iop310_init_irq();

	/*
	 * Setup PIRSR to route PCI interrupts into xs80200
	 */
	*IOP310_PIRSR = 0xff;

	/*
	 * Setup the IRQs in the FE820000/FE860000 registers
	 */
	for (i = IQ80310_IRQ_OFS; i <= IRQ_IQ80310_INTD; i++) {
		set_irq_chip(i, &iq80310_irq_chip);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	/*
	 * Setup the PCI IRQs
	 */
	for (i = IRQ_IQ80310_INTA; i < IRQ_IQ80310_INTC; i++) {
		set_irq_chip(i, &ext_chip);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID);
	}

	*mask = 0xff;  /* mask all sources */

	set_irq_chained_handler(IRQ_XS80200_EXTIRQ,
				&iq80310_cpld_irq_handler);
}
