/*
   hptraid.c  Copyright (C) 2001 Red Hat, Inc. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
   
   Authors: 	Arjan van de Ven <arjanv@redhat.com>

   Based on work
   	Copyleft  (C) 2001 by Wilfried Weissmann <wweissmann@gmx.at>
	Copyright (C) 1994-96 Marc ZYNGIER <zyngier@ufr-info-p7.ibp.fr>
   Based on work done by Søren Schmidt for FreeBSD

   
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/genhd.h>
#include <linux/ioctl.h>

#include <linux/ide.h>
#include <asm/uaccess.h>

#include "ataraid.h"

static int hptraid_open(struct inode *inode, struct file *filp);
static int hptraid_release(struct inode *inode, struct file *filp);
static int hptraid_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg);
static int hptraid_make_request(request_queue_t * q, int rw,
				struct buffer_head *bh);



struct hptdisk {
	kdev_t device;
	unsigned long sectors;
	struct block_device *bdev;
};

struct hptraid {
	unsigned int stride;
	unsigned int disks;
	unsigned long sectors;
	struct geom geom;

	struct hptdisk disk[8];

	unsigned long cutoff[8];
	unsigned int cutoff_disks[8];
};

static struct raid_device_operations hptraid_ops = {
	.open = hptraid_open,
	.release = hptraid_release,
	.ioctl = hptraid_ioctl,
	.make_request = hptraid_make_request
};

static struct hptraid raid[16];

static int hptraid_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	unsigned int minor;
	unsigned char val;
	unsigned long sectors;

	if (!inode || kdev_none(inode->i_rdev))
		return -EINVAL;

	minor = minor(inode->i_rdev) >> SHIFT;

	switch (cmd) {
	case BLKGETSIZE:	/* Return device size */
		if (!arg)
			return -EINVAL;
		sectors =
		    ataraid_gendisk.part[minor(inode->i_rdev)].nr_sects;
		if (minor(inode->i_rdev) & 15)
			return put_user(sectors, (unsigned long *) arg);
		return put_user(raid[minor].sectors,
				(unsigned long *) arg);
		break;


	case HDIO_GETGEO:
		{
			struct hd_geometry *loc =
			    (struct hd_geometry *) arg;
			unsigned short bios_cyl;

			if (!loc)
				return -EINVAL;
			val = 255;
			if (put_user(val, (u8 *) & loc->heads))
				return -EFAULT;
			val = 63;
			if (put_user(val, (u8 *) & loc->sectors))
				return -EFAULT;
			bios_cyl = raid[minor].sectors / 63 / 255;
			if (put_user
			    (bios_cyl, (unsigned short *) &loc->cylinders))
				return -EFAULT;
			if (put_user
			    ((unsigned) ataraid_gendisk.
			     part[minor(inode->i_rdev)].start_sect,
			     (unsigned long *) &loc->start))
				return -EFAULT;
			return 0;
		}

	default:
		return -EINVAL;
	};

	return 0;
}


static int hptraid_make_request(request_queue_t * q, int rw,
				struct buffer_head *bh)
{
	unsigned long rsect;
	unsigned long rsect_left, rsect_accum = 0;
	unsigned long block;
	unsigned int disk = 0, real_disk = 0;
	int i;
	int device;
	struct hptraid *thisraid;

	rsect = bh->b_rsector;

	/* Ok. We need to modify this sector number to a new disk + new sector number. 
	 * If there are disks of different sizes, this gets tricky. 
	 * Example with 3 disks (1Gb, 4Gb and 5 GB):
	 * The first 3 Gb of the "RAID" are evenly spread over the 3 disks.
	 * Then things get interesting. The next 2Gb (RAID view) are spread across disk 2 and 3
	 * and the last 1Gb is disk 3 only.
	 *
	 * the way this is solved is like this: We have a list of "cutoff" points where everytime
	 * a disk falls out of the "higher" count, we mark the max sector. So once we pass a cutoff
	 * point, we have to divide by one less.
	 */

