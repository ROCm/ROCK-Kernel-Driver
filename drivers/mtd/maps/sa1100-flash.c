/*
 * Flash memory access on SA11x0 based devices
 * 
 * (C) 2000 Nicolas Pitre <nico@cam.org>
 * 
 * $Id: sa1100-flash.c,v 1.22 2001/10/02 10:04:52 rmk Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/hardware.h>
#include <asm/io.h>


#ifndef CONFIG_ARCH_SA1100
#error This is for SA1100 architecture only
#endif


#define WINDOW_ADDR 0xe8000000

static __u8 sa1100_read8(struct map_info *map, unsigned long ofs)
{
	return readb(map->map_priv_1 + ofs);
}

static __u16 sa1100_read16(struct map_info *map, unsigned long ofs)
{
	return readw(map->map_priv_1 + ofs);
}

static __u32 sa1100_read32(struct map_info *map, unsigned long ofs)
{
	return readl(map->map_priv_1 + ofs);
}

static void sa1100_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy(to, (void *)(map->map_priv_1 + from), len);
}

static void sa1100_write8(struct map_info *map, __u8 d, unsigned long adr)
{
	writeb(d, map->map_priv_1 + adr);
}

static void sa1100_write16(struct map_info *map, __u16 d, unsigned long adr)
{
	writew(d, map->map_priv_1 + adr);
}

static void sa1100_write32(struct map_info *map, __u32 d, unsigned long adr)
{
	writel(d, map->map_priv_1 + adr);
}

static void sa1100_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	memcpy((void *)(map->map_priv_1 + to), from, len);
}


#ifdef CONFIG_SA1100_H3600

static void h3600_set_vpp(struct map_info *map, int vpp)
{
	if (vpp)
		set_h3600_egpio(EGPIO_H3600_VPP_ON);
	else
		clr_h3600_egpio(EGPIO_H3600_VPP_ON);
}

#endif

#ifdef CONFIG_SA1100_JORNADA720

static void jornada720_set_vpp(int vpp)
{
  if (vpp)
      PPSR |= 0x80;
  else
      PPSR &= ~0x80;
  PPDR |= 0x80;
}

#endif

static struct map_info sa1100_map = {
	name:		"SA1100 flash",
	read8:		sa1100_read8,
	read16:		sa1100_read16,
	read32:		sa1100_read32,
	copy_from:	sa1100_copy_from,
	write8:		sa1100_write8,
	write16:	sa1100_write16,
	write32:	sa1100_write32,
	copy_to:	sa1100_copy_to,

	map_priv_1:	WINDOW_ADDR,
};


/*
 * Here are partition information for all known SA1100-based devices.
 * See include/linux/mtd/partitions.h for definition of the mtd_partition
 * structure.
 * 
 * The *_max_flash_size is the maximum possible mapped flash size which
 * is not necessarily the actual flash size.  It must correspond to the 
 * value specified in the mapping definition defined by the
 * "struct map_desc *_io_desc" for the corresponding machine.
 */

#ifdef CONFIG_SA1100_ASSABET

/* Phase 4 Assabet has two 28F160B3 flash parts in bank 0: */
static unsigned long assabet4_max_flash_size = 0x00400000;
static struct mtd_partition assabet4_partitions[] = {
        {
                name: "bootloader",
                size: 0x00020000,
                offset: 0,
                mask_flags: MTD_WRITEABLE
        },{
                name: "bootloader params",
                size: 0x00020000,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        },{
                name: "jffs",
                size: MTDPART_SIZ_FULL,
                offset: MTDPART_OFS_APPEND
        }
};

/* Phase 5 Assabet has two 28F128J3A flash parts in bank 0: */
static unsigned long assabet5_max_flash_size = 0x02000000;
static struct mtd_partition assabet5_partitions[] = {
        {
                name: "bootloader",
                size: 0x00040000,
                offset: 0,
                mask_flags: MTD_WRITEABLE
        },{
                name: "bootloader params",
                size: 0x00040000,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        },{
                name: "jffs",
                size: MTDPART_SIZ_FULL,
                offset: MTDPART_OFS_APPEND
        }
};

