/*
 * linux/arch/arm/mach-sa1100/adsbitsy.c
 *
 * Author: Woojung Huh
 *
 * Pieces specific to the ADS Bitsy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

static struct resource sa1111_resources[] = {
	[0] = {
		.start		= 0x18000000,
		.end		= 0x18001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_GPIO0,
		.end		= IRQ_GPIO0,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 sa1111_dmamask = 0xffffffffUL;

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.dma_mask = &sa1111_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
};

static int __init adsbitsy_init(void)
{
	int ret;

	if (!machine_is_adsbitsy())
		return -ENODEV;

	/*
	 * Ensure that the memory bus request/grant signals are setup,
	 * and the grant is held in its inactive state
	 */
	sa1110_mb_disable();

	/*
	 * Reset SA1111
	 */
	GPCR |= GPIO_GPIO26;
	udelay(1000);
	GPSR |= GPIO_GPIO26;

	/*
	 * Probe for SA1111.
	 */
	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if (ret < 0)
		return ret;

	/*
	 * Enable PWM control for LCD
	 */
	sa1111_enable_device(SKPCR_PWMCLKEN);
	SKPWM0 = 0x7F;				// VEE
	SKPEN0 = 1;
	SKPWM1 = 0x01;				// Backlight
	SKPEN1 = 1;

	return 0;
}

arch_initcall(adsbitsy_init);

static void __init adsbitsy_init_irq(void)
{
	/* First the standard SA1100 IRQs */
	sa1100_init_irq();
}

static struct map_desc adsbitsy_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf4000000, 0x18000000, 0x00800000, MT_DEVICE }  /* SA1111 */
};

static int adsbitsy_uart_open(struct uart_port *port, struct uart_info *info)
{
	if (port->mapbase == _Ser1UTCR0) {
		Ser1SDCR0 |= SDCR0_UART;
#error Fixme	// Set RTS High (should be done in the set_mctrl fn)
		GPCR = GPIO_GPIO15;
	} else if (port->mapbase == _Ser2UTCR0) {
		Ser2UTCR4 = Ser2HSCR0 = 0;
#error Fixme	// Set RTS High (should be done in the set_mctrl fn)
		GPCR = GPIO_GPIO17;
	} else if (port->mapbase == _Ser2UTCR0) {
#error Fixme	// Set RTS High (should be done in the set_mctrl fn)
		GPCR = GPIO_GPIO19;
	}
	return 0;
}

static struct sa1100_port_fns adsbitsy_port_fns __initdata = {
	.open	= adsbitsy_uart_open,
};

static void __init adsbitsy_map_io(void)
{
	sa1100_map_io();
	iotable_init(adsbitsy_io_desc, ARRAY_SIZE(adsbitsy_io_desc));

	sa1100_register_uart_fns(&adsbitsy_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);
	GPDR |= GPIO_GPIO15 | GPIO_GPIO17 | GPIO_GPIO19;
	GPDR &= ~(GPIO_GPIO14 | GPIO_GPIO16 | GPIO_GPIO18);
}

MACHINE_START(ADSBITSY, "ADS Bitsy")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(adsbitsy_map_io)
	INITIRQ(adsbitsy_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
