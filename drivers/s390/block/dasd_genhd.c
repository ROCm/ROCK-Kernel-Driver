/*
 * File...........: linux/drivers/s390/block/dasd_ioctl.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * Dealing with devices registered to multiple major numbers.
 *
 * 05/04/02 split from dasd.c, code restructuring.
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <linux/blk.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_gendisk:"

#include "dasd_int.h"

static spinlock_t dasd_major_lock = SPIN_LOCK_UNLOCKED;
static struct list_head dasd_major_info = LIST_HEAD_INIT(dasd_major_info);

struct major_info {
	struct list_head list;
	int major;
};

/*
 * Returns the queue corresponding to a device behind a kdev.
 */
static request_queue_t *
dasd_get_queue(kdev_t kdev)
{
	dasd_devmap_t *devmap;
	dasd_device_t *device;
	request_queue_t *queue;

	devmap = dasd_devmap_from_kdev(kdev);
	device = (devmap != NULL) ?
		dasd_get_device(devmap) : ERR_PTR(-ENODEV);
	if (IS_ERR(device))
		return NULL;
	queue = device->request_queue;
	dasd_put_device(devmap);
	return queue;
}

/*
 * Register major number for the dasd driver. Call with DASD_MAJOR to
 * setup the static dasd device major 94 or with 0 to allocated a major
 * dynamically.
 */
static int
dasd_register_major(int major)
{
	struct major_info *mi;
	int new_major;

	/* Allocate major info structure. */
	mi = kmalloc(sizeof(struct major_info), GFP_KERNEL);

	/* Check if one of the allocations failed. */
	if (mi == NULL) {
		MESSAGE(KERN_WARNING, "%s",
			"Cannot get memory to allocate another "
			"major number");
		return -ENOMEM;
	}

	/* Register block device. */
	new_major = register_blkdev(major, "dasd", &dasd_device_operations);
	if (new_major < 0) {
		MESSAGE(KERN_WARNING,
			"Cannot register to major no %d, rc = %d",
			major, new_major);
		kfree(mi);
		return new_major;
	}
	if (major != 0)
		new_major = major;

	/* Initialize major info structure. */
	mi->major = new_major;

	/* Setup block device pointers for the new major. */
	blk_dev[new_major].queue = dasd_get_queue;

	/* Insert the new major info structure into dasd_major_info list. */
	spin_lock(&dasd_major_lock);
	list_add_tail(&mi->list, &dasd_major_info);
	spin_unlock(&dasd_major_lock);

	return 0;
}

static void
dasd_unregister_major(struct major_info * mi)
{
	int rc;

	if (mi == NULL)
		return;

	/* Delete the major info from dasd_major_info. */
	spin_lock(&dasd_major_lock);
	list_del(&mi->list);
	spin_unlock(&dasd_major_lock);

	/* Clear block device pointers. */
	blk_dev[mi->major].queue = NULL;

	rc = unregister_blkdev(mi->major, "dasd");
	if (rc < 0)
		MESSAGE(KERN_WARNING,
			"Cannot unregister from major no %d, rc = %d",
			mi->major, rc);

	/* Free memory. */
	kfree(mi);
}

/*
 * This one is needed for naming 18000+ possible dasd devices.
 *   dasda - dasdz : 26 devices
 *   dasdaa - dasdzz : 676 devices, added up = 702
 *   dasdaaa - dasdzzz : 17576 devices, added up = 18278
 */
int
dasd_device_name(char *str, int index, int partition)
{
	int len;

	if (partition > DASD_PARTN_MASK)
		return -EINVAL;

	len = sprintf(str, "dasd");
	if (index > 25) {
		if (index > 701)
			len += sprintf(str + len, "%c",
				       'a' + (((index - 702) / 676) % 26));
		len += sprintf(str + len, "%c",
			       'a' + (((index - 26) / 26) % 26));
	}
	len += sprintf(str + len, "%c", 'a' + (index % 26));

	if (partition)
		len += sprintf(str + len, "%d", partition);
	return 0;
}

/*
 * Allocate gendisk structure for devindex.
 */