#define assabet_max_flash_size assabet5_max_flash_size
#define assabet_partitions     assabet5_partitions

#endif

#ifdef CONFIG_SA1100_FLEXANET

/* Flexanet has two 28F128J3A flash parts in bank 0: */
static unsigned long flexanet_max_flash_size = 0x02000000;
static struct mtd_partition flexanet_partitions[] = {
        {
                name: "bootloader",
                size: 0x00040000,
                offset: 0,
                mask_flags: MTD_WRITEABLE
        },{
                name: "bootloader params",
                size: 0x00040000,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        },{
                name: "kernel",
                size: 0x000C0000,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        },{
                name: "altkernel",
                size: 0x000C0000,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        },{
                name: "root",
                size: 0x00400000,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        },{
                name: "free1",
                size: 0x00300000,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        },{
                name: "free2",
                size: 0x00300000,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        },{
                name: "free3",
                size: MTDPART_SIZ_FULL,
                offset: MTDPART_OFS_APPEND,
                mask_flags: MTD_WRITEABLE
        }
};

#endif

#ifdef CONFIG_SA1100_HUW_WEBPANEL
static unsigned long huw_webpanel_max_flash_size = 0x01000000;
static struct mtd_partition huw_webpanel_partitions[] = {
	{ 
	  name: "Loader",
	  size: 0x00040000,
	  offset: 0,
	},{
	  name: "Sector 1",
	  size: 0x00040000,
	  offset: MTDPART_OFS_APPEND,
	},{
	  size: MTDPART_SIZ_FULL,
	  offset: MTDPART_OFS_APPEND,
	}
};
#endif /* CONFIG_SA1100_HUW_WEBPANEL */


#ifdef CONFIG_SA1100_H3600

static unsigned long h3600_max_flash_size = 0x02000000;
static struct mtd_partition h3600_partitions[] = {
	{
		name: "H3600 boot firmware",
		size: 0x00040000,
		offset: 0,
		mask_flags: MTD_WRITEABLE  /* force read-only */
	},{
		name: "H3600 kernel",
		size: 0x00080000,
		offset: 0x40000
	},{
		name: "H3600 params",
		size: 0x00040000,
		offset: 0xC0000
	},{
#ifdef CONFIG_JFFS2_FS
		name: "H3600 root jffs2",
		offset: 0x00100000,
		size: MTDPART_SIZ_FULL
#else
		name: "H3600 initrd",
		size: 0x00100000,
		offset: 0x00100000
	},{
		name: "H3600 root cramfs",
		size: 0x00300000,
		offset: 0x00200000
	},{
		name: "H3600 usr cramfs",
		size: 0x00800000,
		offset: 0x00500000
	},{
		name: "H3600 usr local",
		offset: 0x00d00000,
		size: MTDPART_SIZ_FULL
#endif
	}
};

#endif
#ifdef CONFIG_SA1100_FREEBIRD
static unsigned long freebird_max_flash_size = 0x02000000;
static struct mtd_partition freebird_partitions[] = {
#if CONFIG_SA1100_FREEBIRD_NEW
    {
     name: "firmware",
     size: 0x00040000,
     offset: 0,
     mask_flags: MTD_WRITEABLE  /* force read-only */
    },{
     name: "kernel",
     size: 0x00080000,
     offset: 0x40000
    },{
     name: "params",
     size: 0x00040000,
     offset: 0xC0000
    },{
     name: "initrd",
     size: 0x00100000,
     offset: 0x00100000
    },{
     name: "root cramfs",
     size: 0x00300000,
     offset: 0x00200000
    },{
     name: "usr cramfs",
     size: 0x00C00000,
     offset: 0x00500000
    },{
	 name: "local",
	 offset: 0x01100000,
	 size: MTDPART_SIZ_FULL 
	}
#else
	{ offset: 0,            		size: 0x00040000,   },
	{ offset: MTDPART_OFS_APPEND,   size: 0x000c0000,   },
	{ offset: MTDPART_OFS_APPEND,	size: 0x00400000,	},
	{ offset: MTDPART_OFS_APPEND,   size: MTDPART_SIZ_FULL  }
#endif
	};
