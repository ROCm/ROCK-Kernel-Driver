#include <linux/pagemap.h>
#include <linux/blkdev.h>

/*
 * add_gd_partition adds a partitions details to the devices partition
 * description.
 */
void add_gd_partition(struct gendisk *hd, int minor, int start, int size);

extern int warn_no_part;
