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


static rwlock_t gendisk_lock;

/*
 * Global kernel list of partitioning information.
 *
 * XXX: you should _never_ access this directly.
 *	the only reason this is exported is source compatiblity.
 */
/*static*/ struct gendisk *gendisk_head;

EXPORT_SYMBOL(gendisk_head);


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

	write_lock(&gendisk_lock);

	/*
 	 *	In 2.5 this will go away. Fix the drivers who rely on
 	 *	old behaviour.
 	 */

	for (sgp = gendisk_head; sgp; sgp = sgp->next)
	{
		if (sgp == gp)
		{
//			printk(KERN_ERR "add_gendisk: device major %d is buggy and added a live gendisk!\n",
//				sgp->major)
			goto out;
		}
	}
	gp->next = gendisk_head;
	gendisk_head = gp;
out:
	write_unlock(&gendisk_lock);
}

EXPORT_SYMBOL(add_gendisk);


/**
 * del_gendisk - remove partitioning information from kernel list
 * @gp: per-device partitioning information
 *
 * This function unregisters the partitioning information in @gp
 * with the kernel.
 */
void
del_gendisk(struct gendisk *gp)
{
	struct gendisk **gpp;

	write_lock(&gendisk_lock);
	for (gpp = &gendisk_head; *gpp; gpp = &((*gpp)->next))
		if (*gpp == gp)
			break;
	if (*gpp)
		*gpp = (*gpp)->next;
	write_unlock(&gendisk_lock);
}

EXPORT_SYMBOL(del_gendisk);


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
	int maj = MAJOR(dev);

	read_lock(&gendisk_lock);
	for (gp = gendisk_head; gp; gp = gp->next)
		if (gp->major == maj)
			break;
	read_unlock(&gendisk_lock);

	return gp;
}

EXPORT_SYMBOL(get_gendisk);


#ifdef CONFIG_PROC_FS
int
get_partition_list(char *page, char **start, off_t offset, int count)
{
	struct gendisk *gp;
	char buf[64];
	int len, n;

	len = sprintf(page, "major minor  #blocks  name\n\n");
	read_lock(&gendisk_lock);
	for (gp = gendisk_head; gp; gp = gp->next) {
		for (n = 0; n < (gp->nr_real << gp->minor_shift); n++) {
			if (gp->part[n].nr_sects == 0)
				continue;

			len += snprintf(page + len, 63,
					"%4d  %4d %10d %s\n",
					gp->major, n, gp->sizes[n],
					disk_name(gp, n, buf));
			if (len < offset)
				offset -= len, len = 0;
			else if (len >= offset + count)
				goto out;
		}
	}

out:
	read_unlock(&gendisk_lock);
	*start = page + offset;
	len -= offset;
	if (len < 0)
		len = 0;
	return len > count ? count : len;
}
#endif


extern int blk_dev_init(void);
#ifdef CONFIG_FUSION_BOOT
extern int fusion_init(void);
#endif
extern int net_dev_init(void);
extern void console_map_init(void);
extern int soc_probe(void);
extern int atmdev_init(void);
extern int i2o_init(void);
extern int cpqarray_init(void);

int __init device_init(void)
{
	rwlock_init(&gendisk_lock);
	blk_dev_init();
	sti();
#ifdef CONFIG_I2O
	i2o_init();
#endif
#ifdef CONFIG_FUSION_BOOT
	fusion_init();
#endif
#ifdef CONFIG_FC4_SOC
	/* This has to be done before scsi_dev_init */
	soc_probe();
#endif
#ifdef CONFIG_BLK_CPQ_DA
	cpqarray_init();
#endif
#ifdef CONFIG_NET
	net_dev_init();
#endif
#ifdef CONFIG_ATM
	(void) atmdev_init();
#endif
#ifdef CONFIG_VT
	console_map_init();
#endif
	return 0;
}

__initcall(device_init);
