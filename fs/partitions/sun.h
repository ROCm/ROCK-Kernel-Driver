/*
 *  fs/partitions/sun.h
 */

#define SUN_LABEL_MAGIC          0xDABE

int sun_partition(struct gendisk *hd, kdev_t dev,
		  unsigned long first_sector, int first_part_minor);

