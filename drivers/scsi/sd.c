/*
 *      sd.c Copyright (C) 1992 Drew Eckhardt
 *           Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 *
 *      Linux scsi disk driver
 *              Initial versions: Drew Eckhardt
 *              Subsequent revisions: Eric Youngdale
 *	Modification history:
 *       - Drew Eckhardt <drew@colorado.edu> original
 *       - Eric Youngdale <eric@andante.org> add scatter-gather, multiple 
 *         outstanding request, and other enhancements.
 *         Support loadable low-level scsi drivers.
 *       - Jirka Hanika <geo@ff.cuni.cz> support more scsi disks using 
 *         eight major numbers.
 *       - Richard Gooch <rgooch@atnf.csiro.au> support devfs.
 *	 - Torben Mathiasen <tmm@image.dk> Resource allocation fixes in 
 *	   sd_init and cleanups.
 *	 - Alex Davis <letmein@erols.com> Fix problem where partition info
 *	   not being read in sd_open. Fix problem where removable media 
 *	   could be ejected after sd_open.
 *	 - Douglas Gilbert <dgilbert@interlog.com> cleanup for lk 2.5 series
 *
 *	Logging policy (needs CONFIG_SCSI_LOGGING defined):
 *	 - setting up transfer: SCSI_LOG_HLQUEUE levels 1 and 2
 *	 - end of transfer (bh + scsi_lib): SCSI_LOG_HLCOMPLETE level 1
 *	 - entering sd_ioctl: SCSI_LOG_IOCTL level 1
 *	 - entering other commands: SCSI_LOG_HLQUEUE level 3
 *	Note: when the logging level is set by the user, it must be greater
 *	than the level indicated above to trigger output.	
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/string.h>
#include <linux/hdreg.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/smp.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>

#define MAJOR_NR SCSI_DISK0_MAJOR
#define LOCAL_END_REQUEST
#define DEVICE_NR(device) (((major(device) & SD_MAJOR_MASK) << (8 - 4)) + (minor(device) >> 4))
#include <linux/blk.h>
#include <linux/blkpg.h>
#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#include <scsi/scsi_ioctl.h>
#include <scsi/scsicam.h>	/* must follow "hosts.h" */

#include <linux/genhd.h>

/* static char sd_version_str[] = "Version: 2.0.3 (20020417)"; */

#define SD_MAJOR(i) (!(i) ? SCSI_DISK0_MAJOR : SCSI_DISK1_MAJOR-1+(i))

#define SCSI_DISKS_PER_MAJOR	16
#define SD_MAJOR_NUMBER(i)	SD_MAJOR((i) >> 8)
#define SD_MINOR_NUMBER(i)	((i) & 255)
#define MKDEV_SD_PARTITION(i)	mk_kdev(SD_MAJOR_NUMBER(i), (i) & 255)
#define MKDEV_SD(index)		MKDEV_SD_PARTITION((index) << 4)
#define N_USED_SD_MAJORS	(1 + ((sd_template.dev_max - 1) >> 4))

#define MAX_RETRIES 5

/*
 *  Time out in seconds for disks and Magneto-opticals (which are slower).
 */

#define SD_TIMEOUT (30 * HZ)
#define SD_MOD_TIMEOUT (75 * HZ)

#define SD_DSK_ARR_LUMP 6 /* amount to over allocate sd_dsk_arr by */

static Scsi_Disk ** sd_dsk_arr;
static rwlock_t sd_dsk_arr_lock = RW_LOCK_UNLOCKED;

static int check_scsidisk_media_change(kdev_t);
static int sd_revalidate(kdev_t);

static void sd_init_onedisk(Scsi_Disk * sdkp, int dsk_nr);

static int sd_init(void);
static void sd_finish(void);
static int sd_attach(Scsi_Device *);
static int sd_detect(Scsi_Device *);
static void sd_detach(Scsi_Device *);
static int sd_init_command(Scsi_Cmnd *);

static struct Scsi_Device_Template sd_template = {
	module:THIS_MODULE,
	name:"disk",
	tag:"sd",
	scsi_type:TYPE_DISK,
	major:SCSI_DISK0_MAJOR,
        /*
         * Secondary range of majors that this driver handles.
         */
	min_major:SCSI_DISK1_MAJOR,
	max_major:SCSI_DISK7_MAJOR,
	blk:1,
	detect:sd_detect,
	init:sd_init,
	finish:sd_finish,
	attach:sd_attach,
	detach:sd_detach,
	init_command:sd_init_command,
};

static void sd_rw_intr(Scsi_Cmnd * SCpnt);

static Scsi_Disk * sd_get_sdisk(int index);

#if defined(CONFIG_PPC32)
/**
 *	sd_find_target - find kdev_t of first scsi disk that matches
 *	given host and scsi_id. 
 *	@host: Scsi_Host object pointer that owns scsi device of interest
 *	@scsi_id: scsi (target) id number of device of interest
 *
 *	Returns kdev_t of first scsi device that matches arguments or
 *	NODEV of no match.
 *
 *	Notes: Looks like a hack, should scan for <host,channel,id,lin>
 *	tuple.
 *	[Architectural dependency: ppc only.] Moved here from 
 *	arch/ppc/pmac_setup.c.
 **/
kdev_t __init
sd_find_target(void *hp, int scsi_id)
{
	Scsi_Disk *sdkp;
	Scsi_Device *sdp;
	struct Scsi_Host *shp = hp;
	int dsk_nr;
	kdev_t retval = NODEV;
	unsigned long iflags;

	SCSI_LOG_HLQUEUE(3, printk("sd_find_target: host_nr=%d, "
			    "scsi_id=%d\n", shp->host_no, scsi_id));
	read_lock_irqsave(&sd_dsk_arr_lock, iflags);
	for (dsk_nr = 0; dsk_nr < sd_template.dev_max; ++dsk_nr) {
		sdkp = sd_dsk_arr[dsk_nr];
		if (sdkp == NULL)
			continue;
		sdp = sdkp->device;
		if (sdp && (sdp->host == shp) && (sdp->id == scsi_id)) {
			retval = MKDEV_SD(dsk_nr);
			break;
		}
	}
	read_unlock_irqrestore(&sd_dsk_arr_lock, iflags);
	return retval;
}
#endif

/**
 *	sd_ioctl - process an ioctl
 *	@inode: only i_rdev member may be used
 *	@filp: only f_mode and f_flags may be used
 *	@cmd: ioctl command number
 *	@arg: this is third argument given to ioctl(2) system call.
 *	Often contains a pointer.
 *
 *	Returns 0 if successful (some ioctls return postive numbers on
 *	success as well). Returns a negated errno value in case of error.
 *
 *	Note: most ioctls are forward onto the block subsystem or further
 *	down in the scsi subsytem.
 **/
