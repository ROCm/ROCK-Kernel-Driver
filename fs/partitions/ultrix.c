/*
 *  fs/partitions/ultrix.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Re-organised Jul 1999 Russell King
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/blk.h>

#include "check.h"

int ultrix_partition(struct gendisk *hd, kdev_t dev,
                            unsigned long first_sector, int first_part_minor)
{
	int i;
	struct buffer_head *bh;
	struct ultrix_disklabel {
		s32	pt_magic;	/* magic no. indicating part. info exits */
		s32	pt_valid;	/* set by driver if pt is current */
		struct  pt_info {
			s32		pi_nblocks; /* no. of sectors */
			u32		pi_blkoff;  /* block offset for start */
		} pt_part[8];
	} *label;

#define PT_MAGIC	0x032957	/* Partition magic number */
#define PT_VALID	1		/* Indicates if struct is valid */

#define	SBLOCK	((unsigned long)((16384 - sizeof(struct ultrix_disklabel)) \
                  /get_ptable_blocksize(dev)))

	bh = bread (dev, SBLOCK, get_ptable_blocksize(dev));
	if (!bh) {
		if (warn_no_part) printk (" unable to read block 0x%lx\n", SBLOCK);
		return -1;
	}
	
	label = (struct ultrix_disklabel *)(bh->b_data
                                            + get_ptable_blocksize(dev)
                                            - sizeof(struct ultrix_disklabel));

	if (label->pt_magic == PT_MAGIC && label->pt_valid == PT_VALID) {
		for (i=0; i<8; i++, first_part_minor++)
			if (label->pt_part[i].pi_nblocks)
				add_gd_partition(hd, first_part_minor, 
					      label->pt_part[i].pi_blkoff,
					      label->pt_part[i].pi_nblocks);
		brelse(bh);
		printk ("\n");
		return 1;
	} else {
		brelse(bh);
		return 0;
	}
}
