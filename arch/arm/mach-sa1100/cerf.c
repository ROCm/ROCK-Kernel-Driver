/*
 * linux/arch/arm/mach-sa1100/cerf.c
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
fixup_cerf(struct machine_desc *desc, struct param_struct *params,
	   char **cmdline, struct meminfo *mi)
{
#ifdef CONFIG_SA1100_CERF_32MB
	// 32MB RAM
	SET_BANK( 0, 0xc0000000, 32*1024*1024 );
	mi->nr_banks = 1;
#else
	// 16Meg Ram.
	SET_BANK( 0, 0xc0000000, 8*1024*1024 );
	SET_BANK( 1, 0xc8000000, 8*1024*1024 );			// comment this out for 8MB Cerfs
	mi->nr_banks = 2;
#endif

	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk(1,  0, 0, 8192);
	// Save 2Meg for RAMDisk
	setup_initrd(0xc0500000, 3*1024*1024);
}

static struct map_desc cerf_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x00000000, 0x01000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 0 */
  { 0xf0000000, 0x08000000, 0x00100000, DOMAIN_IO, 1, 1, 0, 0 }, /* Crystal Chip */
  LAST_DESC
};

static void __init cerf_map_io(void)
{
	sa1100_map_io();
	iotable_init(cerf_io_desc);

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

MACHINE_START(CERF, "Intrinsyc CerfBoard")
	MAINTAINER("Pieter Truter")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_cerf)
	MAPIO(cerf_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
