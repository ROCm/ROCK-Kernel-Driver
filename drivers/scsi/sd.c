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
 *	 - Douglas Gilbert <dgilbert@interlog.com> cleanup for lk 2.5.x
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
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/vmalloc.h>
#include <linux/blk.h>
#include <linux/blkpg.h>
#include <asm/uaccess.h>

#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>
#include <scsi/scsicam.h>

#include "scsi_logging.h"


/*
 * Remaining dev_t-handling stuff
 */
#define SD_MAJORS	16
#define SD_DISKS	(SD_MAJORS << 4)

/*
 * Time out in seconds for disks and Magneto-opticals (which are slower).
 */
#define SD_TIMEOUT		(30 * HZ)
#define SD_MOD_TIMEOUT		(75 * HZ)

/*
 * Number of allowed retries
 */
#define SD_MAX_RETRIES		5

struct scsi_disk {
	struct list_head list;		/* list of all scsi_disks */
	struct Scsi_Device_Template *driver;	/* always &sd_template */
	struct scsi_device *device;
	struct gendisk	*disk;
	sector_t	capacity;	/* size in 512-byte sectors */
	u32		index;
	u8		media_present;
	u8		write_prot;
	unsigned	WCE : 1;	/* state of disk WCE bit */
	unsigned	RCD : 1;	/* state of disk RCD bit, unused */
};

static LIST_HEAD(sd_devlist);
static spinlock_t sd_devlist_lock = SPIN_LOCK_UNLOCKED;

static unsigned long sd_index_bits[SD_DISKS / BITS_PER_LONG];
static spinlock_t sd_index_lock = SPIN_LOCK_UNLOCKED;

static void sd_init_onedisk(struct scsi_disk * sdkp, struct gendisk *disk);
static void sd_rw_intr(struct scsi_cmnd * SCpnt);

static int sd_attach(struct scsi_device *);
static void sd_detach(struct scsi_device *);
static void sd_rescan(struct scsi_device *);
static int sd_init_command(struct scsi_cmnd *);
static int sd_synchronize_cache(struct scsi_disk *, int);
static int sd_notifier(struct notifier_block *, unsigned long, void *);
static void sd_read_capacity(struct scsi_disk *sdkp, char *diskname,
		 struct scsi_request *SRpnt, unsigned char *buffer);
static struct notifier_block sd_notifier_block = {sd_notifier, NULL, 0}; 

static struct Scsi_Device_Template sd_template = {
	.module		= THIS_MODULE,
	.list		= LIST_HEAD_INIT(sd_template.list),
	.name		= "disk",
	.scsi_type	= TYPE_DISK,
	.attach		= sd_attach,
	.detach		= sd_detach,
	.rescan		= sd_rescan,
	.init_command	= sd_init_command,
	.scsi_driverfs_driver = {
		.name   = "sd",
	},
};

static int sd_major(int major_idx)
{
	switch (major_idx) {
	case 0:
		return SCSI_DISK0_MAJOR;
	case 1 ... 7:
		return SCSI_DISK1_MAJOR + major_idx - 1;
	case 8 ... 15:
		return SCSI_DISK8_MAJOR + major_idx - 8;
	default:
		BUG();
		return 0;	/* shut up gcc */
	}
}

static struct scsi_disk *sd_find_by_sdev(Scsi_Device *sd)
{
	struct scsi_disk *sdkp;

	spin_lock(&sd_devlist_lock);
	list_for_each_entry(sdkp, &sd_devlist, list) {
		if (sdkp->device == sd) {
			spin_unlock(&sd_devlist_lock);
			return sdkp;
		}
	}

	spin_unlock(&sd_devlist_lock);
	return NULL;
}

static inline void sd_devlist_insert(struct scsi_disk *sdkp)
{
	spin_lock(&sd_devlist_lock);
	list_add(&sdkp->list, &sd_devlist);
	spin_unlock(&sd_devlist_lock);
}

static inline void sd_devlist_remove(struct scsi_disk *sdkp)
{
	spin_lock(&sd_devlist_lock);
	list_del(&sdkp->list);
	spin_unlock(&sd_devlist_lock);
}
 
static inline struct scsi_disk *scsi_disk(struct gendisk *disk)
{
	return container_of(disk->private_data, struct scsi_disk, driver);
}

/**
 *	sd_init_command - build a scsi (read or write) command from
 *	information in the request structure.
 *	@SCpnt: pointer to mid-level's per scsi command structure that
 *	contains request and into which the scsi command is written
 *
 *	Returns 1 if successful and 0 if error (or cannot be done now).
 **/