#endif
																									

#ifdef CONFIG_SA1100_CERF

static unsigned long cerf_max_flash_size = 0x01000000;
static struct mtd_partition cerf_partitions[] = {
	{ offset: 0,			size: 0x00800000 	},
	{ offset: MTDPART_OFS_APPEND,	size: 0x00800000 	}
};

#endif

#ifdef CONFIG_SA1100_GRAPHICSCLIENT

static unsigned long graphicsclient_max_flash_size = 0x01000000;
static struct mtd_partition graphicsclient_partitions[] = {
	{ 
	 name: "zImage",
	 offset: 0,
	 size: 0x100000
	},
	{ 
         name: "ramdisk.gz",
         offset: MTDPART_OFS_APPEND,
         size: 0x300000 		
	},
	{ 
	  name: "User FS",
          offset: MTDPART_OFS_APPEND,	
          size: MTDPART_SIZ_FULL
	}
};

#endif

#ifdef CONFIG_SA1100_GRAPHICSMASTER

static unsigned long graphicsmaster_max_flash_size = 0x01000000;
static struct mtd_partition graphicsmaster_partitions[] = {
	{ 
	 name: "zImage",
	 offset: 0,
	 size: 0x100000
	},
	{ 
         name: "ramdisk.gz",
         offset: MTDPART_OFS_APPEND,
         size: 0x300000 		
	},
	{ 
	  name: "User FS",
          offset: MTDPART_OFS_APPEND,	
          size: MTDPART_SIZ_FULL
	}
};

#endif

#ifdef CONFIG_SA1100_PANGOLIN

static unsigned long pangolin_max_flash_size = 0x04000000;
static struct mtd_partition pangolin_partitions[] = {
	{
	  name: "boot firmware",
	  offset: 0x00000000,
	  size: 0x00080000,
	  mask_flags: MTD_WRITEABLE,  /* force read-only */
	},
	{
	  name: "kernel",
	  offset: 0x00080000,
	  size: 0x00100000,
	},
	{
	  name: "initrd",
	  offset: 0x00180000,
	  size: 0x00280000,
	},
	{
	  name: "initrd-test",
	  offset: 0x00400000,
	  size: 0x03C00000,
	}
};

#endif

#ifdef CONFIG_SA1100_YOPY

static unsigned long yopy_max_flash_size = 0x08000000;
static struct mtd_partition yopy_partitions[] = {
	{
		name: "boot firmware",
		offset: 0x00000000,
		size: 0x00040000,
		mask_flags: MTD_WRITEABLE,  /* force read-only */
	},
	{
		name: "kernel",
		offset: 0x00080000,
		size: 0x00080000,
	},
	{
		name: "initrd",
		offset: 0x00100000,
		size: 0x00300000,
	},
	{
		name: "root",
		offset: 0x00400000,
		size: 0x01000000,
	},
};

#endif

#ifdef CONFIG_SA1100_JORNADA720

static unsigned long jornada720_max_flash_size = 0x02000000;
static struct mtd_partition jornada720_partitions[] = {
	{
		name: "JORNADA720 boot firmware",
		size: 0x00040000,
		offset: 0,
		mask_flags: MTD_WRITEABLE  /* force read-only */
	},{
		name: "JORNADA720 kernel",
		size: 0x000c0000,
		offset: 0x40000
	},{
		name: "JORNADA720 params",
		size: 0x00040000,
		offset: 0x100000
	},{
		name: "JORNADA720 initrd",
		size: 0x00100000,
		offset: 0x00140000
	},{
		name: "JORNADA720 root cramfs",
		size: 0x00300000,
		offset: 0x00240000
	},{
		name: "JORNADA720 usr cramfs",
		size: 0x00800000,
		offset: 0x00540000
	},{
		name: "JORNADA720 usr local",
		offset: 0x00d00000,
		size: 0  /* will expand to the end of the flash */
	}
};
#endif

