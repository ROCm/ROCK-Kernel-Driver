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
#include <linux/buffer_head.h>

#include "check.h"
#include "acorn.h"

static struct adfs_discrecord *
adfs_partition(struct parsed_partitions *state, char *name, char *data,
	       unsigned long first_sector, int slot)
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
	put_partition(state, slot, first_sector, nr_sects);
	return dr;
}

#ifdef CONFIG_ACORN_PARTITION_RISCIX
static int
riscix_partition(struct parsed_partitions *state, struct block_device *bdev,
		unsigned long first_sect, int slot, unsigned long nr_sects)
{
	Sector sect;
	struct riscix_record *rr;
	
	rr = (struct riscix_record *)read_dev_sector(bdev, first_sect, &sect);
	if (!rr)
		return -1;

	printk(" [RISCiX]");


	if (rr->magic == RISCIX_MAGIC) {
		unsigned long size = nr_sects > 2 ? 2 : nr_sects;
		int part;

		printk(" <");

		put_partition(state, slot++, first_sect, size);
		for (part = 0; part < 8; part++) {
			if (rr->part[part].one &&
			    memcmp(rr->part[part].name, "All\0", 4)) {
				put_partition(state, slot++,
					le32_to_cpu(rr->part[part].start),
					le32_to_cpu(rr->part[part].length));
				printk("(%s)", rr->part[part].name);
			}
		}

		printk(" >\n");
	} else {
		put_partition(state, slot++, first_sect, nr_sects);
	}

	put_dev_sector(sect);
	return slot;
}
#endif

static int
linux_partition(struct parsed_partitions *state, struct block_device *bdev,
		unsigned long first_sect, int slot, unsigned long nr_sects)
{
	Sector sect;
	struct linux_part *linuxp;
	unsigned long size = nr_sects > 2 ? 2 : nr_sects;

	printk(" [Linux]");

	put_partition(state, slot++, first_sect, size);

	linuxp = (struct linux_part *)read_dev_sector(bdev, first_sect, &sect);
	if (!linuxp)
		return -1;

	printk(" <");
	while (linuxp->magic == cpu_to_le32(LINUX_NATIVE_MAGIC) ||
	       linuxp->magic == cpu_to_le32(LINUX_SWAP_MAGIC)) {
		if (slot == state->limit)
			break;
		put_partition(state, slot++, first_sect +
				 le32_to_cpu(linuxp->start_sect),
				 le32_to_cpu(linuxp->nr_sects));
		linuxp ++;
	}
	printk(" >");

	put_dev_sector(sect);
	return slot;
}

#ifdef CONFIG_ACORN_PARTITION_CUMANA
static int
adfspart_check_CUMANA(struct parsed_partitions *state, struct block_device *bdev)
{
	unsigned long first_sector = 0;
	unsigned int start_blk = 0;
	Sector sect;
	unsigned char *data;
	char *name = "CUMANA/ADFS";
	int first = 1;
	int slot = 1;

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

		data = read_dev_sector(bdev, start_blk * 2 + 6, &sect);
		if (!data)
			return -1;

		if (slot == state->limit)
			break;

		dr = adfs_partition(state, name, data, first_sector, slot++);
		if (!dr)
			break;

		name = NULL;

		nr_sects = (data[0x1fd] + (data[0x1fe] << 8)) *
			   (dr->heads + (dr->lowsector & 0x40 ? 1 : 0)) *
			   dr->secspertrack;

		if (!nr_sects)
			break;

		first = 0;
		first_sector += nr_sects;
		start_blk += nr_sects >> (BLOCK_SIZE_BITS - 9);
		nr_sects = 0; /* hmm - should be partition size */

		switch (data[0x1fc] & 15) {
		case 0: /* No partition / ADFS? */
			break;

#ifdef CONFIG_ACORN_PARTITION_RISCIX
		case PARTITION_RISCIX_SCSI:
			/* RISCiX - we don't know how to find the next one. */
			slot = riscix_partition(state, bdev, first_sector,
						 slot, nr_sects);
			break;
#endif

		case PARTITION_LINUX:
			slot = linux_partition(state, bdev, first_sector,
						slot, nr_sects);
			break;
		}
		put_dev_sector(sect);
		if (slot == -1)
			return -1;
	} while (1);
	put_dev_sector(sect);
	return first ? 0 : 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_ADFS
/*
 * Purpose: allocate ADFS partitions.
 *
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 *
 * Returns: -1 on error, 0 for no ADFS boot sector, 1 for ok.
 *
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition on first drive.
 *	    hda2 = non-ADFS partition.
 */