static int sd_init_command(struct scsi_cmnd * SCpnt)
{
	unsigned int this_count, timeout;
	struct gendisk *disk;
	sector_t block;
	struct scsi_device *sdp = SCpnt->device;

	timeout = SD_TIMEOUT;
	if (SCpnt->device->type != TYPE_DISK)
		timeout = SD_MOD_TIMEOUT;

	/*
	 * these are already setup, just copy cdb basically
	 */
	if (SCpnt->request->flags & REQ_BLOCK_PC) {
		struct request *rq = SCpnt->request;

		if (sizeof(rq->cmd) > sizeof(SCpnt->cmnd))
			return 0;

		memcpy(SCpnt->cmnd, rq->cmd, sizeof(SCpnt->cmnd));
		if (rq_data_dir(rq) == WRITE)
			SCpnt->sc_data_direction = SCSI_DATA_WRITE;
		else if (rq->data_len)
			SCpnt->sc_data_direction = SCSI_DATA_READ;
		else
			SCpnt->sc_data_direction = SCSI_DATA_NONE;

		this_count = rq->data_len;
		if (rq->timeout)
			timeout = rq->timeout;

		SCpnt->transfersize = rq->data_len;
		SCpnt->underflow = rq->data_len;
		goto queue;
	}

	/*
	 * we only do REQ_CMD and REQ_BLOCK_PC
	 */
	if (!(SCpnt->request->flags & REQ_CMD))
		return 0;

	disk = SCpnt->request->rq_disk;
	block = SCpnt->request->sector;
	this_count = SCpnt->request_bufflen >> 9;

	SCSI_LOG_HLQUEUE(1, printk("sd_init_command: disk=%s, block=%llu, "
			    "count=%d\n", disk->disk_name, (unsigned long long)block, this_count));

	if (!sdp || !sdp->online ||
 	    block + SCpnt->request->nr_sectors > get_capacity(disk)) {
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
	SCSI_LOG_HLQUEUE(2, printk("%s : block=%llu\n",
				   disk->disk_name, (unsigned long long)block));

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
		disk->disk_name, (rq_data_dir(SCpnt->request) == WRITE) ? 
		"writing" : "reading", this_count, SCpnt->request->nr_sectors));

	SCpnt->cmnd[1] = 0;
	
	if (block > 0xffffffff) {
		SCpnt->cmnd[0] += READ_16 - READ_6;
		SCpnt->cmnd[2] = sizeof(block) > 4 ? (unsigned char) (block >> 56) & 0xff : 0;
		SCpnt->cmnd[3] = sizeof(block) > 4 ? (unsigned char) (block >> 48) & 0xff : 0;
		SCpnt->cmnd[4] = sizeof(block) > 4 ? (unsigned char) (block >> 40) & 0xff : 0;
		SCpnt->cmnd[5] = sizeof(block) > 4 ? (unsigned char) (block >> 32) & 0xff : 0;
		SCpnt->cmnd[6] = (unsigned char) (block >> 24) & 0xff;
		SCpnt->cmnd[7] = (unsigned char) (block >> 16) & 0xff;
		SCpnt->cmnd[8] = (unsigned char) (block >> 8) & 0xff;
		SCpnt->cmnd[9] = (unsigned char) block & 0xff;
		SCpnt->cmnd[10] = (unsigned char) (this_count >> 24) & 0xff;
		SCpnt->cmnd[11] = (unsigned char) (this_count >> 16) & 0xff;
		SCpnt->cmnd[12] = (unsigned char) (this_count >> 8) & 0xff;
		SCpnt->cmnd[13] = (unsigned char) this_count & 0xff;
		SCpnt->cmnd[14] = SCpnt->cmnd[15] = 0;
	} else if (((this_count > 0xff) || (block > 0x1fffff)) || SCpnt->device->ten) {
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

queue:
	SCpnt->allowed = SD_MAX_RETRIES;
	SCpnt->timeout_per_command = timeout;

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
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct scsi_disk *sdkp = scsi_disk(disk);
	struct scsi_device *sdev = sdkp->device;
	int retval;

	SCSI_LOG_HLQUEUE(3, printk("sd_open: disk=%s\n", disk->disk_name));

	retval = scsi_device_get(sdev);
	if (retval)
		return retval;

	/*
	 * If the device is in error recovery, wait until it is done.
	 * If the device is offline, then disallow any access to it.
	 */
	retval = -ENXIO;
	if (!scsi_block_when_processing_errors(sdev))
		goto error_out;

	if (sdev->removable) {
		check_disk_change(inode->i_bdev);

		/*
		 * If the drive is empty, just let the open fail.
		 */
		retval = -ENOMEDIUM;
		if ((!sdkp->media_present) && !(filp->f_flags & O_NDELAY))
			goto error_out;

		/*
		 * Similarly, if the device has the write protect tab set,
		 * have the open fail if the user expects to be able to write
		 * to the thing.
		 */
		retval = -EROFS;
		if ((sdkp->write_prot) && (filp->f_mode & FMODE_WRITE))
			goto error_out;
	}

	/*
	 * It is possible that the disk changing stuff resulted in
	 * the device being taken offline.  If this is the case,
	 * report this to the user, and don't pretend that the
	 * open actually succeeded.
	 */
	retval = -ENXIO;
	if (!sdev->online)
		goto error_out;

	if (sdev->removable && sdev->access_count == 1)
		if (scsi_block_when_processing_errors(sdev))
			scsi_set_medium_removal(sdev, SCSI_REMOVAL_PREVENT);

	return 0;

error_out:
	scsi_device_put(sdev);
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
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct scsi_device *sdev = scsi_disk(disk)->device;

	SCSI_LOG_HLQUEUE(3, printk("sd_release: disk=%s\n", disk->disk_name));

	if (sdev->removable && sdev->access_count == 1)
		if (scsi_block_when_processing_errors(sdev))
			scsi_set_medium_removal(sdev, SCSI_REMOVAL_ALLOW);

	/*
	 * XXX and what if there are packets in flight and this close()
	 * XXX is followed by a "rmmod sd_mod"?
	 */
	scsi_device_put(sdev);
	return 0;
}