#ifdef CONFIG_SA1100_SHERMAN

static unsigned long sherman_max_flash_size = 0x02000000;
static struct mtd_partition sherman_partitions[] = {
	{ offset: 0,			size: 0x50000 	},
	{ offset: MTDPART_OFS_APPEND,	size: 0x70000 	},
	{ offset: MTDPART_OFS_APPEND,	size: 0x600000 	},
	{ offset: MTDPART_OFS_APPEND,	size: 0xA0000 	}
};

#endif

#ifdef CONFIG_SA1100_STORK

static unsigned long stork_max_flash_size = 0x02000000;
static struct mtd_partition stork_partitions[] = {
	{
		name: "STORK boot firmware",
		size: 0x00040000,
		offset: 0,
		mask_flags: MTD_WRITEABLE  /* force read-only */
	},{
		name: "STORK params",
		size: 0x00040000,
		offset: 0x40000
	},{
		name: "STORK kernel",
		size: 0x00100000,
		offset: 0x80000
	},{
#ifdef CONFIG_JFFS2_FS
		name: "STORK root jffs2",
		offset: 0x00180000,
		size: MTDPART_SIZ_FULL
#else
		name: "STORK initrd",
		size: 0x00100000,
		offset: 0x00180000
	},{
		name: "STORK root cramfs",
		size: 0x00300000,
		offset: 0x00280000
	},{
		name: "STORK usr cramfs",
		size: 0x00800000,
		offset: 0x00580000
	},{
		name: "STORK usr local",
		offset: 0x00d80000,
		size: MTDPART_SIZ_FULL
#endif
	}
};

#endif

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))


extern int parse_redboot_partitions(struct mtd_info *master, struct mtd_partition **pparts);
extern int parse_bootldr_partitions(struct mtd_info *master, struct mtd_partition **pparts);

static struct mtd_partition *parsed_parts;
static struct mtd_info *mymtd;

