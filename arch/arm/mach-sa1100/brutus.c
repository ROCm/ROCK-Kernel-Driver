/*
 * linux/arch/arm/mach-sa1100/brutus.c
 *
 * Author: Nicolas Pitre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>

#include <asm/hardware.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

static void __init brutus_map_io(void)
{
	sa1100_map_io();

	sa1100_register_uart(0, 1);
	sa1100_register_uart(1, 3);
	GAFR |= (GPIO_UART_TXD | GPIO_UART_RXD);
	GPDR |= GPIO_UART_TXD;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;
}

MACHINE_START(BRUTUS, "Intel Brutus (SA1100 eval board)")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(brutus_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
