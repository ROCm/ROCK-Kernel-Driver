/* -*- linux-c -*-
 *  drivers/cdrom/viocd.c
 *
 ***************************************************************************
 *  iSeries Virtual CD Rom
 *
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *
 * (C) Copyright 2000-2003 IBM Corporation
 * 
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) anyu later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.  
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *************************************************************************** 
 * This routine provides access to CD ROM drives owned and managed by an 
 * OS/400 partition running on the same box as this Linux partition.
 *
 * All operations are performed by sending messages back and forth to 
 * the OS/400 partition.  
 */

#include <linux/config.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/proc_fs.h>
#include <linux/module.h>

#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/vio.h>
#include <asm/iSeries/iSeries_proc.h>

/* Decide on the proper naming convention to use for our device */
#ifdef CONFIG_VIOCD_AZTECH
#define DEVICE_NAME			"Aztech CD-ROM"
#define MAJOR_NR			AZTECH_CDROM_MAJOR
#define VIOCD_DEVICE			"aztcd"
#define VIOCD_DEVICE_OFFSET		0
#else
#define DEVICE_NAME			"viocd"
#define MAJOR_NR			VIOCD_MAJOR
#define VIOCD_DEVICE			"iseries/vcd%c"
#define VIOCD_DEVICE_OFFSET		'a'
#endif
#define VIOCD_DEVICE_DEVFS		"viocd/%d"
#define VIOCD_DEVICE_OFFSET_DEVFS	0

#define VIOCD_VERS "1.04"

extern struct device *iSeries_vio_dev;

#define signalLpEventFast HvCallEvent_signalLpEventFast

struct viocdlpevent {
	struct HvLpEvent event;
	u32 mReserved1;
	u16 mVersion;
	u16 mSubTypeRc;
	u16 mDisk;
	u16 mFlags;
	u32 mToken;
	u64 mOffset;		// On open, the max number of disks
	u64 mLen;		// On open, the size of the disk
	u32 mBlockSize;		// Only set on open
	u32 mMediaSize;		// Only set on open
};

enum viocdsubtype {
	viocdopen = 0x0001,
	viocdclose = 0x0002,
	viocdread = 0x0003,
	viocdwrite = 0x0004,
	viocdlockdoor = 0x0005,
	viocdgetinfo = 0x0006,
	viocdcheck = 0x0007
};

/*
 * Should probably make this a module parameter....sigh
 */
#define VIOCD_MAX_CD 8

static const struct vio_error_entry viocd_err_table[] = {
	{0x0201, EINVAL, "Invalid Range"},
	{0x0202, EINVAL, "Invalid Token"},
	{0x0203, EIO, "DMA Error"},
	{0x0204, EIO, "Use Error"},
	{0x0205, EIO, "Release Error"},
	{0x0206, EINVAL, "Invalid CD"},
	{0x020C, EROFS, "Read Only Device"},
	{0x020D, EIO, "Changed or Missing Volume (or Varied Off?)"},
	{0x020E, EIO, "Optical System Error (Varied Off?)"},
	{0x02FF, EIO, "Internal Error"},
	{0x3010, EIO, "Changed Volume"},
	{0xC100, EIO, "Optical System Error"},
	{0x0000, 0, NULL},
};

/*
 * This is the structure we use to exchange info between driver and interrupt
 * handler
 */
struct viocd_waitevent {
	struct semaphore *sem;
	int rc;
	u16 subtypeRc;
	int changed;
};

/* this is a lookup table for the true capabilities of a device */
struct capability_entry {
	char *type;
	int capability;
};

static struct capability_entry capability_table[] __initdata = {
	{ "6330", CDC_LOCK | CDC_DVD_RAM },
	{ "6321", CDC_LOCK },
	{ "632B", 0 },
	{ NULL  , CDC_LOCK },
};

/* These are our internal structures for keeping track of devices */
static int viocd_numdev;

struct cdrom_info {
	char rsrcname[10];
	char type[4];
	char model[3];
};
static struct cdrom_info viocd_unitinfo[VIOCD_MAX_CD];

struct disk_info{
	u32 useCount;
	u32 blocksize;
	u32 mediasize;
	struct gendisk *viocd_disk;
	struct cdrom_device_info viocd_info;
};
static struct disk_info viocd_diskinfo[VIOCD_MAX_CD];

