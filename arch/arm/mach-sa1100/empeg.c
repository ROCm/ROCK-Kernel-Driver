/*
 * linux/arch/arm/mach-sa1100/empeg.c
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

static struct map_desc empeg_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { EMPEG_FLASHBASE, 0x00000000, 0x00200000, DOMAIN_IO, 0, 1, 0, 0 }, /* Flash */
  LAST_DESC
};

static void __init empeg_map_io(void)
{
	sa1100_map_io();
	iotable_init(empeg_io_desc);

	sa1100_register_uart(0, 1);
	sa1100_register_uart(1, 3);
	sa1100_register_uart(2, 2);
	Ser1SDCR0 |= SDCR0_UART;
}

MACHINE_START(EMPEG, "empeg MP3 Car Audio Player")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	MAPIO(empeg_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
