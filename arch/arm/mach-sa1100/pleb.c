/*
 * linux/arch/arm/mach-sa1100/pleb.c
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

static void __init pleb_map_io(void)
{
	sa1100_map_io();

	sa1100_register_uart(0, 3);
        sa1100_register_uart(1, 1);
        GAFR |= (GPIO_UART_TXD | GPIO_UART_RXD);
        GPDR |= GPIO_UART_TXD;
        GPDR &= ~GPIO_UART_RXD;
        PPAR |= PPAR_UPR;
}

MACHINE_START(PLEB, "PLEB")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(pleb_map_io)
	INITIRQ(sa1100_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
