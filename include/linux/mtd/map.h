
/* Overhauled routines for dealing with different mmap regions of flash */
/* $Id: map.h,v 1.10 2000/12/04 13:18:33 dwmw2 Exp $ */

#ifndef __LINUX_MTD_MAP_H__
#define __LINUX_MTD_MAP_H__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mtd/mtd.h>
#include <linux/malloc.h>

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
	int buswidth; /* in octets */
	__u8 (*read8)(struct map_info *, unsigned long);
	__u16 (*read16)(struct map_info *, unsigned long);
	__u32 (*read32)(struct map_info *, unsigned long);  
	/* If it returned a 'long' I'd call it readl.
	 * It doesn't.
	 * I won't.
	 * dwmw2 */
	
	void (*copy_from)(struct map_info *, void *, unsigned long, ssize_t);
	void (*write8)(struct map_info *, __u8, unsigned long);
	void (*write16)(struct map_info *, __u16, unsigned long);
	void (*write32)(struct map_info *, __u32, unsigned long);
	void (*copy_to)(struct map_info *, unsigned long, const void *, ssize_t);

	void (*set_vpp)(int);
	/* We put these two here rather than a single void *map_priv, 
	   because we want mappers to be able to have quickly-accessible
	   cache for the 'currently-mapped page' without the _extra_
	   redirection that would be necessary. If you need more than
	   two longs, turn the second into a pointer. dwmw2 */
	unsigned long map_priv_1;
	unsigned long map_priv_2;
	void *fldrv_priv;
	void (*fldrv_destroy)(struct mtd_info *);
	const char *im_name;
};

#ifdef CONFIG_MODULES
/* 
 * Probe for the contents of a map device and make an MTD structure
 * if anything is recognised. Doesn't register it because the calling
 * map driver needs to set the 'module' field first.
 */
static inline struct mtd_info *do_map_probe(struct map_info *map, const char *funcname, const char *modname)
{
	struct mtd_info *(*probe_p)(struct map_info *);
	struct mtd_info *mtd = NULL;

	if ((probe_p = inter_module_get_request(modname, funcname)))
		mtd = (*probe_p)(map);	/* map->im_name is set by probe */

	return mtd;
}


/* 
 * Commonly-used probe functions for different types of chip.
 */
#define do_cfi_probe(x) do_map_probe(x, "cfi_probe", "cfi_probe")
#define do_jedec_probe(x) do_map_probe(x, "jedec_probe", "jedec_probe")
#define do_ram_probe(x) do_map_probe(x, "map_ram_probe", "map_ram")
#define do_rom_probe(x) do_map_probe(x, "map_rom_probe", "map_rom")
#else
	/* without module support, call probe function directly */
extern struct mtd_info *cfi_probe(struct map_info *);
extern struct mtd_info *jedec_probe(struct map_info *);
extern struct mtd_info *map_ram_probe(struct map_info *);
extern struct mtd_info *map_rom_probe(struct map_info *);

#define do_cfi_probe(x) cfi_probe(x)
#define do_jedec_probe(x) jedec_probe(x)
#define do_ram_probe(x) map_ram_probe(x)
#define do_rom_probe(x) map_rom_probe(x)
#endif

/*
 * Destroy an MTD device which was created for a map device.
 * Make sure the MTD device is already unregistered before calling this
 */
static inline void map_destroy(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;

	map->fldrv_destroy(mtd);
	inter_module_put(map->im_name);
	kfree(mtd);
}

#define ENABLE_VPP(map) do { if(map->set_vpp) map->set_vpp(1); } while(0)
#define DISABLE_VPP(map) do { if(map->set_vpp) map->set_vpp(0); } while(0)

#endif /* __LINUX_MTD_MAP_H__ */