struct gendisk *
dasd_gendisk_alloc(char *device_name, int devindex)
{
	struct list_head *l;
	struct major_info *mi;
	struct gendisk *gdp;
	struct hd_struct *gd_part;
	int index, len, rc;

	/* Make sure the major for this device exists. */
	mi = NULL;
	while (1) {
		spin_lock(&dasd_major_lock);
		index = devindex;
		list_for_each(l, &dasd_major_info) {
			mi = list_entry(l, struct major_info, list);
			if (index < DASD_PER_MAJOR)
				break;
			index -= DASD_PER_MAJOR;
		}
		spin_unlock(&dasd_major_lock);
		if (index < DASD_PER_MAJOR)
			break;
		rc = dasd_register_major(0);
		if (rc) {
			DBF_EXC(DBF_ALERT, "%s", "out of major numbers!");
			return ERR_PTR(rc);
		}
	}

	/* Allocate genhd structure and gendisk arrays. */
	gdp = kmalloc(sizeof(struct gendisk), GFP_KERNEL);
	gd_part = kmalloc(sizeof (struct hd_struct) << DASD_PARTN_BITS,
			  GFP_ATOMIC);

	/* Check if one of the allocations failed. */
	if (gdp == NULL || gd_part == NULL) {
		/* We rely on kfree to do the != NULL check. */
		kfree(gd_part);
		kfree(gdp);
		return ERR_PTR(-ENOMEM);
	}

	/* Initialize gendisk structure. */
	memset(gdp, 0, sizeof(struct gendisk));
	memcpy(gdp->disk_name, device_name, 16);
	gdp->major = mi->major;
	gdp->first_minor = index << DASD_PARTN_BITS;
	gdp->minor_shift = DASD_PARTN_BITS;
	gdp->part = gd_part;
	gdp->fops = &dasd_device_operations;

	/*
	 * Set device name.
	 *   dasda - dasdz : 26 devices
	 *   dasdaa - dasdzz : 676 devices, added up = 702
	 *   dasdaaa - dasdzzz : 17576 devices, added up = 18278
	 */
	len = sprintf(device_name, "dasd");
	if (devindex > 25) {
		if (devindex > 701)
			len += sprintf(device_name + len, "%c",
				       'a' + (((devindex - 702) / 676) % 26));
		len += sprintf(device_name + len, "%c",
			       'a' + (((devindex - 26) / 26) % 26));
	}
	len += sprintf(device_name + len, "%c", 'a' + (devindex % 26));

	/* Initialize the gendisk arrays. */
	memset(gd_part, 0, sizeof (struct hd_struct) << DASD_PARTN_BITS);

	return gdp;
}

/*
 * Free gendisk structure for devindex.
 */
void
dasd_gendisk_free(struct gendisk *gdp)
{
	/* Free memory. */
	kfree(gdp->part);
	kfree(gdp);
}

/*
 * Return devindex of first device using a specific major number.
 */
int dasd_gendisk_major_index(int major)
{
	struct list_head *l;
	struct major_info *mi;
	int devindex, rc;

	spin_lock(&dasd_major_lock);
	rc = -EINVAL;
	devindex = 0;
	list_for_each(l, &dasd_major_info) {
		mi = list_entry(l, struct major_info, list);
		if (mi->major == major) {
			rc = devindex;
			break;
		}
		devindex += DASD_PER_MAJOR;
	}
	spin_unlock(&dasd_major_lock);
	return rc;
}

/*
 * Return major number for device with device index devindex.
 */
int dasd_gendisk_index_major(int devindex)
{
	struct list_head *l;
	struct major_info *mi;
	int rc;

	spin_lock(&dasd_major_lock);
	rc = -ENODEV;
	list_for_each(l, &dasd_major_info) {
		mi = list_entry(l, struct major_info, list);
		if (devindex < DASD_PER_MAJOR) {
			rc = mi->major;
			break;
		}
		devindex -= DASD_PER_MAJOR;
	}
	spin_unlock(&dasd_major_lock);
	return rc;
}

/*
 * Register disk to genhd. This will trigger a partition detection.
 */
void
dasd_setup_partitions(dasd_device_t * device)
{
	/* Make the disk known. */
	set_capacity(device->gdp, device->blocks << device->s2b_shift);
	add_disk(device->gdp);
}

/*
 * Remove all inodes in the system for a device and make the
 * partitions unusable by setting their size to zero.
 */
void
dasd_destroy_partitions(dasd_device_t * device)
{
	del_gendisk(device->gdp);
}

int
dasd_gendisk_init(void)
{
	int rc;

	/* Register to static dasd major 94 */
	rc = dasd_register_major(DASD_MAJOR);
	if (rc != 0)
		MESSAGE(KERN_WARNING,
			"Couldn't register successfully to "
			"major no %d", DASD_MAJOR);
	return rc;
}

void
dasd_gendisk_exit(void)
{
	struct list_head *l, *n;

	spin_lock(&dasd_major_lock);
	list_for_each_safe(l, n, &dasd_major_info)
		dasd_unregister_major(list_entry(l, struct major_info, list));
	spin_unlock(&dasd_major_lock);
}
