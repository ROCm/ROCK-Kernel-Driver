/*
 * linux/arch/arm/mach-omap/board-generic.c
 *
 * Modified from board-innovator1510.c
 *
 * Code for generic OMAP board. Should work on many OMAP systems where
 * the device drivers take care of all the necessary hardware initialization.
 * Do not put any board specific code to this file; create a new machine
 * type if you need custom low-level initializations.
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
#include <asm/arch/mux.h>

#include "common.h"

static void __init omap_generic_init_irq(void)
{
	omap_init_irq();
}

/*
 * Muxes the serial ports on
 */
static void __init omap_early_serial_init(void)
{
	omap_cfg_reg(UART1_TX);
	omap_cfg_reg(UART1_RTS);

	omap_cfg_reg(UART2_TX);
	omap_cfg_reg(UART2_RTS);

	omap_cfg_reg(UART3_TX);
	omap_cfg_reg(UART3_RX);
}

static void __init omap_generic_init(void)
{
	/*
	 * Make sure the serial ports are muxed on at this point.
	 * You have to mux them off in device drivers later on
	 * if not needed.
	 */
	if (cpu_is_omap1510()) {
		omap_early_serial_init();
	}
}

static void __init omap_generic_map_io(void)
{
	omap_map_io();
}

static void __init omap_generic_init_time(void)
{
	omap_init_time();
}

MACHINE_START(OMAP_GENERIC, "Generic OMAP-1510/1610")
	MAINTAINER("Tony Lindgren <tony@atomide.com>")
	BOOT_MEM(0x10000000, 0xfff00000, 0xfef00000)
	BOOT_PARAMS(0x10000100)
	MAPIO(omap_generic_map_io)
	INITIRQ(omap_generic_init_irq)
	INIT_MACHINE(omap_generic_init)
	INITTIME(omap_generic_init_time)
MACHINE_END

