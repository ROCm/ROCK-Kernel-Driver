/*
 * $Id: map_funcs.c,v 1.2 2003/05/21 15:15:07 dwmw2 Exp $
 *
 * Out-of-line map I/O functions for simple maps when CONFIG_COMPLEX_MAPPINGS
 * is enabled.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#include <asm/io.h>

#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>

static u8 simple_map_read8(struct map_info *map, unsigned long ofs)
{
	return __raw_readb(map->virt + ofs);
}

static u16 simple_map_read16(struct map_info *map, unsigned long ofs)
{
	return __raw_readw(map->virt + ofs);
}

static u32 simple_map_read32(struct map_info *map, unsigned long ofs)
{
	return __raw_readl(map->virt + ofs);
}

static u64 simple_map_read64(struct map_info *map, unsigned long ofs)
{
#ifndef CONFIG_MTD_CFI_B8 /* 64-bit mappings */
	BUG();
	return 0;
#else
	return __raw_readll(map->virt + ofs);
#endif
}

static void simple_map_write8(struct map_info *map, u8 datum, unsigned long ofs)
{
	__raw_writeb(datum, map->virt + ofs);
	mb();
}

static void simple_map_write16(struct map_info *map, u16 datum, unsigned long ofs)
{
	__raw_writew(datum, map->virt + ofs);
	mb();
}

static void simple_map_write32(struct map_info *map, u32 datum, unsigned long ofs)
{
	__raw_writel(datum, map->virt + ofs);
	mb();
}

static void simple_map_write64(struct map_info *map, u64 datum, unsigned long ofs)
{
#ifndef CONFIG_MTD_CFI_B8 /* 64-bit mappings */
	BUG();
#else
	__raw_writell(datum, map->virt + ofs);
	mb();
#endif /* CFI_B8 */
}

static void simple_map_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy_fromio(to, map->virt + from, len);
}

static void simple_map_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	memcpy_toio(map->virt + to, from, len);
}

void simple_map_init(struct map_info *map)
{
	map->read8 = simple_map_read8;
	map->read16 = simple_map_read16;
	map->read32 = simple_map_read32;
	map->read64 = simple_map_read64;
	map->write8 = simple_map_write8;
	map->write16 = simple_map_write16;
	map->write32 = simple_map_write32;
	map->write64 = simple_map_write64;
	map->copy_from = simple_map_copy_from;
	map->copy_to = simple_map_copy_to;
}

EXPORT_SYMBOL(simple_map_init);
