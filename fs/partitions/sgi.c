/*
 *  fs/partitions/sgi.c
 *
 *  Code extracted from drivers/block/genhd.c
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>

#include <asm/byteorder.h>
#include <asm/system.h>

#include "check.h"
#include "sgi.h"

int sgi_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector, int current_minor)
{
	int i, csum, magic;
	unsigned int *ui, start, blocks, cs;
	struct buffer_head *bh;
	struct sgi_disklabel {
		int magic_mushroom;         /* Big fat spliff... */
		short root_part_num;        /* Root partition number */
		short swap_part_num;        /* Swap partition number */
		char boot_file[16];         /* Name of boot file for ARCS */
		unsigned char _unused0[48]; /* Device parameter useless crapola.. */
		struct sgi_volume {
			char name[8];       /* Name of volume */
			int  block_num;     /* Logical block number */
			int  num_bytes;     /* How big, in bytes */
		} volume[15];
		struct sgi_partition {
			int num_blocks;     /* Size in logical blocks */
			int first_block;    /* First logical block */
			int type;           /* Type of this partition */
		} partitions[16];
		int csum;                   /* Disk label checksum */
		int _unused1;               /* Padding */
	} *label;
	struct sgi_partition *p;

	if(!(bh = bread(dev, 0, get_ptable_blocksize(dev)))) {
		if (warn_no_part) printk(KERN_WARNING "Dev %s: unable to read partition table\n", kdevname(dev));
		return -1;
	}
	label = (struct sgi_disklabel *) bh->b_data;
	p = &label->partitions[0];
	magic = label->magic_mushroom;
	if(be32_to_cpu(magic) != SGI_LABEL_MAGIC) {
		/*printk("Dev %s SGI disklabel: bad magic %08x\n",
		       kdevname(dev), magic);*/
		brelse(bh);
		return 0;
	}
	ui = ((unsigned int *) (label + 1)) - 1;
	for(csum = 0; ui >= ((unsigned int *) label);) {
		cs = *ui--;
		csum += be32_to_cpu(cs);
	}
	if(csum) {
		printk(KERN_WARNING "Dev %s SGI disklabel: csum bad, label corrupted\n",
		       kdevname(dev));
		brelse(bh);
		return 0;
	}
	/* All SGI disk labels have 16 partitions, disks under Linux only
	 * have 15 minor's.  Luckily there are always a few zero length
	 * partitions which we don't care about so we never overflow the
	 * current_minor.
	 */
	for(i = 0; i < 16; i++, p++) {
		blocks = be32_to_cpu(p->num_blocks);
		start  = be32_to_cpu(p->first_block);
		if(!blocks)
			continue;
		add_gd_partition(hd, current_minor, start, blocks);
		current_minor++;
	}
	printk("\n");
	brelse(bh);
	return 1;
}