	device = (bh->b_rdev >> SHIFT) & MAJOR_MASK;
	thisraid = &raid[device];
	if (thisraid->stride == 0)
		thisraid->stride = 1;

	/* Partitions need adding of the start sector of the partition to the requested sector */

	rsect += ataraid_gendisk.part[minor(bh->b_rdev)].start_sect;

	/* Woops we need to split the request to avoid crossing a stride barrier */
	if ((rsect / thisraid->stride) !=
	    ((rsect + (bh->b_size / 512) - 1) / thisraid->stride)) {
		return -1;
	}

	rsect_left = rsect;

	for (i = 0; i < 8; i++) {
		if (thisraid->cutoff_disks[i] == 0)
			break;
		if (rsect > thisraid->cutoff[i]) {
			/* we're in the wrong area so far */
			rsect_left -= thisraid->cutoff[i];
			rsect_accum +=
			    thisraid->cutoff[i] /
			    thisraid->cutoff_disks[i];
		} else {
			block = rsect_left / thisraid->stride;
			disk = block % thisraid->cutoff_disks[i];
			block =
			    (block / thisraid->cutoff_disks[i]) *
			    thisraid->stride;
			rsect =
			    rsect_accum + (rsect_left % thisraid->stride) +
			    block;
			break;
		}
	}

	for (i = 0; i < 8; i++) {
		if ((disk == 0)
		    && (thisraid->disk[i].sectors > rsect_accum)) {
			real_disk = i;
			break;
		}
		if ((disk > 0)
		    && (thisraid->disk[i].sectors >= rsect_accum)) {
			disk--;
		}

	}
	disk = real_disk;

	/* All but the first disk have a 10 sector offset */
	if (i > 0)
		rsect += 10;


	/*
	 * The new BH_Lock semantics in ll_rw_blk.c guarantee that this
	 * is the only IO operation happening on this bh.
	 */

	bh->b_rdev = thisraid->disk[disk].device;
	bh->b_rsector = rsect;

	/*
	 * Let the main block layer submit the IO and resolve recursion:
	 */
	return 1;
}


#include "hptraid.h"

static int __init read_disk_sb(struct block_device *bdev,
			       struct highpoint_raid_conf *buf)
{
	/* Superblock is at 9*512 bytes */
	Sector sect;
	unsigned char *p = read_dev_sector(bdev, 9, &sect);

	if (p) {
		memcpy(buf, p, 512);
		put_dev_sector(&sect);
		return 0;
	}
	printk(KERN_ERR "hptraid: Error reading superblock.\n");
	return -1;
}

static unsigned long maxsectors(int major, int minor)
{
	unsigned long lba = 0;
	kdev_t dev;
	struct ata_device *ideinfo;

	dev = mk_kdev(major, minor);
	ideinfo = get_info_ptr(dev);
	if (ideinfo == NULL)
		return 0;


	/* first sector of the last cluster */
	if (ideinfo->head == 0)
		return 0;
	if (ideinfo->sect == 0)
		return 0;
	lba = (ideinfo->capacity);

	return lba;
}