#define DEVICE_NR(di)	((di) - &viocd_diskinfo[0])
#define VIOCDI		viocd_diskinfo[deviceno].viocd_info

static request_queue_t *viocd_queue;
static spinlock_t viocd_lock;
static spinlock_t viocd_reqlock;

#define MAX_CD_REQ 1

static int viocd_blk_open(struct inode *inode, struct file *file)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_open(&di->viocd_info, inode, file);
}

static int viocd_blk_release(struct inode *inode, struct file *file)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_release(&di->viocd_info, file);
}

static int viocd_blk_ioctl(struct inode *inode, struct file *file,
			unsigned cmd, unsigned long arg)
{
	struct disk_info *di = inode->i_bdev->bd_disk->private_data;
	return cdrom_ioctl(&di->viocd_info, inode, cmd, arg);
}

static int viocd_blk_media_changed(struct gendisk *disk)
{
	struct disk_info *di = disk->private_data;
	return cdrom_media_changed(&di->viocd_info);
}

struct block_device_operations viocd_fops = {
	.owner =		THIS_MODULE,
	.open =			viocd_blk_open,
	.release =		viocd_blk_release,
	.ioctl =		viocd_blk_ioctl,
	.media_changed =	viocd_blk_media_changed,
};

static void viocd_end_request(struct request *req, int uptodate,
		int num_sectors)
{
	if (!uptodate)
		num_sectors = req->current_nr_sectors;
	if (end_that_request_first(req, uptodate, num_sectors))
		return;
	end_that_request_last(req);
}

/* Get info on CD devices from OS/400 */
static void __init get_viocd_info(void)
{
	dma_addr_t dmaaddr;
	HvLpEvent_Rc hvrc;
	int i;
	DECLARE_MUTEX_LOCKED(Semaphore);
	struct viocd_waitevent we;

	/* If we don't have a host, bail out */
	if (viopath_hostLp == HvLpIndexInvalid)
		return;

	dmaaddr = dma_map_single(iSeries_vio_dev, viocd_unitinfo,
			sizeof(viocd_unitinfo), DMA_FROM_DEVICE);
	if (dmaaddr == 0xFFFFFFFF) {
		printk(KERN_WARNING_VIO "error allocating tce\n");
		return;
	}

	we.sem = &Semaphore;

	hvrc = signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdgetinfo,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, dmaaddr, 0,
			sizeof(viocd_unitinfo), 0);
	if (hvrc != HvLpEvent_Rc_Good) {
		printk(KERN_WARNING_VIO "cdrom error sending event. rc %d\n",
				(int)hvrc);
		return;
	}

	/* wait for completion */
	down(&Semaphore);

	dma_unmap_single(iSeries_vio_dev, dmaaddr, sizeof(viocd_unitinfo),
			DMA_FROM_DEVICE);

	if (we.rc) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viocd_err_table, we.subtypeRc);
		printk(KERN_WARNING_VIO "bad rc %d:0x%04X on getinfo: %s\n",
				we.rc, we.subtypeRc, err->msg);
		return;
	}

	for (i = 0; (i < VIOCD_MAX_CD) && viocd_unitinfo[i].rsrcname[0]; i++)
		viocd_numdev++;
}

static int viocd_open(struct cdrom_device_info *cdi, int purpose)
{
	DECLARE_MUTEX_LOCKED(Semaphore);
        struct disk_info *diskinfo = cdi->handle;
	int device_no = DEVICE_NR(diskinfo);
	HvLpEvent_Rc hvrc;
	struct viocd_waitevent we;

	/* If we don't have a host, bail out */
	if ((viopath_hostLp == HvLpIndexInvalid) || (device_no >= viocd_numdev))
		return -ENODEV;

	we.sem = &Semaphore;
	hvrc = signalLpEventFast(viopath_hostLp, HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdopen,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, ((u64)device_no << 48),
			0, 0, 0);
	if (hvrc != 0) {
		printk(KERN_WARNING_VIO "bad rc on signalLpEventFast %d\n",
		       (int)hvrc);
		return -EIO;
	}

	down(&Semaphore);

	if (we.rc) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viocd_err_table, we.subtypeRc);
		printk(KERN_WARNING_VIO "bad rc %d:0x%04X on open: %s\n",
				we.rc, we.subtypeRc, err->msg);
		return -err->errno;
	}

	if (diskinfo->useCount == 0) {
		if (diskinfo->blocksize > 0) {
			blk_queue_hardsect_size(viocd_queue,
					diskinfo->blocksize);
			set_capacity(diskinfo->viocd_disk,
					diskinfo->mediasize *
					diskinfo->blocksize / 512);
		} else
			set_capacity(diskinfo->viocd_disk, 0xFFFFFFFFFFFFFFFF);
	}
	return 0;
}

