/*
 *  fs/partitions/check.c
 *
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 *
 *  We now have independent partition support from the
 *  block drivers, which allows all the partition code to
 *  be grouped in one location, and it to be mostly self
 *  contained.
 *
 *  Added needed MAJORS for new pairs, {hdi,hdj}, {hdk,hdl}
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/blk.h>
#include <linux/kmod.h>
#include <linux/ctype.h>
#include <../drivers/base/fs/fs.h>	/* Eeeeewwwww */

#include "check.h"

#include "acorn.h"
#include "amiga.h"
#include "atari.h"
#include "ldm.h"
#include "mac.h"
#include "msdos.h"
#include "osf.h"
#include "sgi.h"
#include "sun.h"
#include "ibm.h"
#include "ultrix.h"
#include "efi.h"

#if CONFIG_BLK_DEV_MD
extern void md_autodetect_dev(dev_t dev);
#endif

int warn_no_part = 1; /*This is ugly: should make genhd removable media aware*/

static int (*check_part[])(struct parsed_partitions *, struct block_device *) = {
#ifdef CONFIG_ACORN_PARTITION
	acorn_partition,
#endif
#ifdef CONFIG_EFI_PARTITION
	efi_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_LDM_PARTITION
	ldm_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_MSDOS_PARTITION
	msdos_partition,
#endif
#ifdef CONFIG_OSF_PARTITION
	osf_partition,
#endif
#ifdef CONFIG_SUN_PARTITION
	sun_partition,
#endif
#ifdef CONFIG_AMIGA_PARTITION
	amiga_partition,
#endif
#ifdef CONFIG_ATARI_PARTITION
	atari_partition,
#endif
#ifdef CONFIG_MAC_PARTITION
	mac_partition,
#endif
#ifdef CONFIG_SGI_PARTITION
	sgi_partition,
#endif
#ifdef CONFIG_ULTRIX_PARTITION
	ultrix_partition,
#endif
#ifdef CONFIG_IBM_PARTITION
	ibm_partition,
#endif
	NULL
};
 
/*
 * disk_name() is used by partition check code and the md driver.
 * It formats the devicename of the indicated disk into
 * the supplied buffer (of size at least 32), and returns
 * a pointer to that same buffer (for convenience).
 */

char *disk_name(struct gendisk *hd, int part, char *buf)
{
	int pos;
	if (!part) {
		if (hd->disk_de) {
			pos = devfs_generate_path(hd->disk_de, buf, 64);
			if (pos >= 0)
				return buf + pos;
		}
		sprintf(buf, "%s", hd->disk_name);
	} else {
		if (hd->part[part-1].de) {
			pos = devfs_generate_path(hd->part[part-1].de, buf, 64);
			if (pos >= 0)
				return buf + pos;
		}
		if (isdigit(hd->disk_name[strlen(hd->disk_name)-1]))
			sprintf(buf, "%sp%d", hd->disk_name, part);
		else
			sprintf(buf, "%s%d", hd->disk_name, part);
	}
	return buf;
}

static struct parsed_partitions *
check_partition(struct gendisk *hd, struct block_device *bdev)
{
	struct parsed_partitions *state;
	devfs_handle_t de = NULL;
	char buf[64];
	int i, res;

	state = kmalloc(sizeof(struct parsed_partitions), GFP_KERNEL);
	if (!state)
		return NULL;

	if (hd->flags & GENHD_FL_DEVFS)
		de = hd->de;
	i = devfs_generate_path (de, buf, sizeof buf);
	if (i >= 0) {
		printk(KERN_INFO " /dev/%s:", buf + i);
		sprintf(state->name, "p");
	} else {
		disk_name(hd, 0, state->name);
		printk(KERN_INFO " %s:", state->name);
		if (isdigit(state->name[strlen(state->name)-1]))
			sprintf(state->name, "p");
	}
	state->limit = hd->minors;
	i = res = 0;
	while (!res && check_part[i]) {
		memset(&state->parts, 0, sizeof(state->parts));
		res = check_part[i++](state, bdev);
	}
	if (res > 0)
		return state;
	if (!res)
		printk(" unknown partition table\n");
	else if (warn_no_part)
		printk(" unable to read partition table\n");
	kfree(state);
	return NULL;
}