static int sd_ioctl(struct inode * inode, struct file * filp, 
		    unsigned int cmd, unsigned long arg)
{
	kdev_t dev = inode->i_rdev;
        Scsi_Disk * sdkp;
	struct Scsi_Host * host;
	Scsi_Device * sdp;
	int diskinfo[4];
	int dsk_nr = DEVICE_NR(dev);
    
	SCSI_LOG_IOCTL(1, printk("sd_ioctl: dsk_nr=%d, cmd=0x%x\n",
		       dsk_nr, cmd));
	sdkp = sd_get_sdisk(dsk_nr);
	if ((NULL == sdkp) || (NULL == (sdp = sdkp->device)))
		return -ENODEV;
	/*
	 * If we are in the middle of error recovery, don't let anyone
	 * else try and use this device.  Also, if error recovery fails, it
	 * may try and take the device offline, in which case all further
	 * access to the device is prohibited.
	 */

	if( !scsi_block_when_processing_errors(sdp) )
		return -ENODEV;

	switch (cmd) 
	{
		case HDIO_GETGEO:   /* Return BIOS disk parameters */
		{
			struct hd_geometry *loc = (struct hd_geometry *) arg;
			if(!loc)
				return -EINVAL;

			host = sdp->host;
	
			/* default to most commonly used values */
	
		        diskinfo[0] = 0x40;	/* 1 << 6 */
	        	diskinfo[1] = 0x20;	/* 1 << 5 */
	        	diskinfo[2] = sdkp->capacity >> 11;
	
			/* override with calculated, extended default, 
			   or driver values */
	
			if(host->hostt->bios_param != NULL)
				host->hostt->bios_param(sdkp, inode->i_bdev,
							&diskinfo[0]);
			else
				scsicam_bios_param(sdkp, inode->i_bdev, &diskinfo[0]);
			if (put_user(diskinfo[0], &loc->heads) ||
				put_user(diskinfo[1], &loc->sectors) ||
				put_user(diskinfo[2], &loc->cylinders) ||
				put_user((unsigned) 
					     get_start_sect(inode->i_bdev),
					 (unsigned long *) &loc->start))
				return -EFAULT;
			return 0;
		}
		default:
			return scsi_ioctl(sdp, cmd, (void *) arg);
	}
}

static void sd_dskname(unsigned int dsk_nr, char *buffer)
{
	if (dsk_nr < 26)
		sprintf(buffer, "sd%c", 'a' + dsk_nr);
	else {
		unsigned int min1;
		unsigned int min2;
		/*
		 * For larger numbers of disks, we need to go to a new
		 * naming scheme.
		 */
		min1 = dsk_nr / 26;
		min2 = dsk_nr % 26;
		sprintf(buffer, "sd%c%c", 'a' + min1 - 1, 'a' + min2);
	}
}

/**
 *	sd_find_queue - yields queue associated with device
 *	@dev: kernel device descriptor (kdev_t)
 *
 *	Returns NULL if no match, otherwise returns pointer to associated
 *	request queue.
 *
 *	Note: this function is invoked (often) from the block subsystem
 *	and should not wait on anything (sd_get_sdisk() does have a read
 *	spinlock).
 **/
static request_queue_t *sd_find_queue(kdev_t dev)
{
	Scsi_Disk *sdkp;
 	int dsk_nr = DEVICE_NR(dev);

	sdkp = sd_get_sdisk(dsk_nr);
	if (sdkp && sdkp->device)
		return &sdkp->device->request_queue;
	else
		return NULL;	/* No such device */
}

static struct gendisk **sd_disks;

/**
 *	sd_init_command - build a scsi (read or write) command from
 *	information in the request structure.
 *	@SCpnt: pointer to mid-level's per scsi command structure that
 *	contains request and into which the scsi command is written
 *
 *	Returns 1 if successful and 0 if error (or cannot be done now).
 **/
