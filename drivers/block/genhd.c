/*
 *  Code extracted from
 *  linux/kernel/hd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *
 *  devfs support - jj, rgooch, 980122
 *
 *  Moved partition checking code to fs/partitions* - Russell King
 *  (linux@arm.uk.linux.org)
 */

/*
 * TODO:  rip out the remaining init crap from this file  --hch
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/slab.h>


static rwlock_t gendisk_lock;

/*
 * Global kernel list of partitioning information.
 */
static LIST_HEAD(gendisk_list);

/*
 *  TEMPORARY KLUDGE.
 */
static struct {
	struct list_head list;
	struct gendisk *(*get)(int minor);
} gendisks[MAX_BLKDEV];

void blk_set_probe(int major, struct gendisk *(p)(int))
{
	write_lock(&gendisk_lock);
	gendisks[major].get = p;
	write_unlock(&gendisk_lock);
}
EXPORT_SYMBOL(blk_set_probe);	/* Will go away */
	

/**
 * add_gendisk - add partitioning information to kernel list
 * @gp: per-device partitioning information
 *
 * This function registers the partitioning information in @gp
 * with the kernel.
 */
static void add_gendisk(struct gendisk *gp)
{
	struct hd_struct *p = NULL;

	if (gp->minor_shift) {
		size_t size = sizeof(struct hd_struct)*((1<<gp->minor_shift)-1);
		p = kmalloc(size, GFP_KERNEL);
		if (!p) {
			printk(KERN_ERR "out of memory; no partitions for %s\n",
				gp->disk_name);
			gp->minor_shift = 0;
		} else
			memset(p, 0, size);
	}
	gp->part = p;

	write_lock(&gendisk_lock);
	list_add(&gp->list, &gendisks[gp->major].list);
	if (gp->minor_shift)
		list_add_tail(&gp->full_list, &gendisk_list);
	else
		INIT_LIST_HEAD(&gp->full_list);
	write_unlock(&gendisk_lock);
}

void add_disk(struct gendisk *disk)
{
	add_gendisk(disk);
	register_disk(disk);
}

EXPORT_SYMBOL(add_disk);
EXPORT_SYMBOL(del_gendisk);

void unlink_gendisk(struct gendisk *disk)
{
	write_lock(&gendisk_lock);
	list_del_init(&disk->full_list);
	list_del_init(&disk->list);
	write_unlock(&gendisk_lock);
}

/**
 * get_gendisk - get partitioning information for a given device
 * @dev: device to get partitioning information for
 *
 * This function gets the structure containing partitioning
 * information for the given device @dev.
 */
struct gendisk *
get_gendisk(dev_t dev, int *part)
{
	struct gendisk *disk;
	struct list_head *p;
	int major = MAJOR(dev);
	int minor = MINOR(dev);

	*part = 0;
	read_lock(&gendisk_lock);
	if (gendisks[major].get) {
		disk = gendisks[major].get(minor);
		read_unlock(&gendisk_lock);
		return disk;
	}
	list_for_each(p, &gendisks[major].list) {
		disk = list_entry(p, struct gendisk, list);
		if (disk->first_minor > minor)
			continue;
		if (disk->first_minor + (1<<disk->minor_shift) <= minor)
			continue;
		read_unlock(&gendisk_lock);
		*part = minor - disk->first_minor;
		return disk;
	}
	read_unlock(&gendisk_lock);
	return NULL;
}

EXPORT_SYMBOL(get_gendisk);

#ifdef CONFIG_PROC_FS
/* iterator */
static void *part_start(struct seq_file *part, loff_t *pos)
{
	struct list_head *p;
	loff_t l = *pos;

	read_lock(&gendisk_lock);
	list_for_each(p, &gendisk_list)
		if (!l--)
			return list_entry(p, struct gendisk, full_list);
	return NULL;
}

static void *part_next(struct seq_file *part, void *v, loff_t *pos)
{
	struct list_head *p = ((struct gendisk *)v)->full_list.next;
	++*pos;
	return p==&gendisk_list ? NULL : list_entry(p, struct gendisk, full_list);
}

static void part_stop(struct seq_file *part, void *v)
{
	read_unlock(&gendisk_lock);
}

static int show_partition(struct seq_file *part, void *v)
{
	struct gendisk *sgp = v;
	int n;
	char buf[64];

	if (&sgp->full_list == gendisk_list.next)
		seq_puts(part, "major minor  #blocks  name\n\n");

	/* Don't show non-partitionable devices or empty devices */
	if (!get_capacity(sgp))
		return 0;

	/* show the full disk and all non-0 size partitions of it */
	seq_printf(part, "%4d  %4d %10ld %s\n",
		sgp->major, sgp->first_minor,
		get_capacity(sgp) >> 1,
		disk_name(sgp, 0, buf));
	for (n = 0; n < (1<<sgp->minor_shift) - 1; n++) {
		if (sgp->part[n].nr_sects == 0)
			continue;
		seq_printf(part, "%4d  %4d %10ld %s\n",
			sgp->major, n + 1 + sgp->first_minor,
			sgp->part[n].nr_sects >> 1 ,
			disk_name(sgp, n + 1, buf));
	}

	return 0;
}

struct seq_operations partitions_op = {
	start:	part_start,
	next:	part_next,
	stop:	part_stop,
	show:	show_partition
};
#endif


extern int blk_dev_init(void);
extern int soc_probe(void);
extern int atmdev_init(void);
extern int cpqarray_init(void);

struct device_class disk_devclass = {
	.name		= "disk",
};

int __init device_init(void)
{
	int i;
	rwlock_init(&gendisk_lock);
	for (i = 0; i < MAX_BLKDEV; i++)
		INIT_LIST_HEAD(&gendisks[i].list);
	blk_dev_init();
#ifdef CONFIG_FC4_SOC
	/* This has to be done before scsi_dev_init */
	soc_probe();
#endif
#ifdef CONFIG_BLK_CPQ_DA
	cpqarray_init();
#endif
#ifdef CONFIG_ATM
	(void) atmdev_init();
#endif

	devclass_register(&disk_devclass);
	return 0;
}

__initcall(device_init);

EXPORT_SYMBOL(disk_devclass);