static void devfs_register_partition(struct gendisk *dev, int part)
{
#ifdef CONFIG_DEVFS_FS
	devfs_handle_t dir;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	struct hd_struct *p = dev->part;
	char devname[16];

	if (p[part-1].de)
		return;
	dir = devfs_get_parent(dev->disk_de);
	if (!dir)
		return;
	if (dev->flags & GENHD_FL_REMOVABLE)
		devfs_flags |= DEVFS_FL_REMOVABLE;
	sprintf(devname, "part%d", part);
	p[part-1].de = devfs_register (dir, devname, devfs_flags,
				    dev->major, dev->first_minor + part,
				    S_IFBLK | S_IRUSR | S_IWUSR,
				    dev->fops, NULL);
#endif
}

#ifdef CONFIG_DEVFS_FS
static struct unique_numspace disc_numspace = UNIQUE_NUMBERSPACE_INITIALISER;
static devfs_handle_t cdroms;
static struct unique_numspace cdrom_numspace = UNIQUE_NUMBERSPACE_INITIALISER;
#endif

static void devfs_create_partitions(struct gendisk *dev)
{
#ifdef CONFIG_DEVFS_FS
	int pos = 0;
	devfs_handle_t dir, slave;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	char dirname[64], symlink[16];
	static devfs_handle_t devfs_handle;
	int part, max_p = dev->minors;
	struct hd_struct *p = dev->part;

	if (dev->flags & GENHD_FL_REMOVABLE)
		devfs_flags |= DEVFS_FL_REMOVABLE;
	if (dev->flags & GENHD_FL_DEVFS) {
		dir = dev->de;
		if (!dir)  /*  Aware driver wants to block disc management  */
			return;
		pos = devfs_generate_path(dir, dirname + 3, sizeof dirname-3);
		if (pos < 0)
			return;
		strncpy(dirname + pos, "../", 3);
	} else {
		/*  Unaware driver: construct "real" directory  */
		sprintf(dirname, "../%s/disc%d", dev->disk_name,
			dev->first_minor >> dev->minor_shift);
		dir = devfs_mk_dir(NULL, dirname + 3, NULL);
	}
	if (!devfs_handle)
		devfs_handle = devfs_mk_dir(NULL, "discs", NULL);
	dev->number = devfs_alloc_unique_number (&disc_numspace);
	sprintf(symlink, "disc%d", dev->number);
	devfs_mk_symlink (devfs_handle, symlink, DEVFS_FL_DEFAULT,
			  dirname + pos, &slave, NULL);
	dev->disk_de = devfs_register(dir, "disc", devfs_flags,
			    dev->major, dev->first_minor,
			    S_IFBLK | S_IRUSR | S_IWUSR, dev->fops, NULL);
	devfs_auto_unregister(dev->disk_de, slave);
	if (!(dev->flags & GENHD_FL_DEVFS))
		devfs_auto_unregister (slave, dir);
#endif
}

static void devfs_create_cdrom(struct gendisk *dev)
{
#ifdef CONFIG_DEVFS_FS
	int pos = 0;
	devfs_handle_t dir, slave;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	char dirname[64], symlink[16];
	char vname[23];

	if (!cdroms)
		cdroms = devfs_mk_dir (NULL, "cdroms", NULL);

	dev->number = devfs_alloc_unique_number(&cdrom_numspace);
	sprintf(vname, "cdrom%d", dev->number);
	if (dev->de) {
		int pos;
		devfs_handle_t slave;
		char rname[64];

		dev->disk_de = devfs_register(dev->de, "cd", DEVFS_FL_DEFAULT,
				     dev->major, dev->first_minor,
				     S_IFBLK | S_IRUGO | S_IWUGO,
				     dev->fops, NULL);

		pos = devfs_generate_path(dev->disk_de, rname+3, sizeof(rname)-3);
		if (pos >= 0) {
			strncpy(rname + pos, "../", 3);
			devfs_mk_symlink(cdroms, vname,
					 DEVFS_FL_DEFAULT,
					 rname + pos, &slave, NULL);
			devfs_auto_unregister(dev->de, slave);
		}
	} else {
		dev->disk_de = devfs_register (NULL, vname, DEVFS_FL_DEFAULT,
				    dev->major, dev->first_minor,
				    S_IFBLK | S_IRUGO | S_IWUGO,
				    dev->fops, NULL);
	}
#endif
}

