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
	struct gendisk disks[DASD_PER_MAJOR];
	devfs_handle_t de_arr[DASD_PER_MAJOR];
	char flags[DASD_PER_MAJOR];
	char names[DASD_PER_MAJOR * 8];
	struct hd_struct part[1<<MINORBITS];
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
	int new_major, rc;
	struct list_head *l;
	int index;
	int i;

	rc = 0;
	/* Allocate major info structure. */
	mi = kmalloc(sizeof(struct major_info), GFP_KERNEL);

	/* Check if one of the allocations failed. */
	if (mi == NULL) {
		MESSAGE(KERN_WARNING, "%s",
			"Cannot get memory to allocate another "
			"major number");
		rc = -ENOMEM;
		goto out_error;
	}

	/* Register block device. */
	new_major = register_blkdev(major, "dasd", &dasd_device_operations);
	if (new_major < 0) {
		MESSAGE(KERN_WARNING,
			"Cannot register to major no %d, rc = %d", major, rc);
		rc = new_major;
		goto out_error;
	}
	if (major != 0)
		new_major = major;
	
	/* Initialize major info structure. */
	memset(mi, 0, sizeof(struct major_info));
	mi->major = new_major;
	for (i = 0; i < DASD_PER_MAJOR; i++) {
		struct gendisk *disk = mi->disks + i;
		disk->major = new_major;
		disk->first_minor = i << DASD_PARTN_BITS;
		disk->minor_shift = DASD_PARTN_BITS;
		disk->nr_real = 1;
		disk->fops = &dasd_device_operations;
		disk->de_arr = mi->de_arr + i;
		disk->flags = mi->flags + i;
		disk->part = mi->part + (i << DASD_PARTN_BITS);
	}

	/* Setup block device pointers for the new major. */
	blk_dev[new_major].queue = dasd_get_queue;

	spin_lock(&dasd_major_lock);
	index = 0;
	list_for_each(l, &dasd_major_info)
		index += DASD_PER_MAJOR;
	for (i = 0; i < DASD_PER_MAJOR; i++, index++) {
		char *name = mi->names + i * 8;
		mi->disks[i].major_name = name;
		sprintf(name, "dasd");
		name += 4;
		if (index > 701)
			*name++ = 'a' + (((index - 702) / 676) % 26);
		if (index > 25)
			*name++ = 'a' + (((index - 26) / 26) % 26);
		sprintf(name, "%c", 'a' + (index % 26));
	}
	list_add_tail(&mi->list, &dasd_major_info);
	spin_unlock(&dasd_major_lock);

	return 0;

	/* Something failed. Do the cleanup and return rc. */
out_error:
	/* We rely on kfree to do the != NULL check. */
	kfree(mi);
	return rc;
}

static void
dasd_unregister_major(struct major_info * mi)
{
	int major, rc;

	if (mi == NULL)
		return;

	/* Delete the major info from dasd_major_info. */
	spin_lock(&dasd_major_lock);
	list_del(&mi->list);
	spin_unlock(&dasd_major_lock);

	/* Clear block device pointers. */
	major = mi->major;
	blk_dev[major].queue = NULL;
	blk_clear(major);

	rc = unregister_blkdev(major, "dasd");
	if (rc < 0)
		MESSAGE(KERN_WARNING,
			"Cannot unregister from major no %d, rc = %d",
			major, rc);

	/* Free memory. */
	kfree(mi);
}

/*
 * Dynamically allocate a new major for dasd devices.
 */
int
dasd_gendisk_new_major(void)
{
	int rc;
	
	rc = dasd_register_major(0);
	if (rc)
		DBF_EXC(DBF_ALERT, "%s", "out of major numbers!");
	return rc;
}

/*
 * Return pointer to gendisk structure by kdev.
 */
static struct gendisk *dasd_gendisk_by_dev(kdev_t dev)
{
	struct list_head *l;
	struct major_info *mi;
	struct gendisk *gdp;
	int major = major(dev);

	spin_lock(&dasd_major_lock);
	gdp = NULL;
	list_for_each(l, &dasd_major_info) {
		mi = list_entry(l, struct major_info, list);
		if (mi->major == major) {
			gdp = &mi->disks[minor(dev) >> DASD_PARTN_BITS];
			break;
		}
	}
	spin_unlock(&dasd_major_lock);
	return gdp;
}

/*
 * Return pointer to gendisk structure by devindex.
 */
struct gendisk *
dasd_gendisk_from_devindex(int devindex)
{
	struct list_head *l;
	struct major_info *mi;
	struct gendisk *gdp;

	spin_lock(&dasd_major_lock);
	gdp = NULL;
	list_for_each(l, &dasd_major_info) {
		mi = list_entry(l, struct major_info, list);
		if (devindex < DASD_PER_MAJOR) {
			gdp = &mi->disks[devindex];
			break;
		}
		devindex -= DASD_PER_MAJOR;
	}
	spin_unlock(&dasd_major_lock);
	return gdp;
}

/*
 * Return devindex of first device using a specifiy major number.
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
 * Register disk to genhd. This will trigger a partition detection.
 */
void
dasd_setup_partitions(dasd_device_t * device)
{
	struct gendisk *disk = dasd_gendisk_by_dev(device->kdev);
	if (disk == NULL)
		return;
	add_gendisk(disk);
	register_disk(disk, mk_kdev(disk->major, disk->first_minor),
			1<<disk->minor_shift, disk->fops,
			device->blocks << device->s2b_shift);
}

/*
 * Remove all inodes in the system for a device and make the
 * partitions unusable by setting their size to zero.
 */
void
dasd_destroy_partitions(dasd_device_t * device)
{
	struct gendisk *disk = dasd_gendisk_by_dev(device->kdev);
	int minor, i;

	if (disk == NULL)
		return;

	del_gendisk(disk);
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