static int sd_init_command(Scsi_Cmnd * SCpnt)
{
	int dsk_nr, part_nr, block, this_count;
	Scsi_Device *sdp;
#if CONFIG_SCSI_LOGGING
	char nbuff[6];
#endif
	/*
	 * don't support specials for nwo
	 */
	if (!(SCpnt->request->flags & REQ_CMD))
		return 0;

	part_nr = SD_PARTITION(SCpnt->request->rq_dev);
	dsk_nr = DEVICE_NR(SCpnt->request->rq_dev);

	block = SCpnt->request->sector;
	this_count = SCpnt->request_bufflen >> 9;

	SCSI_LOG_HLQUEUE(1, printk("sd_command_init: dsk_nr=%d, block=%d, "
			    "count=%d\n", dsk_nr, block, this_count));

	sdp = SCpnt->device;
	/* >>>>> the "(part_nr & 0xf)" excludes 15th partition, why?? */
	/* >>>>> this change is not in the lk 2.5 series */
	if (part_nr >= (sd_template.dev_max << 4) || (part_nr & 0xf) ||
	    !sdp || !sdp->online ||
 	    block + SCpnt->request->nr_sectors > get_capacity(sd_disks[dsk_nr])) {
		SCSI_LOG_HLQUEUE(2, printk("Finishing %ld sectors\n", 
				 SCpnt->request->nr_sectors));
		SCSI_LOG_HLQUEUE(2, printk("Retry with 0x%p\n", SCpnt));
		return 0;
	}

	if (sdp->changed) {
		/*
		 * quietly refuse to do anything to a changed disc until 
		 * the changed bit has been reset
		 */
		/* printk("SCSI disk has been changed. Prohibiting further I/O.\n"); */
		return 0;
	}
	SCSI_LOG_HLQUEUE(2, sd_dskname(dsk_nr, nbuff));
	SCSI_LOG_HLQUEUE(2, printk("%s : [part_nr=%d], block=%d\n",
				   nbuff, part_nr, block));

	/*
	 * If we have a 1K hardware sectorsize, prevent access to single
	 * 512 byte sectors.  In theory we could handle this - in fact
	 * the scsi cdrom driver must be able to handle this because
	 * we typically use 1K blocksizes, and cdroms typically have
	 * 2K hardware sectorsizes.  Of course, things are simpler
	 * with the cdrom, since it is read-only.  For performance
	 * reasons, the filesystems should be able to handle this
	 * and not force the scsi disk driver to use bounce buffers
	 * for this.
	 */
	if (sdp->sector_size == 1024) {
		if ((block & 1) || (SCpnt->request->nr_sectors & 1)) {
			printk(KERN_ERR "sd: Bad block number requested");
			return 0;
		} else {
			block = block >> 1;
			this_count = this_count >> 1;
		}
	}
	if (sdp->sector_size == 2048) {
		if ((block & 3) || (SCpnt->request->nr_sectors & 3)) {
			printk(KERN_ERR "sd: Bad block number requested");
			return 0;
		} else {
			block = block >> 2;
			this_count = this_count >> 2;
		}
	}
	if (sdp->sector_size == 4096) {
		if ((block & 7) || (SCpnt->request->nr_sectors & 7)) {
			printk(KERN_ERR "sd: Bad block number requested");
			return 0;
		} else {
			block = block >> 3;
			this_count = this_count >> 3;
		}
	}
	if (rq_data_dir(SCpnt->request) == WRITE) {
		if (!sdp->writeable) {
			return 0;
		}
		SCpnt->cmnd[0] = WRITE_6;
		SCpnt->sc_data_direction = SCSI_DATA_WRITE;
	} else if (rq_data_dir(SCpnt->request) == READ) {
		SCpnt->cmnd[0] = READ_6;
		SCpnt->sc_data_direction = SCSI_DATA_READ;
	} else {
		printk(KERN_ERR "sd: Unknown command %lx\n", 
		       SCpnt->request->flags);
/* overkill 	panic("Unknown sd command %lx\n", SCpnt->request->flags); */
		return 0;
	}

	SCSI_LOG_HLQUEUE(2, printk("%s : %s %d/%ld 512 byte blocks.\n", 
		nbuff, (rq_data_dir(SCpnt->request) == WRITE) ? 
		"writing" : "reading", this_count, SCpnt->request->nr_sectors));

	SCpnt->cmnd[1] = (SCpnt->device->scsi_level <= SCSI_2) ?
			 ((SCpnt->lun << 5) & 0xe0) : 0;

	if (((this_count > 0xff) || (block > 0x1fffff)) || SCpnt->device->ten) {
		if (this_count > 0xffff)
			this_count = 0xffff;

		SCpnt->cmnd[0] += READ_10 - READ_6;
		SCpnt->cmnd[2] = (unsigned char) (block >> 24) & 0xff;
		SCpnt->cmnd[3] = (unsigned char) (block >> 16) & 0xff;
		SCpnt->cmnd[4] = (unsigned char) (block >> 8) & 0xff;
		SCpnt->cmnd[5] = (unsigned char) block & 0xff;
		SCpnt->cmnd[6] = SCpnt->cmnd[9] = 0;
		SCpnt->cmnd[7] = (unsigned char) (this_count >> 8) & 0xff;
		SCpnt->cmnd[8] = (unsigned char) this_count & 0xff;
	} else {
		if (this_count > 0xff)
			this_count = 0xff;

		SCpnt->cmnd[1] |= (unsigned char) ((block >> 16) & 0x1f);
		SCpnt->cmnd[2] = (unsigned char) ((block >> 8) & 0xff);
		SCpnt->cmnd[3] = (unsigned char) block & 0xff;
		SCpnt->cmnd[4] = (unsigned char) this_count;
		SCpnt->cmnd[5] = 0;
	}

	/*
	 * We shouldn't disconnect in the middle of a sector, so with a dumb
	 * host adapter, it's safe to assume that we can at least transfer
	 * this many bytes between each connect / disconnect.
	 */
	SCpnt->transfersize = sdp->sector_size;
	SCpnt->underflow = this_count << 9;

	SCpnt->allowed = MAX_RETRIES;
	SCpnt->timeout_per_command = (SCpnt->device->type == TYPE_DISK ?
				      SD_TIMEOUT : SD_MOD_TIMEOUT);

	/*
	 * This is the completion routine we use.  This is matched in terms
	 * of capability to this function.
	 */
	SCpnt->done = sd_rw_intr;

	/*
	 * This indicates that the command is ready from our end to be
	 * queued.
	 */
	return 1;
}

/**
 *	sd_open - open a scsi disk device
 *	@inode: only i_rdev member may be used
 *	@filp: only f_mode and f_flags may be used
 *
 *	Returns 0 if successful. Returns a negated errno value in case 
 *	of error.
 *
 *	Note: This can be called from a user context (e.g. fsck(1) )
 *	or from within the kernel (e.g. as a result of a mount(1) ).
 *	In the latter case @inode and @filp carry an abridged amount
 *	of information as noted above.
 **/
static int sd_open(struct inode *inode, struct file *filp)
{
	int retval = -ENXIO;
	Scsi_Device * sdp;
	Scsi_Disk * sdkp;
	int dsk_nr = DEVICE_NR(inode->i_rdev);

	SCSI_LOG_HLQUEUE(3, printk("sd_open: dsk_nr=%d, part_nr=%d\n", 
			    dsk_nr, SD_PARTITION(inode->i_rdev)));

	sdkp = sd_get_sdisk(dsk_nr);
	if ((NULL == sdkp) || (NULL == (sdp = sdkp->device)))
		return -ENXIO;	/* No such device */

	/*
	 * If the device is in error recovery, wait until it is done.
	 * If the device is offline, then disallow any access to it.
	 */
	if (!scsi_block_when_processing_errors(sdp))
		return -ENXIO;
	/*
	 * The following code can sleep.
	 * Module unloading must be prevented
	 */
	if (sdp->host->hostt->module)
		__MOD_INC_USE_COUNT(sdp->host->hostt->module);
	if (sd_template.module)
		__MOD_INC_USE_COUNT(sd_template.module);
	sdp->access_count++;

	if (sdp->removable) {
		check_disk_change(inode->i_bdev);

		/*
		 * If the drive is empty, just let the open fail.
		 */
		if ((!sdkp->media_present) && !(filp->f_flags & O_NDELAY)) {
			retval = -ENOMEDIUM;
			goto error_out;
		}

		/*
		 * Similarly, if the device has the write protect tab set,
		 * have the open fail if the user expects to be able to write
		 * to the thing.
		 */
		if ((sdkp->write_prot) && (filp->f_mode & FMODE_WRITE)) {
			retval = -EROFS;
			goto error_out;
		}
	}
	/*
	 * It is possible that the disk changing stuff resulted in the device
	 * being taken offline.  If this is the case, report this to the user,
	 * and don't pretend that the open actually succeeded.
	 */
	if (!sdp->online) {
		goto error_out;
	}

	if (sdp->removable)
		if (sdp->access_count==1)
			if (scsi_block_when_processing_errors(sdp))
				scsi_ioctl(sdp, SCSI_IOCTL_DOORLOCK, NULL);

	return 0;

error_out:
	sdp->access_count--;
	if (sdp->host->hostt->module)
		__MOD_DEC_USE_COUNT(sdp->host->hostt->module);
	if (sd_template.module)
		__MOD_DEC_USE_COUNT(sd_template.module);
	return retval;	
}

/**
 *	sd_release - invoked when the (last) close(2) is called on this
 *	scsi disk.
 *	@inode: only i_rdev member may be used
 *	@filp: only f_mode and f_flags may be used
 *
 *	Returns 0. 
 *
 *	Note: may block (uninterruptible) if error recovery is underway
 *	on this disk.
 **/
