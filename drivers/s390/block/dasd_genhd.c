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
	struct gendisk gendisk;	/* actually contains the major number */
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
	struct hd_struct *gd_part;
	devfs_handle_t *gd_de_arr;
	char *gd_flags;
	int new_major, rc;

	rc = 0;
	/* Allocate major info structure. */
	mi = kmalloc(sizeof(struct major_info), GFP_KERNEL);

	/* Allocate gendisk arrays. */
	gd_de_arr = kmalloc(DASD_PER_MAJOR * sizeof(devfs_handle_t),
			    GFP_KERNEL);
	gd_flags = kmalloc(DASD_PER_MAJOR * sizeof(char), GFP_KERNEL);
	gd_part = kmalloc(sizeof (struct hd_struct) << MINORBITS, GFP_ATOMIC);

	/* Check if one of the allocations failed. */
	if (mi == NULL || gd_de_arr == NULL || gd_flags == NULL ||
	    gd_part == NULL) {
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
	mi->gendisk.major = new_major;
	mi->gendisk.major_name = "dasd";
	mi->gendisk.minor_shift = DASD_PARTN_BITS;
	mi->gendisk.nr_real = DASD_PER_MAJOR;
	mi->gendisk.fops = &dasd_device_operations;
	mi->gendisk.de_arr = gd_de_arr;
	mi->gendisk.flags = gd_flags;
	mi->gendisk.part = gd_part;

	/* Initialize the gendisk arrays. */
	memset(gd_de_arr, 0, DASD_PER_MAJOR * sizeof(devfs_handle_t));
	memset(gd_flags, 0, DASD_PER_MAJOR * sizeof (char));
	memset(gd_part, 0, sizeof (struct hd_struct) << MINORBITS);

	/* Setup block device pointers for the new major. */
	blk_dev[new_major].queue = dasd_get_queue;

	/* Insert the new major info structure into dasd_major_info list. */
	spin_lock(&dasd_major_lock);
	list_add_tail(&mi->list, &dasd_major_info);
	spin_unlock(&dasd_major_lock);

	/* Make the gendisk known. */
	add_gendisk(&mi->gendisk);
	return 0;

	/* Something failed. Do the cleanup and return rc. */
out_error:
	/* We rely on kfree to do the != NULL check. */
	kfree(gd_part);
	kfree(gd_flags);
	kfree(gd_de_arr);
	kfree(mi);
	return rc;
}

static void
dasd_unregister_major(struct major_info * mi)
{
	int major, rc;

	if (mi == NULL)
		return;

	/* Remove gendisk information. */
	del_gendisk(&mi->gendisk);

	/* Delete the major info from dasd_major_info. */
	spin_lock(&dasd_major_lock);
	list_del(&mi->list);
	spin_unlock(&dasd_major_lock);

	/* Clear block device pointers. */
	major = mi->gendisk.major;
	blk_dev[major].queue = NULL;
	blk_clear(major);

	rc = unregister_blkdev(major, "dasd");
	if (rc < 0)
		MESSAGE(KERN_WARNING,
			"Cannot unregister from major no %d, rc = %d",
			major, rc);

	/* Free memory. */
	kfree(mi->gendisk.part);
	kfree(mi->gendisk.flags);
	kfree(mi->gendisk.de_arr);
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
struct gendisk *
dasd_gendisk_from_major(int major)
{
	struct list_head *l;
	struct major_info *mi;
	struct gendisk *gdp;

	spin_lock(&dasd_major_lock);
	gdp = NULL;
	list_for_each(l, &dasd_major_info) {
		mi = list_entry(l, struct major_info, list);
		if (mi->gendisk.major == major) {
			gdp = &mi->gendisk;
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
			gdp = &mi->gendisk;
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
		if (mi->gendisk.major == major) {
			rc = devindex;
			break;
		}
		devindex += DASD_PER_MAJOR;
	}
	spin_unlock(&dasd_major_lock);
	return rc;
}


/*
 * This one is needed for naming 18000+ possible dasd devices.
 *   dasda - dasdz : 26 devices
 *   dasdaa - dasdzz : 676 devices, added up = 702
 *   dasdaaa - dasdzzz : 17576 devices, added up = 18278
 * This function is called from the partition detection code (see disk_name)
 * via the genhd_dasd_name hook. As mentioned in partition/check.c this
 * is ugly...
 */
int
dasd_device_name(char *str, int index, int partition, struct gendisk *hd)
{
	struct list_head *l;
	int len, found;

	/* Check if this is on of our gendisk structures. */
	found = 0;
	spin_lock(&dasd_major_lock);
	list_for_each(l, &dasd_major_info) {
		struct major_info *mi;
		mi = list_entry(l, struct major_info, list);
		if (&mi->gendisk == hd) {
			found = 1;
			break;
		}
		index += DASD_PER_MAJOR;
	}
	spin_unlock(&dasd_major_lock);
	if (!found)
		/* Not one of our structures. Can't be a dasd. */
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

	if (partition > DASD_PARTN_MASK)
		return -EINVAL;
	if (partition)
		len += sprintf(str + len, "%d", partition);
	return 0;
}

/*
 * Register disk to genhd. This will trigger a partition detection.
 */
void
dasd_setup_partitions(dasd_device_t * device)
{
	grok_partitions(device->kdev, device->blocks << device->s2b_shift);
}

/*
 * Remove all inodes in the system for a device and make the
 * partitions unusable by setting their size to zero.
 */
void
dasd_destroy_partitions(dasd_device_t * device)
{
	struct gendisk *gdp;
	int minor, i;

	gdp = dasd_gendisk_from_major(major(device->kdev));
	if (gdp == NULL)
		return;

	wipe_partitions(device->kdev);

	/*
	 * This is confusing. The funcions is devfs_register_partitions
	 * but the 1 as third parameter makes it do an unregister...
	 * FIXME: there must be a better way to get rid of the devfs entries
	 */
	devfs_register_partitions(gdp, minor(device->kdev), 1);
}

extern int (*genhd_dasd_name)(char *, int, int, struct gendisk *);
extern int (*genhd_dasd_ioctl) (struct inode *inp, struct file *filp,
                                unsigned int no, unsigned long data);

int
dasd_gendisk_init(void)
{
	int rc;

	/* Register to static dasd major 94 */
	rc = dasd_register_major(DASD_MAJOR);
	if (rc != 0) {
		MESSAGE(KERN_WARNING,
			"Couldn't register successfully to "
			"major no %d", DASD_MAJOR);
		return rc;
	}
	genhd_dasd_name = dasd_device_name;
        genhd_dasd_ioctl = dasd_ioctl;
	return 0;

}

void
dasd_gendisk_exit(void)
{
	struct list_head *l, *n;

	genhd_dasd_ioctl = NULL;
	genhd_dasd_name = NULL;
	spin_lock(&dasd_major_lock);
	list_for_each_safe(l, n, &dasd_major_info)
		dasd_unregister_major(list_entry(l, struct major_info, list));
	spin_unlock(&dasd_major_lock);
}
