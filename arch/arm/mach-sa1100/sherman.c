/*
 * linux/arch/arm/mach-sa1100/sherman.c
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

static void __init sherman_map_io(void)
{
	sa1100_map_io();

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

MACHINE_START(SHERMAN, "Blazie Engineering Sherman")
        BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
        MAPIO(sherman_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
