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
extern void md_autodetect_dev(kdev_t dev);
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

char *disk_name (struct gendisk *hd, int minor, char *buf)
{
	unsigned int unit = (minor >> hd->minor_shift);
	unsigned int part = (minor & ((1 << hd->minor_shift) -1 ));
	struct hd_struct *p = hd->part + minor - hd->first_minor;
	char s[40];
	const char *maj;

	if ((((minor - hd->first_minor) >> hd->minor_shift) < hd->nr_real) &&
	     p->de) {
		int pos;

		pos = devfs_generate_path(p->de, buf, 64);
		if (pos >= 0)
			return buf + pos;
	}

	/*
	 * Yes, I know, ... in cases is gccism and not a pretty one.  
	 * However, the first variant will eventually consume _all_ cases
	 * and switch will disappear.
	 */
	switch (hd->major) {
		default:
			maj = hd->major_name;
			break;
		case MD_MAJOR:
			sprintf(s, "%s%d", "md", unit);
			maj = s;
			break;
		case I2O_MAJOR:
			sprintf(s, "%s%c", hd->major_name, unit + 'a');
			maj = s;
	}
	if (!part)
		sprintf(buf, "%s", maj);
	else if (isdigit(maj[strlen(maj)-1]))
		sprintf(buf, "%sp%d", maj, part);
	else
		sprintf(buf, "%s%d", maj, part);
	return buf;
}

/* Driverfs file support */
static ssize_t partition_device_kdev_read(struct device *driverfs_dev, 
			char *page, size_t count, loff_t off)
{
	kdev_t kdev; 
	kdev.value=(int)(long)driverfs_dev->driver_data;
	return off ? 0 : sprintf (page, "%x\n",kdev.value);
}
static DEVICE_ATTR(kdev,S_IRUGO,partition_device_kdev_read,NULL);

static ssize_t partition_device_type_read(struct device *driverfs_dev, 
			char *page, size_t count, loff_t off) 
{
	return off ? 0 : sprintf (page, "BLK\n");
}
static DEVICE_ATTR(type,S_IRUGO,partition_device_type_read,NULL);

void driverfs_create_partitions(struct gendisk *hd, int minor)
{
	int pos = -1;
	int devnum = (minor - hd->first_minor) >> hd->minor_shift;
	char dirname[256];
	struct device *parent = 0;
	int max_p;
	int part;
	devfs_handle_t dir = 0;
	struct hd_struct *p = hd->part + minor - hd->first_minor;
	
	/* get parent driverfs device structure */
	if (hd->driverfs_dev_arr)
		parent = hd->driverfs_dev_arr[devnum];
	else /* if driverfs not supported by subsystem, skip partitions */
		return;
	
	/* get parent device node directory name */
	if (hd->de_arr) {
		dir = hd->de_arr[devnum];
		if (dir)
			pos = devfs_generate_path (dir, dirname, 
						   sizeof dirname);
	}
	
	if (pos < 0) {
		disk_name(hd, minor, dirname);
		pos = 0;
	}
	
	max_p = (1 << hd->minor_shift);
	
	/* for all partitions setup parents and device node names */
	for(part=0; part < max_p; part++) {
		if ((part == 0) || (p[part].nr_sects >= 1)) {
			struct device * current_driverfs_dev = 
				&p[part].hd_driverfs_dev;
			current_driverfs_dev->parent = parent;
			/* handle disc case */
			current_driverfs_dev->driver_data =
					(void *)(long)__mkdev(hd->major, minor+part);
			if (part == 0) {
				if (parent)  {
					sprintf(current_driverfs_dev->name,
						"%sdisc", parent->name);
					sprintf(current_driverfs_dev->bus_id,
						"%s:disc", parent->bus_id);
				} else {
					sprintf(current_driverfs_dev->name, 
						"disc");
					sprintf(current_driverfs_dev->bus_id,
						"disc");
				}
			} else { /* this is a partition */
				if (parent) {
					sprintf(current_driverfs_dev->name,
						"%spart%d", parent->name, part);
					sprintf(current_driverfs_dev->bus_id,
						"%s:p%d", parent->bus_id, part);
				} else {
					sprintf(current_driverfs_dev->name, 
						"part%d", part);
					sprintf(current_driverfs_dev->bus_id, 
						"p%d" ,part);
				}
			}
			if (parent) current_driverfs_dev->bus = parent->bus;
			device_register(current_driverfs_dev);
			device_create_file(current_driverfs_dev,
					   &dev_attr_type);
			device_create_file(current_driverfs_dev,
					   &dev_attr_kdev);
		}
	}
}

