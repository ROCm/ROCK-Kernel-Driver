/*
 * linux/arch/arm/mach-sa1100/cerf.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Apr-2003 : Removed some old PDA crud [FB]
 * Oct-2003 : Added uart2 resource [FB]
 * Jan-2004 : Removed io map for flash [FB]
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/device.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include <asm/arch/cerf.h>
#include "generic.h"

static struct resource cerfuart2_resources[] = {
	[0] = {
		.start	= 0x80030000,
		.end	= 0x8003ffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device cerfuart2_device = {
	.name		= "sa11x0-uart",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(cerfuart2_resources),
	.resource	= cerfuart2_resources,
};

static struct platform_device *cerf_devices[] __initdata = {
	&cerfuart2_device,
};

static void __init cerf_init_irq(void)
{
	sa1100_init_irq();
	set_irq_type(CERF_ETH_IRQ, IRQT_RISING);
}

static struct map_desc cerf_io_desc[] __initdata = {
  /* virtual	 physical    length	 type */
  { 0xf0000000, 0x08000000, 0x00100000, MT_DEVICE }  /* Crystal Ethernet Chip */
};

static void __init cerf_map_io(void)
{
	sa1100_map_io();
	iotable_init(cerf_io_desc, ARRAY_SIZE(cerf_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 2); /* disable this and the uart2 device for sa1100_fir */
	sa1100_register_uart(2, 1);

	/* set some GPDR bits here while it's safe */
	GPDR |= CERF_GPIO_CF_RESET;
}

static int __init cerf_init(void)
{
	int ret;

	if (!machine_is_cerf())
		return -ENODEV;

	ret = platform_add_devices(cerf_devices, ARRAY_SIZE(cerf_devices));
	if (ret < 0)
		return ret;

	return 0;
}

arch_initcall(cerf_init);

MACHINE_START(CERF, "Intrinsyc CerfBoard/CerfCube")
	MAINTAINER("support@intrinsyc.com")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(cerf_map_io)
	INITIRQ(cerf_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
