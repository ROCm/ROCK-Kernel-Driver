/*
 * linux/arch/arm/mach-iop310/iq80310-irq.c
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

#include <linux/config.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/hardware.h>
#include <asm/system.h>

#include <asm/mach-types.h>

extern void xs80200_irq_mask(unsigned int);
extern void xs80200_irq_unmask(unsigned int);
extern void xs80200_init_irq(void);

extern void do_IRQ(int, struct pt_regs *);

extern u32 iop310_mask;

extern void iop310_irq_demux(int, void *, struct pt_regs *);

extern int iop310_init_irq(void);

static void
iq80310_irq_mask (unsigned int irq)
{
	volatile char *mask = (volatile char *)IQ80310_INT_MASK;
	*mask |= (1 << (irq - IQ80310_IRQ_OFS));

	/*
	 * There's no mask for PCI INT A-C, so we just mask out all
	 * external interrupts on the CPU.
	 *
	 * We set a bit of the iop310 mask so that the iop310_irq_mask
	 * function does not unmask EXTINT
	 */
    	if (irq > IRQ_IQ80310_INTD)
    	{
		xs80200_irq_mask(IRQ_XS80200_EXTIRQ);
		iop310_mask |=  (0x80000000 >> (irq - IRQ_IQ80310_INTD));
    	}
}

static void
iq80310_irq_unmask (unsigned int irq)
{
	volatile char *mask = (volatile char *)IQ80310_INT_MASK;
	*mask &= ~(1 << (irq - IQ80310_IRQ_OFS));

	/*
	 * See comment above
	 */
    	if (irq > IRQ_IQ80310_INTD)
    	{
		xs80200_irq_unmask(IRQ_XS80200_EXTIRQ);
		iop310_mask &= ~((0x80000000 >> (irq - IRQ_IQ80310_INTD)));
    	}
}

static void iq80310_cpld_irq_demux(int irq, void *dev_id,
                                   struct pt_regs *regs)
{
	u8 irq_stat = *((volatile u8*)IQ80310_INT_STAT);
	u8 irq_mask = *((volatile u8*)IQ80310_INT_MASK);
	unsigned int irqno = 0xffffffff;

	// Needed? If IRQ is masked, it shouldn't get through...
	irq_stat &= ~irq_mask;


	if(irq_stat & 0x01)
		irqno = IRQ_IQ80310_TIMER;
	else if(irq_stat & 0x02)
		irqno = IRQ_IQ80310_I82559;
	else if(irq_stat & 0x04)
		irqno = IRQ_IQ80310_UART1;
	else if(irq_stat & 0x08)
		irqno = IRQ_IQ80310_UART2;
	else if(irq_stat & 0x10)
		irqno = IRQ_IQ80310_INTD;
	else if(system_rev)
	{
		irq_stat = *((volatile u8*)IQ80310_PCI_INT_STAT) & 0xf;

		if(irq_stat & 0x1)
			irqno = IRQ_IQ80310_INTA;
		else if(irq_stat & 0x2)
			irqno = IRQ_IQ80310_INTB;
		else if(irq_stat & 0x4)
			irqno = IRQ_IQ80310_INTC;
	}
	else	/* Running on a REV D.1 or older, assume PCI INTA */
		irqno = IRQ_IQ80310_INTA;

	/*
	 * If we didn't read a CPLD interrupt, we assume it's from
	 * a device on the chipset itself.
	 */
	if(irqno == 0xffffffff)
	{
		iop310_irq_demux(irq, dev_id, regs);
		return;
	}

	/*
	 * If on a REV D.1 or lower board, we just assumed INTA since
	 * PCI is not routed, and it may actually be an on-chip interrupt.
	 *
	 * Note that we're giving on-chip interrupts slightly higher
	 * priority than PCI by handling them first.
	 */
	if(irqno == IRQ_IQ80310_INTA && !system_rev)
		iop310_irq_demux(irq, dev_id, regs);

	do_IRQ(irqno, regs);
}


static struct irqaction iq80310_cpld_irq = {
    name:       "CPLD_IRQ",
    handler:    iq80310_cpld_irq_demux,
    flags:      SA_INTERRUPT
};

extern int setup_arm_irq(int, struct irqaction *);

void __init iq80310_init_irq(void)
{
	volatile char *mask = (volatile char *)IQ80310_INT_MASK;
	unsigned int i;

	iop310_init_irq();

	/*
	 * Setup PIRSR to route PCI interrupts into xs80200
	 */
	*IOP310_PIRSR = 0xff;

	for (i = IQ80310_IRQ_OFS; i < NR_IRQS; i++) {
		irq_desc[i].valid	= 1;
		irq_desc[i].probe_ok	= 1;
		irq_desc[i].mask_ack	= iq80310_irq_mask;
		irq_desc[i].mask	= iq80310_irq_mask;
		irq_desc[i].unmask	= iq80310_irq_unmask;
	}

	*mask = 0xff;  /* mask all sources */
	setup_arm_irq(IRQ_XS80200_EXTIRQ, &iq80310_cpld_irq);

	/* enable only external IRQ in the INTCTL for now */
	asm ("mcr p13, 0, %0, c0, c0, 0" : : "r" (1<<1));
}
