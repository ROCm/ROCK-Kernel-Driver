/*
 * $Id: physmap.c,v 1.29 2003/05/29 09:24:10 dwmw2 Exp $
 *
 * Normal mappings of chips in physical memory
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/config.h>
#include <linux/mtd/partitions.h>

#define WINDOW_ADDR CONFIG_MTD_PHYSMAP_START
#define WINDOW_SIZE CONFIG_MTD_PHYSMAP_LEN
#define BUSWIDTH CONFIG_MTD_PHYSMAP_BUSWIDTH

static struct mtd_info *mymtd;


struct map_info physmap_map = {
	.name = "Physically mapped flash",
	.size = WINDOW_SIZE,
	.buswidth = BUSWIDTH,
	.phys = WINDOW_ADDR,
};

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition *mtd_parts;
static int                   mtd_parts_nb;

static struct mtd_partition physmap_partitions[] = {
#if 0
/* Put your own partition definitions here */
	{
		.name =		"bootROM",
		.size =		0x80000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"zImage",
		.size =		0x100000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"ramdisk.gz",
		.size =		0x300000,
		.offset =	MTDPART_OFS_APPEND,
		.mask_flags =	MTD_WRITEABLE,  /* force read-only */
	}, {
		.name =		"User FS",
		.size =		MTDPART_SIZ_FULL,
		.offset =	MTDPART_OFS_APPEND,
	}
#endif
};

#define NUM_PARTITIONS	(sizeof(physmap_partitions)/sizeof(struct mtd_partition))
const char *part_probes[] = {"cmdlinepart", "RedBoot", NULL};

#endif /* CONFIG_MTD_PARTITIONS */

int __init init_physmap(void)
{
	static const char *rom_probe_types[] = { "cfi_probe", "jedec_probe", "map_rom", 0 };
	const char **type;

       	printk(KERN_NOTICE "physmap flash device: %x at %x\n", WINDOW_SIZE, WINDOW_ADDR);
	physmap_map.virt = (unsigned long)ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!physmap_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}

	simple_map_init(&physmap_map);

	mymtd = 0;
	type = rom_probe_types;
	for(; !mymtd && *type; type++) {
		mymtd = do_map_probe(*type, &physmap_map);
	}
	if (mymtd) {
		mymtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
		mtd_parts_nb = parse_mtd_partitions(mymtd, part_probes, 
						    &mtd_parts, 0);

		if (mtd_parts_nb > 0)
		{
			add_mtd_partitions (mymtd, mtd_parts, mtd_parts_nb);
			return 0;
		}

		if (NUM_PARTITIONS != 0) 
		{
			printk(KERN_NOTICE 
			       "Using physmap partition definition\n");
			add_mtd_partitions (mymtd, physmap_partitions, NUM_PARTITIONS);
			return 0;
		}

#endif
		add_mtd_device(mymtd);

		return 0;
	}

	iounmap((void *)physmap_map.virt);
	return -ENXIO;
}

static void __exit cleanup_physmap(void)
{
#ifdef CONFIG_MTD_PARTITIONS
	if (mtd_parts_nb) {
		del_mtd_partitions(mymtd);
		kfree(mtd_parts);
	} else if (NUM_PARTITIONS) {
		del_mtd_partitions(mymtd);
	} else {
		del_mtd_device(mymtd);
	}
#else
	del_mtd_device(mymtd);
#endif
	map_destroy(mymtd);

	iounmap((void *)physmap_map.virt);
	physmap_map.virt = 0;
}

module_init(init_physmap);
module_exit(cleanup_physmap);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Generic configurable MTD map driver");
