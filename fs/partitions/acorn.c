/*
 *  linux/fs/partitions/acorn.c
 *
 *  Copyright (c) 1996-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Scan ADFS partitions on hard disk drives.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/genhd.h>
#include <linux/fs.h>

#include "check.h"
#include "acorn.h"

static void
adfspart_setgeometry(kdev_t dev, unsigned int secspertrack, unsigned int heads,
		     unsigned long totalblocks)
{
	extern void xd_set_geometry(kdev_t dev, unsigned char, unsigned char,
				    unsigned long, unsigned int);

#ifdef CONFIG_BLK_DEV_MFM
	if (MAJOR(dev) == MFM_ACORN_MAJOR)
		xd_set_geometry(dev, secspertrack, heads, totalblocks, 1);
#endif
}

static struct adfs_discrecord *
adfs_partition(struct gendisk *hd, char *name, char *data,
	       unsigned long first_sector, int minor)
{
	struct adfs_discrecord *dr;
	unsigned int nr_sects;

	if (adfs_checkbblk(data))
		return NULL;

	dr = (struct adfs_discrecord *)(data + 0x1c0);

	if (dr->disc_size == 0 && dr->disc_size_high == 0)
		return NULL;

	nr_sects = (le32_to_cpu(dr->disc_size_high) << 23) |
		   (le32_to_cpu(dr->disc_size) >> 9);

	if (name)
		printk(" [%s]", name);
	add_gd_partition(hd, minor, first_sector, nr_sects);
	return dr;
}

#ifdef CONFIG_ACORN_PARTITION_RISCIX
static int
riscix_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sect,
		 int minor, unsigned long nr_sects)
{
	struct buffer_head *bh;
	struct riscix_record *rr;
	unsigned int riscix_minor;
	
	if(get_ptable_blocksize(dev)!=1024)
		return 0;

	printk(" [RISCiX]");

	add_gd_partition(hd, riscix_minor = minor++, first_sect, nr_sects);
	hd->sizes[riscix_minor] = hd->part[riscix_minor].nr_sects >>
					(BLOCK_SIZE_BITS - 9);
	dev = MKDEV(hd->major, riscix_minor);

	if (!(bh = bread(dev, 0, 1024)))
		return -1;

	rr = (struct riscix_record *)bh->b_data;
	if (rr->magic == RISCIX_MAGIC) {
		int part;

		printk(" <");

		for (part = 0; part < 8; part++) {
			if (rr->part[part].one &&
			    memcmp(rr->part[part].name, "All\0", 4)) {
				add_gd_partition(hd, minor++,
						le32_to_cpu(rr->part[part].start),
						le32_to_cpu(rr->part[part].length));
				printk("(%s)", rr->part[part].name);
			}
		}

		printk(" >\n");

		if (hd->part[riscix_minor].nr_sects > 2)
			hd->part[riscix_minor].nr_sects = 2;
	}

	brelse(bh);
	return minor;
}
#endif

static int
linux_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sect,
		int minor, unsigned long nr_sects)
{
	struct buffer_head *bh;
	struct linux_part *linuxp;
	unsigned int linux_minor, mask = (1 << hd->minor_shift) - 1;

	if(get_ptable_blocksize(dev)!=1024)
		return 0;
		
	printk(" [Linux]");

	add_gd_partition(hd, linux_minor = minor++, first_sect, nr_sects);
	hd->sizes[linux_minor] = hd->part[linux_minor].nr_sects >>
					(BLOCK_SIZE_BITS - 9);
	dev = MKDEV(hd->major, linux_minor);

	if (!(bh = bread(dev, 0, 1024)))
		return -1;

	linuxp = (struct linux_part *)bh->b_data;
	printk(" <");
	while (linuxp->magic == cpu_to_le32(LINUX_NATIVE_MAGIC) ||
	       linuxp->magic == cpu_to_le32(LINUX_SWAP_MAGIC)) {
		if (!(minor & mask))
			break;
		add_gd_partition(hd, minor++, first_sect +
				 le32_to_cpu(linuxp->start_sect),
				 le32_to_cpu(linuxp->nr_sects));
		linuxp ++;
	}
	printk(" >");
	/*
	 * Prevent someone doing a mkswap or mkfs on this partition
	 */
	if(hd->part[linux_minor].nr_sects > 2)
		hd->part[linux_minor].nr_sects = 2;

	brelse(bh);
	return minor;
}