static struct highpoint_raid_conf __initdata prom;
static void __init probedisk(int major, int minor, int device)
{
	int i;
	struct block_device *bdev = bdget(mk_kdev(major, minor));
	struct gendisk *gd;

	if (!bdev)
		return;

	if (blkdev_get(bdev, FMODE_READ | FMODE_WRITE, 0, BDEV_RAW) < 0)
		return;

	if (maxsectors(major, minor) == 0)
		goto out;

	if (read_disk_sb(bdev, &prom))
		goto out;

	if (prom.magic != 0x5a7816f0)
		goto out;
	if (prom.type) {
		printk(KERN_INFO
		       "hptraid: only RAID0 is supported currently\n");
		goto out;
	}

	i = prom.disk_number;
	if (i < 0)
		goto out;
	if (i > 8)
		goto out;

	raid[device].disk[i].bdev = bdev;
	/* This is supposed to prevent others from stealing our underlying disks */
	/* now blank the /proc/partitions table for the wrong partition table,
	   so that scripts don't accidentally mount it and crash the kernel */
	/* XXX: the 0 is an utter hack  --hch */
	gd = get_gendisk(mk_kdev(major, 0));
	if (gd != NULL) {
		int j;
		for (j = 1 + (minor << gd->minor_shift);
		     j < ((minor + 1) << gd->minor_shift); j++)
			gd->part[j].nr_sects = 0;
	}

	raid[device].disk[i].device = mk_kdev(major, minor);
	raid[device].disk[i].sectors = maxsectors(major, minor);
	raid[device].stride = (1 << prom.raid0_shift);
	raid[device].disks = prom.raid_disks;
	raid[device].sectors = prom.total_secs;
	return;
      out:
	blkdev_put(bdev);
}

static void __init fill_cutoff(int device)
{
	int i, j;
	unsigned long smallest;
	unsigned long bar;
	int count;

	bar = 0;
	for (i = 0; i < 8; i++) {
		smallest = ~0;
		for (j = 0; j < 8; j++)
			if ((raid[device].disk[j].sectors < smallest)
			    && (raid[device].disk[j].sectors > bar))
				smallest = raid[device].disk[j].sectors;
		count = 0;
		for (j = 0; j < 8; j++)
			if (raid[device].disk[j].sectors >= smallest)
				count++;

		smallest = smallest * count;
		bar = smallest;
		raid[device].cutoff[i] = smallest;
		raid[device].cutoff_disks[i] = count;

	}
}


static __init int hptraid_init_one(int device)
{
	int i, count;

	probedisk(IDE0_MAJOR, 0, device);
	probedisk(IDE0_MAJOR, 64, device);
	probedisk(IDE1_MAJOR, 0, device);
	probedisk(IDE1_MAJOR, 64, device);
	probedisk(IDE2_MAJOR, 0, device);
	probedisk(IDE2_MAJOR, 64, device);
	probedisk(IDE3_MAJOR, 0, device);
	probedisk(IDE3_MAJOR, 64, device);

	fill_cutoff(device);

	/* Initialize the gendisk structure */

	ataraid_register_disk(device, raid[device].sectors);

	count = 0;
	printk(KERN_INFO
	       "Highpoint HPT370 Softwareraid driver for linux version 0.01\n");

	for (i = 0; i < 8; i++) {
		if (raid[device].disk[i].device != 0) {
			printk(KERN_INFO "Drive %i is %li Mb \n",
			       i, raid[device].disk[i].sectors / 2048);
			count++;
		}
	}
	if (count) {
		printk(KERN_INFO "Raid array consists of %i drives. \n",
		       count);
		return 0;
	} else {
		printk(KERN_INFO "No raid array found\n");
		return -ENODEV;
	}

}

static __init int hptraid_init(void)
{
	int retval, device;

	device = ataraid_get_device(&hptraid_ops);
	if (device < 0)
		return -ENODEV;
	retval = hptraid_init_one(device);
	if (retval)
		ataraid_release_device(device);
	return retval;
}

static void __exit hptraid_exit(void)
{
	int i, device;
	for (device = 0; device < 16; device++) {
		for (i = 0; i < 8; i++) {
			struct block_device *bdev =
			    raid[device].disk[i].bdev;
			raid[device].disk[i].bdev = NULL;
			if (bdev)
				blkdev_put(bdev, BDEV_RAW);
		}
		if (raid[device].sectors)
			ataraid_release_device(device);
	}
}

static int hptraid_open(struct inode *inode, struct file *filp)
{
	MOD_INC_USE_COUNT;
	return 0;
}
static int hptraid_release(struct inode *inode, struct file *filp)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

module_init(hptraid_init);
module_exit(hptraid_exit);
MODULE_LICENSE("GPL");