static int sd_release(struct inode *inode, struct file *filp)
{
	Scsi_Disk * sdkp;
	int dsk_nr = DEVICE_NR(inode->i_rdev);
	Scsi_Device * sdp;

	SCSI_LOG_HLQUEUE(3, printk("sd_release: dsk_nr=%d, part_nr=%d\n", 
			    dsk_nr, SD_PARTITION(inode->i_rdev)));
	sdkp = sd_get_sdisk(dsk_nr);
	if ((NULL == sdkp) || (NULL == (sdp = sdkp->device)))
		return -ENODEV; /* open uses ENXIO ?? */

	/* ... and what if there are packets in flight and this close()
	 * is followed by a "rmmod sd_mod" */

	sdp->access_count--;

	if (sdp->removable) {
		if (!sdp->access_count)
			if (scsi_block_when_processing_errors(sdp))
				scsi_ioctl(sdp, SCSI_IOCTL_DOORUNLOCK, NULL);
	}
	if (sdp->host->hostt->module)
		__MOD_DEC_USE_COUNT(sdp->host->hostt->module);
	if (sd_template.module)
		__MOD_DEC_USE_COUNT(sd_template.module);
	return 0;
}

static struct block_device_operations sd_fops =
{
	owner:			THIS_MODULE,
	open:			sd_open,
	release:		sd_release,
	ioctl:			sd_ioctl,
	check_media_change:	check_scsidisk_media_change,
	revalidate:		sd_revalidate
};

/**
 *	sd_rw_intr - bottom half handler: called when the lower level
 *	driver has completed (successfully or otherwise) a scsi command.
 *	@SCpnt: mid-level's per command structure.
 *
 *	Note: potentially run from within an ISR. Must not block.
 **/
static void sd_rw_intr(Scsi_Cmnd * SCpnt)
{
	int result = SCpnt->result;
	int this_count = SCpnt->bufflen >> 9;
	int good_sectors = (result == 0 ? this_count : 0);
	int block_sectors = 1;
	long error_sector;
#if CONFIG_SCSI_LOGGING
	char nbuff[6];

	SCSI_LOG_HLCOMPLETE(1, sd_dskname(DEVICE_NR(SCpnt->request->rq_dev), 
			    nbuff));
	SCSI_LOG_HLCOMPLETE(1, printk("sd_rw_intr: %s: res=0x%x\n", 
				      nbuff, result));
	if (0 != result) {
		SCSI_LOG_HLCOMPLETE(1, printk("sd_rw_intr: sb[0,2,asc,ascq]"
				"=%x,%x,%x,%x\n", SCpnt->sense_buffer[0],
			SCpnt->sense_buffer[2], SCpnt->sense_buffer[12],
			SCpnt->sense_buffer[13]));
	}
#endif
	/*
	   Handle MEDIUM ERRORs that indicate partial success.  Since this is a
	   relatively rare error condition, no care is taken to avoid
	   unnecessary additional work such as memcpy's that could be avoided.
	 */

	/* An error occurred */
	if (driver_byte(result) != 0 && 	/* An error occured */
	    SCpnt->sense_buffer[0] != 0xF0) {	/* Sense data is valid */
		switch (SCpnt->sense_buffer[2]) {
		case MEDIUM_ERROR:
			error_sector = (SCpnt->sense_buffer[3] << 24) |
			(SCpnt->sense_buffer[4] << 16) |
			(SCpnt->sense_buffer[5] << 8) |
			SCpnt->sense_buffer[6];
			if (SCpnt->request->bio != NULL)
				block_sectors = bio_sectors(SCpnt->request->bio);
			switch (SCpnt->device->sector_size) {
			case 1024:
				error_sector <<= 1;
				if (block_sectors < 2)
					block_sectors = 2;
				break;
			case 2048:
				error_sector <<= 2;
				if (block_sectors < 4)
					block_sectors = 4;
				break;
			case 4096:
				error_sector <<=3;
				if (block_sectors < 8)
					block_sectors = 8;
				break;
			case 256:
				error_sector >>= 1;
				break;
			default:
				break;
			}

			error_sector &= ~(block_sectors - 1);
			good_sectors = error_sector - SCpnt->request->sector;
			if (good_sectors < 0 || good_sectors >= this_count)
				good_sectors = 0;
			break;

		case RECOVERED_ERROR:
			/*
			 * An error occured, but it recovered.  Inform the
			 * user, but make sure that it's not treated as a
			 * hard error.
			 */
			print_sense("sd", SCpnt);
			result = 0;
			SCpnt->sense_buffer[0] = 0x0;
			good_sectors = this_count;
			break;

		case ILLEGAL_REQUEST:
			if (SCpnt->device->ten == 1) {
				if (SCpnt->cmnd[0] == READ_10 ||
				    SCpnt->cmnd[0] == WRITE_10)
					SCpnt->device->ten = 0;
			}
			break;

		default:
			break;
		}
	}
	/*
	 * This calls the generic completion function, now that we know
	 * how many actual sectors finished, and how many sectors we need
	 * to say have failed.
	 */
	scsi_io_completion(SCpnt, good_sectors, block_sectors);
}

static void
sd_set_media_not_present(Scsi_Disk *sdkp) {
	sdkp->media_present = 0;
	sdkp->capacity = 0;
	sdkp->device->changed = 1;
}

/**
 *	check_scsidisk_media_change - self descriptive
 *	@full_dev: kernel device descriptor (kdev_t)
 *
 *	Returns 0 if not applicable or no change; 1 if change
 *
 *	Note: this function is invoked from the block subsystem.
 **/
