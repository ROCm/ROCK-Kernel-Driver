/*
 * linux/arch/arm/mach-omap/innovator1610.c
 *
 * This file contains Innovator-specific code.
 *
 * Copyright (C) 2002 MontaVista Software, Inc.
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/arch/irqs.h>

#include "common.h"

void
innovator_init_irq(void)
{
	omap_init_irq();
}

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= OMAP1610_ETHR_START,		/* Physical */
		.end	= OMAP1610_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,				/* Really GPIO 0 */
		.end	= 0,
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

static struct map_desc innovator_io_desc[] __initdata = {
{ OMAP1610_ETHR_BASE, OMAP1610_ETHR_START, OMAP1610_ETHR_SIZE,MT_DEVICE },
{ OMAP1610_NOR_FLASH_BASE, OMAP1610_NOR_FLASH_START, OMAP1610_NOR_FLASH_SIZE,
	MT_DEVICE },
};

static void __init innovator_map_io(void)
{
	omap_map_io();
	iotable_init(innovator_io_desc, ARRAY_SIZE(innovator_io_desc));
}

MACHINE_START(INNOVATOR, "TI-Innovator/OMAP1610")
	MAINTAINER("MontaVista Software, Inc.")
	BOOT_MEM(0x10000000, 0xe0000000, 0xe0000000)
	BOOT_PARAMS(0x10000100)
	MAPIO(innovator_map_io)
	INITIRQ(innovator_init_irq)
	INIT_MACHINE(innovator_init)
MACHINE_END

