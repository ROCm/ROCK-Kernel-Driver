/*
 *  drivers/mtd/nandids.c
 *
 *  Copyright (C) 2002 Thomas Gleixner (tglx@linutronix.de)
 *
 *
 * $Id: nand_ids.c,v 1.4 2003/05/21 15:15:08 dwmw2 Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/mtd/nand.h>

/*
*	Chip ID list
*/
struct nand_flash_dev nand_flash_ids[] = {
	{"NAND 1MiB 5V", 0x6e, 20, 0x1000, 1},
	{"NAND 2MiB 5V", 0x64, 21, 0x1000, 1},
	{"NAND 4MiB 5V", 0x6b, 22, 0x2000, 0},
	{"NAND 1MiB 3,3V", 0xe8, 20, 0x1000, 1},
	{"NAND 1MiB 3,3V", 0xec, 20, 0x1000, 1},
	{"NAND 2MiB 3,3V", 0xea, 21, 0x1000, 1},
	{"NAND 4MiB 3,3V", 0xd5, 22, 0x2000, 0},
	{"NAND 4MiB 3,3V", 0xe3, 22, 0x2000, 0},
	{"NAND 4MiB 3,3V", 0xe5, 22, 0x2000, 0},
	{"NAND 8MiB 3,3V", 0xd6, 23, 0x2000, 0},
	{"NAND 8MiB 3,3V", 0xe6, 23, 0x2000, 0},
	{"NAND 16MiB 3,3V", 0x73, 24, 0x4000, 0},
	{"NAND 32MiB 3,3V", 0x75, 25, 0x4000, 0},
	{"NAND 64MiB 3,3V", 0x76, 26, 0x4000, 0},
	{"NAND 128MiB 3,3V", 0x79, 27, 0x4000, 0},
	{NULL,}
};

/*
*	Manufacturer ID list
*/
struct nand_manufacturers nand_manuf_ids[] = {
	{NAND_MFR_TOSHIBA, "Toshiba"},
	{NAND_MFR_SAMSUNG, "Samsung"},
	{NAND_MFR_FUJITSU, "Fujitsu"},
	{NAND_MFR_NATIONAL, "National"},
	{0x0, "Unknown"}
};


EXPORT_SYMBOL (nand_manuf_ids);
EXPORT_SYMBOL (nand_flash_ids);

MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Thomas Gleixner <tglx@linutronix.de>");
MODULE_DESCRIPTION ("Nand device & manufacturer ID's");
