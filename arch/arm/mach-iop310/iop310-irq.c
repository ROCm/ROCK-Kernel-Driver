/*
 * linux/arch/arm/mach-iop310/iop310-irq.c
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

#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/mach/irq.h>
#include <asm/irq.h>
#include <asm/hardware.h>

#include <asm/mach-types.h>

extern void xs80200_irq_mask(unsigned int);
extern void xs80200_irq_unmask(unsigned int);
extern void xs80200_init_irq(void);

extern void do_IRQ(int, struct pt_regs *);

u32 iop310_mask = 0;

static void
iop310_irq_mask (unsigned int irq)
{
	iop310_mask |= (1 << (irq - IOP310_IRQ_OFS));

	/*
	 * No mask bits on the 80312, so we have to
	 * mask everything from the outside!
	 */
	xs80200_irq_mask(IRQ_XS80200_EXTIRQ);
}

static void
iop310_irq_unmask (unsigned int irq)
{
	iop310_mask &= ~(1 << (irq - IOP310_IRQ_OFS));

	/*
	 * Check if all 80312 sources are unmasked now
	 */
	if(!iop310_mask)
	{
		xs80200_irq_unmask(IRQ_XS80200_EXTIRQ);

	}
}

void iop310_irq_demux(int irq, void *dev_id,
                                   struct pt_regs *regs)
{
	u32 fiq1isr = *((volatile u32*)IOP310_FIQ1ISR);
	u32 fiq2isr = *((volatile u32*)IOP310_FIQ2ISR);
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

	do_IRQ(irqno, regs);
}

void __init iop310_init_irq(void)
{
	int i;

	for(i = IOP310_IRQ_OFS; i < NR_IOP310_IRQS; i++)
	{
		irq_desc[i].valid	= 1;
		irq_desc[i].probe_ok	= 1;
		irq_desc[i].mask_ack	= iop310_irq_mask;
		irq_desc[i].mask	= iop310_irq_mask;
		irq_desc[i].unmask	= iop310_irq_unmask;
	}

	xs80200_init_irq();
}

