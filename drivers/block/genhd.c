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
#include <linux/kmod.h>


static rwlock_t gendisk_lock;

/*
 * Global kernel list of partitioning information.
 */
static LIST_HEAD(gendisk_list);

struct blk_probe {
	struct blk_probe *next;
	dev_t dev;
	unsigned long range;
	struct module *owner;
	struct gendisk *(*get)(dev_t dev, int *part, void *data);
	void (*lock)(dev_t, void *);
	void *data;
} *probes[MAX_BLKDEV];

/* index in the above */
static inline int dev_to_index(dev_t dev)
{
	return MAJOR(dev);
}

void blk_register_region(dev_t dev, unsigned long range, struct module *module,
		    struct gendisk *(*probe)(dev_t, int *, void *),
		    void (*lock)(dev_t, void *), void *data)
{
	int index = dev_to_index(dev);
	struct blk_probe *p = kmalloc(sizeof(struct blk_probe), GFP_KERNEL);
	struct blk_probe **s;
	p->owner = module;
	p->get = probe;
	p->lock = lock;
	p->dev = dev;
	p->range = range;
	p->data = data;
	write_lock(&gendisk_lock);
	for (s = &probes[index]; *s && (*s)->range < range; s = &(*s)->next)
		;
	p->next = *s;
	*s = p;
	write_unlock(&gendisk_lock);
}

void blk_unregister_region(dev_t dev, unsigned long range)
{
	int index = dev_to_index(dev);
	struct blk_probe **s;
	write_lock(&gendisk_lock);
	for (s = &probes[index]; *s; s = &(*s)->next) {
		struct blk_probe *p = *s;
		if (p->dev == dev || p->range == range) {
			*s = p->next;
			kfree(p);
			break;
		}
	}
	write_unlock(&gendisk_lock);
}

EXPORT_SYMBOL(blk_register_region);
EXPORT_SYMBOL(blk_unregister_region);

static struct gendisk *exact_match(dev_t dev, int *part, void *data)
{
	struct gendisk *p = data;
	*part = MINOR(dev) - p->first_minor;
	return p;
}

static void exact_lock(dev_t dev, void *data)
{
	struct gendisk *p = data;
	get_disk(p);
}

/**
 * add_gendisk - add partitioning information to kernel list
 * @gp: per-device partitioning information
 *
 * This function registers the partitioning information in @gp
 * with the kernel.
 */
void add_disk(struct gendisk *disk)
{
	write_lock(&gendisk_lock);
	list_add_tail(&disk->full_list, &gendisk_list);
	write_unlock(&gendisk_lock);
	disk->flags |= GENHD_FL_UP;
	blk_register_region(MKDEV(disk->major, disk->first_minor), disk->minors,
			NULL, exact_match, exact_lock, disk);
	register_disk(disk);
}

EXPORT_SYMBOL(add_disk);
EXPORT_SYMBOL(del_gendisk);

void unlink_gendisk(struct gendisk *disk)
{
	write_lock(&gendisk_lock);
	list_del_init(&disk->full_list);
	write_unlock(&gendisk_lock);
	blk_unregister_region(MKDEV(disk->major, disk->first_minor),
			      disk->minors);
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
	int index = dev_to_index(dev);
	struct gendisk *disk;
	struct blk_probe *p;
	unsigned best = ~0U;

retry:
	read_lock(&gendisk_lock);
	for (p = probes[index]; p; p = p->next) {
		struct gendisk *(*probe)(dev_t, int *, void *);
		struct module *owner;
		void *data;
		if (p->dev > dev || p->dev + p->range <= dev)
			continue;
		if (p->range >= best) {
			read_unlock(&gendisk_lock);
			return NULL;
		}
		if (!try_inc_mod_count(p->owner))
			continue;
		owner = p->owner;
		data = p->data;
		probe = p->get;
		best = p->range;
		*part = dev - p->dev;
		if (p->lock)
			p->lock(dev, data);
		read_unlock(&gendisk_lock);
		disk = probe(dev, part, data);
		/* Currently ->owner protects _only_ ->probe() itself. */
		if (owner)
			__MOD_DEC_USE_COUNT(owner);
		if (disk)
			return disk;
		goto retry;
	}
	read_unlock(&gendisk_lock);
	return NULL;
}

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
	if (!get_capacity(sgp) || sgp->minors == 1)
		return 0;

	/* show the full disk and all non-0 size partitions of it */
	seq_printf(part, "%4d  %4d %10llu %s\n",
		sgp->major, sgp->first_minor,
		(unsigned long long)get_capacity(sgp) >> 1,
		disk_name(sgp, 0, buf));
	for (n = 0; n < sgp->minors - 1; n++) {
		if (sgp->part[n].nr_sects == 0)
			continue;
		seq_printf(part, "%4d  %4d %10llu %s\n",
			sgp->major, n + 1 + sgp->first_minor,
			(unsigned long long)sgp->part[n].nr_sects >> 1 ,
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

struct device_class disk_devclass = {
	.name		= "disk",
};

static struct bus_type disk_bus = {
	name:		"block",
};
 
static struct gendisk *base_probe(dev_t dev, int *part, void *data)
{
	char name[20];
	sprintf(name, "block-major-%d", MAJOR(dev));
	request_module(name);
	return NULL;
}

int __init device_init(void)
{
	struct blk_probe *base = kmalloc(sizeof(struct blk_probe), GFP_KERNEL);
	int i;
	rwlock_init(&gendisk_lock);
	memset(base, 0, sizeof(struct blk_probe));
	base->dev = MKDEV(1,0);
	base->range = MKDEV(MAX_BLKDEV-1, 255) - base->dev + 1;
	base->get = base_probe;
	for (i = 1; i < MAX_BLKDEV; i++)
		probes[i] = base;
	blk_dev_init();
	devclass_register(&disk_devclass);
	bus_register(&disk_bus);
	return 0;
}

__initcall(device_init);

EXPORT_SYMBOL(disk_devclass);

static void disk_release(struct device *dev)
{
	struct gendisk *disk = dev->driver_data;
	kfree(disk->part);
	kfree(disk);
}

struct gendisk *alloc_disk(int minors)
{
	struct gendisk *disk = kmalloc(sizeof(struct gendisk), GFP_KERNEL);
	if (disk) {
		memset(disk, 0, sizeof(struct gendisk));
		if (minors > 1) {
			int size = (minors - 1) * sizeof(struct hd_struct);
			disk->part = kmalloc(size, GFP_KERNEL);
			if (!disk->part) {
				kfree(disk);
				return NULL;
			}
			memset(disk->part, 0, size);
		}
		disk->minors = minors;
		while (minors >>= 1)
			disk->minor_shift++;
		INIT_LIST_HEAD(&disk->full_list);
		disk->disk_dev.bus = &disk_bus;
		disk->disk_dev.release = disk_release;
		disk->disk_dev.driver_data = disk;
		device_initialize(&disk->disk_dev);
	}
	return disk;
}

struct gendisk *get_disk(struct gendisk *disk)
{
	atomic_inc(&disk->disk_dev.refcount);
	return disk;
}

void put_disk(struct gendisk *disk)
{
	if (disk)
		put_device(&disk->disk_dev);
}

EXPORT_SYMBOL(alloc_disk);
EXPORT_SYMBOL(get_disk);
EXPORT_SYMBOL(put_disk);
