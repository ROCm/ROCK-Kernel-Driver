/*
 *  fs/partitions/sun.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>

#include <asm/system.h>

#include "check.h"
#include "sun.h"

int sun_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector, int first_part_minor)
{
	int i, csum;
	unsigned short *ush;
	struct buffer_head *bh;
	struct sun_disklabel {
		unsigned char info[128];   /* Informative text string */
		unsigned char spare[292];  /* Boot information etc. */
		unsigned short rspeed;     /* Disk rotational speed */
		unsigned short pcylcount;  /* Physical cylinder count */
		unsigned short sparecyl;   /* extra sects per cylinder */
		unsigned char spare2[4];   /* More magic... */
		unsigned short ilfact;     /* Interleave factor */
		unsigned short ncyl;       /* Data cylinder count */
		unsigned short nacyl;      /* Alt. cylinder count */
		unsigned short ntrks;      /* Tracks per cylinder */
		unsigned short nsect;      /* Sectors per track */
		unsigned char spare3[4];   /* Even more magic... */
		struct sun_partition {
			__u32 start_cylinder;
			__u32 num_sectors;
		} partitions[8];
		unsigned short magic;      /* Magic number */
		unsigned short csum;       /* Label xor'd checksum */
	} * label;		
	struct sun_partition *p;
	unsigned long spc;

	if(!(bh = bread(dev, 0, get_ptable_blocksize(dev)))) {
		if (warn_no_part) printk(KERN_WARNING "Dev %s: unable to read partition table\n",
		       kdevname(dev));
		return -1;
	}
	label = (struct sun_disklabel *) bh->b_data;
	p = label->partitions;
	if (be16_to_cpu(label->magic) != SUN_LABEL_MAGIC) {
/*		printk(KERN_INFO "Dev %s Sun disklabel: bad magic %04x\n",
		       kdevname(dev), be16_to_cpu(label->magic)); */
		brelse(bh);
		return 0;
	}
	/* Look at the checksum */
	ush = ((unsigned short *) (label+1)) - 1;
	for(csum = 0; ush >= ((unsigned short *) label);)
		csum ^= *ush--;
	if(csum) {
		printk("Dev %s Sun disklabel: Csum bad, label corrupted\n",
		       kdevname(dev));
		brelse(bh);
		return 0;
	}
	/* All Sun disks have 8 partition entries */
	spc = be16_to_cpu(label->ntrks) * be16_to_cpu(label->nsect);
	for(i=0; i < 8; i++, p++) {
		unsigned long st_sector;
		int num_sectors;

		st_sector = first_sector + be32_to_cpu(p->start_cylinder) * spc;
		num_sectors = be32_to_cpu(p->num_sectors);
		if (num_sectors)
			add_gd_partition(hd, first_part_minor, st_sector, num_sectors);
		first_part_minor++;
	}
	printk("\n");
	brelse(bh);
	return 1;
}

