#include <linux/pagemap.h>
#include <linux/blkdev.h>

/*
 * add_gd_partition adds a partitions details to the devices partition
 * description.
 */
void add_gd_partition(struct gendisk *hd, int minor, int start, int size);

/*
 * check_and_add_subpartition does the same for subpartitions
 */
int check_and_add_subpartition(struct gendisk *hd, int super_minor,
			       int minor, int sub_start, int sub_size);

extern int warn_no_part;