static int
adfspart_check_ADFS(struct parsed_partitions *state, struct block_device *bdev)
{
	unsigned long start_sect, nr_sects, sectscyl, heads;
	Sector sect;
	unsigned char *data;
	struct adfs_discrecord *dr;
	unsigned char id;
	int slot = 1;

	data = read_dev_sector(bdev, 6, &sect);
	if (!data)
		return -1;

	dr = adfs_partition(state, "ADFS", data, 0, slot++);
	if (!dr) {
		put_dev_sector(sect);
    		return 0;
	}

	heads = dr->heads + ((dr->lowsector >> 6) & 1);
	sectscyl = dr->secspertrack * heads;
	start_sect = ((data[0x1fe] << 8) + data[0x1fd]) * sectscyl;
	id = data[0x1fc] & 15;
	put_dev_sector(sect);

#ifdef CONFIG_BLK_DEV_MFM
	if (MAJOR(bdev->bd_dev) == MFM_ACORN_MAJOR) {
		extern void xd_set_geometry(struct block_device *,
			unsigned char, unsigned char, unsigned int);
		xd_set_geometry(bdev, dr->secspertrack, heads, 1);
		invalidate_bdev(bdev, 1);
		truncate_inode_pages(bdev->bd_inode->i_mapping, 0);
	}
#endif

	/*
	 * Work out start of non-adfs partition.
	 */
	nr_sects = (bdev->bd_inode->i_size >> 9) - start_sect;

	if (start_sect) {
		switch (id) {
#ifdef CONFIG_ACORN_PARTITION_RISCIX
		case PARTITION_RISCIX_SCSI:
		case PARTITION_RISCIX_MFM:
			slot = riscix_partition(state, bdev, start_sect,
						 slot, nr_sects);
			break;
#endif

		case PARTITION_LINUX:
			slot = linux_partition(state, bdev, start_sect,
						slot, nr_sects);
			break;
		}
	}
	return 1;
}
#endif

#ifdef CONFIG_ACORN_PARTITION_ICS
static int adfspart_check_ICSLinux(struct block_device *bdev, unsigned long block)
{
	Sector sect;
	unsigned char *data = read_dev_sector(bdev, block, &sect);
	int result = 0;

	if (data) {
		if (memcmp(data, "LinuxPart", 9) == 0)
			result = 1;
		put_dev_sector(sect);
	}

	return result;
}

/*
 * Purpose: allocate ICS partitions.
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 * Returns: -1 on error, 0 for no ICS table, 1 for partitions ok.
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition 0 on first drive.
 *	    hda2 = ADFS partition 1 on first drive.
 *		..etc..
 */
static int
adfspart_check_ICS(struct parsed_partitions *state, struct block_device *bdev)
{
	Sector sect;
	unsigned char *data;
	unsigned long sum;
	unsigned int i;
	int slot;
	struct ics_part *p;

	/*
	 * Try ICS style partitions - sector 0 contains partition info.
	 */
	data = read_dev_sector(bdev, 0, &sect);
	if (!data)
	    	return -1;

	/*
	 * check for a valid checksum
	 */
	for (i = 0, sum = 0x50617274; i < 508; i++)
		sum += data[i];

	sum -= le32_to_cpu(*(__u32 *)(&data[508]));
	if (sum) {
	    	put_dev_sector(sect);
		return 0; /* not ICS partition table */
	}

	printk(" [ICS]");

	for (slot = 1, p = (struct ics_part *)data; p->size; p++) {
		u32 start = le32_to_cpu(p->start);
		u32 size = le32_to_cpu(p->size);

		if (slot == state->limit)
			break;

		if (size < 0) {
			size = -size;

			/*
			 * We use the first sector to identify what type
			 * this partition is...
			 */
			if (size > 1 && adfspart_check_ICSLinux(bdev, start)) {
				start += 1;
				size -= 1;
			}
		}

		if (size)
			put_partition(state, slot++, start, size);
	}

	put_dev_sector(sect);
	return 1;
}
#endif

/*
 * Purpose: allocate ICS partitions.
 * Params : hd		- pointer to gendisk structure to store partition info.
 *	    dev		- device number to access.
 * Returns: -1 on error, 0 for no ICS table, 1 for partitions ok.
 * Alloc  : hda  = whole drive
 *	    hda1 = ADFS partition 0 on first drive.
 *	    hda2 = ADFS partition 1 on first drive.
 *		..etc..
 */
#ifdef CONFIG_ACORN_PARTITION_POWERTEC
static int
adfspart_check_POWERTEC(struct parsed_partitions *state, struct block_device *bdev)
{
	Sector sect;
	unsigned char *data;
	struct ptec_partition *p;
	unsigned char checksum;
	int slot = 1;
	int i;

	data = read_dev_sector(bdev, 0, &sect);
	if (!data)
		return -1;

	for (checksum = 0x2a, i = 0; i < 511; i++)
		checksum += data[i];

	if (checksum != data[511]) {
		put_dev_sector(sect);
		return 0;
	}

	printk(" [POWERTEC]");

	for (i = 0, p = (struct ptec_partition *)data; i < 12; i++, p++) {
		u32 start = le32_to_cpu(p->start);
		u32 size = le32_to_cpu(p->size);

		if (size)
			put_partition(state, slot++, start, size);
	}

	put_dev_sector(sect);
	return 1;
}
#endif

static int (*partfn[])(struct parsed_partitions *, struct block_device *) = {
#ifdef CONFIG_ACORN_PARTITION_ICS
	adfspart_check_ICS,
#endif
#ifdef CONFIG_ACORN_PARTITION_POWERTEC
	adfspart_check_POWERTEC,
#endif
#ifdef CONFIG_ACORN_PARTITION_CUMANA
	adfspart_check_CUMANA,
#endif
#ifdef CONFIG_ACORN_PARTITION_ADFS
	adfspart_check_ADFS,
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
 *
 * Returns: -1 on error, 0 if not ADFS format, 1 if ok.
 */
int acorn_partition(struct parsed_partitions *state, struct block_device *bdev)
{
	int i;

	for (i = 0; partfn[i]; i++) {
		int r = partfn[i](state, bdev);
		if (r) {
			if (r > 0)
				printk("\n");
			return r;
		}
	}
	return 0;
}