static int check_scsidisk_media_change(kdev_t full_dev)
{
	int retval;
	int flag = 0;	/* <<<< what is this for?? */
	Scsi_Disk * sdkp;
	Scsi_Device * sdp;
	int dsk_nr = DEVICE_NR(full_dev);

	sdkp = sd_get_sdisk(dsk_nr);

	SCSI_LOG_HLQUEUE(3, printk("check_scsidisk_media_change: "
			    "dsk_nr=%d\n", dsk_nr));
	if ((NULL == sdkp) || (NULL == (sdp = sdkp->device))) {
		printk(KERN_ERR "check_scsidisk_media_change: dsk_nr=%d, "
		       "invalid device\n", dsk_nr);
		return 0;
	}
	if (!sdp->removable)
		return 0;

	/*
	 * If the device is offline, don't send any commands - just pretend as
	 * if the command failed.  If the device ever comes back online, we
	 * can deal with it then.  It is only because of unrecoverable errors
	 * that we would ever take a device offline in the first place.
	 */
	if (sdp->online == FALSE) {
		sd_set_media_not_present(sdkp);
		return 1;	/* This will force a flush, if called from
				 * check_disk_change */
	}

	/* Using Start/Stop enables differentiation between drive with
	 * no cartridge loaded - NOT READY, drive with changed cartridge -
	 * UNIT ATTENTION, or with same cartridge - GOOD STATUS.
	 * This also handles drives that auto spin down. eg iomega jaz 1GB
	 * as this will spin up the drive.
	 */
	retval = -ENODEV;
	if (scsi_block_when_processing_errors(sdp))
		retval = scsi_ioctl(sdp, SCSI_IOCTL_START_UNIT, NULL);

	if (retval) {		/* Unable to test, unit probably not ready.
				 * This usually means there is no disc in the
				 * drive.  Mark as changed, and we will figure
				 * it out later once the drive is available
				 * again.  */

		sd_set_media_not_present(sdkp);
		return 1;	/* This will force a flush, if called from
				 * check_disk_change */
	}
	/*
	 * For removable scsi disk we have to recognise the presence
	 * of a disk in the drive. This is kept in the Scsi_Disk
	 * struct and tested at open !  Daniel Roche ( dan@lectra.fr )
	 */

	sdkp->media_present = 1;

	retval = sdp->changed;
	if (!flag)
		sdp->changed = 0;
	return retval;
}

static int
sd_media_not_present(Scsi_Disk *sdkp, Scsi_Request *SRpnt) {
	int the_result = SRpnt->sr_result;

	if (the_result != 0
	    && (driver_byte(the_result) & DRIVER_SENSE) != 0
	    && (SRpnt->sr_sense_buffer[2] == NOT_READY ||
		SRpnt->sr_sense_buffer[2] == UNIT_ATTENTION)
	    && SRpnt->sr_sense_buffer[12] == 0x3A /* medium not present */) {
		sd_set_media_not_present(sdkp);
		return 1;
	}
	return 0;
}

/*
 * spinup disk - called only in sd_init_onedisk()
 */
static void
sd_spinup_disk(Scsi_Disk *sdkp, char *diskname,
	       Scsi_Request *SRpnt, unsigned char *buffer) {
	unsigned char cmd[10];
	Scsi_Device *sdp = sdkp->device;
	unsigned long spintime_value = 0;
	int the_result, retries, spintime;

	spintime = 0;

	/* Spin up drives, as required.  Only do this at boot time */
	/* Spinup needs to be done for module loads too. */
	do {
		retries = 0;

		while (retries < 3) {
			cmd[0] = TEST_UNIT_READY;
			cmd[1] = (sdp->scsi_level <= SCSI_2) ?
				((sdp->lun << 5) & 0xe0) : 0;
			memset((void *) &cmd[2], 0, 8);

			SRpnt->sr_cmd_len = 0;
			SRpnt->sr_sense_buffer[0] = 0;
			SRpnt->sr_sense_buffer[2] = 0;
			SRpnt->sr_data_direction = SCSI_DATA_NONE;

			scsi_wait_req (SRpnt, (void *) cmd, (void *) buffer,
				       0/*512*/, SD_TIMEOUT, MAX_RETRIES);

			the_result = SRpnt->sr_result;
			retries++;
			if (the_result == 0
			    || SRpnt->sr_sense_buffer[2] != UNIT_ATTENTION)
				break;
		}

		/*
		 * If the drive has indicated to us that it doesn't have
		 * any media in it, don't bother with any of the rest of
		 * this crap.
		 */
		if (sd_media_not_present(sdkp, SRpnt))
			return;

		/* Look for non-removable devices that return NOT_READY.
		 * Issue command to spin up drive for these cases. */
		if (the_result && !sdp->removable &&
		    SRpnt->sr_sense_buffer[2] == NOT_READY) {
			unsigned long time1;
			if (!spintime) {
				printk(KERN_NOTICE "%s: Spinning up disk...",
				       diskname);
				cmd[0] = START_STOP;
				cmd[1] = (sdp->scsi_level <= SCSI_2) ?
					((sdp->lun << 5) & 0xe0) : 0;
				cmd[1] |= 1;	/* Return immediately */
				memset((void *) &cmd[2], 0, 8);
				cmd[4] = 1;	/* Start spin cycle */
				SRpnt->sr_cmd_len = 0;
				SRpnt->sr_sense_buffer[0] = 0;
				SRpnt->sr_sense_buffer[2] = 0;

				SRpnt->sr_data_direction = SCSI_DATA_READ;
				scsi_wait_req(SRpnt, (void *)cmd, 
					      (void *) buffer, 0/*512*/, 
					      SD_TIMEOUT, MAX_RETRIES);
				spintime_value = jiffies;
			}
			spintime = 1;
			time1 = HZ;
			/* Wait 1 second for next try */
			do {
				current->state = TASK_UNINTERRUPTIBLE;
				time1 = schedule_timeout(time1);
			} while(time1);
			printk(".");
		}
	} while (the_result && spintime &&
		 time_after(spintime_value + 100 * HZ, jiffies));

	if (spintime) {
		if (the_result)
			printk("not responding...\n");
		else
			printk("ready\n");
	}
}

/*
 * read disk capacity - called only in sd_init_onedisk()
 */
