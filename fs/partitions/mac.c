/*
 *  fs/partitions/mac.c
 *
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/ctype.h>

#include <asm/system.h>

#include "check.h"
#include "mac.h"

#ifdef CONFIG_ALL_PPC
extern void note_bootable_part(kdev_t dev, int part, int goodness);
#endif

/*
 * Code to understand MacOS partition tables.
 */

static inline void mac_fix_string(char *stg, int len)
{
	int i;

	for (i = len - 1; i >= 0 && stg[i] == ' '; i--)
		stg[i] = 0;
}

int mac_partition(struct gendisk *hd, struct block_device *bdev,
		unsigned long fsec, int first_part_minor)
{
	Sector sect;
	unsigned char *data;
	int blk, blocks_in_map;
	unsigned secsize;
#ifdef CONFIG_ALL_PPC
	int found_root = 0;
	int found_root_goodness = 0;
#endif
	struct mac_partition *part;
	struct mac_driver_desc *md;

	/* Get 0th block and look at the first partition map entry. */
	md = (struct mac_driver_desc *) read_dev_sector(bdev, 0, &sect);
	if (!md)
		return -1;
	if (be16_to_cpu(md->signature) != MAC_DRIVER_MAGIC) {
		put_dev_sector(sect);
		return 0;
	}
	secsize = be16_to_cpu(md->block_size);
	put_dev_sector(sect);
	data = read_dev_sector(bdev, secsize/512, &sect);
	if (!data)
		return -1;
	part = (struct mac_partition *) (data + secsize%512);
	if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC) {
		put_dev_sector(sect);
		return 0;		/* not a MacOS disk */
	}
	printk(" [mac]");
	blocks_in_map = be32_to_cpu(part->map_count);
	for (blk = 1; blk <= blocks_in_map; ++blk) {
		int pos = blk * secsize;
		put_dev_sector(sect);
		data = read_dev_sector(bdev, pos/512, &sect);
		if (!data)
			return -1;
		part = (struct mac_partition *) (data + pos%512);
		if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC)
			break;
		add_gd_partition(hd, first_part_minor,
			fsec + be32_to_cpu(part->start_block) * (secsize/512),
			be32_to_cpu(part->block_count) * (secsize/512));

#ifdef CONFIG_ALL_PPC
		/*
		 * If this is the first bootable partition, tell the
		 * setup code, in case it wants to make this the root.
		 */
		if (_machine == _MACH_Pmac) {
			int goodness = 0;

			mac_fix_string(part->processor, 16);
			mac_fix_string(part->name, 32);
			mac_fix_string(part->type, 32);					
		    
			if ((be32_to_cpu(part->status) & MAC_STATUS_BOOTABLE)
			    && strcasecmp(part->processor, "powerpc") == 0)
				goodness++;

			if (strcasecmp(part->type, "Apple_UNIX_SVR2") == 0
			    || (strnicmp(part->type, "Linux", 5) == 0
			        && strcasecmp(part->type, "Linux_swap") != 0)) {
				int i, l;

				goodness++;
				l = strlen(part->name);
				if (strcmp(part->name, "/") == 0)
					goodness++;
				for (i = 0; i <= l - 4; ++i) {
					if (strnicmp(part->name + i, "root",
						     4) == 0) {
						goodness += 2;
						break;
					}
				}
				if (strnicmp(part->name, "swap", 4) == 0)
					goodness--;
			}

			if (goodness > found_root_goodness) {
				found_root = blk;
				found_root_goodness = goodness;
			}
		}
#endif /* CONFIG_ALL_PPC */

		++first_part_minor;
	}
#ifdef CONFIG_ALL_PPC
	if (found_root_goodness)
		note_bootable_part(to_kdev_t(bdev->bd_dev),
					found_root, found_root_goodness);
#endif

	put_dev_sector(sect);
	printk("\n");
	return 1;
}

