/*
 * linux/arch/arm/mach-sa1100/flexanet.c
 *
 * Author: Jordi Colomer <jco@ict.es>
 *
 * This file contains all FlexaNet-specific tweaks.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"


unsigned long BCR_value         = BCR_POWERUP;
unsigned long flexanet_GUI_type = 0x0000000F;

EXPORT_SYMBOL(BCR_value);
EXPORT_SYMBOL(flexanet_GUI_type);

static unsigned long probe_gui_board (void);

static void __init
fixup_flexanet(struct machine_desc *desc, struct param_struct *params,
	      char **cmdline, struct meminfo *mi)
{
	/* fixed RAM size, by now (64MB) */
	SET_BANK( 0, 0xc0000000, 64*1024*1024 );
	mi->nr_banks = 1;

	/* setup ramdisk */
	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( 0xc0800000, 3*1024*1024 );
}


static struct map_desc flexanet_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x00000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x10000000, 0x00001000, DOMAIN_IO, 1, 1, 0, 0 }, /* Board Control Register */
  LAST_DESC
};

static void __init flexanet_map_io(void)
{
	sa1100_map_io();
	iotable_init(flexanet_io_desc);

	sa1100_register_uart(0, 1);
	Ser1SDCR0 |= SDCR0_UART;
}


MACHINE_START(FLEXANET, "FlexaNet")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	FIXUP(fixup_flexanet)
	MAPIO(flexanet_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