static void
sd_read_capacity(Scsi_Disk *sdkp, char *diskname,
		 Scsi_Request *SRpnt, unsigned char *buffer) {
	unsigned char cmd[10];
	Scsi_Device *sdp = sdkp->device;
	int the_result, retries;
	int sector_size;

	retries = 3;
	do {
		cmd[0] = READ_CAPACITY;
		cmd[1] = (sdp->scsi_level <= SCSI_2) ?
			((sdp->lun << 5) & 0xe0) : 0;
		memset((void *) &cmd[2], 0, 8);
		memset((void *) buffer, 0, 8);

		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_sense_buffer[0] = 0;
		SRpnt->sr_sense_buffer[2] = 0;
		SRpnt->sr_data_direction = SCSI_DATA_READ;

		scsi_wait_req(SRpnt, (void *) cmd, (void *) buffer,
			      8, SD_TIMEOUT, MAX_RETRIES);

		if (sd_media_not_present(sdkp, SRpnt))
			return;

		the_result = SRpnt->sr_result;
		retries--;

	} while (the_result && retries);

	if (the_result) {
		printk(KERN_NOTICE "%s : READ CAPACITY failed.\n"
		       "%s : status=%x, message=%02x, host=%d, driver=%02x \n",
		       diskname, diskname,
		       status_byte(the_result),
		       msg_byte(the_result),
		       host_byte(the_result),
		       driver_byte(the_result));

		if (driver_byte(the_result) & DRIVER_SENSE)
			print_req_sense("sd", SRpnt);
		else
			printk("%s : sense not available. \n", diskname);

		/* Set dirty bit for removable devices if not ready -
		 * sometimes drives will not report this properly. */
		if (sdp->removable &&
		    SRpnt->sr_sense_buffer[2] == NOT_READY)
			sdp->changed = 1;

		/* Either no media are present but the drive didnt tell us,
		   or they are present but the read capacity command fails */
		/* sdkp->media_present = 0; -- not always correct */
		sdkp->capacity = 0x200000; /* 1 GB - random */

		return;
	}

	sdkp->capacity = 1 + ((buffer[0] << 24) |
			      (buffer[1] << 16) |
			      (buffer[2] << 8) |
			      buffer[3]);

	sector_size = (buffer[4] << 24) |
		(buffer[5] << 16) | (buffer[6] << 8) | buffer[7];

	if (sector_size == 0) {
		sector_size = 512;
		printk(KERN_NOTICE "%s : sector size 0 reported, "
		       "assuming 512.\n", diskname);
	}

	if (sector_size != 512 &&
	    sector_size != 1024 &&
	    sector_size != 2048 &&
	    sector_size != 4096 &&
	    sector_size != 256) {
		printk(KERN_NOTICE "%s : unsupported sector size "
		       "%d.\n", diskname, sector_size);
		/*
		 * The user might want to re-format the drive with
		 * a supported sectorsize.  Once this happens, it
		 * would be relatively trivial to set the thing up.
		 * For this reason, we leave the thing in the table.
		 */
		sdkp->capacity = 0;
	}
	{
		/*
		 * The msdos fs needs to know the hardware sector size
		 * So I have created this table. See ll_rw_blk.c
		 * Jacques Gelinas (Jacques@solucorp.qc.ca)
		 */
		int hard_sector = sector_size;
		int sz = sdkp->capacity * (hard_sector/256);
		request_queue_t *queue = &sdp->request_queue;

		blk_queue_hardsect_size(queue, hard_sector);
		printk(KERN_NOTICE "SCSI device %s: "
		       "%d %d-byte hdwr sectors (%d MB)\n",
		       diskname, sdkp->capacity,
		       hard_sector, (sz/2 - sz/1250 + 974)/1950);
	}

	/* Rescale capacity to 512-byte units */
	if (sector_size == 4096)
		sdkp->capacity <<= 3;
	if (sector_size == 2048)
		sdkp->capacity <<= 2;
	if (sector_size == 1024)
		sdkp->capacity <<= 1;
	if (sector_size == 256)
		sdkp->capacity >>= 1;

	sdkp->device->sector_size = sector_size;
}

static int
sd_do_mode_sense6(Scsi_Device *sdp, Scsi_Request *SRpnt,
		  int modepage, unsigned char *buffer, int len) {
	unsigned char cmd[8];

	memset((void *) &cmd[0], 0, 8);
	cmd[0] = MODE_SENSE;
	cmd[1] = (sdp->scsi_level <= SCSI_2) ? ((sdp->lun << 5) & 0xe0) : 0;
	cmd[2] = modepage;
	cmd[4] = len;

	SRpnt->sr_cmd_len = 0;
	SRpnt->sr_sense_buffer[0] = 0;
	SRpnt->sr_sense_buffer[2] = 0;
	SRpnt->sr_data_direction = SCSI_DATA_READ;

	scsi_wait_req(SRpnt, (void *) cmd, (void *) buffer,
		      len, SD_TIMEOUT, MAX_RETRIES);

	return SRpnt->sr_result;
}

/*
 * read write protect setting, if possible - called only in sd_init_onedisk()
 */
static void
sd_read_write_protect_flag(Scsi_Disk *sdkp, char *diskname,
			   Scsi_Request *SRpnt, unsigned char *buffer) {
	Scsi_Device *sdp = sdkp->device;
	int res;

	/*
	 * First attempt: ask for all pages (0x3F), but only 4 bytes.
	 * We have to start carefully: some devices hang if we ask
	 * for more than is available.
	 */
	res = sd_do_mode_sense6(sdp, SRpnt, 0x3F, buffer, 4);

	/*
	 * Second attempt: ask for page 0
	 * When only page 0 is implemented, a request for page 3F may return
	 * Sense Key 5: Illegal Request, Sense Code 24: Invalid field in CDB.
	 */
	if (res)
		res = sd_do_mode_sense6(sdp, SRpnt, 0, buffer, 4);

	/*
	 * Third attempt: ask 255 bytes, as we did earlier.
	 */
	if (res)
		res = sd_do_mode_sense6(sdp, SRpnt, 0x3F, buffer, 255);

	if (res) {
		printk(KERN_WARNING
		       "%s: test WP failed, assume Write Enabled\n", diskname);
	} else {
		sdkp->write_prot = ((buffer[2] & 0x80) != 0);
		printk(KERN_NOTICE "%s: Write Protect is %s\n", diskname,
		       sdkp->write_prot ? "on" : "off");
		printk(KERN_DEBUG "%s: Mode Sense: %02x %02x %02x %02x\n",
		       diskname, buffer[0], buffer[1], buffer[2], buffer[3]);
	}
}

/**
 *	sd_init_onedisk - called the first time a new disk is seen,
 *	performs disk spin up, read_capacity, etc.
 *	@sdkp: pointer to associated Scsi_Disk object
 *	@dsk_nr: disk number within this driver (e.g. 0->/dev/sda,
 *	1->/dev/sdb, etc)
 *
 *	Note: this function is local to this driver.
 **/
static void
sd_init_onedisk(Scsi_Disk * sdkp, int dsk_nr) {
	char diskname[40];
	unsigned char *buffer;
	Scsi_Device *sdp;
	Scsi_Request *SRpnt;

	SCSI_LOG_HLQUEUE(3, printk("sd_init_onedisk: dsk_nr=%d\n", dsk_nr));

	/*
	 * Get the name of the disk, in case we need to log it somewhere.
	 */
	sd_dskname(dsk_nr, diskname);

	/*
	 * If the device is offline, don't try and read capacity or any
	 * of the other niceties.
	 */
	sdp = sdkp->device;
	if (sdp->online == FALSE)
		return;

	SRpnt = scsi_allocate_request(sdp);
	if (!SRpnt) {
		printk(KERN_WARNING "(sd_init_onedisk:) Request allocation "
		       "failure.\n");
		return;
	}

	buffer = kmalloc(512, GFP_DMA);
	if (!buffer) {
		printk(KERN_WARNING "(sd_init_onedisk:) Memory allocation "
		       "failure.\n");
		goto leave;
	}

	/* defaults, until the device tells us otherwise */
	sdkp->capacity = 0;
	sdkp->device->sector_size = 512;
	sdkp->media_present = 1;
	sdkp->write_prot = 0;

	sd_spinup_disk(sdkp, diskname, SRpnt, buffer);

	if (sdkp->media_present)
		sd_read_capacity(sdkp, diskname, SRpnt, buffer);

	if (sdp->removable && sdkp->media_present)
		sd_read_write_protect_flag(sdkp, diskname, SRpnt, buffer);

	SRpnt->sr_device->ten = 1;
	SRpnt->sr_device->remap = 1;

 leave:
	scsi_release_request(SRpnt);

	kfree(buffer);
}

