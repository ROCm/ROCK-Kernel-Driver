/*
 * linux/arch/arm/mach-sa1100/nanoengine.c
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

static void __init
fixup_nanoengine(struct machine_desc *desc, struct tag *tags,
		 char **cmdline, struct meminfo *mi)
{
	/* Get command line parameters passed from the loader (if any) */
	if (*((char*)0xc0000100))
		*cmdline = ((char *)0xc0000100);
}

static struct map_desc nanoengine_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x10000000, 0x00100000, MT_DEVICE }, /* System Registers */
  { 0xf1000000, 0x18A00000, 0x00100000, MT_DEVICE }  /* Internal PCI Config Space */
};

static void __init nanoengine_map_io(void)
{
	sa1100_map_io();
	iotable_init(nanoengine_io_desc, ARRAY_SIZE(nanoengine_io_desc));

	sa1100_register_uart(0, 1);
	sa1100_register_uart(1, 2);
	sa1100_register_uart(2, 3);
	Ser1SDCR0 |= SDCR0_UART;
	/* disable IRDA -- UART2 is used as a normal serial port */
	Ser2UTCR4=0;
	Ser2HSCR0 = 0;
}

MACHINE_START(NANOENGINE, "BSE nanoEngine")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_nanoengine)
	MAPIO(nanoengine_map_io)
	INITIRQ(sa1100_init_irq)
	INITTIME(sa1100_init_time)
MACHINE_END
