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
static struct gendisk *gendisk_head;

/**
 * add_gendisk - add partitioning information to kernel list
 * @gp: per-device partitioning information
 *
 * This function registers the partitioning information in @gp
 * with the kernel.
 */
void
add_gendisk(struct gendisk *gp)
{
	struct gendisk *sgp;
	struct hd_struct *p = NULL;

	if (gp->minor_shift) {
		size_t size = sizeof(struct hd_struct)*((1<<gp->minor_shift)-1);
		p = kmalloc(size, GFP_KERNEL);
		if (!p) {
			printk(KERN_ERR "out of memory; no partitions for %s\n",
				gp->major_name);
			gp->minor_shift = 0;
		} else
			memset(p, 0, size);
	}
	gp->part = p;

	write_lock(&gendisk_lock);

	/*
 	 *	In 2.5 this will go away. Fix the drivers who rely on
 	 *	old behaviour.
 	 */

	for (sgp = gendisk_head; sgp; sgp = sgp->next)
	{
		if (sgp == gp)
		{
			printk(KERN_ERR "add_gendisk: device major %d is buggy and added a live gendisk!\n",
				sgp->major);
			goto out;
		}
	}
	gp->next = gendisk_head;
	gendisk_head = gp;
out:
	write_unlock(&gendisk_lock);
}

EXPORT_SYMBOL(add_gendisk);
EXPORT_SYMBOL(del_gendisk);

void unlink_gendisk(struct gendisk *disk)
{
	struct gendisk **p;
	write_lock(&gendisk_lock);
	for (p = &gendisk_head; *p; p = &((*p)->next))
		if (*p == disk)
			break;
	if (*p)
		*p = (*p)->next;
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
get_gendisk(kdev_t dev)
{
	struct gendisk *gp = NULL;
	int major = major(dev);
	int minor = minor(dev);

	read_lock(&gendisk_lock);
	for (gp = gendisk_head; gp; gp = gp->next) {
		if (gp->major != major)
			continue;
		if (gp->first_minor > minor)
			continue;
		if (gp->first_minor + (1<<gp->minor_shift) <= minor)
			continue;
		read_unlock(&gendisk_lock);
		return gp;
	}
	read_unlock(&gendisk_lock);
	return NULL;
}

EXPORT_SYMBOL(get_gendisk);

#ifdef CONFIG_PROC_FS
/* iterator */
static void *part_start(struct seq_file *part, loff_t *pos)
{
	loff_t k = *pos;
	struct gendisk *sgp;

	read_lock(&gendisk_lock);
	for (sgp = gendisk_head; sgp; sgp = sgp->next) {
		if (!k--)
			return sgp;
	}
	return NULL;
}

static void *part_next(struct seq_file *part, void *v, loff_t *pos)
{
	++*pos;
	return ((struct gendisk *)v)->next;
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

	if (sgp == gendisk_head)
		seq_puts(part, "major minor  #blocks  name\n\n");

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

int __init device_init(void)
{
	rwlock_init(&gendisk_lock);
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
	return 0;
}

__initcall(device_init);