/*
 * The sd_init() function looks at all SCSI drives present, determines
 * their size, and reads partition table entries for them.
 */

static int sd_registered;

/**
 *	sd_init- called during driver initialization (after
 *	sd_detect() is called for each scsi device present).
 *
 *	Returns 0 is successful (or already called); 1 if error
 *
 *	Note: this function is invoked from the scsi mid-level.
 **/
static int sd_init()
{
	int k, maxparts;
	Scsi_Disk * sdkp;

	SCSI_LOG_HLQUEUE(3, printk("sd_init: dev_noticed=%d\n",
			    sd_template.dev_noticed));
	if (sd_template.dev_noticed == 0)
		return 0;

	if (NULL == sd_dsk_arr)
		sd_template.dev_max = sd_template.dev_noticed + SD_EXTRA_DEVS;

	if (sd_template.dev_max > N_SD_MAJORS * SCSI_DISKS_PER_MAJOR)
		sd_template.dev_max = N_SD_MAJORS * SCSI_DISKS_PER_MAJOR;

	/* At most 16 partitions on each scsi disk. */
	maxparts = (sd_template.dev_max << 4);
	if (maxparts == 0)
		return 0;

	if (!sd_registered) {
		for (k = 0; k < N_USED_SD_MAJORS; k++) {
			if (register_blkdev(SD_MAJOR(k), "sd", &sd_fops)) {
				printk(KERN_NOTICE "Unable to get major %d "
				       "for SCSI disk\n", SD_MAJOR(k));
				return 1;
			}
		}
		sd_registered++;
	}
	/* We do not support attaching loadable devices yet. */
	if (sd_dsk_arr)
		return 0;

	/* allocate memory */
#define init_mem_lth(x,n)	x = vmalloc((n) * sizeof(*x))
#define zero_mem_lth(x,n)	memset(x, 0, (n) * sizeof(*x))

	init_mem_lth(sd_dsk_arr, sd_template.dev_max);
	if (sd_dsk_arr) {
		zero_mem_lth(sd_dsk_arr, sd_template.dev_max);
		for (k = 0; k < sd_template.dev_max; ++k) {
			sdkp = vmalloc(sizeof(Scsi_Disk));
			if (NULL == sdkp)
				goto cleanup_mem;
			memset(sdkp, 0, sizeof(Scsi_Disk));
			sd_dsk_arr[k] = sdkp;
		}
	}
	init_mem_lth(sd_disks, sd_template.dev_max);
	if (sd_disks)
		zero_mem_lth(sd_disks, sd_template.dev_max);

	if (!sd_dsk_arr || !sd_disks)
		goto cleanup_mem;

	return 0;

#undef init_mem_lth
#undef zero_mem_lth

cleanup_mem:
	vfree(sd_disks);
	sd_disks = NULL;
	if (sd_dsk_arr) {
                for (k = 0; k < sd_template.dev_max; ++k)
			vfree(sd_dsk_arr[k]);
		vfree(sd_dsk_arr);
		sd_dsk_arr = NULL;
	}
	for (k = 0; k < N_USED_SD_MAJORS; k++) {
		unregister_blkdev(SD_MAJOR(k), "sd");
	}
	sd_registered--;
	return 1;
}

/**
 *	sd_finish - called during driver initialization, after all
 *	the sd_attach() calls are finished.
 *
 *	Note: this function is invoked from the scsi mid-level.
 *	This function is not called after driver initialization has completed.
 *	Specifically later device attachments invoke sd_attach() but not
 *	this function.
 **/
static void sd_finish()
{
	int k;
	Scsi_Disk * sdkp;

	SCSI_LOG_HLQUEUE(3, printk("sd_finish: \n"));
	for (k = 0; k < N_USED_SD_MAJORS; k++)
		blk_dev[SD_MAJOR(k)].queue = sd_find_queue;

	for (k = 0; k < sd_template.dev_max; ++k) {
		sdkp = sd_get_sdisk(k);
		if (sdkp && (0 == sdkp->capacity) && sdkp->device) {
			sd_init_onedisk(sdkp, k);
			if (!sdkp->has_been_registered) {
				add_gendisk(sd_disks[k]);
				register_disk(sd_disks[k], MKDEV_SD(k),
						1<<4, &sd_fops,
						sdkp->capacity);
				sdkp->has_been_registered = 1;
			}
		}
	}
	return;
}

/**
 *	sd_detect - called at the start of driver initialization, once 
 *	for each scsi device (not just disks) present.
 *
 *	Returns 0 if not interested in this scsi device (e.g. scanner);
 *	1 if this device is of interest (e.g. a disk).
 *
 *	Note: this function is invoked from the scsi mid-level.
 *	This function is called before sd_init() so very little is available.
 **/
static int sd_detect(Scsi_Device * sdp)
{
	SCSI_LOG_HLQUEUE(3, printk("sd_detect: type=%d\n", sdp->type));
	if (sdp->type != TYPE_DISK && sdp->type != TYPE_MOD)
		return 0;
	sd_template.dev_noticed++;
	return 1;
}

/**
 *	sd_attach - called during driver initialization and whenever a
 *	new scsi device is attached to the system. It is called once
 *	for each scsi device (not just disks) present.
 *	@sdp: pointer to mid level scsi device object
 *
 *	Returns 0 if successful (or not interested in this scsi device 
 *	(e.g. scanner)); 1 when there is an error.
 *
 *	Note: this function is invoked from the scsi mid-level.
 *	This function sets up the mapping between a given 
 *	<host,channel,id,lun> (found in sdp) and new device name 
 *	(e.g. /dev/sda). More precisely it is the block device major 
 *	and minor number that is chosen here.
 **/
