/*
 * Common code to handle map devices which are simple ROM
 * (C) 2000 Red Hat. GPL'd.
 * $Id: map_rom.c,v 1.10 2000/12/10 01:39:13 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/malloc.h>

#include <linux/mtd/map.h>

static int maprom_read (struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int maprom_write (struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static void maprom_nop (struct mtd_info *);

static const char im_name[] = "map_rom_probe";

/* This routine is made available to other mtd code via
 * inter_module_register.  It must only be accessed through
 * inter_module_get which will bump the use count of this module.  The
 * addresses passed back in mtd are valid as long as the use count of
 * this module is non-zero, i.e. between inter_module_get and
 * inter_module_put.  Keith Owens <kaos@ocs.com.au> 29 Oct 2000.
 */

struct mtd_info *map_rom_probe(struct map_info *map)
{
	struct mtd_info *mtd;

	mtd = kmalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return NULL;

	memset(mtd, 0, sizeof(*mtd));

	map->im_name = im_name;
	map->fldrv_destroy = maprom_nop;
	mtd->priv = map;
	mtd->name = map->name;
	mtd->type = MTD_ROM;
	mtd->size = map->size;
	mtd->read = maprom_read;
	mtd->write = maprom_write;
	mtd->sync = maprom_nop;
	mtd->flags = MTD_CAP_ROM;
	mtd->erasesize = 131072;

	return mtd;
}


static int maprom_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	struct map_info *map = (struct map_info *)mtd->priv;

	map->copy_from(map, buf, from, len);
	*retlen = len;
	return 0;
}

static void maprom_nop(struct mtd_info *mtd)
{
	/* Nothing to see here */
}

static int maprom_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	printk(KERN_NOTICE "maprom_write called\n");
	return -EIO;
}

#if LINUX_VERSION_CODE < 0x20212 && defined(MODULE)
#define map_rom_init init_module
#define map_rom_exit cleanup_module
#endif

static int __init map_rom_init(void)
{
	inter_module_register(im_name, THIS_MODULE, &map_rom_probe);
	return 0;
}

static void __exit map_rom_exit(void)
{
	inter_module_unregister(im_name);
}

module_init(map_rom_init);
module_exit(map_rom_exit);
