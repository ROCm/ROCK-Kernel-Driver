/*
 * linux/arch/arm/mach-omap/board-innovator.c
 *
 * Board specific inits for OMAP-1510 and OMAP-1610 Innovator
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

#ifdef CONFIG_ARCH_OMAP1510

extern int omap_gpio_init(void);

/* Only FPGA needs to be mapped here. All others are done with ioremap */
static struct map_desc innovator1510_io_desc[] __initdata = {
{ OMAP1510P1_FPGA_BASE, OMAP1510P1_FPGA_START, OMAP1510P1_FPGA_SIZE,
	MT_DEVICE },
};

static struct resource innovator1510_smc91x_resources[] = {
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

static struct platform_device innovator1510_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(innovator1510_smc91x_resources),
	.resource	= innovator1510_smc91x_resources,
};

static struct platform_device *innovator1510_devices[] __initdata = {
	&innovator1510_smc91x_device,
};

#endif /* CONFIG_ARCH_OMAP1510 */

#ifdef CONFIG_ARCH_OMAP1610

static struct map_desc innovator1610_io_desc[] __initdata = {
{ OMAP1610_ETHR_BASE, OMAP1610_ETHR_START, OMAP1610_ETHR_SIZE,MT_DEVICE },
{ OMAP1610_NOR_FLASH_BASE, OMAP1610_NOR_FLASH_START, OMAP1610_NOR_FLASH_SIZE,
	MT_DEVICE },
};

static struct resource innovator1610_smc91x_resources[] = {
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

static struct platform_device innovator1610_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(innovator1610_smc91x_resources),
	.resource	= innovator1610_smc91x_resources,
};

static struct platform_device *innovator1610_devices[] __initdata = {
	&innovator1610_smc91x_device,
};

#endif /* CONFIG_ARCH_OMAP1610 */

void innovator_init_irq(void)
{
	omap_init_irq();
#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		omap_gpio_init();
		fpga_init_irq();
	}
#endif
}

static void __init innovator_init(void)
{
#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		platform_add_devices(innovator1510_devices, ARRAY_SIZE(innovator1510_devices));
	}
#endif
#ifdef CONFIG_ARCH_OMAP1610
	if (cpu_is_omap1610()) {
		platform_add_devices(innovator1610_devices, ARRAY_SIZE(innovator1610_devices));
	}
#endif
}

static void __init innovator_map_io(void)
{
	omap_map_io();

#ifdef CONFIG_ARCH_OMAP1510
	if (cpu_is_omap1510()) {
		iotable_init(innovator1510_io_desc, ARRAY_SIZE(innovator1510_io_desc));

		/* Dump the Innovator FPGA rev early - useful info for support. */
		printk("Innovator FPGA Rev %d.%d Board Rev %d\n",
		       fpga_read(OMAP1510P1_FPGA_REV_HIGH),
		       fpga_read(OMAP1510P1_FPGA_REV_LOW),
		       fpga_read(OMAP1510P1_FPGA_BOARD_REV));
	}
#endif
#ifdef CONFIG_ARCH_OMAP1610
	if (cpu_is_omap1610()) {
		iotable_init(innovator1610_io_desc, ARRAY_SIZE(innovator1610_io_desc));
	}
#endif
}

MACHINE_START(OMAP_INNOVATOR, "TI-Innovator")
	MAINTAINER("MontaVista Software, Inc.")
	BOOT_MEM(0x10000000, 0xfff00000, 0xfef00000)
	BOOT_PARAMS(0x10000100)
	MAPIO(innovator_map_io)
	INITIRQ(innovator_init_irq)
	INIT_MACHINE(innovator_init)
MACHINE_END