static int sd_hdio_getgeo(struct block_device *bdev, struct hd_geometry *loc)
{
	struct scsi_disk *sdkp = scsi_disk(bdev->bd_disk);
	struct scsi_device *sdp = sdkp->device;
	struct Scsi_Host *host = sdp->host;
	int diskinfo[4];

	/* default to most commonly used values */
        diskinfo[0] = 0x40;	/* 1 << 6 */
       	diskinfo[1] = 0x20;	/* 1 << 5 */
       	diskinfo[2] = sdkp->capacity >> 11;
	
	/* override with calculated, extended default, or driver values */
	if (host->hostt->bios_param)
		host->hostt->bios_param(sdp, bdev, sdkp->capacity, diskinfo);
	else
		scsicam_bios_param(bdev, sdkp->capacity, diskinfo);

	if (put_user(diskinfo[0], &loc->heads))
		return -EFAULT;
	if (put_user(diskinfo[1], &loc->sectors))
		return -EFAULT;
	if (put_user(diskinfo[2], &loc->cylinders))
		return -EFAULT;
	if (put_user((unsigned)get_start_sect(bdev),
	             (unsigned long *)&loc->start))
		return -EFAULT;
	return 0;
}

/**
 *	sd_ioctl - process an ioctl
 *	@inode: only i_rdev/i_bdev members may be used
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
	struct block_device *bdev = inode->i_bdev;
	struct gendisk *disk = bdev->bd_disk;
	struct scsi_device *sdp = scsi_disk(disk)->device;
	int error;
    
	SCSI_LOG_IOCTL(1, printk("sd_ioctl: disk=%s, cmd=0x%x\n",
						disk->disk_name, cmd));

	/*
	 * If we are in the middle of error recovery, don't let anyone
	 * else try and use this device.  Also, if error recovery fails, it
	 * may try and take the device offline, in which case all further
	 * access to the device is prohibited.
	 */
	if (!scsi_block_when_processing_errors(sdp))
		return -ENODEV;

	if (cmd == HDIO_GETGEO) {
		if (!arg)
			return -EINVAL;
		return sd_hdio_getgeo(bdev, (struct hd_geometry *)arg);
	}

	/*
	 * Send SCSI addressing ioctls directly to mid level, send other
	 * ioctls to block level and then onto mid level if they can't be
	 * resolved.
	 */
	switch (cmd) {
		case SCSI_IOCTL_GET_IDLUN:
		case SCSI_IOCTL_GET_BUS_NUMBER:
			return scsi_ioctl(sdp, cmd, (void *)arg);
		default:
			error = scsi_cmd_ioctl(bdev, cmd, arg);
			if (error != -ENOTTY)
				return error;
	}
	return scsi_ioctl(sdp, cmd, (void *)arg);
}

static void set_media_not_present(struct scsi_disk *sdkp)
{
	sdkp->media_present = 0;
	sdkp->capacity = 0;
	sdkp->device->changed = 1;
}

/**
 *	sd_media_changed - check if our medium changed
 *	@disk: kernel device descriptor 
 *
 *	Returns 0 if not applicable or no change; 1 if change
 *
 *	Note: this function is invoked from the block subsystem.
 **/
