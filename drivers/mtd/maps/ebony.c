/*
 * $Id: ebony.c,v 1.8 2003/06/23 11:48:18 dwmw2 Exp $
 * 
 * Mapping for Ebony user flash
 *
 * Matt Porter <mporter@mvista.com>
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/config.h>
#include <asm/io.h>
#include <asm/ibm440.h>
#include <platforms/ebony.h>

static struct mtd_info *flash;

static struct map_info ebony_small_map = {
	.name =		"Ebony small flash",
	.size =		EBONY_SMALL_FLASH_SIZE,
	.buswidth =	1,
};

static struct map_info ebony_large_map = {
	.name =		"Ebony large flash",
	.size =		EBONY_LARGE_FLASH_SIZE,
	.buswidth =	1,
};

static struct mtd_partition ebony_small_partitions[] = {
	{
		.name =   "OpenBIOS",
		.offset = 0x0,
		.size =   0x80000,
	}
};

static struct mtd_partition ebony_large_partitions[] = {
	{
		.name =   "fs",
		.offset = 0,
		.size =   0x380000,
	},
	{
		.name =   "firmware",
		.offset = 0x380000,
		.size =   0x80000,
	}
};

int __init init_ebony(void)
{
	u8 fpga0_reg;
	unsigned long fpga0_adr;
	unsigned long long small_flash_base, large_flash_base;

	fpga0_adr = ioremap64(EBONY_FPGA_ADDR, 16);
	if (!fpga0_adr)
		return -ENOMEM;

	fpga0_reg = readb(fpga0_adr);
	iounmap64(fpga0_adr);

	if (EBONY_BOOT_SMALL_FLASH(fpga0_reg) &&
			!EBONY_FLASH_SEL(fpga0_reg))
		small_flash_base = EBONY_SMALL_FLASH_HIGH2;
	else if (EBONY_BOOT_SMALL_FLASH(fpga0_reg) &&
			EBONY_FLASH_SEL(fpga0_reg))
		small_flash_base = EBONY_SMALL_FLASH_HIGH1;
	else if (!EBONY_BOOT_SMALL_FLASH(fpga0_reg) &&
			!EBONY_FLASH_SEL(fpga0_reg))
		small_flash_base = EBONY_SMALL_FLASH_LOW2;
	else
		small_flash_base = EBONY_SMALL_FLASH_LOW1;
			
	if (EBONY_BOOT_SMALL_FLASH(fpga0_reg) &&
			!EBONY_ONBRD_FLASH_EN(fpga0_reg))
		large_flash_base = EBONY_LARGE_FLASH_LOW;
	else
		large_flash_base = EBONY_LARGE_FLASH_HIGH;

	ebony_small_map.phys = small_flash_base;
	ebony_small_map.virt =
		(unsigned long)ioremap64(small_flash_base,
					 ebony_small_map.size);

	if (!ebony_small_map.virt) {
		printk("Failed to ioremap flash\n");
		return -EIO;
	}

	simple_map_init(&ebony_small_map);

	flash = do_map_probe("map_rom", &ebony_small_map);
	if (flash) {
		flash->owner = THIS_MODULE;
		add_mtd_partitions(flash, ebony_small_partitions,
					ARRAY_SIZE(ebony_small_partitions));
	} else {
		printk("map probe failed for flash\n");
		return -ENXIO;
	}

	ebony_large_map.phys = large_flash_base;
	ebony_large_map.virt =
		(unsigned long)ioremap64(large_flash_base,
					 ebony_large_map.size);

	if (!ebony_large_map.virt) {
		printk("Failed to ioremap flash\n");
		return -EIO;
	}

	simple_map_init(&ebony_large_map);

	flash = do_map_probe("cfi_probe", &ebony_large_map);
	if (flash) {
		flash->owner = THIS_MODULE;
		add_mtd_partitions(flash, ebony_large_partitions,
					ARRAY_SIZE(ebony_large_partitions));
	} else {
		printk("map probe failed for flash\n");
		return -ENXIO;
	}

	return 0;
}

static void __exit cleanup_ebony(void)
{
	if (flash) {
		del_mtd_partitions(flash);
		map_destroy(flash);
	}

	if (ebony_small_map.virt) {
		iounmap((void *)ebony_small_map.virt);
		ebony_small_map.virt = 0;
	}

	if (ebony_large_map.virt) {
		iounmap((void *)ebony_large_map.virt);
		ebony_large_map.virt = 0;
	}
}

module_init(init_ebony);
module_exit(cleanup_ebony);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matt Porter <mporter@mvista.com>");
MODULE_DESCRIPTION("MTD map and partitions for IBM 440GP Ebony boards");