#ifdef CONFIG_ACORN_PARTITION_CUMANA
static int
adfspart_check_CUMANA(struct gendisk *hd, kdev_t dev,
		      unsigned long first_sector, int minor)
{
	unsigned int start_blk = 0, mask = (1 << hd->minor_shift) - 1;
	struct buffer_head *bh = NULL;
	char *name = "CUMANA/ADFS";
	int first = 1;

	if(get_ptable_blocksize(dev)!=1024)
		return 0;

	/*
	 * Try Cumana style partitions - sector 3 contains ADFS boot block
	 * with pointer to next 'drive'.
	 *
	 * There are unknowns in this code - is the 'cylinder number' of the
	 * next partition relative to the start of this one - I'm assuming
	 * it is.
	 *
	 * Also, which ID did Cumana use?
	 *
	 * This is totally unfinished, and will require more work to get it
	 * going. Hence it is totally untested.
	 */
	do {
		struct adfs_discrecord *dr;
		unsigned int nr_sects;

		if (!(minor & mask))
			break;

		if (!(bh = bread(dev, start_blk + 3, 1024)))
			return -1;

		dr = adfs_partition(hd, name, bh->b_data,
				    first_sector, minor++);
		if (!dr)
			break;
		name = NULL;

		nr_sects = (bh->b_data[0x1fd] + (bh->b_data[0x1fe] << 8)) *
			   (dr->heads + (dr->lowsector & 0x40 ? 1 : 0)) *
			   dr->secspertrack;

		if (!nr_sects)
			break;

		first = 0;
		first_sector += nr_sects;
		start_blk += nr_sects >> (BLOCK_SIZE_BITS - 9);
		nr_sects = 0; /* hmm - should be partition size */

		switch (bh->b_data[0x1fc] & 15) {
		case 0: /* No partition / ADFS? */
			break;

#ifdef CONFIG_ACORN_PARTITION_RISCIX
		case PARTITION_RISCIX_SCSI:
			/* RISCiX - we don't know how to find the next one. */
			minor = riscix_partition(hd, dev, first_sector,
						 minor, nr_sects);
			break;
#endif

		case PARTITION_LINUX:
			minor = linux_partition(hd, dev, first_sector,
						minor, nr_sects);
			break;
		}
		brelse(bh);
		bh = NULL;
		if (minor == -1)
			return minor;
	} while (1);
	if (bh)
		bforget(bh);
	return first ? 0 : 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_ADFS
/*
 * Purpose: allocate ADFS partitions.
 *
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 *	    first_sector- first readable sector on the device.
 *	    minor	- first available minor on device.
 *
 * Returns: -1 on error, 0 for no ADFS boot sector, 1 for ok.
 *
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition on first drive.
 *	    hda2 = non-ADFS partition.
 */
static int
adfspart_check_ADFS(struct gendisk *hd, kdev_t dev,
		   unsigned long first_sector, int minor)
{
	unsigned long start_sect, nr_sects, sectscyl, heads;
	struct buffer_head *bh;
	struct adfs_discrecord *dr;

	if(get_ptable_blocksize(dev)!=1024)
		return 0;

	if (!(bh = bread(dev, 3, 1024)))
		return -1;

	dr = adfs_partition(hd, "ADFS", bh->b_data, first_sector, minor++);
	if (!dr) {
		bforget(bh);
    		return 0;
	}

	heads = dr->heads + ((dr->lowsector >> 6) & 1);
	adfspart_setgeometry(dev, dr->secspertrack, heads,
			     hd->part[MINOR(dev)].nr_sects);
	sectscyl = dr->secspertrack * heads;

	/*
	 * Work out start of non-adfs partition.
	 */
	start_sect = ((bh->b_data[0x1fe] << 8) + bh->b_data[0x1fd]) * sectscyl;
	nr_sects = hd->part[MINOR(dev)].nr_sects - start_sect;

	if (start_sect) {
		first_sector += start_sect;

		switch (bh->b_data[0x1fc] & 15) {
#ifdef CONFIG_ACORN_PARTITION_RISCIX
		case PARTITION_RISCIX_SCSI:
		case PARTITION_RISCIX_MFM:
			minor = riscix_partition(hd, dev, first_sector,
						 minor, nr_sects);
			break;
#endif

		case PARTITION_LINUX:
			minor = linux_partition(hd, dev, first_sector,
						minor, nr_sects);
			break;
		}
	}
	brelse(bh);
	return 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_ICS
static int adfspart_check_ICSLinux(kdev_t dev, unsigned long block)
{
	struct buffer_head *bh;
	unsigned int offset = block & 1 ? 512 : 0;
	int result = 0;

	bh = bread(dev, block >> 1, 1024);

	if (bh != NULL) {
		if (memcmp(bh->b_data + offset, "LinuxPart", 9) == 0)
			result = 1;

		brelse(bh);
	}

	return result;
}

/*
 * Purpose: allocate ICS partitions.
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 *	    first_sector- first readable sector on the device.
 *	    minor	- first available minor on device.
 * Returns: -1 on error, 0 for no ICS table, 1 for partitions ok.
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition 0 on first drive.
 *	    hda2 = ADFS partition 1 on first drive.
 *		..etc..
 */
static int
adfspart_check_ICS(struct gendisk *hd, kdev_t dev,
		   unsigned long first_sector, int minor)
{
	struct buffer_head *bh;
	unsigned long sum;
	unsigned int i, mask = (1 << hd->minor_shift) - 1;
	struct ics_part *p;

	if(get_ptable_blocksize(dev)!=1024)
		return 0;
		
	/*
	 * Try ICS style partitions - sector 0 contains partition info.
	 */
	if (!(bh = bread(dev, 0, 1024)))
	    	return -1;

	/*
	 * check for a valid checksum
	 */
	for (i = 0, sum = 0x50617274; i < 508; i++)
		sum += bh->b_data[i];

	sum -= le32_to_cpu(*(__u32 *)(&bh->b_data[508]));
	if (sum) {
	    	bforget(bh);
		return 0; /* not ICS partition table */
	}

	printk(" [ICS]");

	for (p = (struct ics_part *)bh->b_data; p->size; p++) {
		unsigned long start;
		long size;

		if ((minor & mask) == 0)
			break;

		start = le32_to_cpu(p->start);
		size  = le32_to_cpu(p->size);

		if (size < 0) {
			size = -size;

			/*
			 * We use the first sector to identify what type
			 * this partition is...
			 */
			if (size > 1 && adfspart_check_ICSLinux(dev, start)) {
				start += 1;
				size -= 1;
			}
		}

		if (size) {
			add_gd_partition(hd, minor, first_sector + start, size);
			minor++;
		}
	}

	brelse(bh);
	return 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_POWERTEC
/*
 * Purpose: allocate ICS partitions.
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 *	    first_sector- first readable sector on the device.
 *	    minor	- first available minor on device.
 * Returns: -1 on error, 0 for no ICS table, 1 for partitions ok.
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition 0 on first drive.
 *	    hda2 = ADFS partition 1 on first drive.
 *		..etc..
 */
static int
adfspart_check_POWERTEC(struct gendisk *hd, kdev_t dev,
			unsigned long first_sector, int minor)
{
	struct buffer_head *bh;
	struct ptec_partition *p;
	unsigned char checksum;
	int i;

	if (!(bh = bread(dev, 0, 1024)))
		return -1;

	for (checksum = 0x2a, i = 0; i < 511; i++)
		checksum += bh->b_data[i];

	if (checksum != bh->b_data[511]) {
		bforget(bh);
		return 0;
	}

	printk(" [POWERTEC]");

	for (i = 0, p = (struct ptec_partition *)bh->b_data; i < 12; i++, p++) {
		unsigned long start;
		unsigned long size;

		start = le32_to_cpu(p->start);
		size  = le32_to_cpu(p->size);

		if (size)
			add_gd_partition(hd, minor, first_sector + start,
					 size);
		minor++;
	}

	brelse(bh);
	return 1;
}
#endif

static int (*partfn[])(struct gendisk *, kdev_t, unsigned long, int) = {
#ifdef CONFIG_ACORN_PARTITION_ICS
	adfspart_check_ICS,
#endif
#ifdef CONFIG_ACORN_PARTITION_CUMANA
	adfspart_check_CUMANA,
#endif
#ifdef CONFIG_ACORN_PARTITION_ADFS
	adfspart_check_ADFS,
#endif
#ifdef CONFIG_ACORN_PARTITION_POWERTEC
	adfspart_check_POWERTEC,
#endif
	NULL
};
/*
 * Purpose: initialise all the partitions on an ADFS drive.
 *          These may be other ADFS partitions or a Linux/RiscBSD/RISCiX
 *	    partition.
 *
 * Params : hd		- pointer to gendisk structure
 *          dev		- device number to access
 *	    first_sect  - first available sector on the disk.
 *	    first_minor	- first available minor on this device.
 *
 * Returns: -1 on error, 0 if not ADFS format, 1 if ok.
 */
int acorn_partition(struct gendisk *hd, kdev_t dev,
		    unsigned long first_sect, int first_minor)
{
	int r = 0, i;

	for (i = 0; partfn[i] && r == 0; i++)
		r = partfn[i](hd, dev, first_sect, first_minor);

	if (r < 0 && warn_no_part)
		printk(" unable to read boot sectors / partition sectors\n");
	if (r > 0)
		printk("\n");
	return r;
}