static int sd_media_changed(struct gendisk *disk)
{
	struct scsi_disk *sdkp = scsi_disk(disk);
	struct scsi_device *sdp = sdkp->device;
	int retval;

	SCSI_LOG_HLQUEUE(3, printk("sd_media_changed: disk=%s\n",
						disk->disk_name));

	if (!sdp->removable)
		return 0;

	/*
	 * If the device is offline, don't send any commands - just pretend as
	 * if the command failed.  If the device ever comes back online, we
	 * can deal with it then.  It is only because of unrecoverable errors
	 * that we would ever take a device offline in the first place.
	 */
	if (!sdp->online)
		goto not_present;

	/*
	 * Using TEST_UNIT_READY enables differentiation between drive with
	 * no cartridge loaded - NOT READY, drive with changed cartridge -
	 * UNIT ATTENTION, or with same cartridge - GOOD STATUS.
	 *
	 * Drives that auto spin down. eg iomega jaz 1G, will be started
	 * by sd_spinup_disk() from sd_init_onedisk(), which happens whenever
	 * sd_revalidate() is called.
	 */
	retval = -ENODEV;
	if (scsi_block_when_processing_errors(sdp))
		retval = scsi_ioctl(sdp, SCSI_IOCTL_TEST_UNIT_READY, NULL);

	/*
	 * Unable to test, unit probably not ready.   This usually
	 * means there is no disc in the drive.  Mark as changed,
	 * and we will figure it out later once the drive is
	 * available again.
	 */
	if (retval)
		 goto not_present;

	/*
	 * For removable scsi disk we have to recognise the presence
	 * of a disk in the drive. This is kept in the struct scsi_disk
	 * struct and tested at open !  Daniel Roche (dan@lectra.fr)
	 */
	sdkp->media_present = 1;

	retval = sdp->changed;
	sdp->changed = 0;

	return retval;

not_present:
	set_media_not_present(sdkp);
	return 1;
}

static void sd_rescan(struct scsi_device * sdp)
{
	unsigned char *buffer;
	struct scsi_disk *sdkp = sd_find_by_sdev(sdp);
	struct gendisk *gd;
	struct scsi_request *SRpnt;

	if (!sdkp || sdp->online == FALSE || !sdkp->media_present)
		return;
		
	gd = sdkp->disk;
	
	SCSI_LOG_HLQUEUE(3, printk("sd_rescan: disk=%s\n", gd->disk_name));
	
	SRpnt = scsi_allocate_request(sdp);
	if (!SRpnt) {
		printk(KERN_WARNING "(sd_rescan:) Request allocation "
		       "failure.\n");
		return;
	}

	if (sdkp->device->host->unchecked_isa_dma)
		buffer = kmalloc(512, GFP_DMA);
	else
		buffer = kmalloc(512, GFP_KERNEL);

    	sd_read_capacity(sdkp, gd->disk_name, SRpnt, buffer);
	set_capacity(gd, sdkp->capacity);	
	scsi_release_request(SRpnt);
	kfree(buffer);
}

static int sd_revalidate_disk(struct gendisk *disk)
{
	struct scsi_disk *sdkp = scsi_disk(disk);

	sd_init_onedisk(sdkp, disk);
	set_capacity(disk, sdkp->capacity);
	return 0;
}

static struct block_device_operations sd_fops = {
	.owner			= THIS_MODULE,
	.open			= sd_open,
	.release		= sd_release,
	.ioctl			= sd_ioctl,
	.media_changed		= sd_media_changed,
	.revalidate_disk	= sd_revalidate_disk,
};

/**
 *	sd_rw_intr - bottom half handler: called when the lower level
 *	driver has completed (successfully or otherwise) a scsi command.
 *	@SCpnt: mid-level's per command structure.
 *
 *	Note: potentially run from within an ISR. Must not block.
 **/
