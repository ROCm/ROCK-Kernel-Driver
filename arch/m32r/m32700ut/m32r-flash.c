/*
 * Flash memory access on M32R based devices
 *
 * Copyright (C) 2003	Takeo Takahashi
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * $Id$
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/kernel.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/m32r.h>
#include <asm/io.h>

#define WINDOW_ADDR (0xa0000000)	/* start of flash memory */

static __u8 m32r_read8(struct map_info *map, unsigned long ofs)
{
	return readb(map->map_priv_1 + ofs);
}

static __u16 m32r_read16(struct map_info *map, unsigned long ofs)
{
	return readw(map->map_priv_1 + ofs);
}

static __u32 m32r_read32(struct map_info *map, unsigned long ofs)
{
	return readl(map->map_priv_1 + ofs);
}

static void m32r_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy(to, (void *)(map->map_priv_1 + from), len);
}

static void m32r_write8(struct map_info *map, __u8 d, unsigned long adr)
{
	writeb(d, map->map_priv_1 + adr);
}

static void m32r_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	writew(d, map->map_priv_1 + adr);
}

static void m32r_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	writel(d, map->map_priv_1 + adr);
}

static void m32r_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	memcpy((void *)(map->map_priv_1 + to), from, len);
}

static struct map_info m32r_map = {
	name:		"M32R flash",
	read8:		m32r_read8,
	read16:		m32r_read16,
	read32:		m32r_read32,
	copy_from:	m32r_copy_from,
	write8:		m32r_write8,
	write16:	m32r_write16,
	write32:	m32r_write32,
	copy_to:	m32r_copy_to,

	map_priv_1:	WINDOW_ADDR,
	map_priv_2:	-1,
};

#ifdef CONFIG_PLAT_M32700UT
#define M32700UT_FLASH_SIZE		0x00400000
static struct mtd_partition m32700ut_partitions[] = {
	{
		name:		"M32700UT boot firmware",
		size:		0x30000,		/* 192KB */
		offset:		0,
		mask_flags:	MTD_WRITEABLE,  	/* force read-only */
	}, {
		name:		"M32700UT kernel",
		size:		0xd0000,		/* 832KB */
		offset:		MTDPART_OFS_APPEND,
	}, {
		name:		"M32700UT root",
		size:		0x2f0000,		/* 3008KB */
		offset:		MTDPART_OFS_APPEND,
	}, {
		name:		"M32700UT params",
		size:		MTDPART_SIZ_FULL,	/* 64KB */
		offset:		MTDPART_OFS_APPEND,
	}
};
#endif

extern int parse_redboot_partitions(struct mtd_info *master, struct mtd_partition **pparts);
extern int parse_bootldr_partitions(struct mtd_info *master, struct mtd_partition **pparts);

static struct mtd_partition *parsed_parts;
static struct mtd_info *mymtd;

int __init m32r_mtd_init(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0, ret;
	int parsed_nr_parts = 0;
	const char *part_type;
	unsigned long base = -1UL;


	/* Default flash buswidth */
	m32r_map.buswidth = 2;

	/*
	 * Static partition definition selection
	 */
	part_type = "static";

#ifdef CONFIG_PLAT_M32700UT
	parts = m32700ut_partitions;
	nb_parts = ARRAY_SIZE(m32700ut_partitions);
	m32r_map.size = M32700UT_FLASH_SIZE;
	m32r_map.buswidth = 2;
#endif

	/*
	 * For simple flash devices, use ioremap to map the flash.
	 */
	if (base != (unsigned long)-1) {
		if (!request_mem_region(base, m32r_map.size, "flash"))
			return -EBUSY;
		m32r_map.map_priv_2 = base;
		m32r_map.map_priv_1 = (unsigned long)
				ioremap(base, m32r_map.size);
		ret = -ENOMEM;
		if (!m32r_map.map_priv_1)
			goto out_err;
	}

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "M32R flash: probing %d-bit flash bus\n", m32r_map.buswidth*8);
	mymtd = do_map_probe("m5drv", &m32r_map);
	ret = -ENXIO;
	if (!mymtd)
		goto out_err;
	mymtd->module = THIS_MODULE;

	/*
	 * Dynamic partition selection stuff (might override the static ones)
	 */
#ifdef CONFIG_MTD_REDBOOT_PARTS
	if (parsed_nr_parts == 0) {
		ret = parse_redboot_partitions(mymtd, &parsed_parts);

		if (ret > 0) {
			part_type = "RedBoot";
			parsed_nr_parts = ret;
		}
	}
#endif
#ifdef CONFIG_MTD_BOOTLDR_PARTS
	if (parsed_nr_parts == 0) {
		ret = parse_bootldr_partitions(mymtd, &parsed_parts);
		if (ret > 0) {
			part_type = "Compaq bootldr";
			parsed_nr_parts = ret;
		}
	}
#endif

	if (parsed_nr_parts > 0) {
		parts = parsed_parts;
		nb_parts = parsed_nr_parts;
	}

	if (nb_parts == 0) {
		printk(KERN_NOTICE "M32R flash: no partition info available, registering whole flash at once\n");
		add_mtd_device(mymtd);
	} else {
		printk(KERN_NOTICE "Using %s partition definition\n", part_type);
		add_mtd_partitions(mymtd, parts, nb_parts);
	}
	return 0;

 out_err:
	if (m32r_map.map_priv_2 != -1) {
		iounmap((void *)m32r_map.map_priv_1);
		release_mem_region(m32r_map.map_priv_2, m32r_map.size);
	}
	return ret;
}

static void __exit m32r_mtd_cleanup(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
		if (parsed_parts)
			kfree(parsed_parts);
	}
	if (m32r_map.map_priv_2 != -1) {
		iounmap((void *)m32r_map.map_priv_1);
		release_mem_region(m32r_map.map_priv_2, m32r_map.size);
	}
}

module_init(m32r_mtd_init);
module_exit(m32r_mtd_cleanup);

MODULE_AUTHOR("Takeo Takahashi");
MODULE_DESCRIPTION("M32R Flash map driver");
MODULE_LICENSE("GPL");
