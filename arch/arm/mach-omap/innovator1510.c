/*
 * linux/arch/arm/mach-omap/innovator1510.c
 *
 * Board specific inits for OMAP-1510 Innovator
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Copyright (C) 2002 MontaVista Software, Inc.
 *
 * Separated FPGA interrupts from innovator1510.c and cleaned up for 2.6
 * Copyright (C) 2004 Nokia Corporation by Tony Lindrgen <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/clocks.h>
#include <asm/arch/gpio.h>
#include <asm/arch/fpga.h>

#include "common.h"

extern int omap_gpio_init(void);

void innovator_init_irq(void)
{
	omap_init_irq();
	omap_gpio_init();
	fpga_init_irq();
}

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= OMAP1510P1_FPGA_ETHR_START,	/* Physical */
		.end	= OMAP1510P1_FPGA_ETHR_START + 16,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_ETHER,
		.end	= INT_ETHER,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct platform_device *devices[] __initdata = {
	&smc91x_device,
};

static void __init innovator_init(void)
{
	if (!machine_is_innovator())
		return;

	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
}

/* Only FPGA needs to be mapped here. All others are done with ioremap */
static struct map_desc innovator_io_desc[] __initdata = {
{ OMAP1510P1_FPGA_BASE, OMAP1510P1_FPGA_START, OMAP1510P1_FPGA_SIZE,
	MT_DEVICE },
};

static void __init innovator_map_io(void)
{
	omap_map_io();
	iotable_init(innovator_io_desc, ARRAY_SIZE(innovator_io_desc));

	/* Dump the Innovator FPGA rev early - useful info for support. */
	printk("Innovator FPGA Rev %d.%d Board Rev %d\n",
	       fpga_read(OMAP1510P1_FPGA_REV_HIGH),
	       fpga_read(OMAP1510P1_FPGA_REV_LOW),
	       fpga_read(OMAP1510P1_FPGA_BOARD_REV));
}

MACHINE_START(INNOVATOR, "TI-Innovator/OMAP1510")
	MAINTAINER("MontaVista Software, Inc.")
	BOOT_MEM(0x10000000, 0xe0000000, 0xe0000000)
	BOOT_PARAMS(0x10000100)
	MAPIO(innovator_map_io)
	INITIRQ(innovator_init_irq)
	INIT_MACHINE(innovator_init)
MACHINE_END