static void devfs_remove_partitions(struct gendisk *dev)
{
#ifdef CONFIG_DEVFS_FS
	devfs_unregister(dev->disk_de);
	dev->disk_de = NULL;
	if (dev->flags & GENHD_FL_CD)
		devfs_dealloc_unique_number(&cdrom_numspace, dev->number);
	else
		devfs_dealloc_unique_number(&disc_numspace, dev->number);
#endif
}

static ssize_t part_dev_read(struct device *dev,
			char *page, size_t count, loff_t off)
{
	struct gendisk *disk = dev->parent->driver_data;
	struct hd_struct *p = dev->driver_data;
	int part = p - disk->part + 1;
	dev_t base = MKDEV(disk->major, disk->first_minor); 
	return off ? 0 : sprintf(page, "%04x\n",base + part);
}
static ssize_t part_start_read(struct device *dev,
			char *page, size_t count, loff_t off)
{
	struct hd_struct *p = dev->driver_data;
	return off ? 0 : sprintf(page, "%llu\n",(unsigned long long)p->start_sect);
}
static ssize_t part_size_read(struct device *dev,
			char *page, size_t count, loff_t off)
{
	struct hd_struct *p = dev->driver_data;
	return off ? 0 : sprintf(page, "%llu\n",(unsigned long long)p->nr_sects);
}
static struct device_attribute part_attr_dev = {
	.attr = {.name = "dev", .mode = S_IRUGO },
	.show	= part_dev_read
};
static struct device_attribute part_attr_start = {
	.attr = {.name = "start", .mode = S_IRUGO },
	.show	= part_start_read
};
static struct device_attribute part_attr_size = {
	.attr = {.name = "size", .mode = S_IRUGO },
	.show	= part_size_read
};

void delete_partition(struct gendisk *disk, int part)
{
	struct hd_struct *p = disk->part + part - 1;
	struct device *dev;
	if (!p->nr_sects)
		return;
	p->start_sect = 0;
	p->nr_sects = 0;
	devfs_unregister(p->de);
	dev = p->hd_driverfs_dev;
	p->hd_driverfs_dev = NULL;
	if (dev) {
		device_remove_file(dev, &part_attr_size);
		device_remove_file(dev, &part_attr_start);
		device_remove_file(dev, &part_attr_dev);
		device_unregister(dev);	
	}
}

static void part_release(struct device *dev)
{
	kfree(dev);
}

void add_partition(struct gendisk *disk, int part, sector_t start, sector_t len)
{
	struct hd_struct *p = disk->part + part - 1;
	struct device *parent = &disk->disk_dev;
	struct device *dev;

	p->start_sect = start;
	p->nr_sects = len;
	devfs_register_partition(disk, part);
	dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev)
		return;
	memset(dev, 0, sizeof(struct device));
	dev->parent = parent;
	sprintf(dev->bus_id, "p%d", part);
	dev->release = part_release;
	dev->driver_data = p;
	device_register(dev);
	device_create_file(dev, &part_attr_dev);
	device_create_file(dev, &part_attr_start);
	device_create_file(dev, &part_attr_size);
	p->hd_driverfs_dev = dev;
}