static void viocd_release(struct cdrom_device_info *cdi)
{
	int device_no = DEVICE_NR((struct disk_info *)cdi->handle);
	HvLpEvent_Rc hvrc;

	/* If we don't have a host, bail out */
	if ((viopath_hostLp == HvLpIndexInvalid) ||
			(device_no >= viocd_numdev))
		return;

	hvrc = signalLpEventFast(viopath_hostLp, HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdclose,
			HvLpEvent_AckInd_NoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp), 0,
			VIOVERSION << 16, ((u64)device_no << 48), 0, 0, 0);
	if (hvrc != 0)
		printk(KERN_WARNING_VIO "bad rc on signalLpEventFast %d\n",
				(int)hvrc);
}

/* Send a read or write request to OS/400 */
static int send_request(struct request *req)
{
	HvLpEvent_Rc hvrc;
	struct disk_info *diskinfo = req->rq_disk->private_data;
	int device_no = DEVICE_NR(diskinfo);
	u64 start = req->sector * 512;
	u64 len;
	int reading = rq_data_dir(req) == READ;
	int dir = reading ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	u16 command = viomajorsubtype_cdio | (reading ? viocdread : viocdwrite);
	dma_addr_t dmaaddr;
	int nsg, ntce;
	struct scatterlist sg[30];

	if ((req->sector + req->nr_sectors) >
			get_capacity(diskinfo->viocd_disk)) {
		printk(KERN_WARNING_VIO
				"viocd%d; access position %lx, past size %lx\n",
				device_no, req->sector + req->nr_sectors,
				get_capacity(diskinfo->viocd_disk));
		return -1;
	}

        nsg = blk_rq_map_sg(req->q, req, sg);
	if (!nsg) {
		printk(KERN_WARNING_VIO
				"error setting up scatter/gather list\n");
		return -1;
	}

	ntce = dma_map_sg(iSeries_vio_dev, sg, nsg, dir);
	if (!ntce) {
		printk(KERN_WARNING_VIO "error allocating sg tces\n");
		return -1;
	}
	dmaaddr = sg[0].dma_address;
	len = sg[0].dma_length;
	if (ntce > 1) {
		printk("viocd: unmapping %d extra sg entries\n", ntce - 1);
		dma_unmap_sg(iSeries_vio_dev, &sg[1], ntce - 1, dir);
	}
	if (dmaaddr == 0xFFFFFFFF) {
		printk(KERN_WARNING_VIO
				"error allocating tce for address %p len %ld\n",
				req->buffer, len);
		return -1;
	}

	hvrc = signalLpEventFast(viopath_hostLp, HvLpEvent_Type_VirtualIo,
			command, HvLpEvent_AckInd_DoAck,
			HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)req, VIOVERSION << 16,
			((u64)device_no << 48) | dmaaddr, start, len, 0);
	if (hvrc != HvLpEvent_Rc_Good) {
		printk(KERN_WARNING_VIO "hv error on op %d\n", (int)hvrc);
		return -1;
	}

	return 0;
}


static int rwreq;

static void do_viocd_request(request_queue_t *q)
{
	for (; rwreq < MAX_CD_REQ;) {
		struct request *req;
		int device_no;

		if ((req = elv_next_request(q)) == NULL)
			return;
		/* remove the current request from the queue */
		blkdev_dequeue_request(req);

		/* check for any kind of error */
		device_no = DEVICE_NR((struct disk_info *)req->rq_disk->private_data);
		if (device_no > viocd_numdev) {
			printk(KERN_WARNING_VIO "Invalid device number %d",
					device_no);
			viocd_end_request(req, 0, 0);
		} else if (send_request(req) < 0) {
			printk(KERN_WARNING_VIO
					"unable to send message to OS/400!");
			viocd_end_request(req, 0, 0);
		} else {
			spin_lock(&viocd_lock);
			++rwreq;
			spin_unlock(&viocd_lock);
		}
	}
}

