/*
 *  linux/include/asm-arm/arch-ebsa110/irq.h
 *
 *  Copyright (C) 1996-1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   22-08-1998	RMK	Restructured IRQ routines
 */

#define IRQ_MCLR	((volatile unsigned char *)0xf3000000)
#define IRQ_MSET	((volatile unsigned char *)0xf2c00000)
#define IRQ_MASK	((volatile unsigned char *)0xf2c00000)

#define fixup_irq(x) (x)

static void ebsa110_mask_and_ack_irq(unsigned int irq)
{
	*IRQ_MCLR = 1 << irq;
}

static void ebsa110_mask_irq(unsigned int irq)
{
	*IRQ_MCLR = 1 << irq;
}

static void ebsa110_unmask_irq(unsigned int irq)
{
	*IRQ_MSET = 1 << irq;
}
 
static __inline__ void irq_init_irq(void)
{
	unsigned long flags;
	int irq;

	save_flags_cli (flags);
	*IRQ_MCLR = 0xff;
	*IRQ_MSET = 0x55;
	*IRQ_MSET = 0x00;
	if (*IRQ_MASK != 0x55)
		while (1);
	*IRQ_MCLR = 0xff;		/* clear all interrupt enables */
	restore_flags (flags);

	for (irq = 0; irq < NR_IRQS; irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= ebsa110_mask_and_ack_irq;
		irq_desc[irq].mask	= ebsa110_mask_irq;
		irq_desc[irq].unmask	= ebsa110_unmask_irq;
	}
}
