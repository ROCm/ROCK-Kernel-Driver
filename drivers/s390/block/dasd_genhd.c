/*
 * File...........: linux/drivers/s390/block/dasd_genhd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * gendisk related functions for the dasd driver.
 *
 * $Revision: 1.41 $
 */

#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/blkpg.h>

#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_gendisk:"

#include "dasd_int.h"

/*
 * Allocate and register gendisk structure for device.
 */
int
dasd_gendisk_alloc(struct dasd_device *device)
{
	struct gendisk *gdp;
	int len;

	/* Make sure the minor for this device exists. */
	if (device->devindex >= DASD_PER_MAJOR)
		return -EBUSY;

	gdp = alloc_disk(1 << DASD_PARTN_BITS);
	if (!gdp)
		return -ENOMEM;

	/* Initialize gendisk structure. */
	gdp->major = DASD_MAJOR;
	gdp->first_minor = device->devindex << DASD_PARTN_BITS;
	gdp->fops = &dasd_device_operations;
	gdp->driverfs_dev = &device->cdev->dev;

	/*
	 * Set device name.
	 *   dasda - dasdz : 26 devices
	 *   dasdaa - dasdzz : 676 devices, added up = 702
	 *   dasdaaa - dasdzzz : 17576 devices, added up = 18278
	 */
	len = sprintf(gdp->disk_name, "dasd");
	if (device->devindex > 25) {
		if (device->devindex > 701)
			len += sprintf(gdp->disk_name + len, "%c",
				       'a'+(((device->devindex-702)/676)%26));
		len += sprintf(gdp->disk_name + len, "%c",
			       'a'+(((device->devindex-26)/26)%26));
	}
	len += sprintf(gdp->disk_name + len, "%c", 'a'+(device->devindex%26));

 	sprintf(gdp->devfs_name, "dasd/%s", device->cdev->dev.bus_id);

	if (device->ro_flag)
		set_disk_ro(gdp, 1);
	gdp->private_data = device;
	gdp->queue = device->request_queue;
	device->gdp = gdp;
	set_capacity(device->gdp, 0);
	add_disk(device->gdp);
	return 0;
}

/*
 * Unregister and free gendisk structure for device.
 */
void
dasd_gendisk_free(struct dasd_device *device)
{
	del_gendisk(device->gdp);
	device->gdp->queue = 0;
	put_disk(device->gdp);
	device->gdp = 0;
}

/*
 * Trigger a partition detection.
 */
void
dasd_scan_partitions(struct dasd_device * device)
{
	struct block_device *bdev;

	/* Make the disk known. */
	set_capacity(device->gdp, device->blocks << device->s2b_shift);
	/* See fs/partition/check.c:register_disk,rescan_partitions */
	bdev = bdget_disk(device->gdp, 0);
	if (bdev) {
		if (blkdev_get(bdev, FMODE_READ, 1, BDEV_RAW) >= 0) {
			/* Can't call rescan_partitions directly. Use ioctl. */
			ioctl_by_bdev(bdev, BLKRRPART, 0);
			blkdev_put(bdev, BDEV_RAW);
		}
	}
}

/*
 * Remove all inodes in the system for a device, delete the
 * partitions and make device unusable by setting its size to zero.
 */
void
dasd_destroy_partitions(struct dasd_device * device)
{
	int p;

	for (p = device->gdp->minors - 1; p > 0; p--) {
		invalidate_partition(device->gdp, p);
		delete_partition(device->gdp, p);
	}
	invalidate_partition(device->gdp, 0);
	set_capacity(device->gdp, 0);
}

int
dasd_gendisk_init(void)
{
	int rc;

	/* Register to static dasd major 94 */
	rc = register_blkdev(DASD_MAJOR, "dasd");
	if (rc != 0) {
		MESSAGE(KERN_WARNING,
			"Couldn't register successfully to "
			"major no %d", DASD_MAJOR);
		return rc;
	}
	return 0;
}

void
dasd_gendisk_exit(void)
{
	unregister_blkdev(DASD_MAJOR, "dasd");
}
