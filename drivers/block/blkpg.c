/*
 * Partition table and disk geometry handling
 *
 * This obsoletes the partition-handling code in genhd.c:
 * Userspace can look at a disk in arbitrary format and tell
 * the kernel what partitions there are on the disk, and how
 * these should be numbered.
 * It also allows one to repartition a disk that is being used.
 *
 * A single ioctl with lots of subfunctions:
 *
 * Device number stuff:
 *    get_whole_disk()          (given the device number of a partition, find
 *                               the device number of the encompassing disk)
 *    get_all_partitions()      (given the device number of a disk, return the
 *                               device numbers of all its known partitions)
 *
 * Partition stuff:
 *    add_partition()
 *    delete_partition()
 *    test_partition_in_use()   (also for test_disk_in_use)
 *
 * Geometry stuff:
 *    get_geometry()
 *    set_geometry()
 *    get_bios_drivedata()
 *
 * For today, only the partition stuff - aeb, 990515
 */

#include <linux/errno.h>
#include <linux/fs.h>			/* for BLKROSET, ... */
#include <linux/sched.h>		/* for capable() */
#include <linux/blk.h>			/* for set_device_ro() */
#include <linux/blkpg.h>
#include <linux/genhd.h>
#include <linux/module.h>               /* for EXPORT_SYMBOL */
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>

#include <asm/uaccess.h>

/*
 * What is the data describing a partition?
 *
 * 1. a device number (kdev_t)
 * 2. a starting sector and number of sectors (hd_struct)
 *    given in the part[] array of the gendisk structure for the drive.
 *
 * The number of sectors is replicated in the sizes[] array of
 * the gendisk structure for the major, which again is copied to
 * the blk_size[][] array.
 * (However, hd_struct has the number of 512-byte sectors,
 *  g->sizes[] and blk_size[][] have the number of 1024-byte blocks.)
 * Note that several drives may have the same major.
 */

/*
 * Add a partition.
 *
 * returns: EINVAL: bad parameters
 *          ENXIO: cannot find drive
 *          EBUSY: proposed partition overlaps an existing one
 *                 or has the same number as an existing one
 *          0: all OK.
 */
int add_partition(struct block_device *bdev, struct blkpg_partition *p)
{
	struct gendisk *g;
	long long ppstart, pplength;
	long pstart, plength;
	int i;
	kdev_t dev = to_kdev_t(bdev->bd_dev);
	struct hd_struct *part;

	/* convert bytes to sectors, check for fit in a hd_struct */
	ppstart = (p->start >> 9);
	pplength = (p->length >> 9);
	pstart = ppstart;
	plength = pplength;
	if (pstart != ppstart || plength != pplength
	    || pstart < 0 || plength < 0)
		return -EINVAL;

	/* find the drive major */
	g = get_gendisk(dev);
	if (!g)
		return -ENXIO;
	part = g->part + minor(dev);

	/* existing drive? */

	/* drive and partition number OK? */
	if (bdev != bdev->bd_contains)
		return -EINVAL;
	if (p->pno <= 0 || p->pno >= (1 << g->minor_shift))
		return -EINVAL;

	/* partition number in use? */
	if (part[p->pno].nr_sects != 0)
		return -EBUSY;

	/* overlap? */
	for (i = 1; i < (1<<g->minor_shift); i++)
		if (!(pstart+plength <= part[i].start_sect ||
		      pstart >= part[i].start_sect + part[i].nr_sects))
			return -EBUSY;

	/* all seems OK */
	part[p->pno].start_sect = pstart;
	part[p->pno].nr_sects = plength;
	if (g->sizes)
		g->sizes[minor(dev)+p->pno] = (plength >> (BLOCK_SIZE_BITS-9));
	devfs_register_partitions (g, minor(dev), 0);
	return 0;
}

/*
 * Delete a partition given by partition number
 *
 * returns: EINVAL: bad parameters
 *          ENXIO: cannot find partition
 *          EBUSY: partition is busy
 *          0: all OK.
 *
 * Note that the dev argument refers to the entire disk, not the partition.
 */
