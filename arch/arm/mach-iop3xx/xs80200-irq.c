/*
 * linux/arch/arm/mach-iop3xx/xs80200-irq.c
 *
 * Generic IRQ handling for the XS80200 XScale core.
 *
 * Author:  Nicolas Pitre
 * Copyright:   (C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/list.h>

#include <asm/mach/irq.h>
#include <asm/irq.h>
#include <asm/hardware.h>

static void xs80200_irq_mask (unsigned int irq)
{
	unsigned long intctl;
	asm ("mrc p13, 0, %0, c0, c0, 0" : "=r" (intctl));
	switch (irq) {
	    case IRQ_XS80200_BCU:     intctl &= ~(1<<3); break;
	    case IRQ_XS80200_PMU:     intctl &= ~(1<<2); break;
	    case IRQ_XS80200_EXTIRQ:  intctl &= ~(1<<1); break;
	    case IRQ_XS80200_EXTFIQ:  intctl &= ~(1<<0); break;
	}
	asm ("mcr p13, 0, %0, c0, c0, 0" : : "r" (intctl));
}

static void xs80200_irq_unmask (unsigned int irq)
{
	unsigned long intctl;
	asm ("mrc p13, 0, %0, c0, c0, 0" : "=r" (intctl));
	switch (irq) {
	    case IRQ_XS80200_BCU:	intctl |= (1<<3); break;
	    case IRQ_XS80200_PMU:	intctl |= (1<<2); break;
	    case IRQ_XS80200_EXTIRQ:	intctl |= (1<<1); break;
	    case IRQ_XS80200_EXTFIQ:	intctl |= (1<<0); break;
	}
	asm ("mcr p13, 0, %0, c0, c0, 0" : : "r" (intctl));
}

static struct irqchip xs80200_chip = {
	.ack	= xs80200_irq_mask,
	.mask	= xs80200_irq_mask,
	.unmask = xs80200_irq_unmask,
};

void __init xs80200_init_irq(void)
{
	unsigned int i;

	asm("mcr p13, 0, %0, c0, c0, 0" : : "r" (0));

	for (i = 0; i < NR_XS80200_IRQS; i++) {
		set_irq_chip(i, &xs80200_chip);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID);
	}
}
