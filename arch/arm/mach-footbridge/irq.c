/*
 *  linux/arch/arm/mach-footbridge/irq.c
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   22-Aug-1998 RMK	Restructured IRQ routines
 *   03-Sep-1998 PJB	Merged CATS support
 *   20-Jan-1998 RMK	Started merge of EBSA286, CATS and NetWinder
 *   26-Jan-1999 PJB	Don't use IACK on CATS
 *   16-Mar-1999 RMK	Added autodetect of ISA PICs
 */
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/init.h>

#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/hardware/dec21285.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach-types.h>

extern void __init isa_init_irq(unsigned int irq);

/*
 * Footbridge IRQ translation table
 *  Converts from our IRQ numbers into FootBridge masks
 */
static const int fb_irq_mask[] = {
	IRQ_MASK_UART_RX,	/*  0 */
	IRQ_MASK_UART_TX,	/*  1 */
	IRQ_MASK_TIMER1,	/*  2 */
	IRQ_MASK_TIMER2,	/*  3 */
	IRQ_MASK_TIMER3,	/*  4 */
	IRQ_MASK_IN0,		/*  5 */
	IRQ_MASK_IN1,		/*  6 */
	IRQ_MASK_IN2,		/*  7 */
	IRQ_MASK_IN3,		/*  8 */
	IRQ_MASK_DOORBELLHOST,	/*  9 */
	IRQ_MASK_DMA1,		/* 10 */
	IRQ_MASK_DMA2,		/* 11 */
	IRQ_MASK_PCI,		/* 12 */
	IRQ_MASK_SDRAMPARITY,	/* 13 */
	IRQ_MASK_I2OINPOST,	/* 14 */
	IRQ_MASK_PCI_ABORT,	/* 15 */
	IRQ_MASK_PCI_SERR,	/* 16 */
	IRQ_MASK_DISCARD_TIMER,	/* 17 */
	IRQ_MASK_PCI_DPERR,	/* 18 */
	IRQ_MASK_PCI_PERR,	/* 19 */
};

static void fb_mask_irq(unsigned int irq)
{
	*CSR_IRQ_DISABLE = fb_irq_mask[_DC21285_INR(irq)];
}

static void fb_unmask_irq(unsigned int irq)
{
	*CSR_IRQ_ENABLE = fb_irq_mask[_DC21285_INR(irq)];
}

static struct irqchip fb_chip = {
	.ack	= fb_mask_irq,
	.mask	= fb_mask_irq,
	.unmask = fb_unmask_irq,
};

static void __init __fb_init_irq(void)
{
	unsigned int irq;

	/*
	 * setup DC21285 IRQs
	 */
	*CSR_IRQ_DISABLE = -1;
	*CSR_FIQ_DISABLE = -1;

	for (irq = _DC21285_IRQ(0); irq < _DC21285_IRQ(20); irq++) {
		set_irq_chip(irq, &fb_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
}

void __init footbridge_init_irq(void)
{
	__fb_init_irq();

	if (!footbridge_cfn_mode())
		return;

	if (machine_is_ebsa285())
		/* The following is dependent on which slot
		 * you plug the Southbridge card into.  We
		 * currently assume that you plug it into
		 * the right-hand most slot.
		 */
		isa_init_irq(IRQ_PCI);

	if (machine_is_cats())
		isa_init_irq(IRQ_IN2);

	if (machine_is_netwinder())
		isa_init_irq(IRQ_IN3);
}
