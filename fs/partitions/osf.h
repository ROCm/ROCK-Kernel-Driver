/*
 *  fs/partitions/osf.h
 */

#define DISKLABELMAGIC (0x82564557UL)

int osf_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector,
		  int current_minor);