int del_partition(struct block_device *bdev, struct blkpg_partition *p)
{
	kdev_t dev = to_kdev_t(bdev->bd_dev);
	struct gendisk *g;
	struct block_device *bdevp;
	struct hd_struct *part;
	int holder;

	/* find the drive major */
	g = get_gendisk(dev);
	if (!g)
		return -ENXIO;
	part = g->part + minor(dev);

	if (bdev != bdev->bd_contains)
		return -EINVAL;
	if (p->pno <= 0 || p->pno >= (1 << g->minor_shift))
  		return -EINVAL;

	/* existing drive and partition? */
	if (part[p->pno].nr_sects == 0)
		return -ENXIO;

	/* partition in use? Incomplete check for now. */
	bdevp = bdget(MKDEV(major(dev), minor(dev) + p->pno));
	if (!bdevp)
		return -ENOMEM;
	if (bd_claim(bdevp, &holder) < 0) {
		bdput(bdevp);
		return -EBUSY;
	}

	/* all seems OK */
	fsync_bdev(bdevp);
	invalidate_bdev(bdevp, 0);

	part[p->pno].start_sect = 0;
	part[p->pno].nr_sects = 0;
	if (g->sizes)
		g->sizes[minor(dev) + p->pno] = 0;
	devfs_register_partitions (g, minor(dev), 0);
	bd_release(bdevp);
	bdput(bdevp);

	return 0;
}

int blkpg_ioctl(struct block_device *bdev, struct blkpg_ioctl_arg *arg)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;
	int len;

	if (copy_from_user(&a, arg, sizeof(struct blkpg_ioctl_arg)))
		return -EFAULT;

	switch (a.op) {
		case BLKPG_ADD_PARTITION:
		case BLKPG_DEL_PARTITION:
			len = a.datalen;
			if (len < sizeof(struct blkpg_partition))
				return -EINVAL;
			if (copy_from_user(&p, a.data, sizeof(struct blkpg_partition)))
				return -EFAULT;
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (a.op == BLKPG_ADD_PARTITION)
				return add_partition(bdev, &p);
			else
				return del_partition(bdev, &p);
		default:
			return -EINVAL;
	}
}

/*
 * Common ioctl's for block devices
 */
int blk_ioctl(struct block_device *bdev, unsigned int cmd, unsigned long arg)
{
	request_queue_t *q;
	u64 ullval = 0;
	int intval;
	unsigned short usval;
	kdev_t dev = to_kdev_t(bdev->bd_dev);
	int holder;
	struct backing_dev_info *bdi;

	switch (cmd) {
		case BLKROSET:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (get_user(intval, (int *)(arg)))
				return -EFAULT;
			set_device_ro(dev, intval);
			return 0;
		case BLKROGET:
			intval = (bdev_read_only(bdev) != 0);
			return put_user(intval, (int *)(arg));

		case BLKRASET:
		case BLKFRASET:
			if(!capable(CAP_SYS_ADMIN))
				return -EACCES;
			bdi = blk_get_backing_dev_info(bdev);
			if (bdi == NULL)
				return -ENOTTY;
			bdi->ra_pages = (arg * 512) / PAGE_CACHE_SIZE;
			return 0;

		case BLKRAGET:
		case BLKFRAGET:
			if (!arg)
				return -EINVAL;
			bdi = blk_get_backing_dev_info(bdev);
			if (bdi == NULL)
				return -ENOTTY;
			return put_user((bdi->ra_pages * PAGE_CACHE_SIZE) / 512,
						(long *)arg);

		case BLKSECTGET:
			if ((q = bdev_get_queue(bdev)) == NULL)
				return -EINVAL;

			usval = q->max_sectors;
			blk_put_queue(q);
			return put_user(usval, (unsigned short *)arg);

		case BLKFLSBUF:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			fsync_bdev(bdev);
			invalidate_bdev(bdev, 0);
			return 0;

		case BLKSSZGET:
			/* get block device hardware sector size */
			intval = bdev_hardsect_size(bdev);
			return put_user(intval, (int *) arg);

		case BLKGETSIZE:
			/* size in sectors, works up to 2 TB */
			ullval = bdev->bd_inode->i_size;
			return put_user((unsigned long)(ullval >> 9), (unsigned long *) arg);
		case BLKGETSIZE64:
			/* size in bytes */
			ullval = bdev->bd_inode->i_size;
			return put_user(ullval, (u64 *) arg);
#if 0
		case BLKRRPART: /* Re-read partition tables */
			if (!capable(CAP_SYS_ADMIN)) 
				return -EACCES;
			return reread_partitions(dev, 1);
#endif

		case BLKPG:
			return blkpg_ioctl(bdev, (struct blkpg_ioctl_arg *) arg);
		case BLKBSZGET:
			/* get the logical block size (cf. BLKSSZGET) */
			intval = block_size(bdev);
			return put_user(intval, (int *) arg);

		case BLKBSZSET:
			/* set the logical block size */
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (!arg)
				return -EINVAL;
			if (get_user(intval, (int *) arg))
				return -EFAULT;
			if (intval > PAGE_SIZE || intval < 512 ||
			    (intval & (intval - 1)))
				return -EINVAL;
			if (bd_claim(bdev, &holder) < 0)
				return -EBUSY;
			set_blocksize(bdev, intval);
			bd_release(bdev);
			return 0;

		default:
			return -EINVAL;
	}
}