static int sd_attach(Scsi_Device * sdp)
{
	Scsi_Disk *sdkp;
	int dsk_nr;
	char diskname[6];
	unsigned long iflags;
	struct {
		struct gendisk disk;
		char name[5];
	} *p;
	struct gendisk *gd;

	if ((NULL == sdp) ||
	    ((sdp->type != TYPE_DISK) && (sdp->type != TYPE_MOD)))
		return 0;

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return 1;
	memset(p, 0, sizeof(*p));
	gd = &p->disk;

	SCSI_LOG_HLQUEUE(3, printk("sd_attach: scsi device: <%d,%d,%d,%d>\n", 
			 sdp->host->host_no, sdp->channel, sdp->id, sdp->lun));
	if (sd_template.nr_dev >= sd_template.dev_max) {
		sdp->attached--;
		printk(KERN_ERR "sd_init: no more room for device\n");
		kfree(p);
		return 1;
	}

/* Assume sd_attach is not re-entrant (for time being) */
/* Also think about sd_attach() and sd_detach() running coincidentally. */
	write_lock_irqsave(&sd_dsk_arr_lock, iflags);
	for (dsk_nr = 0; dsk_nr < sd_template.dev_max; dsk_nr++) {
		sdkp = sd_dsk_arr[dsk_nr];
		if (!sdkp->device) {
			memset(sdkp, 0, sizeof(Scsi_Disk));
			sdkp->device = sdp;
			break;
		}
	}
	write_unlock_irqrestore(&sd_dsk_arr_lock, iflags);

	if (dsk_nr >= sd_template.dev_max) {
		/* panic("scsi_devices corrupt (sd)");  overkill */
		printk(KERN_ERR "sd_init: sd_dsk_arr corrupted\n");
		kfree(p);
		return 1;
	}

	sd_template.nr_dev++;
        gd->de = sdp->de;
	gd->major = SD_MAJOR(dsk_nr>>4);
	gd->first_minor = (dsk_nr & 15)<<4;
	gd->minor_shift = 4;
	gd->fops = &sd_fops;
	if (dsk_nr > 26)
		sprintf(p->name, "sd%c%c", 'a'+dsk_nr/26-1, 'a'+dsk_nr%26);
	else
		sprintf(p->name, "sd%c", 'a'+dsk_nr%26);
	gd->major_name = p->name;
        gd->flags = sdp->removable ? GENHD_FL_REMOVABLE : 0;
        gd->driverfs_dev = &sdp->sdev_driverfs_dev;
        gd->flags |= GENHD_FL_DRIVERFS | GENHD_FL_DEVFS;
	sd_disks[dsk_nr] = gd;
	sd_dskname(dsk_nr, diskname);
	printk(KERN_NOTICE "Attached scsi %sdisk %s at scsi%d, channel %d, "
	       "id %d, lun %d\n", sdp->removable ? "removable " : "",
	       diskname, sdp->host->host_no, sdp->channel, sdp->id, sdp->lun);
	return 0;
}

static int sd_revalidate(kdev_t dev)
{
	int dsk_nr = DEVICE_NR(dev);
	Scsi_Disk * sdkp = sd_get_sdisk(dsk_nr);

	if (!sdkp || !sdkp->device)
		return -ENODEV;

	sd_init_onedisk(sdkp, dsk_nr);
	set_capacity(sd_disks[dsk_nr], sdkp->capacity);
	return 0;
}

/**
 *	sd_detach - called whenever a scsi disk (previously recognized by
 *	sd_attach) is detached from the system. It is called (potentially
 *	multiple times) during sd module unload.
 *	@sdp: pointer to mid level scsi device object
 *
 *	Note: this function is invoked from the scsi mid-level.
 *	This function potentially frees up a device name (e.g. /dev/sdc)
 *	that could be re-used by a subsequent sd_attach().
 *	This function is not called when the built-in sd driver is "exit-ed".
 **/
static void sd_detach(Scsi_Device * sdp)
{
	Scsi_Disk *sdkp = NULL;
	kdev_t dev;
	int dsk_nr;
	unsigned long iflags;

	SCSI_LOG_HLQUEUE(3, printk("sd_detach: <%d,%d,%d,%d>\n", 
			    sdp->host->host_no, sdp->channel, sdp->id, 
			    sdp->lun));
	write_lock_irqsave(&sd_dsk_arr_lock, iflags);
	for (dsk_nr = 0; dsk_nr < sd_template.dev_max; dsk_nr++) {
		sdkp = sd_dsk_arr[dsk_nr];
		if (sdkp->device == sdp) {
			sdkp->device = NULL;
			sdkp->capacity = 0;
			/* sdkp->detaching = 1; */
			break;
		}
	}
	write_unlock_irqrestore(&sd_dsk_arr_lock, iflags);
	if (dsk_nr >= sd_template.dev_max)
		return;

	if (sdkp->has_been_registered) {
		sdkp->has_been_registered = 0;
		dev = MKDEV_SD(dsk_nr);
		del_gendisk(sd_disks[dsk_nr]);
	}
	sdp->attached--;
	sd_template.dev_noticed--;
	sd_template.nr_dev--;
	kfree(sd_disks[dsk_nr]);
	sd_disks[dsk_nr] = NULL;
}

/**
 *	init_sd - entry point for this driver (both when built in or when
 *	a module).
 *
 *	Note: this function registers this driver with the scsi mid-level.
 **/
static int __init init_sd(void)
{
	int rc;
	SCSI_LOG_HLQUEUE(3, printk("init_sd: sd driver entry point\n"));
	sd_template.module = THIS_MODULE;
	rc = scsi_register_device(&sd_template);
	if (!rc) {
		sd_template.scsi_driverfs_driver.name = (char *)sd_template.tag;
		sd_template.scsi_driverfs_driver.bus = &scsi_driverfs_bus_type;
		driver_register(&sd_template.scsi_driverfs_driver);
	}
	return rc;
}

/**
 *	exit_sd - exit point for this driver (when it is	a module).
 *
 *	Note: this function unregisters this driver from the scsi mid-level.
 **/
static void __exit exit_sd(void)
{
	int k;

	SCSI_LOG_HLQUEUE(3, printk("exit_sd: exiting sd driver\n"));
	scsi_unregister_device(&sd_template);
	for (k = 0; k < N_USED_SD_MAJORS; k++)
		unregister_blkdev(SD_MAJOR(k), "sd");

	sd_registered--;
	if (sd_dsk_arr != NULL) {
		for (k = 0; k < sd_template.dev_max; ++k)
			vfree(sd_dsk_arr[k]);
		vfree(sd_dsk_arr);
	}
	for (k = 0; k < N_USED_SD_MAJORS; k++) {
		blk_dev[SD_MAJOR(k)].queue = NULL;
		blk_clear(SD_MAJOR(k));
	}
	sd_template.dev_max = 0;
	remove_driver(&sd_template.scsi_driverfs_driver);
}

static Scsi_Disk * sd_get_sdisk(int index)
{
	Scsi_Disk * sdkp = NULL;
	unsigned long iflags;

	read_lock_irqsave(&sd_dsk_arr_lock, iflags);
	if (sd_dsk_arr && (index >= 0) && (index < sd_template.dev_max))
		sdkp = sd_dsk_arr[index];
	read_unlock_irqrestore(&sd_dsk_arr_lock, iflags);
	return sdkp;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Youngdale");
MODULE_DESCRIPTION("SCSI disk (sd) driver");

module_init(init_sd);
module_exit(exit_sd);
