/*
 *  fs/partitions/ultrix.h
 */

int ultrix_partition(struct gendisk *hd, kdev_t dev,
                     unsigned long first_sector, int first_part_minor);

