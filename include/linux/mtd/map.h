
/* Overhauled routines for dealing with different mmap regions of flash */
/* $Id: map.h,v 1.34 2003/05/28 12:42:22 dwmw2 Exp $ */

#ifndef __LINUX_MTD_MAP_H__
#define __LINUX_MTD_MAP_H__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/list.h>
#include <asm/system.h>
#include <asm/io.h>

/* The map stuff is very simple. You fill in your struct map_info with
   a handful of routines for accessing the device, making sure they handle
   paging etc. correctly if your device needs it. Then you pass it off
   to a chip driver which deals with a mapped device - generally either
   do_cfi_probe() or do_ram_probe(), either of which will return a 
   struct mtd_info if they liked what they saw. At which point, you
   fill in the mtd->module with your own module address, and register 
   it.
   
   The mtd->priv field will point to the struct map_info, and any further
   private data required by the chip driver is linked from the 
   mtd->priv->fldrv_priv field. This allows the map driver to get at 
   the destructor function map->fldrv_destroy() when it's tired
   of living.
*/

struct map_info {
	char *name;
	unsigned long size;
	unsigned long phys;
#define NO_XIP (-1UL)

	unsigned long virt;
	void *cached;

	int buswidth; /* in octets */

#ifdef CONFIG_MTD_COMPLEX_MAPPINGS
	u8 (*read8)(struct map_info *, unsigned long);
	u16 (*read16)(struct map_info *, unsigned long);
	u32 (*read32)(struct map_info *, unsigned long);  
	u64 (*read64)(struct map_info *, unsigned long);  
	/* If it returned a 'long' I'd call it readl.
	 * It doesn't.
	 * I won't.
	 * dwmw2 */
	
	void (*copy_from)(struct map_info *, void *, unsigned long, ssize_t);
	void (*write8)(struct map_info *, u8, unsigned long);
	void (*write16)(struct map_info *, u16, unsigned long);
	void (*write32)(struct map_info *, u32, unsigned long);
	void (*write64)(struct map_info *, u64, unsigned long);
	void (*copy_to)(struct map_info *, unsigned long, const void *, ssize_t);

	/* We can perhaps put in 'point' and 'unpoint' methods, if we really
	   want to enable XIP for non-linear mappings. Not yet though. */
#endif
	/* set_vpp() must handle being reentered -- enable, enable, disable 
	   must leave it enabled. */
	void (*set_vpp)(struct map_info *, int);

	unsigned long map_priv_1;
	unsigned long map_priv_2;
	void *fldrv_priv;
	struct mtd_chip_driver *fldrv;
};

struct mtd_chip_driver {
	struct mtd_info *(*probe)(struct map_info *map);
	void (*destroy)(struct mtd_info *);
	struct module *module;
	char *name;
	struct list_head list;
};

void register_mtd_chip_driver(struct mtd_chip_driver *);
void unregister_mtd_chip_driver(struct mtd_chip_driver *);

struct mtd_info *do_map_probe(const char *name, struct map_info *map);
void map_destroy(struct mtd_info *mtd);

#define ENABLE_VPP(map) do { if(map->set_vpp) map->set_vpp(map, 1); } while(0)
#define DISABLE_VPP(map) do { if(map->set_vpp) map->set_vpp(map, 0); } while(0)

#ifdef CONFIG_MTD_COMPLEX_MAPPINGS
#define map_read8(map, ofs) (map)->read8(map, ofs)
#define map_read16(map, ofs) (map)->read16(map, ofs)
#define map_read32(map, ofs) (map)->read32(map, ofs)
#define map_read64(map, ofs) (map)->read64(map, ofs)
#define map_copy_from(map, to, from, len) (map)->copy_from(map, to, from, len)
#define map_write8(map, datum, ofs) (map)->write8(map, datum, ofs)
#define map_write16(map, datum, ofs) (map)->write16(map, datum, ofs)
#define map_write32(map, datum, ofs) (map)->write32(map, datum, ofs)
#define map_write64(map, datum, ofs) (map)->write64(map, datum, ofs)
#define map_copy_to(map, to, from, len) (map)->copy_to(map, to, from, len)

extern void simple_map_init(struct map_info *);
#define map_is_linear(map) (map->phys != NO_XIP)

#else
static inline u8 map_read8(struct map_info *map, unsigned long ofs)
{
	return __raw_readb(map->virt + ofs);
}

static inline u16 map_read16(struct map_info *map, unsigned long ofs)
{
	return __raw_readw(map->virt + ofs);
}

static inline u32 map_read32(struct map_info *map, unsigned long ofs)
{
	return __raw_readl(map->virt + ofs);
}

static inline u64 map_read64(struct map_info *map, unsigned long ofs)
{
#ifndef CONFIG_MTD_CFI_B8 /* 64-bit mappings */
	BUG();
	return 0;
#else
	return __raw_readll(map->virt + ofs);
#endif
}

static inline void map_write8(struct map_info *map, u8 datum, unsigned long ofs)
{
	__raw_writeb(datum, map->virt + ofs);
	mb();
}

static inline void map_write16(struct map_info *map, u16 datum, unsigned long ofs)
{
	__raw_writew(datum, map->virt + ofs);
	mb();
}

static inline void map_write32(struct map_info *map, u32 datum, unsigned long ofs)
{
	__raw_writel(datum, map->virt + ofs);
	mb();
}

static inline void map_write64(struct map_info *map, u64 datum, unsigned long ofs)
{
#ifndef CONFIG_MTD_CFI_B8 /* 64-bit mappings */
	BUG();
#else
	__raw_writell(datum, map->virt + ofs);
	mb();
#endif /* CFI_B8 */
}

static inline void map_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy_fromio(to, map->virt + from, len);
}

static inline void map_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	memcpy_toio(map->virt + to, from, len);
}

#define simple_map_init(map) do { } while (0)
#define map_is_linear(map) (1)

#endif /* !CONFIG_MTD_COMPLEX_MAPPINGS */

#endif /* __LINUX_MTD_MAP_H__ */