void driverfs_remove_partitions(struct gendisk *hd, int minor)
{
	int max_p;
	int part;
	struct device * current_driverfs_dev;
	struct hd_struct *p = hd->part + minor - hd->first_minor;
	
	max_p=(1 << hd->minor_shift);
	
	/* for all parts setup parent relationships and device node names */
	for(part=1; part < max_p; part++) {
		if ((p[part].nr_sects >= 1)) {
			current_driverfs_dev = &p[part].hd_driverfs_dev;
			device_remove_file(current_driverfs_dev,
					   &dev_attr_type);
			device_remove_file(current_driverfs_dev,
					   &dev_attr_kdev);
			put_device(current_driverfs_dev);	
		}
	}
	current_driverfs_dev = &p->hd_driverfs_dev;
	device_remove_file(current_driverfs_dev,
			   &dev_attr_type);
	device_remove_file(current_driverfs_dev,
			   &dev_attr_kdev);
	put_device(current_driverfs_dev);	
	return;
}

/*
 *	DON'T EXPORT
 */
void check_partition(struct gendisk *hd, struct block_device *bdev)
{
	devfs_handle_t de = NULL;
	kdev_t dev = to_kdev_t(bdev->bd_dev);
	char buf[64];
	struct parsed_partitions *state;
	int i;

	state = kmalloc(sizeof(struct parsed_partitions), GFP_KERNEL);
	if (!state)
		return;

	if (hd->de_arr)
		de = hd->de_arr[(minor(dev)-hd->first_minor)>>hd->minor_shift];
	i = devfs_generate_path (de, buf, sizeof buf);
	if (i >= 0) {
		printk(KERN_INFO " /dev/%s:", buf + i);
		sprintf(state->name, "p");
	} else {
		unsigned n = hd->major;
		disk_name(hd, minor(dev), state->name);
		printk(KERN_INFO " %s:", state->name);
		if (n - COMPAQ_SMART2_MAJOR <= 7 || n - COMPAQ_CISS_MAJOR <= 7)
			sprintf(state->name, "p");
	}
	state->limit = 1<<hd->minor_shift;
	for (i = 0; check_part[i]; i++) {
		int res, j;
		struct hd_struct *p;
		memset(&state->parts, 0, sizeof(state->parts));
		res = check_part[i](state, bdev);
		if (!res)
			continue;
		if (res < 0) {
			if (warn_no_part)
				printk(" unable to read partition table\n");
			goto out;
		} 
		p = hd->part + minor(dev) - hd->first_minor;
		for (j = 1; j < state->limit; j++) {
			p[j].start_sect = state->parts[j].from;
			p[j].nr_sects = state->parts[j].size;
#if CONFIG_BLK_DEV_MD
			if (!state->parts[j].flags)
				continue;
			md_autodetect_dev(mk_kdev(major(dev),minor(dev)+j));
#endif
		}
		goto out;
	}

	printk(" unknown partition table\n");
out:
	driverfs_create_partitions(hd, minor(dev));
	devfs_register_partitions (hd, minor(dev), 0);
}

#ifdef CONFIG_DEVFS_FS
static void devfs_register_partition (struct gendisk *dev, int minor, int part)
{
	int devnum = (minor - dev->first_minor) >> dev->minor_shift;
	devfs_handle_t dir;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	struct hd_struct *p = dev->part + minor - dev->first_minor;
	char devname[16];

	if (p[part].de)
		return;
	dir = devfs_get_parent(p[0].de);
	if (!dir)
		return;
	if ( dev->flags && (dev->flags[devnum] & GENHD_FL_REMOVABLE) )
		devfs_flags |= DEVFS_FL_REMOVABLE;
	sprintf (devname, "part%d", part);
	p[part].de = devfs_register (dir, devname, devfs_flags,
				    dev->major, minor + part,
				    S_IFBLK | S_IRUSR | S_IWUSR,
				    dev->fops, NULL);
}

static struct unique_numspace disc_numspace = UNIQUE_NUMBERSPACE_INITIALISER;