static ssize_t disk_dev_read(struct device *dev,
			char *page, size_t count, loff_t off)
{
	struct gendisk *disk = dev->driver_data;
	dev_t base = MKDEV(disk->major, disk->first_minor); 
	return off ? 0 : sprintf(page, "%04x\n",base);
}
static ssize_t disk_range_read(struct device *dev,
			char *page, size_t count, loff_t off)
{
	struct gendisk *disk = dev->driver_data;
	return off ? 0 : sprintf(page, "%d\n",disk->minors);
}
static ssize_t disk_size_read(struct device *dev,
			char *page, size_t count, loff_t off)
{
	struct gendisk *disk = dev->driver_data;
	return off ? 0 : sprintf(page, "%llu\n",(unsigned long long)get_capacity(disk));
}
static struct device_attribute disk_attr_dev = {
	.attr = {.name = "dev", .mode = S_IRUGO },
	.show	= disk_dev_read
};
static struct device_attribute disk_attr_range = {
	.attr = {.name = "range", .mode = S_IRUGO },
	.show	= disk_range_read
};
static struct device_attribute disk_attr_size = {
	.attr = {.name = "size", .mode = S_IRUGO },
	.show	= disk_size_read
};

static void disk_driverfs_symlinks(struct gendisk *disk)
{
	struct device *target = disk->driverfs_dev;
	struct device *dev = &disk->disk_dev;
	struct device *p;
	char *path;
	char *s;
	int length;
	int depth;

	if (!target)
		return;

	get_device(target);

	length = get_devpath_length(target);
	length += strlen("..");

	if (length > PATH_MAX)
		return;

	if (!(path = kmalloc(length,GFP_KERNEL)))
		return;
	memset(path,0,length);

	/* our relative position */
	strcpy(path,"..");

	fill_devpath(target, path, length);
	driverfs_create_symlink(&dev->dir, "device", path);
	kfree(path);

	for (p = target, depth = 0; p; p = p->parent, depth++)
		;
	length = get_devpath_length(dev);
	length += 3 * depth - 1;

	if (length > PATH_MAX)
		return;

	if (!(path = kmalloc(length,GFP_KERNEL)))
		return;
	memset(path,0,length);
	for (s = path; depth--; s += 3)
		strcpy(s, "../");

	fill_devpath(dev, path, length);
	driverfs_create_symlink(&target->dir, "block", path);
	kfree(path);
}

/* Not exported, helper to add_disk(). */
void register_disk(struct gendisk *disk)
{
	struct device *dev = &disk->disk_dev;
	struct parsed_partitions *state;
	struct block_device *bdev;
	char *s;
	int j;

	strcpy(dev->bus_id, disk->disk_name);
	/* ewww... some of these buggers have / in name... */
	s = strchr(dev->bus_id, '/');
	if (s)
		*s = '!';
	device_add(dev);
	device_create_file(dev, &disk_attr_dev);
	device_create_file(dev, &disk_attr_range);
	device_create_file(dev, &disk_attr_size);
	disk_driverfs_symlinks(disk);

	if (disk->flags & GENHD_FL_CD)
		devfs_create_cdrom(disk);

	/* No minors to use for partitions */
	if (disk->minors == 1)
		return;

	/* No such device (e.g., media were just removed) */
	if (!get_capacity(disk))
		return;

	bdev = bdget(MKDEV(disk->major, disk->first_minor));
	if (blkdev_get(bdev, FMODE_READ, 0, BDEV_RAW) < 0)
		return;
	state = check_partition(disk, bdev);
	devfs_create_partitions(disk);
	if (state) {
		for (j = 1; j < state->limit; j++) {
			sector_t size = state->parts[j].size;
			sector_t from = state->parts[j].from;
			if (!size)
				continue;
			add_partition(disk, j, from, size);
#if CONFIG_BLK_DEV_MD
			if (!state->parts[j].flags)
				continue;
			md_autodetect_dev(bdev->bd_dev+j);
#endif
		}
		kfree(state);
	}
	blkdev_put(bdev, BDEV_RAW);
}

