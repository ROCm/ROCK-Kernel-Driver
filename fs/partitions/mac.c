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

#ifdef CONFIG_PPC
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

int mac_partition(struct gendisk *hd, kdev_t dev, unsigned long fsec, int first_part_minor)
{
	struct buffer_head *bh;
	int blk, blocks_in_map;
	int dev_bsize, dev_pos, pos;
	unsigned secsize;
#ifdef CONFIG_PPC
	int found_root = 0;
	int found_root_goodness = 0;
#endif
	struct mac_partition *part;
	struct mac_driver_desc *md;

	dev_bsize = get_ptable_blocksize(dev);
	dev_pos = 0;
	/* Get 0th block and look at the first partition map entry. */
	if ((bh = bread(dev, 0, dev_bsize)) == 0) {
	    printk("%s: error reading partition table\n",
		   kdevname(dev));
	    return -1;
	}
	md = (struct mac_driver_desc *) bh->b_data;
	if (be16_to_cpu(md->signature) != MAC_DRIVER_MAGIC) {
		brelse(bh);
		return 0;
	}
	secsize = be16_to_cpu(md->block_size);
	if (secsize >= dev_bsize) {
		brelse(bh);
		dev_pos = secsize;
		if ((bh = bread(dev, secsize/dev_bsize, dev_bsize)) == 0) {
			printk("%s: error reading Mac partition table\n",
			       kdevname(dev));
			return -1;
		}
	}
	part = (struct mac_partition *) (bh->b_data + secsize - dev_pos);
	if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC) {
		brelse(bh);
		return 0;		/* not a MacOS disk */
	}
	printk(" [mac]");
	blocks_in_map = be32_to_cpu(part->map_count);
	for (blk = 1; blk <= blocks_in_map; ++blk) {
		pos = blk * secsize;
		if (pos >= dev_pos + dev_bsize) {
			brelse(bh);
			dev_pos = pos;
			if ((bh = bread(dev, pos/dev_bsize, dev_bsize)) == 0) {
				printk("%s: error reading partition table\n",
				       kdevname(dev));
				return -1;
			}
		}
		part = (struct mac_partition *) (bh->b_data + pos - dev_pos);
		if (be16_to_cpu(part->signature) != MAC_PARTITION_MAGIC)
			break;
		blocks_in_map = be32_to_cpu(part->map_count);
		add_gd_partition(hd, first_part_minor,
			fsec + be32_to_cpu(part->start_block) * (secsize/512),
			be32_to_cpu(part->block_count) * (secsize/512));

#ifdef CONFIG_PPC
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
#endif /* CONFIG_PPC */

		++first_part_minor;
	}
#ifdef CONFIG_PPC
	if (found_root_goodness)
		note_bootable_part(dev, found_root, found_root_goodness);
#endif
	brelse(bh);
	printk("\n");
	return 1;
}

