/*
 * Common code to handle map devices which are simple RAM
 * (C) 2000 Red Hat. GPL'd.
 * $Id: map_ram.c,v 1.7 2000/12/10 01:39:13 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/malloc.h>

#include <linux/mtd/map.h>


static int mapram_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int mapram_write (struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static int mapram_erase (struct mtd_info *, struct erase_info *);
static void mapram_nop (struct mtd_info *);

static const char im_name[] = "map_ram_probe";

/* This routine is made available to other mtd code via
 * inter_module_register.  It must only be accessed through
 * inter_module_get which will bump the use count of this module.  The
 * addresses passed back in mtd are valid as long as the use count of
 * this module is non-zero, i.e. between inter_module_get and
 * inter_module_put.  Keith Owens <kaos@ocs.com.au> 29 Oct 2000.
 */

static struct mtd_info *map_ram_probe(struct map_info *map)
{
	struct mtd_info *mtd;

	/* Check the first byte is RAM */
	map->write8(map, 0x55, 0);
	if (map->read8(map, 0) != 0x55)
		return NULL;

	map->write8(map, 0xAA, 0);
	if (map->read8(map, 0) != 0xAA)
		return NULL;

	/* Check the last byte is RAM */
	map->write8(map, 0x55, map->size-1);
	if (map->read8(map, map->size-1) != 0x55)
		return NULL;

	map->write8(map, 0xAA, map->size-1);
	if (map->read8(map, map->size-1) != 0xAA)
		return NULL;

	/* OK. It seems to be RAM. */

	mtd = kmalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return NULL;

	memset(mtd, 0, sizeof(*mtd));

	map->im_name = im_name;
	map->fldrv_destroy = mapram_nop;
	mtd->priv = map;
	mtd->name = map->name;
	mtd->type = MTD_RAM;
	mtd->erasesize = 0x10000;
	mtd->size = map->size;
	mtd->erase = mapram_erase;
	mtd->read = mapram_read;
	mtd->write = mapram_write;
	mtd->sync = mapram_nop;
	mtd->flags = MTD_CAP_RAM | MTD_VOLATILE;
	mtd->erasesize = PAGE_SIZE;

	return mtd;
}


static int mapram_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = (struct map_info *)mtd->priv;

	map->copy_from(map, buf, from, len);
	*retlen = len;
	return 0;
}

static int mapram_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	struct map_info *map = (struct map_info *)mtd->priv;

	map->copy_to(map, to, buf, len);
	*retlen = len;
	return 0;
}

static int mapram_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	/* Yeah, it's inefficient. Who cares? It's faster than a _real_
	   flash erase. */
	struct map_info *map = (struct map_info *)mtd->priv;
	unsigned long i;

	for (i=0; i<instr->len; i++)
		map->write8(map, 0xFF, instr->addr + i);

	if (instr->callback)
		instr->callback(instr);

	return 0;
}

static void mapram_nop(struct mtd_info *mtd)
{
	/* Nothing to see here */
}

#if LINUX_VERSION_CODE < 0x20212 && defined(MODULE)
#define map_ram_init init_module
#define map_ram_exit cleanup_module
#endif

static int __init map_ram_init(void)
{
	inter_module_register(im_name, THIS_MODULE, &map_ram_probe);
	return 0;
}

static void __exit map_ram_exit(void)
{
	inter_module_unregister(im_name);
}

module_init(map_ram_init);
module_exit(map_ram_exit);
