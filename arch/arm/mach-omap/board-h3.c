/*
 * linux/arch/arm/mach-omap/board-h3.c
 *
 * This file contains OMAP1710 H3 specific code.
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
 * Copyright (C) 2002 MontaVista Software, Inc.
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *         Greg Lonnon (glonnon@ridgerun.com) or info@ridgerun.com
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
#include <asm/arch/gpio.h>
#include <asm/mach-types.h>
#include "common.h"

extern void __init omap_init_time(void);

void h3_init_irq(void)
{
	omap_init_irq();
}

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= OMAP1710_ETHR_START,		/* Physical */
		.end	= OMAP1710_ETHR_START + OMAP1710_ETHR_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,
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

static void __init h3_init(void)
{
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
}

static struct map_desc h3_io_desc[] __initdata = {
{ OMAP1710_ETHR_BASE,  OMAP1710_ETHR_START,  OMAP1710_ETHR_SIZE,  MT_DEVICE },
{ OMAP_NOR_FLASH_BASE, OMAP_NOR_FLASH_START, OMAP_NOR_FLASH_SIZE, MT_DEVICE },
};

static void __init h3_map_io(void)
{
	omap_map_io();
	iotable_init(h3_io_desc, ARRAY_SIZE(h3_io_desc));
}

MACHINE_START(OMAP_H3, "TI OMAP1710 H3 board")
	MAINTAINER("Texas Instruments, Inc.")
	BOOT_MEM(0x10000000, 0xfff00000, 0xfef00000)
	BOOT_PARAMS(0x10000100)
	MAPIO(h3_map_io)
	INITIRQ(h3_init_irq)
	INIT_MACHINE(h3_init)
	INITTIME(omap_init_time)
MACHINE_END
