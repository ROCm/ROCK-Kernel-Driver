/*
 *  linux/arch/arm/mach-ftvpci/core.c
 *
 *  Architecture specific fixups.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>

extern unsigned long soft_irq_mask;

static const unsigned char irq_cmd[] =
{
	INTCONT_IRQ_DUART,
	INTCONT_IRQ_PLX,
	INTCONT_IRQ_D,
	INTCONT_IRQ_C,
	INTCONT_IRQ_B,
	INTCONT_IRQ_A,
	INTCONT_IRQ_SYSERR
};

static void ftvpci_mask_irq(unsigned int irq)
{
	__raw_writel(irq_cmd[irq], INTCONT_BASE);
	soft_irq_mask &= ~(1<<irq);
}

static void ftvpci_unmask_irq(unsigned int irq)
{
	soft_irq_mask |= (1<<irq);
	__raw_writel(irq_cmd[irq] | 1, INTCONT_BASE);
}
 
static void __init ftvpci_init_irq(void)
{
	unsigned int i;

	/* Mask all FIQs */
	__raw_writel(INTCONT_FIQ_PLX, INTCONT_BASE);
	__raw_writel(INTCONT_FIQ_D, INTCONT_BASE);
	__raw_writel(INTCONT_FIQ_C, INTCONT_BASE);
	__raw_writel(INTCONT_FIQ_B, INTCONT_BASE);
	__raw_writel(INTCONT_FIQ_A, INTCONT_BASE);
	__raw_writel(INTCONT_FIQ_SYSERR, INTCONT_BASE);

	/* Disable all interrupts initially. */
	for (i = 0; i < NR_IRQS; i++) {
		if (i >= FIRST_IRQ && i <= LAST_IRQ) {
			irq_desc[i].valid	= 1;
			irq_desc[i].probe_ok	= 1;
			irq_desc[i].mask_ack	= ftvpci_mask_irq;
			irq_desc[i].mask	= ftvpci_mask_irq;
			irq_desc[i].unmask	= ftvpci_unmask_irq;
			ftvpci_mask_irq(i);
		} else {
			irq_desc[i].valid	= 0;
			irq_desc[i].probe_ok	= 0;
		}	
	}		
}

static struct map_desc ftvpci_io_desc[] __initdata = {
 	{ INTCONT_BASE,	INTCONT_START,	0x00001000, MT_DEVICE },
 	{ PLX_BASE,	PLX_START,	0x00001000, MT_DEVICE },
 	{ PCIO_BASE,	PLX_IO_START,	0x00100000, MT_DEVICE },
 	{ DUART_BASE,	DUART_START,	0x00001000, MT_DEVICE },
	{ STATUS_BASE,	STATUS_START,	0x00001000, MT_DEVICE }
};

static void __init ftvpci_map_io(void)
{
	iotable_init(ftvpci_io_desc, ARRAY_SIZE(ftvpci_io_desc));
}

static irqreturn_t
ftvpci_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	static int count = 25;
	unsigned char stat = __raw_readb(DUART_BASE + 0x14);
	if (!(stat & 0x10))
		return;		/* Not for us */

	/* Reset counter */
	__raw_writeb(0x90, DUART_BASE + 8);

	if (--count == 0) {
		static int state = 1;
		state ^= 1;
		__raw_writeb(0x1a + state, INTCONT_BASE);
		__raw_writeb(0x18 + state, INTCONT_BASE);
		count = 50;
	}

	/* Wait for slow rise time */
	__raw_readb(DUART_BASE + 0x14);
	__raw_readb(DUART_BASE + 0x14);
	__raw_readb(DUART_BASE + 0x14);
	__raw_readb(DUART_BASE + 0x14);
	__raw_readb(DUART_BASE + 0x14);
	__raw_readb(DUART_BASE + 0x14);

	do_timer(regs);

	return IRQ_HANDLED;
}

void __init ftvpci_time_init(void)
{
	int tick = 3686400 / 16 / 2 / 100;

	__raw_writeb(tick & 0xff, DUART_BASE + 0x1c);
	__raw_writeb(tick >> 8, DUART_BASE + 0x18);
	__raw_writeb(0x80, DUART_BASE + 8);
	__raw_writeb(0x10, DUART_BASE + 0x14);

	timer_irq.handler = timer_interrupt;
	timer_irq.flags = SA_SHIRQ;

	set_timer_irq_handler(IRQ_TIMER, timer_interrupt);
}

MACHINE_START(NEXUSPCI, "FTV/PCI")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x40000000, 0x10000000, 0xe0000000)
	MAPIO(ftvpci_map_io)
	INITIRQ(ftvpci_init_irq)
MACHINE_END