static void devfs_register_disc (struct gendisk *dev, int minor)
{
	int pos = 0;
	int devnum = (minor - dev->first_minor) >> dev->minor_shift;
	devfs_handle_t dir, slave;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	char dirname[64], symlink[16];
	static devfs_handle_t devfs_handle;
	struct hd_struct *p = dev->part + minor - dev->first_minor;

	if (p[0].de)
		return;
	if ( dev->flags && (dev->flags[devnum] & GENHD_FL_REMOVABLE) )
		devfs_flags |= DEVFS_FL_REMOVABLE;
	if (dev->de_arr) {
		dir = dev->de_arr[devnum];
		if (!dir)  /*  Aware driver wants to block disc management  */
			return;
		pos = devfs_generate_path(dir, dirname + 3, sizeof dirname-3);
		if (pos < 0)
			return;
		strncpy(dirname + pos, "../", 3);
	} else {
		/*  Unaware driver: construct "real" directory  */
		sprintf(dirname, "../%s/disc%d", dev->major_name,
			(dev->first_minor >> dev->minor_shift) + devnum);
		dir = devfs_mk_dir(NULL, dirname + 3, NULL);
	}
	if (!devfs_handle)
		devfs_handle = devfs_mk_dir(NULL, "discs", NULL);
	p[0].number = devfs_alloc_unique_number (&disc_numspace);
	sprintf(symlink, "disc%d", p[0].number);
	devfs_mk_symlink (devfs_handle, symlink, DEVFS_FL_DEFAULT,
			  dirname + pos, &slave, NULL);
	p[0].de = devfs_register (dir, "disc", devfs_flags, dev->major, minor,
			    S_IFBLK | S_IRUSR | S_IWUSR, dev->fops, NULL);
	devfs_auto_unregister(p[0].de, slave);
	if (!dev->de_arr)
		devfs_auto_unregister (slave, dir);
}
#endif  /*  CONFIG_DEVFS_FS  */

void devfs_register_partitions (struct gendisk *dev, int minor, int unregister)
{
#ifdef CONFIG_DEVFS_FS
	int part, max_p;
	struct hd_struct *p = dev->part + minor - dev->first_minor;

	if (!unregister)
		devfs_register_disc (dev, minor);
	max_p = (1 << dev->minor_shift);
	for (part = 1; part < max_p; part++) {
		if ( unregister || (p[part].nr_sects < 1) ) {
			devfs_unregister(p[part].de);
			p[part].de = NULL;
			continue;
		}
		devfs_register_partition (dev, minor, part);
	}
	if (unregister) {
		devfs_unregister(p[0].de);
		p[0].de = NULL;
		devfs_dealloc_unique_number(&disc_numspace, p[0].number);
	}
#endif  /*  CONFIG_DEVFS_FS  */
}

/*
 * This function will re-read the partition tables for a given device,
 * and set things back up again.  There are some important caveats,
 * however.  You must ensure that no one is using the device, and no one
 * can start using the device while this function is being executed.
 *
 * Much of the cleanup from the old partition tables should have already been
 * done
 */

void register_disk(struct gendisk *gdev, kdev_t dev, unsigned minors,
	struct block_device_operations *ops, long size)
{
	if (!gdev)
		return;
	grok_partitions(dev, size);
}

void grok_partitions(kdev_t dev, long size)
{
	struct block_device *bdev;
	struct gendisk *g = get_gendisk(dev);
	struct hd_struct *p;

	if (!g)
		return;

	p = g->part + minor(dev) - g->first_minor;
	p[0].nr_sects = size;

	/* No minors to use for partitions */
	if (!g->minor_shift)
		return;

	/* No such device (e.g., media were just removed) */
	if (!size)
		return;

	bdev = bdget(kdev_t_to_nr(dev));
	if (blkdev_get(bdev, FMODE_READ, 0, BDEV_RAW) < 0)
		return;
	check_partition(g, bdev);
	blkdev_put(bdev, BDEV_RAW);
}

unsigned char *read_dev_sector(struct block_device *bdev, unsigned long n, Sector *p)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;
	int sect = PAGE_CACHE_SIZE / 512;
	struct page *page;

	page = read_cache_page(mapping, n/sect,
			(filler_t *)mapping->a_ops->readpage, NULL);
	if (!IS_ERR(page)) {
		wait_on_page_locked(page);
		if (!PageUptodate(page))
			goto fail;
		if (PageError(page))
			goto fail;
		p->v = page;
		return (unsigned char *)page_address(page) + 512 * (n % sect);
fail:
		page_cache_release(page);
	}
	p->v = NULL;
	return NULL;
}

int wipe_partitions(kdev_t dev)
{
	struct gendisk *g;
	kdev_t devp;
	int p, major, minor, minor0, max_p, res;
	struct hd_struct *part;

	g = get_gendisk(dev);
	if (g == NULL)
		return -EINVAL;

	max_p = 1 << g->minor_shift;
	major = major(dev);
	minor = minor(dev);
	minor0 = minor & ~(max_p - 1);
	if (minor0 != minor)		/* for now only whole-disk reread */
		return -EINVAL;		/* %%% later.. */

	part = g->part + minor - g->first_minor;
	/* invalidate stuff */
	for (p = max_p - 1; p >= 0; p--) {
		minor = minor0 + p;
		devp = mk_kdev(major,minor);
#if 0					/* %%% superfluous? */
		if (part[p].nr_sects == 0)
			continue;
#endif
		res = invalidate_device(devp, 1);
		if (res)
			return res;
		part[p].start_sect = 0;
		part[p].nr_sects = 0;
	}
	return 0;
}
