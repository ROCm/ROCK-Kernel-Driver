/*
 * $Id: arctic-mtd.c,v 1.8 2003/05/21 12:45:17 dwmw2 Exp $
 * 
 * drivers/mtd/maps/arctic-mtd.c MTD mappings and partition tables for 
 *                              IBM 405LP Arctic boards.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Copyright (C) 2002, International Business Machines Corporation
 * All Rights Reserved.
 *
 * Bishop Brock
 * IBM Research, Austin Center for Low-Power Computing
 * bcbrock@us.ibm.com
 * March 2002
 *
 * modified for Arctic by,
 * David Gibson
 * IBM OzLabs, Canberra, Australia
 * <arctic@gibson.dropbear.id.au>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/ibm4xx.h>

/*
 * fe000000 -- ff9fffff  Arctic FFS (26MB)
 * ffa00000 -- fff5ffff  kernel (5.504MB)
 * fff60000 -- ffffffff  firmware (640KB)
 */

#define ARCTIC_FFS_SIZE		0x01a00000 /* 26 M */
#define ARCTIC_FIRMWARE_SIZE	0x000a0000 /* 640K */

#define NAME     "Arctic Linux Flash"
#define PADDR    SUBZERO_BOOTFLASH_PADDR
#define SIZE     SUBZERO_BOOTFLASH_SIZE
#define BUSWIDTH 2

/* Flash memories on these boards are memory resources, accessed big-endian. */

{
  /* do nothing for now */
}

static struct map_info arctic_mtd_map = {
	.name		= NAME,
	.size		= SIZE,
	.buswidth	= BUSWIDTH,
	.phys		= PADDR,
};

static struct mtd_info *arctic_mtd;

static struct mtd_partition arctic_partitions[3] = {
	{ .name		= "Arctic FFS",
	  .size		= ARCTIC_FFS_SIZE,
	  .offset	= 0,},
	{ .name		= "Kernel",
	  .size		= SUBZERO_BOOTFLASH_SIZE - ARCTIC_FFS_SIZE -
	  		  ARCTIC_FIRMWARE_SIZE,
	  .offset	= ARCTIC_FFS_SIZE,},
	{ .name		= "Firmware",
	  .size		= ARCTIC_FIRMWARE_SIZE,
	  .offset	= SUBZERO_BOOTFLASH_SIZE - ARCTIC_FIRMWARE_SIZE,},
};

static int __init
init_arctic_mtd(void)
{
	printk("%s: 0x%08x at 0x%08x\n", NAME, SIZE, PADDR);

	arctic_mtd_map.virt = (unsigned long) ioremap(PADDR, SIZE);

	if (!arctic_mtd_map.virt) {
		printk("%s: failed to ioremap 0x%x\n", NAME, PADDR);
		return -EIO;
	}
	simple_map_init(&arctic_mtd_map);

	printk("%s: probing %d-bit flash bus\n", NAME, BUSWIDTH * 8);
	arctic_mtd = do_map_probe("cfi_probe", &arctic_mtd_map);

	if (!arctic_mtd)
		return -ENXIO;

	arctic_mtd->owner = THIS_MODULE;

	return add_mtd_partitions(arctic_mtd, arctic_partitions, 3);
}

static void __exit
cleanup_arctic_mtd(void)
{
	if (arctic_mtd) {
		del_mtd_partitions(arctic_mtd);
		map_destroy(arctic_mtd);
		iounmap((void *) arctic_mtd_map.virt);
	}
}

module_init(init_arctic_mtd);
module_exit(cleanup_arctic_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Gibson <arctic@gibson.dropbear.id.au>");
MODULE_DESCRIPTION("MTD map and partitions for IBM 405LP Arctic boards");