int __init sa1100_mtd_init(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	int parsed_nr_parts = 0;
	char *part_type;
	
	/* Default flash buswidth */
	sa1100_map.buswidth = (MSC0 & MSC_RBW) ? 2 : 4;

	/*
	 * Static partition definition selection
	 */
	part_type = "static";
#ifdef CONFIG_SA1100_ASSABET
	if (machine_is_assabet()) {
		parts = assabet_partitions;
		nb_parts = NB_OF(assabet_partitions);
		sa1100_map.size = assabet_max_flash_size;
	}
#endif

#ifdef CONFIG_SA1100_HUW_WEBPANEL
	if (machine_is_huw_webpanel()) {
		parts = huw_webpanel_partitions;
		nb_parts = NB_OF(huw_webpanel_partitions);
		sa1100_map.size = huw_webpanel_max_flash_size;
	}
#endif

#ifdef CONFIG_SA1100_H3600
	if (machine_is_h3600()) {
		parts = h3600_partitions;
		nb_parts = NB_OF(h3600_partitions);
		sa1100_map.size = h3600_max_flash_size;
		sa1100_map.set_vpp = h3600_set_vpp;
	}
#endif
#ifdef CONFIG_SA1100_FREEBIRD
	if (machine_is_freebird()) {
		parts = freebird_partitions;
		nb_parts = NB_OF(freebird_partitions);
		sa1100_map.size = freebird_max_flash_size;
	}
#endif
#ifdef CONFIG_SA1100_CERF
	if (machine_is_cerf()) {
		parts = cerf_partitions;
		nb_parts = NB_OF(cerf_partitions);
		sa1100_map.size = cerf_max_flash_size;
	}
#endif
#ifdef CONFIG_SA1100_GRAPHICSCLIENT
	if (machine_is_graphicsclient()) {
		parts = graphicsclient_partitions;
		nb_parts = NB_OF(graphicsclient_partitions);
		sa1100_map.size = graphicsclient_max_flash_size;
		sa1100_map.buswidth = (MSC1 & MSC_RBW) ? 2:4;
	}
#endif
#ifdef CONFIG_SA1100_GRAPHICSMASTER
	if (machine_is_graphicsmaster()) {
		parts = graphicsmaster_partitions;
		nb_parts = NB_OF(graphicsmaster_partitions);
		sa1100_map.size = graphicsmaster_max_flash_size;
		sa1100_map.buswidth = (MSC1 & MSC_RBW) ? 2:4;
	}
#endif
#ifdef CONFIG_SA1100_PANGOLIN
	if (machine_is_pangolin()) {
		parts = pangolin_partitions;
		nb_parts = NB_OF(pangolin_partitions);
		sa1100_map.size = pangolin_max_flash_size;
	}
#endif
#ifdef CONFIG_SA1100_JORNADA720
	if (machine_is_jornada720()) {
		parts = jornada720_partitions;
		nb_parts = NB_OF(jornada720_partitions);
		sa1100_map.size = jornada720_max_flash_size;
		sa1100_map.set_vpp = jornada720_set_vpp;
	}
#endif
#ifdef CONFIG_SA1100_YOPY
	if (machine_is_yopy()) {
		parts = yopy_partitions;
		nb_parts = NB_OF(yopy_partitions);
		sa1100_map.size = yopy_max_flash_size;
	}
#endif
#ifdef CONFIG_SA1100_SHERMAN
	if (machine_is_sherman()) {
		parts = sherman_partitions;
		nb_parts = NB_OF(sherman_partitions);
		sa1100_map.size = sherman_max_flash_size;
	}
#endif
#ifdef CONFIG_SA1100_FLEXANET
	if (machine_is_flexanet()) {
		parts = flexanet_partitions;
		nb_parts = NB_OF(flexanet_partitions);
		sa1100_map.size = flexanet_max_flash_size;
	}
#endif
#ifdef CONFIG_SA1100_STORK
	if (machine_is_stork()) {
		parts = stork_partitions;
		nb_parts = NB_OF(stork_partitions);
		sa1100_map.size = stork_max_flash_size;
	}
#endif

	/*
	 * Now let's probe for the actual flash.  Do it here since
	 * specific machine settings might have been set above.
	 */
	printk(KERN_NOTICE "SA1100 flash: probing %d-bit flash bus\n", sa1100_map.buswidth*8);
	mymtd = do_map_probe("cfi_probe", &sa1100_map);
	if (!mymtd)
		return -ENXIO;
	mymtd->module = THIS_MODULE;

	/*
	 * Dynamic partition selection stuff (might override the static ones)
	 */
#ifdef CONFIG_MTD_REDBOOT_PARTS
	if (parsed_nr_parts == 0) {
		int ret = parse_redboot_partitions(mymtd, &parsed_parts);
		
		if (ret > 0) {
			part_type = "RedBoot";
			parsed_nr_parts = ret;
		}
	}
#endif
#ifdef CONFIG_MTD_BOOTLDR_PARTS
	if (parsed_nr_parts == 0) {
		int ret = parse_bootldr_partitions(mymtd, &parsed_parts);
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
		printk(KERN_NOTICE "SA1100 flash: no partition info available, registering whole flash at once\n");
		add_mtd_device(mymtd);
	} else {
		printk(KERN_NOTICE "Using %s partition definition\n", part_type);
		add_mtd_partitions(mymtd, parts, nb_parts);
	}
	return 0;
}

static void __exit sa1100_mtd_cleanup(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
		if (parsed_parts)
			kfree(parsed_parts);
	}
}

module_init(sa1100_mtd_init);
module_exit(sa1100_mtd_cleanup);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("SA1100 CFI map driver");
MODULE_LICENSE("GPL");
