/*
 * MTD partitioning layer definitions
 *
 * (C) 2000 Nicolas Pitre <nico@cam.org>
 *
 * This code is GPL
 *
 * $Id: partitions.h,v 1.3 2000/11/10 23:35:12 nico Exp $
 */

#ifndef MTD_PARTITIONS_H
#define MTD_PARTITIONS_H

#include <linux/types.h>


/*
 * Partition definition structure:
 * 
 * An array of struct partition is passed along with a MTD object to
 * add_mtd_partitions() to create them.
 *
 * For each partition, these fields are available:
 * name: string that will be used to label the partition's MTD device.
 * size: the partition size; if 0, the partition will extend to the end of the 
 * 	master MTD device.
 * offset: absolute starting position within the master MTD device; if 0,
 * 	partition will start where the previous one ended.
 * mask_flags: contains flags that have to be masked (removed) from the 
 * 	master MTD flag set for the corresponding MTD partition.
 * 	For example, to force a read-only partition, simply adding 
 * 	MTD_WRITEABLE to the mask_flags will do the trick.
 *
 * Note: writeable partitions require their size and offset be 
 * erasesize aligned.
 */ 

struct mtd_partition {
	char *name;		/* identifier string */
	u_long size;		/* partition size */
	u_long offset;		/* offset within the master MTD space */
	u_long mask_flags;	/* master MTD flags to mask out for this partition */
};


int add_mtd_partitions(struct mtd_info *, struct mtd_partition *, int);
int del_mtd_partitions(struct mtd_info *);

#endif

