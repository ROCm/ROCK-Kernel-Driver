/*
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

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/raid/md.h>

#include "check.h"

#include "acorn.h"
#include "amiga.h"
#include "atari.h"
#include "mac.h"
#include "msdos.h"
#include "osf.h"
#include "sgi.h"
#include "sun.h"
#include "ibm.h"
#include "ultrix.h"

extern void device_init(void);
extern int *blk_size[];
extern void rd_load(void);
extern void initrd_load(void);

struct gendisk *gendisk_head;
int warn_no_part = 1; /*This is ugly: should make genhd removable media aware*/

static int (*check_part[])(struct gendisk *hd, kdev_t dev, unsigned long first_sect, int first_minor) = {
#ifdef CONFIG_ACORN_PARTITION
	acorn_partition,
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
 * disk_name() is used by genhd.c and blkpg.c.
 * It formats the devicename of the indicated disk into
 * the supplied buffer (of size at least 32), and returns
 * a pointer to that same buffer (for convenience).
 */
char *disk_name (struct gendisk *hd, int minor, char *buf)
{
	unsigned int part;
	const char *maj = hd->major_name;
	int unit = (minor >> hd->minor_shift) + 'a';

	part = minor & ((1 << hd->minor_shift) - 1);
	if (hd->part[minor].de) {
		int pos;

		pos = devfs_generate_path (hd->part[minor].de, buf, 64);
		if (pos >= 0)
			return buf + pos;
	}
	/*
	 * IDE devices use multiple major numbers, but the drives
	 * are named as:  {hda,hdb}, {hdc,hdd}, {hde,hdf}, {hdg,hdh}..
	 * This requires special handling here.
	 */
	switch (hd->major) {
		case IDE9_MAJOR:
			unit += 2;
		case IDE8_MAJOR:
			unit += 2;
		case IDE7_MAJOR:
			unit += 2;
		case IDE6_MAJOR:
			unit += 2;
		case IDE5_MAJOR:
			unit += 2;
		case IDE4_MAJOR:
			unit += 2;
		case IDE3_MAJOR:
			unit += 2;
		case IDE2_MAJOR:
			unit += 2;
		case IDE1_MAJOR:
			unit += 2;
		case IDE0_MAJOR:
			maj = "hd";
			break;
		case MD_MAJOR:
			sprintf(buf, "%s%d", maj, unit - 'a');
			return buf;
	}
	if (hd->major >= SCSI_DISK1_MAJOR && hd->major <= SCSI_DISK7_MAJOR) {
		unit = unit + (hd->major - SCSI_DISK1_MAJOR + 1) * 16;
		if (unit > 'z') {
			unit -= 'z' + 1;
			sprintf(buf, "sd%c%c", 'a' + unit / 26, 'a' + unit % 26);
			if (part)
				sprintf(buf + 4, "%d", part);
			return buf;
		}
	}
	if (hd->major >= COMPAQ_SMART2_MAJOR && hd->major <= COMPAQ_SMART2_MAJOR+7) {
		int ctlr = hd->major - COMPAQ_SMART2_MAJOR;
 		int disk = minor >> hd->minor_shift;
 		int part = minor & (( 1 << hd->minor_shift) - 1);
 		if (part == 0)
 			sprintf(buf, "%s/c%dd%d", maj, ctlr, disk);
 		else
 			sprintf(buf, "%s/c%dd%dp%d", maj, ctlr, disk, part);
 		return buf;
 	}
	if (hd->major >= COMPAQ_CISS_MAJOR && hd->major <= COMPAQ_CISS_MAJOR+7) {
                int ctlr = hd->major - COMPAQ_CISS_MAJOR;
                int disk = minor >> hd->minor_shift;
                int part = minor & (( 1 << hd->minor_shift) - 1);
                if (part == 0)
                        sprintf(buf, "%s/c%dd%d", maj, ctlr, disk);
                else
                        sprintf(buf, "%s/c%dd%dp%d", maj, ctlr, disk, part);
                return buf;
	}
	if (hd->major >= DAC960_MAJOR && hd->major <= DAC960_MAJOR+7) {
		int ctlr = hd->major - DAC960_MAJOR;
 		int disk = minor >> hd->minor_shift;
 		int part = minor & (( 1 << hd->minor_shift) - 1);
 		if (part == 0)
 			sprintf(buf, "%s/c%dd%d", maj, ctlr, disk);
 		else
 			sprintf(buf, "%s/c%dd%dp%d", maj, ctlr, disk, part);
 		return buf;
 	}
	if (part)
		sprintf(buf, "%s%c%d", maj, unit, part);
	else
		sprintf(buf, "%s%c", maj, unit);
	return buf;
}

/*
 * Add a partitions details to the devices partition description.
 */
void add_gd_partition(struct gendisk *hd, int minor, int start, int size)
{
#ifndef CONFIG_DEVFS_FS
	char buf[40];
#endif

	hd->part[minor].start_sect = start;
	hd->part[minor].nr_sects   = size;
#ifdef CONFIG_DEVFS_FS
	printk(" p%d", (minor & ((1 << hd->minor_shift) - 1)));
#else
	if ((hd->major >= COMPAQ_SMART2_MAJOR+0 && hd->major <= COMPAQ_SMART2_MAJOR+7) ||
	    (hd->major >= COMPAQ_CISS_MAJOR+0 && hd->major <= COMPAQ_CISS_MAJOR+7))
		printk(" p%d", (minor & ((1 << hd->minor_shift) - 1)));
	else
		printk(" %s", disk_name(hd, minor, buf));
#endif
}

unsigned int get_ptable_blocksize(kdev_t dev)
{
	int ret = 1024;

	/*
	 * See whether the low-level driver has given us a minumum blocksize.
	 * If so, check to see whether it is larger than the default of 1024.
	 */
	if (!blksize_size[MAJOR(dev)])
		return ret;

	/*
	 * Check for certain special power of two sizes that we allow.
	 * With anything larger than 1024, we must force the blocksize up to
	 * the natural blocksize for the device so that we don't have to try
	 * and read partial sectors.  Anything smaller should be just fine.
	 */

	switch (blksize_size[MAJOR(dev)][MINOR(dev)]) {
		case 2048:
			ret = 2048;
			break;
		case 4096:
			ret = 4096;
			break;
		case 8192:
			ret = 8192;
			break;
		case 1024:
		case 512:
		case 256:
		case 0:
			/*
			 * These are all OK.
			 */
			break;
		default:
			panic("Strange blocksize for partition table\n");
	}

	return ret;
}

#ifdef CONFIG_PROC_FS
int get_partition_list(char *page, char **start, off_t offset, int count)
{
	struct gendisk *dsk;
	int len;

	len = sprintf(page, "major minor  #blocks  name\n\n");
	for (dsk = gendisk_head; dsk; dsk = dsk->next) {
		int n;

		for (n = 0; n < (dsk->nr_real << dsk->minor_shift); n++)
			if (dsk->part[n].nr_sects) {
				char buf[64];

				len += sprintf(page + len,
					       "%4d  %4d %10d %s\n",
					       dsk->major, n, dsk->sizes[n],
					       disk_name(dsk, n, buf));
				if (len < offset)
					offset -= len, len = 0;
				else if (len >= offset + count)
					goto leave_loops;
			}
	}
leave_loops:
	*start = page + offset;
	len -= offset;
	if (len < 0)
		len = 0;
	return len > count ? count : len;
}
#endif

static void check_partition(struct gendisk *hd, kdev_t dev, int first_part_minor)
{
	devfs_handle_t de = NULL;
	static int first_time = 1;
	unsigned long first_sector;
	char buf[64];
	int i;

	if (first_time)
		printk(KERN_INFO "Partition check:\n");
	first_time = 0;
	first_sector = hd->part[MINOR(dev)].start_sect;

	/*
	 * This is a kludge to allow the partition check to be
	 * skipped for specific drives (e.g. IDE CD-ROM drives)
	 */
	if ((int)first_sector == -1) {
		hd->part[MINOR(dev)].start_sect = 0;
		return;
	}

	if (hd->de_arr)
		de = hd->de_arr[MINOR(dev) >> hd->minor_shift];
	i = devfs_generate_path (de, buf, sizeof buf);
	if (i >= 0)
		printk(KERN_INFO " /dev/%s:", buf + i);
	else
		printk(KERN_INFO " %s:", disk_name(hd, MINOR(dev), buf));
	for (i = 0; check_part[i]; i++)
		if (check_part[i](hd, dev, first_sector, first_part_minor))
			goto setup_devfs;

	printk(" unknown partition table\n");
setup_devfs:
	i = first_part_minor - 1;
	devfs_register_partitions (hd, i, hd->sizes ? 0 : 1);
}

#ifdef CONFIG_DEVFS_FS
static void devfs_register_partition (struct gendisk *dev, int minor, int part)
{
	int devnum = minor >> dev->minor_shift;
	devfs_handle_t dir;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	char devname[16];

	if (dev->part[minor + part].de) return;
	dir = devfs_get_parent (dev->part[minor].de);
	if (!dir) return;
	if ( dev->flags && (dev->flags[devnum] & GENHD_FL_REMOVABLE) )
		devfs_flags |= DEVFS_FL_REMOVABLE;
	sprintf (devname, "part%d", part);
	dev->part[minor + part].de =
	    devfs_register (dir, devname, devfs_flags,
			    dev->major, minor + part,
			    S_IFBLK | S_IRUSR | S_IWUSR,
			    dev->fops, NULL);
}

static void devfs_register_disc (struct gendisk *dev, int minor)
{
	int pos = 0;
	int devnum = minor >> dev->minor_shift;
	devfs_handle_t dir, slave;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	char dirname[64], symlink[16];
	static unsigned int disc_counter;
	static devfs_handle_t devfs_handle;

	if (dev->part[minor].de) return;
	if ( dev->flags && (dev->flags[devnum] & GENHD_FL_REMOVABLE) )
		devfs_flags |= DEVFS_FL_REMOVABLE;
	if (dev->de_arr) {
		dir = dev->de_arr[devnum];
		if (!dir)  /*  Aware driver wants to block disc management  */
			return;
		pos = devfs_generate_path (dir, dirname + 3, sizeof dirname-3);
		if (pos < 0) return;
		strncpy (dirname + pos, "../", 3);
	}
	else {
		/*  Unaware driver: construct "real" directory  */
		sprintf (dirname, "../%s/disc%d", dev->major_name, devnum);
		dir = devfs_mk_dir (NULL, dirname + 3, NULL);
	}
	if (!devfs_handle)
		devfs_handle = devfs_mk_dir (NULL, "discs", NULL);
	sprintf (symlink, "disc%u", disc_counter++);
	devfs_mk_symlink (devfs_handle, symlink, DEVFS_FL_DEFAULT,
			  dirname + pos, &slave, NULL);
	dev->part[minor].de =
	    devfs_register (dir, "disc", devfs_flags, dev->major, minor,
			    S_IFBLK | S_IRUSR | S_IWUSR, dev->fops, NULL);
	devfs_auto_unregister (dev->part[minor].de, slave);
	if (!dev->de_arr)
		devfs_auto_unregister (slave, dir);
}
#endif  /*  CONFIG_DEVFS_FS  */

void devfs_register_partitions (struct gendisk *dev, int minor, int unregister)
{
#ifdef CONFIG_DEVFS_FS
	int part;

	if (!unregister)
		devfs_register_disc (dev, minor);
	for (part = 1; part < dev->max_p; part++) {
		if ( unregister || (dev->part[part + minor].nr_sects < 1) ) {
			devfs_unregister (dev->part[part + minor].de);
			dev->part[part + minor].de = NULL;
			continue;
		}
		devfs_register_partition (dev, minor, part);
	}
	if (unregister) {
		devfs_unregister (dev->part[minor].de);
		dev->part[minor].de = NULL;
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
	grok_partitions(gdev, MINOR(dev)>>gdev->minor_shift, minors, size);
}

void grok_partitions(struct gendisk *dev, int drive, unsigned minors, long size)
{
	int i;
	int first_minor	= drive << dev->minor_shift;
	int end_minor	= first_minor + dev->max_p;

	if(!dev->sizes)
		blk_size[dev->major] = NULL;

	dev->part[first_minor].nr_sects = size;
	/* No Such Agen^Wdevice or no minors to use for partitions */
	if (!size || minors == 1)
		return;

	blk_size[dev->major] = NULL;
	check_partition(dev, MKDEV(dev->major, first_minor), 1 + first_minor);

 	/*
 	 * We need to set the sizes array before we will be able to access
 	 * any of the partitions on this device.
 	 */
	if (dev->sizes != NULL) {	/* optional safeguard in ll_rw_blk.c */
		for (i = first_minor; i < end_minor; i++)
			dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
		blk_size[dev->major] = dev->sizes;
	}
}

int __init partition_setup(void)
{
	device_init();

#ifdef CONFIG_BLK_DEV_RAM
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && mount_initrd) initrd_load();
	else
#endif
	rd_load();
#endif
	return 0;
}

__initcall(partition_setup);