int rescan_partitions(struct gendisk *disk, struct block_device *bdev)
{
	kdev_t dev = to_kdev_t(bdev->bd_dev);
	struct parsed_partitions *state;
	int p, res;

	if (!bdev->bd_invalidated)
		return 0;
	if (bdev->bd_part_count)
		return -EBUSY;
	res = invalidate_device(dev, 1);
	if (res)
		return res;
	bdev->bd_invalidated = 0;
	for (p = 1; p < disk->minors; p++)
		delete_partition(disk, p);
	if (bdev->bd_op->revalidate_disk)
		bdev->bd_op->revalidate_disk(disk);
	else if (bdev->bd_op->revalidate)
		bdev->bd_op->revalidate(dev);
	if (!get_capacity(disk) || !(state = check_partition(disk, bdev)))
		return res;
	for (p = 1; p < state->limit; p++) {
		sector_t size = state->parts[p].size;
		sector_t from = state->parts[p].from;
		if (!size)
			continue;
		add_partition(disk, p, from, size);
#if CONFIG_BLK_DEV_MD
		if (state->parts[p].flags)
			md_autodetect_dev(bdev->bd_dev+p);
#endif
	}
	kfree(state);
	return res;
}

unsigned char *read_dev_sector(struct block_device *bdev, sector_t n, Sector *p)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;
	struct page *page;

	page = read_cache_page(mapping, (pgoff_t)(n >> (PAGE_CACHE_SHIFT-9)),
			(filler_t *)mapping->a_ops->readpage, NULL);
	if (!IS_ERR(page)) {
		wait_on_page_locked(page);
		if (!PageUptodate(page))
			goto fail;
		if (PageError(page))
			goto fail;
		p->v = page;
		return (unsigned char *)page_address(page) +  ((n & ((1 << (PAGE_CACHE_SHIFT - 9)) - 1)) << 9);
fail:
		page_cache_release(page);
	}
	p->v = NULL;
	return NULL;
}

void del_gendisk(struct gendisk *disk)
{
	int max_p = disk->minors;
	kdev_t devp;
	int p;

	/* invalidate stuff */
	for (p = max_p - 1; p > 0; p--) {
		devp = mk_kdev(disk->major,disk->first_minor + p);
		invalidate_device(devp, 1);
		delete_partition(disk, p);
	}
	devp = mk_kdev(disk->major,disk->first_minor);
	invalidate_device(devp, 1);
	disk->capacity = 0;
	disk->flags &= ~GENHD_FL_UP;
	unlink_gendisk(disk);
	devfs_remove_partitions(disk);
	device_remove_file(&disk->disk_dev, &disk_attr_dev);
	device_remove_file(&disk->disk_dev, &disk_attr_range);
	device_remove_file(&disk->disk_dev, &disk_attr_size);
	driverfs_remove_file(&disk->disk_dev.dir, "device");
	if (disk->driverfs_dev) {
		driverfs_remove_file(&disk->driverfs_dev->dir, "block");
		put_device(disk->driverfs_dev);
	}
	device_del(&disk->disk_dev);
}

struct dev_name {
	struct list_head list;
	dev_t dev;
	char namebuf[64];
	char *name;
};

static LIST_HEAD(device_names);

char *partition_name(dev_t dev)
{
	struct gendisk *hd;
	static char nomem [] = "<nomem>";
	struct dev_name *dname;
	struct list_head *tmp;
	int part;

	list_for_each(tmp, &device_names) {
		dname = list_entry(tmp, struct dev_name, list);
		if (dname->dev == dev)
			return dname->name;
	}

	dname = kmalloc(sizeof(*dname), GFP_KERNEL);

	if (!dname)
		return nomem;
	/*
	 * ok, add this new device name to the list
	 */
	hd = get_gendisk(dev, &part);
	dname->name = NULL;
	if (hd)
		dname->name = disk_name(hd, part, dname->namebuf);
	put_disk(hd);
	if (!dname->name) {
		sprintf(dname->namebuf, "[dev %s]", kdevname(to_kdev_t(dev)));
		dname->name = dname->namebuf;
	}

	dname->dev = dev;
	list_add(&dname->list, &device_names);

	return dname->name;
}
