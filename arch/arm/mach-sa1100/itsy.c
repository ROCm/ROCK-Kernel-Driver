/*
 * linux/arch/arm/mach-sa1100/itsy.c
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

/* BRADFIXME The egpio addresses aren't verifiably correct. (i.e. they're most
   likely wrong. */
static struct map_desc itsy_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x49000000, 0x01000000, MT_DEVICE } /* EGPIO 0 */
};

static void __init itsy_map_io(void)
{
	sa1100_map_io();
	iotable_init(itsy_io_desc, ARRAY_SIZE(itsy_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);
}

MACHINE_START(ITSY, "Compaq Itsy")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(itsy_map_io)
	INITIRQ(sa1100_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
