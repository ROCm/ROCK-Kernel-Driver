/*
 * linux/arch/arm/mach-iop3xx/iop310-irq.c
 *
 * Generic IOP310 IRQ handling functionality
 *
 * Author:  Nicolas Pitre
 * Copyright:   (C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Added IOP310 chipset and IQ80310 board demuxing, masking code. - DS
 *
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>

#include <asm/mach/irq.h>
#include <asm/irq.h>
#include <asm/hardware.h>

#include <asm/mach-types.h>

extern void xs80200_irq_mask(unsigned int);
extern void xs80200_irq_unmask(unsigned int);
extern void xs80200_init_irq(void);

extern void do_IRQ(int, struct pt_regs *);

static u32 iop310_mask /* = 0 */;

static void iop310_irq_mask (unsigned int irq)
{
	iop310_mask ++;

	/*
	 * No mask bits on the 80312, so we have to
	 * mask everything from the outside!
	 */
	if (iop310_mask == 1) {
		disable_irq(IRQ_XS80200_EXTIRQ);
		irq_desc[IRQ_XS80200_EXTIRQ].chip->mask(IRQ_XS80200_EXTIRQ);
	}
}

static void iop310_irq_unmask (unsigned int irq)
{
	if (iop310_mask)
		iop310_mask --;

	/*
	 * Check if all 80312 sources are unmasked now
	 */
	if (iop310_mask == 0)
		enable_irq(IRQ_XS80200_EXTIRQ);
}

struct irqchip ext_chip = {
	.ack	= iop310_irq_mask,
	.mask	= iop310_irq_mask,
	.unmask = iop310_irq_unmask,
};

void
iop310_irq_demux(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	u32 fiq1isr = *((volatile u32*)IOP310_FIQ1ISR);
	u32 fiq2isr = *((volatile u32*)IOP310_FIQ2ISR);
	struct irqdesc *d;
	unsigned int irqno = 0;

	if(fiq1isr)
	{
		if(fiq1isr & 0x1)
			irqno = IRQ_IOP310_DMA0;
		if(fiq1isr & 0x2)
			irqno = IRQ_IOP310_DMA1;
		if(fiq1isr & 0x4)
			irqno = IRQ_IOP310_DMA2;
		if(fiq1isr & 0x10)
			irqno = IRQ_IOP310_PMON;
		if(fiq1isr & 0x20)
			irqno = IRQ_IOP310_AAU;
	}
	else
	{
		if(fiq2isr & 0x2)
			irqno = IRQ_IOP310_I2C;
		if(fiq2isr & 0x4)
			irqno = IRQ_IOP310_MU;
	}

	if (irqno) {
		d = irq_desc + irqno;
		d->handle(irqno, d, regs);
	}
}

void __init iop310_init_irq(void)
{
	unsigned int i;

	for(i = IOP310_IRQ_OFS; i < NR_IOP310_IRQS; i++)
	{
		set_irq_chip(i, &ext_chip);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	xs80200_init_irq();
}
