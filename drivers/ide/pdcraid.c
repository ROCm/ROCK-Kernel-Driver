/*
   pdcraid.c  Copyright (C) 2001 Red Hat, Inc. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
   
   Authors: 	Arjan van de Ven <arjanv@redhat.com>
   		
   



*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/genhd.h>
#include <linux/ioctl.h>

#include <linux/ide.h>
#include <asm/uaccess.h>

#include "ataraid.h"

static int pdcraid_open(struct inode * inode, struct file * filp);
static int pdcraid_release(struct inode * inode, struct file * filp);
static int pdcraid_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
static int pdcraid_make_request (request_queue_t *q, int rw, struct buffer_head * bh);



struct pdcdisk {
	kdev_t	device;
	unsigned long sectors;
	struct block_device *bdev;
};

struct pdcraid {
	unsigned int stride;
	unsigned int disks;
	unsigned long sectors;
	struct geom geom;
	
	struct pdcdisk disk[8];
	
	unsigned long cutoff[8];
	unsigned int cutoff_disks[8];
};

static struct raid_device_operations pdcraid_ops = {
        open:                   pdcraid_open,
	release:                pdcraid_release,
	ioctl:			pdcraid_ioctl,
	make_request:		pdcraid_make_request
};

static struct pdcraid raid[16];

static int pdcraid_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int minor;
   	unsigned long sectors;

	if (!inode || !inode->i_rdev) 	
		return -EINVAL;

	minor = MINOR(inode->i_rdev)>>SHIFT;
	
	switch (cmd) {

         	case BLKGETSIZE:   /* Return device size */
 			if (!arg)  return -EINVAL;
			sectors = ataraid_gendisk.part[MINOR(inode->i_rdev)].nr_sects;
			if (MINOR(inode->i_rdev)&15)
				return put_user(sectors, (long *) arg);
			return put_user(raid[minor].sectors , (long *) arg);
			break;
			

		case HDIO_GETGEO:
		{
			struct hd_geometry *loc = (struct hd_geometry *) arg;
			unsigned short bios_cyl = raid[minor].geom.cylinders; /* truncate */
			
			if (!loc) return -EINVAL;
			if (put_user(raid[minor].geom.heads, (byte *) &loc->heads)) return -EFAULT;
			if (put_user(raid[minor].geom.sectors, (byte *) &loc->sectors)) return -EFAULT;
			if (put_user(bios_cyl, (unsigned short *) &loc->cylinders)) return -EFAULT;
			if (put_user((unsigned)ataraid_gendisk.part[MINOR(inode->i_rdev)].start_sect,
				(unsigned long *) &loc->start)) return -EFAULT;
			return 0;
		}

		case HDIO_GETGEO_BIG:
		{
			struct hd_big_geometry *loc = (struct hd_big_geometry *) arg;
			if (!loc) return -EINVAL;
			if (put_user(raid[minor].geom.heads, (byte *) &loc->heads)) return -EFAULT;
			if (put_user(raid[minor].geom.sectors, (byte *) &loc->sectors)) return -EFAULT;
			if (put_user(raid[minor].geom.cylinders, (unsigned int *) &loc->cylinders)) return -EFAULT;
			if (put_user((unsigned)ataraid_gendisk.part[MINOR(inode->i_rdev)].start_sect,
				(unsigned long *) &loc->start)) return -EFAULT;
			return 0;
		}

			
		case BLKROSET:
		case BLKROGET:
		case BLKSSZGET:
			return blk_ioctl(inode->i_rdev, cmd, arg);

		default:
			return -EINVAL;
	};

	return 0;
}


