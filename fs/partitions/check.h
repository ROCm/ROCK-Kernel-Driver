/*
 * add_partition adds a partitions details to the devices partition
 * description.
 */
void add_gd_partition(struct gendisk *hd, int minor, int start, int size);

/*
 * Get the default block size for this device
 */
unsigned int get_ptable_blocksize(kdev_t dev);

extern int warn_no_part;