static int viocd_media_changed(struct cdrom_device_info *cdi, int disc_nr)
{
	struct viocd_waitevent we;
	HvLpEvent_Rc hvrc;
	int device_no = DEVICE_NR((struct disk_info *)cdi->handle);

	/* This semaphore is raised in the interrupt handler */
	DECLARE_MUTEX_LOCKED(Semaphore);

	/* Check that we are dealing with a valid hosting partition */
	if (viopath_hostLp == HvLpIndexInvalid) {
		printk(KERN_WARNING_VIO "Invalid hosting partition\n");
		return -EIO;
	}

	we.sem = &Semaphore;

	/* Send the open event to OS/400 */
	hvrc = signalLpEventFast(viopath_hostLp, HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdcheck,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16, ((u64)device_no << 48),
			0, 0, 0);
	if (hvrc != 0) {
		printk(KERN_WARNING_VIO "bad rc on signalLpEventFast %d\n",
				(int)hvrc);
		return -EIO;
	}

	/* Wait for the interrupt handler to get the response */
	down(&Semaphore);

	/* Check the return code.  If bad, assume no change */
	if (we.rc) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viocd_err_table, we.subtypeRc);
		printk(KERN_WARNING_VIO
				"bad rc %d:0x%04X on check_change: %s; Assuming no change\n",
				we.rc, we.subtypeRc, err->msg);
		return 0;
	}

	return we.changed;
}

static int viocd_lock_door(struct cdrom_device_info *cdi, int locking)
{
	HvLpEvent_Rc hvrc;
	u64 device_no = DEVICE_NR((struct disk_info *)cdi->handle);
	/* NOTE: flags is 1 or 0 so it won't overwrite the device_no */
	u64 flags = !!locking;
	/* This semaphore is raised in the interrupt handler */
	DECLARE_MUTEX_LOCKED(Semaphore);
	struct viocd_waitevent we = { .sem = &Semaphore };

	/* Check that we are dealing with a valid hosting partition */
	if (viopath_hostLp == HvLpIndexInvalid) {
		printk(KERN_WARNING_VIO "Invalid hosting partition\n");
		return -EIO;
	}

	we.sem = &Semaphore;

	/* Send the lockdoor event to OS/400 */
	hvrc = signalLpEventFast(viopath_hostLp, HvLpEvent_Type_VirtualIo,
			viomajorsubtype_cdio | viocdlockdoor,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)&we, VIOVERSION << 16,
			(device_no << 48) | (flags << 32), 0, 0, 0);
	if (hvrc != 0) {
		printk(KERN_WARNING_VIO "bad rc on signalLpEventFast %d\n",
				(int)hvrc);
		return -EIO;
	}

	/* Wait for the interrupt handler to get the response */
	down(&Semaphore);

	if (we.rc != 0)
		return -EIO;
	return 0;
}

