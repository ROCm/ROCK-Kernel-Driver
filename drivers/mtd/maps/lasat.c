/*
 * Flash device on lasat 100 and 200 boards
 *
 * Presumably (C) 2002 Brian Murphy <brian@murphy.dk> or whoever he
 * works for.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * $Id: lasat.c,v 1.5 2003/05/21 12:45:19 dwmw2 Exp $
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/config.h>
#include <asm/lasat/lasat.h>
#include <asm/lasat/lasat_mtd.h>

static struct mtd_info *mymtd;

static struct map_info sp_map = {
	.name = "SP flash",
	.buswidth = 4,
};

static struct mtd_partition partition_info[LASAT_MTD_LAST];
static char *lasat_mtd_partnames[] = {"Bootloader", "Service", "Normal", "Filesystem", "Config"};

static int __init init_sp(void)
{
	int i;
	/* this does not play well with the old flash code which 
	 * protects and uprotects the flash when necessary */
	/* FIXME: Implement set_vpp() */
       	printk(KERN_NOTICE "Unprotecting flash\n");
	*lasat_misc->flash_wp_reg |= 1 << lasat_misc->flash_wp_bit;

	sp_map.virt = lasat_flash_partition_start(LASAT_MTD_BOOTLOADER);
	sp_map.phys = virt_to_phys(sp_map.virt);
	sp_map.size = lasat_board_info.li_flash_size;

	simple_map_init(&sp_map);

       	printk(KERN_NOTICE "sp flash device: %lx at %lx\n", 
			sp_map.size, sp_map.phys);

	for (i=0; i < LASAT_MTD_LAST; i++)
		partition_info[i].name = lasat_mtd_partnames[i];

	mymtd = do_map_probe("cfi_probe", &sp_map);
	if (mymtd) {
		u32 size, offset = 0;

		mymtd->owner = THIS_MODULE;

		for (i=0; i < LASAT_MTD_LAST; i++) {
			size = lasat_flash_partition_size(i);
			partition_info[i].size = size;
			partition_info[i].offset = offset;
			offset += size;
		}

		add_mtd_partitions( mymtd, partition_info, LASAT_MTD_LAST );
		return 0;
	}

	return -ENXIO;
}

static void __exit cleanup_sp(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
	}
	if (sp_map.virt) {
		sp_map.virt = 0;
	}
}

module_init(init_sp);
module_exit(cleanup_sp);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brian Murphy <brian@murphy.dk>");
MODULE_DESCRIPTION("Lasat Safepipe/Masquerade MTD map driver");