static void sd_rw_intr(struct scsi_cmnd * SCpnt)
{
	int result = SCpnt->result;
	int this_count = SCpnt->bufflen >> 9;
	int good_sectors = (result == 0 ? this_count : 0);
	sector_t block_sectors = 1;
	sector_t error_sector;
#if CONFIG_SCSI_LOGGING
	SCSI_LOG_HLCOMPLETE(1, printk("sd_rw_intr: %s: res=0x%x\n", 
				SCpnt->request->rq_disk->disk_name, result));
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
	if (driver_byte(result) != 0 && 	/* An error occurred */
	    SCpnt->sense_buffer[0] == 0xF0) {	/* Sense data is valid */
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
			 * An error occurred, but it recovered.  Inform the
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

static int media_not_present(struct scsi_disk *sdkp, struct scsi_request *srp)
{
	if (!srp->sr_result)
		return 0;
	if (!(driver_byte(srp->sr_result) & DRIVER_SENSE))
		return 0;
	if (srp->sr_sense_buffer[2] != NOT_READY &&
	    srp->sr_sense_buffer[2] != UNIT_ATTENTION)
	    	return 0;
	if (srp->sr_sense_buffer[12] != 0x3A) /* medium not present */
		return 0;

	set_media_not_present(sdkp);
	return 1;
}

/*
 * spinup disk - called only in sd_init_onedisk()
 */
static void
sd_spinup_disk(struct scsi_disk *sdkp, char *diskname,
	       struct scsi_request *SRpnt, unsigned char *buffer) {
	unsigned char cmd[10];
	unsigned long spintime_value = 0;
	int the_result, retries, spintime;

	spintime = 0;

	/* Spin up drives, as required.  Only do this at boot time */
	/* Spinup needs to be done for module loads too. */
	do {
		retries = 0;

		while (retries < 3) {
			cmd[0] = TEST_UNIT_READY;
			memset((void *) &cmd[1], 0, 9);

			SRpnt->sr_cmd_len = 0;
			SRpnt->sr_sense_buffer[0] = 0;
			SRpnt->sr_sense_buffer[2] = 0;
			SRpnt->sr_data_direction = SCSI_DATA_NONE;

			scsi_wait_req (SRpnt, (void *) cmd, (void *) buffer,
				       0/*512*/, SD_TIMEOUT, SD_MAX_RETRIES);

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
		if (media_not_present(sdkp, SRpnt))
			return;

		if (the_result == 0)
			break;		/* all is well now */

		/*
		 * If manual intervention is required, or this is an
		 * absent USB storage device, a spinup is meaningless.
		 */
		if (SRpnt->sr_sense_buffer[2] == NOT_READY &&
		    SRpnt->sr_sense_buffer[12] == 4 /* not ready */ &&
		    SRpnt->sr_sense_buffer[13] == 3)
			break;		/* manual intervention required */

		/*
		 * Issue command to spin up drive when not ready
		 */
		if (SRpnt->sr_sense_buffer[2] == NOT_READY) {
			unsigned long time1;
			if (!spintime) {
				printk(KERN_NOTICE "%s: Spinning up disk...",
				       diskname);
				cmd[0] = START_STOP;
				cmd[1] = 1;	/* Return immediately */
				memset((void *) &cmd[2], 0, 8);
				cmd[4] = 1;	/* Start spin cycle */
				SRpnt->sr_cmd_len = 0;
				SRpnt->sr_sense_buffer[0] = 0;
				SRpnt->sr_sense_buffer[2] = 0;

				SRpnt->sr_data_direction = SCSI_DATA_READ;
				scsi_wait_req(SRpnt, (void *)cmd, 
					      (void *) buffer, 0/*512*/, 
					      SD_TIMEOUT, SD_MAX_RETRIES);
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
	} while (spintime &&
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
sd_read_capacity(struct scsi_disk *sdkp, char *diskname,
		 struct scsi_request *SRpnt, unsigned char *buffer) {
	unsigned char cmd[16];
	struct scsi_device *sdp = sdkp->device;
	int the_result, retries;
	int sector_size = 0;
	int longrc = 0;

repeat:
	retries = 3;
	do {
		if (longrc) {
			memset((void *) cmd, 0, 16);
			cmd[0] = SERVICE_ACTION_IN;
			cmd[1] = SAI_READ_CAPACITY_16;
			cmd[13] = 12;
			memset((void *) buffer, 0, 12);
		} else {
			cmd[0] = READ_CAPACITY;
			memset((void *) &cmd[1], 0, 9);
			memset((void *) buffer, 0, 8);
		}
		
		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_sense_buffer[0] = 0;
		SRpnt->sr_sense_buffer[2] = 0;
		SRpnt->sr_data_direction = SCSI_DATA_READ;

		scsi_wait_req(SRpnt, (void *) cmd, (void *) buffer,
			      longrc ? 12 : 8, SD_TIMEOUT, SD_MAX_RETRIES);

		if (media_not_present(sdkp, SRpnt))
			return;

		the_result = SRpnt->sr_result;
		retries--;

	} while (the_result && retries);

	if (the_result && !longrc) {
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

		/* Either no media are present but the drive didn't tell us,
		   or they are present but the read capacity command fails */
		/* sdkp->media_present = 0; -- not always correct */
		sdkp->capacity = 0x200000; /* 1 GB - random */

		return;
	} else if (the_result && longrc) {
		/* READ CAPACITY(16) has been failed */
		printk(KERN_NOTICE "%s : READ CAPACITY(16) failed.\n"
		       "%s : status=%x, message=%02x, host=%d, driver=%02x \n",
		       diskname, diskname,
		       status_byte(the_result),
		       msg_byte(the_result),
		       host_byte(the_result),
		       driver_byte(the_result));
		printk(KERN_NOTICE "%s : use 0xffffffff as device size\n",
		       diskname);
		
		sdkp->capacity = 1 + (sector_t) 0xffffffff;		
		goto got_data;
	}	
	
	if (!longrc) {
		sector_size = (buffer[4] << 24) |
			(buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
		if (buffer[0] == 0xff && buffer[1] == 0xff &&
		    buffer[2] == 0xff && buffer[3] == 0xff) {
			if(sizeof(sdkp->capacity) > 4) {
				printk(KERN_NOTICE "%s : very big device. try to use"
				       " READ CAPACITY(16).\n", diskname);
				longrc = 1;
				goto repeat;
			} else {
				printk(KERN_ERR "%s: too big for kernel.  Assuming maximum 2Tb\n", diskname);
			}
		}
		sdkp->capacity = 1 + (((sector_t)buffer[0] << 24) |
			(buffer[1] << 16) |
			(buffer[2] << 8) |
			buffer[3]);			
	} else {
		sdkp->capacity = 1 + (((u64)buffer[0] << 56) |
			((u64)buffer[1] << 48) |
			((u64)buffer[2] << 40) |
			((u64)buffer[3] << 32) |
			((sector_t)buffer[4] << 24) |
			((sector_t)buffer[5] << 16) |
			((sector_t)buffer[6] << 8)  |
			(sector_t)buffer[7]);
			
		sector_size = (buffer[8] << 24) |
			(buffer[9] << 16) | (buffer[10] << 8) | buffer[11];
	}	

got_data:
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
		sector_t sz = sdkp->capacity * (hard_sector/256);
		request_queue_t *queue = sdp->request_queue;
		sector_t mb;

		blk_queue_hardsect_size(queue, hard_sector);
		/* avoid 64-bit division on 32-bit platforms */
		mb = sz >> 1;
		sector_div(sz, 1250);
		mb -= sz - 974;
		sector_div(mb, 1950);

		printk(KERN_NOTICE "SCSI device %s: "
		       "%llu %d-byte hdwr sectors (%llu MB)\n",
		       diskname, (unsigned long long)sdkp->capacity,
		       hard_sector, (unsigned long long)mb);
	}

	/* Rescale capacity to 512-byte units */
	if (sector_size == 4096)
		sdkp->capacity <<= 3;
	else if (sector_size == 2048)
		sdkp->capacity <<= 2;
	else if (sector_size == 1024)
		sdkp->capacity <<= 1;
	else if (sector_size == 256)
		sdkp->capacity >>= 1;

	sdkp->device->sector_size = sector_size;
}

static int
sd_do_mode_sense6(struct scsi_device *sdp, struct scsi_request *SRpnt,
		  int dbd, int modepage, unsigned char *buffer, int len) {
	unsigned char cmd[8];

	memset((void *) &cmd[0], 0, 8);
	cmd[0] = MODE_SENSE;
	cmd[1] = dbd;
	cmd[2] = modepage;
	cmd[4] = len;

	SRpnt->sr_cmd_len = 0;
	SRpnt->sr_sense_buffer[0] = 0;
	SRpnt->sr_sense_buffer[2] = 0;
	SRpnt->sr_data_direction = SCSI_DATA_READ;

	memset((void *) buffer, 0, len);

	scsi_wait_req(SRpnt, (void *) cmd, (void *) buffer,
		      len, SD_TIMEOUT, SD_MAX_RETRIES);

	return SRpnt->sr_result;
}

/*
 * read write protect setting, if possible - called only in sd_init_onedisk()
 */
static void
sd_read_write_protect_flag(struct scsi_disk *sdkp, char *diskname,
		   struct scsi_request *SRpnt, unsigned char *buffer) {
	struct scsi_device *sdp = sdkp->device;
	int res;

	/*
	 * First attempt: ask for all pages (0x3F), but only 4 bytes.
	 * We have to start carefully: some devices hang if we ask
	 * for more than is available.
	 */
	res = sd_do_mode_sense6(sdp, SRpnt, 0, 0x3F, buffer, 4);

	/*
	 * Second attempt: ask for page 0
	 * When only page 0 is implemented, a request for page 3F may return
	 * Sense Key 5: Illegal Request, Sense Code 24: Invalid field in CDB.
	 */
	if (res)
		res = sd_do_mode_sense6(sdp, SRpnt, 0, 0, buffer, 4);

	/*
	 * Third attempt: ask 255 bytes, as we did earlier.
	 */
	if (res)
		res = sd_do_mode_sense6(sdp, SRpnt, 0, 0x3F, buffer, 255);

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

/*
 * sd_read_cache_type - called only from sd_init_onedisk()
 */
static void
sd_read_cache_type(struct scsi_disk *sdkp, char *diskname,
		   struct scsi_request *SRpnt, unsigned char *buffer) {
	struct scsi_device *sdp = sdkp->device;
	int len = 0, res;

	const int dbd = 0x08;	   /* DBD */
	const int modepage = 0x08; /* current values, cache page */

	/* cautiously ask */
	res = sd_do_mode_sense6(sdp, SRpnt, dbd, modepage, buffer, 4);

	if (res == 0) {
		/* that went OK, now ask for the proper length */
		len = buffer[0] + 1;
		if (len > 128)
			len = 128;
		res = sd_do_mode_sense6(sdp, SRpnt, dbd, modepage, buffer, len);
	}

	if (res == 0 && buffer[3] + 6 < len) {
		const char *types[] = {
			"write through", "none", "write back",
			"write back, no read (daft)"
		};
		int ct = 0;
		int offset = buffer[3] + 4; /* start of mode page */

		sdkp->WCE = ((buffer[offset + 2] & 0x04) != 0);
		sdkp->RCD = ((buffer[offset + 2] & 0x01) != 0);

		ct =  sdkp->RCD + 2*sdkp->WCE;

		printk(KERN_NOTICE "SCSI device %s: drive cache: %s\n",
		       diskname, types[ct]);
	} else {
		if (res == 0 ||
		    (status_byte(res) == CHECK_CONDITION
		     && (SRpnt->sr_sense_buffer[0] & 0x70) == 0x70
		     && (SRpnt->sr_sense_buffer[2] & 0x0f) == ILLEGAL_REQUEST
		     /* ASC 0x24 ASCQ 0x00: Invalid field in CDB */
		     && SRpnt->sr_sense_buffer[12] == 0x24
		     && SRpnt->sr_sense_buffer[13] == 0x00)) {
			printk(KERN_NOTICE "%s: cache data unavailable\n",
			       diskname);
		} else {
			printk(KERN_ERR "%s: asking for cache data failed\n",
			       diskname);
		}
		printk(KERN_ERR "%s: assuming drive cache: write through\n",
		       diskname);
		sdkp->WCE = 0;
		sdkp->RCD = 0;
	}
}

/**
 *	sd_init_onedisk - called the first time a new disk is seen,
 *	performs disk spin up, read_capacity, etc.
 *	@sdkp: pointer to associated struct scsi_disk object
 *	@dsk_nr: disk number within this driver (e.g. 0->/dev/sda,
 *	1->/dev/sdb, etc)
 *
 *	Note: this function is local to this driver.
 **/
static void
sd_init_onedisk(struct scsi_disk * sdkp, struct gendisk *disk)
{
	unsigned char *buffer;
	struct scsi_device *sdp;
	struct scsi_request *SRpnt;

	SCSI_LOG_HLQUEUE(3, printk("sd_init_onedisk: disk=%s\n", disk->disk_name));

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

	buffer = kmalloc(512, GFP_KERNEL | GFP_DMA);
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
	sdkp->WCE = 0;
	sdkp->RCD = 0;

	sd_spinup_disk(sdkp, disk->disk_name, SRpnt, buffer);

	if (sdkp->media_present)
		sd_read_capacity(sdkp, disk->disk_name, SRpnt, buffer);
	
	if (sdp->removable && sdkp->media_present)
		sd_read_write_protect_flag(sdkp, disk->disk_name, SRpnt, buffer);
	/* without media there is no reason to ask;
	   moreover, some devices react badly if we do */
	if (sdkp->media_present)
		sd_read_cache_type(sdkp, disk->disk_name, SRpnt, buffer);
		
	SRpnt->sr_device->ten = 1;
	SRpnt->sr_device->remap = 1;

 leave:
	scsi_release_request(SRpnt);

	kfree(buffer);
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
 *
 *	Assume sd_attach is not re-entrant (for time being)
 *	Also think about sd_attach() and sd_detach() running coincidentally.
 **/
static int sd_attach(struct scsi_device * sdp)
{
	struct scsi_disk *sdkp;
	struct gendisk *gd;
	u32 index;
	int error;

	if ((sdp->type != TYPE_DISK) && (sdp->type != TYPE_MOD))
		return 1;

	SCSI_LOG_HLQUEUE(3, printk("sd_attach: scsi device: <%d,%d,%d,%d>\n", 
			 sdp->host->host_no, sdp->channel, sdp->id, sdp->lun));

	error = scsi_slave_attach(sdp);
	if (error)
		goto out;

	error = -ENOMEM;
	sdkp = kmalloc(sizeof(*sdkp), GFP_KERNEL);
	if (!sdkp)
		goto out_detach;

	gd = alloc_disk(16);
	if (!gd)
		goto out_free;

	spin_lock(&sd_index_lock);
	index = find_first_zero_bit(sd_index_bits, SD_DISKS);
	if (index == SD_DISKS) {
		spin_unlock(&sd_index_lock);
		error = -EBUSY;
		goto out_put;
	}
	__set_bit(index, sd_index_bits);
	spin_unlock(&sd_index_lock);

	sdkp->device = sdp;
	sdkp->driver = &sd_template;
	sdkp->disk = gd;
	sdkp->index = index;

	gd->major = sd_major(index >> 4);
	gd->first_minor = (index & 15) << 4;
	gd->minors = 16;
	gd->fops = &sd_fops;

	if (index >= 26) {
		sprintf(gd->disk_name, "sd%c%c",
			'a' + index/26-1,'a' + index % 26);
	} else {
		sprintf(gd->disk_name, "sd%c", 'a' + index % 26);
	}

	strcpy(gd->devfs_name, sdp->devfs_name);

	sd_init_onedisk(sdkp, gd);

	gd->driverfs_dev = &sdp->sdev_driverfs_dev;
	gd->flags = GENHD_FL_DRIVERFS;
	if (sdp->removable)
		gd->flags |= GENHD_FL_REMOVABLE;
	gd->private_data = &sdkp->driver;
	gd->queue = sdkp->device->request_queue;

	sd_devlist_insert(sdkp);
	set_capacity(gd, sdkp->capacity);
	add_disk(gd);

	printk(KERN_NOTICE "Attached scsi %sdisk %s at scsi%d, channel %d, "
	       "id %d, lun %d\n", sdp->removable ? "removable " : "",
	       gd->disk_name, sdp->host->host_no, sdp->channel,
	       sdp->id, sdp->lun);

	return 0;

out_put:
	put_disk(gd);
out_free:
	kfree(sdkp);
out_detach:
	scsi_slave_detach(sdp);
out:
	return error;
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
static void sd_detach(struct scsi_device * sdp)
{
	struct scsi_disk *sdkp;

	SCSI_LOG_HLQUEUE(3, printk("sd_detach: <%d,%d,%d,%d>\n", 
			    sdp->host->host_no, sdp->channel, sdp->id, 
			    sdp->lun));

	sdkp = sd_find_by_sdev(sdp);
	if (!sdkp)
		return;

	/* check that we actually have a write back cache to synchronize */
	if (sdkp->WCE) {
		printk(KERN_NOTICE "Synchronizing SCSI cache: ");
		sd_synchronize_cache(sdkp, 1);
		printk("\n");
	}

	sd_devlist_remove(sdkp);
	del_gendisk(sdkp->disk);
	scsi_slave_detach(sdp);

	spin_lock(&sd_index_lock);
	clear_bit(sdkp->index, sd_index_bits);
	spin_unlock(&sd_index_lock);

	put_disk(sdkp->disk);
	kfree(sdkp);
}

/**
 *	init_sd - entry point for this driver (both when built in or when
 *	a module).
 *
 *	Note: this function registers this driver with the scsi mid-level.
 **/
static int __init init_sd(void)
{
	int majors = 0, rc = -ENODEV, i;

	SCSI_LOG_HLQUEUE(3, printk("init_sd: sd driver entry point\n"));

	for (i = 0; i < SD_MAJORS; i++)
		if (register_blkdev(sd_major(i), "sd") == 0)
			majors++;

	if (!majors)
		return -ENODEV;

	rc = scsi_register_device(&sd_template);
	if (rc)
		return rc;
	register_reboot_notifier(&sd_notifier_block);
	return rc;
}

/**
 *	exit_sd - exit point for this driver (when it is a module).
 *
 *	Note: this function unregisters this driver from the scsi mid-level.
 **/
static void __exit exit_sd(void)
{
	int i;

	SCSI_LOG_HLQUEUE(3, printk("exit_sd: exiting sd driver\n"));

	unregister_reboot_notifier(&sd_notifier_block);
	scsi_unregister_device(&sd_template);
	for (i = 0; i < SD_MAJORS; i++)
		unregister_blkdev(sd_major(i), "sd");
}

/*
 * XXX: this function does not take sd_devlist_lock to synchronize
 *	access to sd_devlist.  This should be safe as no other reboot
 *	notifier can access it.
 */
static int sd_notifier(struct notifier_block *n, unsigned long event, void *p)
{
	if (event != SYS_RESTART &&
	    event != SYS_HALT &&
	    event != SYS_POWER_OFF)
		return NOTIFY_DONE;

	if (!list_empty(&sd_devlist)) {
		struct scsi_disk *sdkp;

		printk(KERN_NOTICE "Synchronizing SCSI caches: ");
		list_for_each_entry(sdkp, &sd_devlist, list)
			if (sdkp->WCE)
				sd_synchronize_cache(sdkp, 1);
		printk("\n");
	}

	return NOTIFY_OK;
}

/* send a SYNCHRONIZE CACHE instruction down to the device through the
 * normal SCSI command structure.  Wait for the command to complete (must
 * have user context) */
static int sd_synchronize_cache(struct scsi_disk *sdkp, int verbose)
{
	struct scsi_request *SRpnt;
	struct scsi_device *SDpnt = sdkp->device;
	int retries, the_result;

	if (!SDpnt->online)
		return 0;

	if (verbose)
		printk("%s ", sdkp->disk->disk_name);

	SRpnt = scsi_allocate_request(SDpnt);
	if(!SRpnt) {
		if(verbose)
			printk("FAILED\n  No memory for request\n");
		return 0;
	}
		

	for(retries = 3; retries > 0; --retries) {
		unsigned char cmd[10] = { 0 };

		cmd[0] = SYNCHRONIZE_CACHE;
		/* leave the rest of the command zero to indicate 
		 * flush everything */
		scsi_wait_req(SRpnt, (void *)cmd, NULL, 0,
			      SD_TIMEOUT, SD_MAX_RETRIES);

		if(SRpnt->sr_result == 0)
			break;
	}

	the_result = SRpnt->sr_result;
	if(verbose) {
		if(the_result != 0) {
			printk("FAILED\n  status = %x, message = %02x, host = %d, driver = %02x\n  ",
			       status_byte(the_result),
			       msg_byte(the_result),
			       host_byte(the_result),
			       driver_byte(the_result));
			if (driver_byte(the_result) & DRIVER_SENSE)
				print_req_sense("sd", SRpnt);

		}
	}
	scsi_release_request(SRpnt);
	return (the_result == 0);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Youngdale");
MODULE_DESCRIPTION("SCSI disk (sd) driver");

module_init(init_sd);
module_exit(exit_sd);