/* This routine handles incoming CD LP events */
static void vioHandleCDEvent(struct HvLpEvent *event)
{
	struct viocdlpevent *bevent = (struct viocdlpevent *)event;
	struct viocd_waitevent *pwe;

	if (event == NULL)
		/* Notification that a partition went away! */
		return;
	/* First, we should NEVER get an int here...only acks */
	if (event->xFlags.xFunction == HvLpEvent_Function_Int) {
		printk(KERN_WARNING_VIO
				"Yikes! got an int in viocd event handler!\n");
		if (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}

	switch (event->xSubtype & VIOMINOR_SUBTYPE_MASK) {
	case viocdopen:
printk(KERN_INFO "viocdopen event: size %lu blocks %u mediasize %u\n", bevent->mLen, bevent->mBlockSize, bevent->mMediaSize);
		viocd_diskinfo[bevent->mDisk].blocksize = bevent->mBlockSize;
		viocd_diskinfo[bevent->mDisk].mediasize = bevent->mMediaSize;
		/* FALLTHROUGH !! */
	case viocdgetinfo:
	case viocdlockdoor:
		pwe = (struct viocd_waitevent *)event->xCorrelationToken;
		pwe->rc = event->xRc;
		pwe->subtypeRc = bevent->mSubTypeRc;
		up(pwe->sem);
		break;

	case viocdclose:
		break;

	case viocdwrite:
	case viocdread:
	{
		unsigned long flags;
		int dir =
			((event->xSubtype & VIOMINOR_SUBTYPE_MASK) == viocdread)
			? DMA_FROM_DEVICE : DMA_TO_DEVICE;
		struct request *req;

		/*
		 * Since this is running in interrupt mode, we need to
		 * make sure we're not stepping on any global I/O operations
		 */
		spin_lock_irqsave(&viocd_reqlock, flags);
		dma_unmap_single(iSeries_vio_dev, bevent->mToken, bevent->mLen,
				dir);
		req = (struct request *)bevent->event.xCorrelationToken;
		spin_lock(&viocd_lock);
		--rwreq;
		spin_unlock(&viocd_lock);

		if (event->xRc != HvLpEvent_Rc_Good) {
			const struct vio_error_entry *err =
				vio_lookup_rc(viocd_err_table,
						bevent->mSubTypeRc);
			printk(KERN_WARNING_VIO
					"request %p failed with rc %d:0x%04X: %s\n",
					req, event->xRc,
					bevent->mSubTypeRc, err->msg);
			viocd_end_request(req, 0, 0);
		} else
			viocd_end_request(req, 1, bevent->mLen >> 9);

		/* restart handling of incoming requests */
		spin_unlock_irqrestore(&viocd_reqlock, flags);
		blk_run_queue(viocd_queue);
		break;
	}
	case viocdcheck:
		pwe = (struct viocd_waitevent *)event->xCorrelationToken;
		pwe->rc = event->xRc;
		pwe->subtypeRc = bevent->mSubTypeRc;
		pwe->changed = bevent->mFlags;
		up(pwe->sem);
		break;

	default:
		printk(KERN_WARNING_VIO
				"message with invalid subtype %0x04X!\n",
				event->xSubtype & VIOMINOR_SUBTYPE_MASK);
		if (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

static struct cdrom_device_ops viocd_dops = {
	.open = viocd_open,
	.release = viocd_release,
	.media_changed = viocd_media_changed,
	.lock_door = viocd_lock_door,
	.capability = CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK | CDC_SELECT_SPEED | CDC_SELECT_DISC | CDC_MULTI_SESSION | CDC_MCN | CDC_MEDIA_CHANGED | CDC_PLAY_AUDIO | CDC_RESET | CDC_IOCTLS | CDC_DRIVE_STATUS | CDC_GENERIC_PACKET | CDC_CD_R | CDC_CD_RW | CDC_DVD | CDC_DVD_R | CDC_DVD_RAM
};

static int proc_read(char *buf, char **start, off_t offset, int blen,
		int *eof, void *data)
{
	int len = 0;
	int i;

	for (i = 0; i < viocd_numdev; i++) {
		len += sprintf(buf + len,
				"viocd device %d is iSeries resource %10.10s type %4.4s, model %3.3s\n",
				i, viocd_unitinfo[i].rsrcname,
				viocd_unitinfo[i].type,
				viocd_unitinfo[i].model);
	}
	*eof = 1;
	return len;
}


static void viocd_proc_init(struct proc_dir_entry *iSeries_proc)
{
	struct proc_dir_entry *ent;
	ent = create_proc_entry("viocd", S_IFREG | S_IRUSR, iSeries_proc);
	if (!ent)
		return;
	ent->nlink = 1;
	ent->data = NULL;
	ent->read_proc = proc_read;
}

static void viocd_proc_delete(struct proc_dir_entry *iSeries_proc)
{
	remove_proc_entry("viocd", iSeries_proc);
}

static int __init find_capability(const char *type)
{
	struct capability_entry *entry;

	for(entry = capability_table; entry->type; ++entry)
		if(!strncmp(entry->type, type, 4))
			break;
	return entry->capability;
}

static int __init viocd_init(void)
{
	struct gendisk *gendisk;
	int deviceno, rc;
	int ret = 0;

	if (viopath_hostLp == HvLpIndexInvalid)
		vio_set_hostlp();

	/* If we don't have a host, bail out */
	if (viopath_hostLp == HvLpIndexInvalid)
		return -ENODEV;

	rc = viopath_open(viopath_hostLp, viomajorsubtype_cdio, MAX_CD_REQ + 2);
	if (rc) {
		printk(KERN_WARNING_VIO
				"error opening path to host partition %d\n",
				viopath_hostLp);
		return rc;
	}

	/* Initialize our request handler */
	rwreq = 0;
	vio_setHandler(viomajorsubtype_cdio, vioHandleCDEvent);

	get_viocd_info();
	if (viocd_numdev == 0)
		goto out_undo_vio;

	printk(KERN_INFO_VIO
			"%s: iSeries Virtual CD vers %s, major %d, max disks %d, hosting partition %d\n",
			DEVICE_NAME, VIOCD_VERS, MAJOR_NR, VIOCD_MAX_CD,
			viopath_hostLp);

	ret = -EIO;
	if (register_blkdev(MAJOR_NR, "viocd") != 0) {
		printk(KERN_WARNING_VIO
				"Unable to get major %d for viocd CD-ROM\n",
				MAJOR_NR);
		goto out_undo_vio;
	}

	ret = -ENOMEM;
	spin_lock_init(&viocd_reqlock);
	viocd_queue = blk_init_queue(do_viocd_request, &viocd_reqlock);
	if (viocd_queue == NULL)
		goto out_unregister;
	blk_queue_max_hw_segments(viocd_queue, 1);

	/* initialize units */
	for (deviceno = 0; deviceno < viocd_numdev; deviceno++) {
		VIOCDI.ops = &viocd_dops;
		VIOCDI.speed = 4;
		VIOCDI.capacity = 1;
		VIOCDI.handle = &viocd_diskinfo[deviceno];
		VIOCDI.mask = ~find_capability(viocd_unitinfo[deviceno].type);
		sprintf(VIOCDI.name, VIOCD_DEVICE,
				VIOCD_DEVICE_OFFSET + deviceno);

		if (register_cdrom(&VIOCDI) != 0) {
			printk(KERN_WARNING_VIO
					"Cannot register viocd CD-ROM %s!\n",
					VIOCDI.name);
			continue;
		}
		printk(KERN_INFO_VIO
				"cd %s is iSeries resource %10.10s type %4.4s, model %3.3s\n",
				VIOCDI.name, viocd_unitinfo[deviceno].rsrcname,
				viocd_unitinfo[deviceno].type,
				viocd_unitinfo[deviceno].model);
		if ((gendisk = alloc_disk(1)) == NULL) {
			printk(KERN_WARNING_VIO
					"Cannot create gendisk for %s!\n",
					VIOCDI.name);
			unregister_cdrom(&VIOCDI);
			continue;
		}
		gendisk->major = MAJOR_NR;
		gendisk->first_minor = deviceno;
		strncpy(gendisk->disk_name, VIOCDI.name, 16);
		sprintf(gendisk->devfs_name, VIOCD_DEVICE_DEVFS,
				VIOCD_DEVICE_OFFSET_DEVFS + deviceno);
		gendisk->queue = viocd_queue;
		gendisk->fops = &viocd_fops;
		gendisk->flags = GENHD_FL_CD;
		set_capacity(gendisk, 0);
		gendisk->private_data = &viocd_diskinfo[deviceno];
		add_disk(gendisk);
		viocd_diskinfo[deviceno].viocd_disk = gendisk;
	}

	iSeries_proc_callback(&viocd_proc_init);

	return 0;

out_unregister:
	unregister_blkdev(MAJOR_NR, "viocd");
out_undo_vio:
	vio_clearHandler(viomajorsubtype_cdio);
	viopath_close(viopath_hostLp, viomajorsubtype_cdio, MAX_CD_REQ + 2);
	return ret;
}

static void __exit viocd_exit(void)
{
	int deviceno;

	for (deviceno = 0; deviceno < viocd_numdev; deviceno++) {
		if (unregister_cdrom(&VIOCDI) != 0)
			printk(KERN_WARNING_VIO
					"Cannot unregister viocd CD-ROM %s!\n",
					VIOCDI.name);
		del_gendisk(viocd_diskinfo[deviceno].viocd_disk);
		put_disk(viocd_diskinfo[deviceno].viocd_disk);
		kfree(viocd_diskinfo[deviceno].viocd_disk);
	}
	if ((unregister_blkdev(MAJOR_NR, "viocd") == -EINVAL)) {
		printk(KERN_WARNING_VIO "can't unregister viocd\n");
		return;
	}
	blk_cleanup_queue(viocd_queue);

	iSeries_proc_callback(&viocd_proc_delete);

	viopath_close(viopath_hostLp, viomajorsubtype_cdio, MAX_CD_REQ+2);
	vio_clearHandler(viomajorsubtype_cdio);
}

module_init(viocd_init);
module_exit(viocd_exit);
MODULE_LICENSE("GPL");
