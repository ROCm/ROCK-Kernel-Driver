/*
 *  linux/arch/arm/mach-pxa/lubbock.c
 *
 *  Support for the Intel DBPXA250 Development Platform.
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/irq.h>
#include <asm/hardware/sa1111.h>

#include "generic.h"
#include "sa1111.h"

static void lubbock_ack_irq(unsigned int irq)
{
	int lubbock_irq = (irq - LUBBOCK_IRQ(0));
	LUB_IRQ_SET_CLR &= ~(1 << lubbock_irq);
}

static void lubbock_mask_irq(unsigned int irq)
{
	int lubbock_irq = (irq - LUBBOCK_IRQ(0));
	LUB_IRQ_MASK_EN &= ~(1 << lubbock_irq);
}

static void lubbock_unmask_irq(unsigned int irq)
{
	int lubbock_irq = (irq - LUBBOCK_IRQ(0));
	LUB_IRQ_MASK_EN |= (1 << lubbock_irq);
}

static struct irqchip lubbock_irq_chip = {
	.ack		= lubbock_ack_irq,
	.mask		= lubbock_mask_irq,
	.unmask		= lubbock_unmask_irq,
};

void lubbock_irq_handler(unsigned int irq, struct irqdesc *desc,
			 struct pt_regs *regs)
{
	unsigned int enabled, pending;

	/* get active pending irq mask */
	enabled = LUB_IRQ_MASK_EN & 0x003f;
	pending = LUB_IRQ_SET_CLR & enabled;

	do {
//printk("%s a: set_clr %#x, mask_en %#x LR/DR %d/%d\n", __FUNCTION__, LUB_IRQ_SET_CLR, LUB_IRQ_MASK_EN, GPLR(0)&1, GEDR(0)&1 );
		/* clear our parent irq */
		GEDR(0) = GPIO_bit(0);

		/* process them */
		irq = LUBBOCK_IRQ(0);
		desc = irq_desc + irq;
		do {
			if (pending & 1)
				desc->handle(irq, desc, regs);
			irq++;
			desc++;
			pending >>= 1;
		} while (pending);
//printk("%s b: set_clr %#x, mask_en %#x LR/DR %d/%d\n", __FUNCTION__, LUB_IRQ_SET_CLR, LUB_IRQ_MASK_EN, GPLR(0)&1, GEDR(0)&1 );
		enabled = LUB_IRQ_MASK_EN & 0x003f;
		pending = LUB_IRQ_SET_CLR & enabled;
	} while (pending);
}

static void __init lubbock_init_irq(void)
{
	int irq;

	pxa_init_irq();

	/* setup extra lubbock irqs */
	for (irq = LUBBOCK_IRQ(0); irq <= LUBBOCK_IRQ(5); irq++) {
		set_irq_chip(irq, &lubbock_irq_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	set_irq_chained_handler(IRQ_GPIO(0), lubbock_irq_handler);
	set_irq_type(IRQ_GPIO(0), IRQT_FALLING);
}

static int __init lubbock_init(void)
{
	int ret;

	ret = sa1111_probe(LUBBOCK_SA1111_BASE);
	if (ret)
		return ret;
	sa1111_wake();
	sa1111_init_irq(LUBBOCK_SA1111_IRQ);
	return 0;
}

__initcall(lubbock_init);

static struct map_desc lubbock_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x08000000, 0x00100000, MT_DEVICE }, /* CPLD */
  { 0xf1000000, 0x0c000000, 0x00100000, MT_DEVICE }, /* LAN91C96 IO */
  { 0xf1100000, 0x0e000000, 0x00100000, MT_DEVICE }, /* LAN91C96 Attr */
  { 0xf4000000, 0x10000000, 0x00400000, MT_DEVICE }  /* SA1111 */
};

static void __init lubbock_map_io(void)
{
	pxa_map_io();
	iotable_init(lubbock_io_desc, ARRAY_SIZE(lubbock_io_desc));

	/* This enables the BTUART */
	CKEN |= CKEN7_BTUART;
	pxa_gpio_mode(GPIO42_BTRXD_MD);
	pxa_gpio_mode(GPIO43_BTTXD_MD);
	pxa_gpio_mode(GPIO44_BTCTS_MD);
	pxa_gpio_mode(GPIO45_BTRTS_MD);

	/* This is for the SMC chip select */
	pxa_gpio_mode(GPIO79_nCS_3_MD);

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x00008000;
	PGSR1 = 0x003F0202;
	PGSR2 = 0x0001C000;
	PCFR |= PCFR_OPDE;
}

MACHINE_START(LUBBOCK, "Intel DBPXA250 Development Platform")
	MAINTAINER("MontaVista Software Inc.")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	MAPIO(lubbock_map_io)
	INITIRQ(lubbock_init_irq)
MACHINE_END