static int pdcraid_make_request (request_queue_t *q, int rw, struct buffer_head * bh)
{
	unsigned long rsect;
	unsigned long rsect_left,rsect_accum = 0;
	unsigned long block;
	unsigned int disk=0,real_disk=0;
	int i;
	int device;
	struct pdcraid *thisraid;

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
	
	device = (bh->b_rdev >> SHIFT)&MAJOR_MASK;
	thisraid = &raid[device];
	if (thisraid->stride==0)
		thisraid->stride=1;

	/* Partitions need adding of the start sector of the partition to the requested sector */
	
	rsect += ataraid_gendisk.part[MINOR(bh->b_rdev)].start_sect;

	/* Woops we need to split the request to avoid crossing a stride barrier */
	if ((rsect/thisraid->stride) != ((rsect+(bh->b_size/512)-1)/thisraid->stride)) {
		return -1;  
	}
	
	rsect_left = rsect;
	
	for (i=0;i<8;i++) {
		if (thisraid->cutoff_disks[i]==0)
			break;
		if (rsect > thisraid->cutoff[i]) {
			/* we're in the wrong area so far */
			rsect_left -= thisraid->cutoff[i];
			rsect_accum += thisraid->cutoff[i]/thisraid->cutoff_disks[i];
		} else {
			block = rsect_left / thisraid->stride;
			disk = block % thisraid->cutoff_disks[i];
			block = (block / thisraid->cutoff_disks[i]) * thisraid->stride;
			rsect = rsect_accum + (rsect_left % thisraid->stride) + block;
			break;
		}
	}
	
	for (i=0;i<8;i++) {
		if ((disk==0) && (thisraid->disk[i].sectors > rsect_accum)) {
			real_disk = i;
			break;
		}
		if ((disk>0) && (thisraid->disk[i].sectors >= rsect_accum)) {
			disk--;
		}
		
	}
	disk = real_disk;
		
	
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


#include "pdcraid.h"

static unsigned long calc_pdcblock_offset (int major,int minor)
{
	unsigned long lba = 0;
	kdev_t dev;
	ide_drive_t *ideinfo;
	
	dev = MKDEV(major,minor);
	ideinfo = get_info_ptr (dev);
	if (ideinfo==NULL)
		return 0;
	
	
	/* first sector of the last cluster */
	if (ideinfo->head==0) 
		return 0;
	if (ideinfo->sect==0)
		return 0;
	lba = (ideinfo->capacity / (ideinfo->head*ideinfo->sect));
	lba = lba * (ideinfo->head*ideinfo->sect);
	lba = lba - ideinfo->sect;

	return lba;
}


static int read_disk_sb (int major, int minor, unsigned char *buffer,int bufsize)
{
	int ret = -EINVAL;
	struct buffer_head *bh = NULL;
	kdev_t dev = MKDEV(major,minor);
	unsigned long sb_offset;

	if (blksize_size[major]==NULL)   /* device doesn't exist */
		return -EINVAL;
                       
	
	/*
	 * Calculate the position of the superblock,
	 * it's at first sector of the last cylinder
	 */
	sb_offset = calc_pdcblock_offset(major,minor)/8;
	/* The /8 transforms sectors into 4Kb blocks */

	if (sb_offset==0)
		return -1;	
	
	set_blocksize (dev, 4096);

	bh = bread (dev, sb_offset, 4096);
	
	if (bh) {
		memcpy (buffer, bh->b_data, bufsize);
	} else {
		printk(KERN_ERR "pdcraid: Error reading superblock.\n");
		goto abort;
	}
	ret = 0;
abort:
	if (bh)
		brelse (bh);
	return ret;
}

static unsigned int calc_sb_csum (unsigned int* ptr)
{	
	unsigned int sum;
	int count;
	
	sum = 0;
	for (count=0;count<511;count++)
		sum += *ptr++;
	
	return sum;
}

static void __init probedisk(int major, int minor,int device)
{
	int i;
        struct promise_raid_conf *prom;
	static unsigned char block[4096];
	
        if (read_disk_sb(major,minor,(unsigned char*)&block,sizeof(block)))
        	return;
                                                                                                                 
        prom = (struct promise_raid_conf*)&block[512];

        /* the checksums must match */
	if (prom->checksum != calc_sb_csum((unsigned int*)prom))
		return;
	if (prom->raid.type!=0x00) /* Only raid 0 is supported right now */
		return;
	

	/* This looks evil. But basically, we have to search for our adapternumber
	   in the arraydefinition, both of which are in the superblock */	
        for (i=0;(i<prom->raid.total_disks)&&(i<8);i++) {
        	if ( (prom->raid.disk[i].channel== prom->raid.channel) &&
        	     (prom->raid.disk[i].device == prom->raid.device) ) {
			struct block_device *bdev = bdget(MKDEV(major,minor));
			if (bdev && blkdev_get(bdev,FMODE_READ|FMODE_WRITE,0,BDEV_RAW) == 0) {
        	        	struct gendisk *gd;
        	        	int j;
        	        	/* This is supposed to prevent others from stealing our underlying disks */
				raid[device].disk[i].bdev = bdev;
				gd=get_gendisk(major);
				if (gd!=NULL) {
					for (j=1+(minor<<gd->minor_shift);j<((minor+1)<<gd->minor_shift);j++) 
						gd->part[j].nr_sects=0;					
					put_gendisk(gd);
				}
			}
			raid[device].disk[i].device = MKDEV(major,minor);
			raid[device].disk[i].sectors = prom->raid.disk_secs;
			raid[device].stride = (1<<prom->raid.raid0_shift);
			raid[device].disks = prom->raid.total_disks;
			raid[device].sectors = prom->raid.total_secs;
			raid[device].geom.heads = prom->raid.heads+1;
			raid[device].geom.sectors = prom->raid.sectors;
			raid[device].geom.cylinders = prom->raid.cylinders+1;
			
        	     }
        }
	               
}

static void __init fill_cutoff(int device)
{
	int i,j;
	unsigned long smallest;
	unsigned long bar;
	int count;
	
	bar = 0;
	for (i=0;i<8;i++) {
		smallest = ~0;
		for (j=0;j<8;j++) 
			if ((raid[device].disk[j].sectors < smallest) && (raid[device].disk[j].sectors>bar))
				smallest = raid[device].disk[j].sectors;
		count = 0;
		for (j=0;j<8;j++) 
			if (raid[device].disk[j].sectors >= smallest)
				count++;
				
		smallest = smallest * count;
		bar = smallest;
		raid[device].cutoff[i] = smallest;
		raid[device].cutoff_disks[i] = count;
	}
}
			   
static __init int pdcraid_init_one(int device)
{
	int i,count;

	probedisk(IDE0_MAJOR,  0, device);
	probedisk(IDE0_MAJOR, 64, device);
	probedisk(IDE1_MAJOR,  0, device);
	probedisk(IDE1_MAJOR, 64, device);
	probedisk(IDE2_MAJOR,  0, device);
	probedisk(IDE2_MAJOR, 64, device);
	probedisk(IDE3_MAJOR,  0, device);
	probedisk(IDE3_MAJOR, 64, device);
	
	fill_cutoff(device);
	
	/* Initialize the gendisk structure */
	
	ataraid_register_disk(device,raid[device].sectors);        
		
	count=0;
	printk(KERN_INFO "Promise Fasttrak(tm) Softwareraid driver for linux version 0.02\n");
	
	for (i=0;i<8;i++) {
		if (raid[device].disk[i].device!=0) {
			printk(KERN_INFO "Drive %i is %li Mb \n",
				i,raid[device].disk[i].sectors/2048);
			count++;
		}
	}
	if (count) {
		printk(KERN_INFO "Raid array consists of %i drives. \n",count);
		return 0;
	} else {
		printk(KERN_INFO "No raid array found\n");
		return -ENODEV;
	}
}

static __init int pdcraid_init(void)
{
	int retval,device;
	
	device=ataraid_get_device(&pdcraid_ops);
	if (device<0)
		return -ENODEV;
	retval = pdcraid_init_one(device);
	if (retval)
		ataraid_release_device(device);
	return retval;
}

static void __exit pdcraid_exit (void)
{
	int i,device;
	for (device = 0; device<16; device++) {
		for (i=0;i<8;i++)  {
			struct block_device *bdev = raid[device].disk[i].bdev;
			raid[device].disk[i].bdev = NULL;
			if (bdev)
				blkdev_put(bdev, BDEV_RAW);
		}	
		if (raid[device].sectors)
			ataraid_release_device(device);
	}
}

static int pdcraid_open(struct inode * inode, struct file * filp) 
{
	MOD_INC_USE_COUNT;
	return 0;
}
static int pdcraid_release(struct inode * inode, struct file * filp)
{	
	MOD_DEC_USE_COUNT;
	return 0;
}

module_init(pdcraid_init);
module_exit(pdcraid_exit);
