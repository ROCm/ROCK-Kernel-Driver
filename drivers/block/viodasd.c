/* -*- linux-c -*-
 * viodasd.c
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *
 * (C) Copyright 2000 IBM Corporation
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 ***************************************************************************
 * This routine provides access to disk space (termed "DASD" in historical
 * IBM terms) owned and managed by an OS/400 partition running on the
 * same box as this Linux partition.
 *
 * All disk operations are performed by sending messages back and forth to 
 * the OS/400 partition. 
 * 
 * This device driver can either use its own major number, or it can
 * pretend to be an IDE drive (grep 'IDE[0-9]_MAJOR' ../../include/linux/major.h).
 * This is controlled with a CONFIG option.  You can either call this an
 * elegant solution to the fact that a lot of software doesn't recognize
 * a new disk major number...or you can call this a really ugly hack.
 * Your choice.
 */

#include <linux/major.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/blkpg.h>

/* Changelog:
	2001-11-27	devilbis	Added first pass at complete IDE emulation
	2002-07-07      boutcher        Added randomness
 */

/* Decide if we are using our own major or pretending to be an IDE drive
 *
 * If we are using our own major, we only support 7 partitions per physical
 * disk....so with minor numbers 0-255 we get a maximum of 32 disks.  If we
 * are emulating IDE, we get 63 partitions per disk, with a maximum of 4
 * disks per major, but common practice is to place only 2 devices in /dev
 * for each IDE major, for a total of 20 (since there are 10 IDE majors).
 */

#ifdef CONFIG_VIODASD_IDE
static const int major_table[] = {
	IDE0_MAJOR,
	IDE1_MAJOR,
	IDE2_MAJOR,
	IDE3_MAJOR,
	IDE4_MAJOR,
	IDE5_MAJOR,
	IDE6_MAJOR,
	IDE7_MAJOR,
	IDE8_MAJOR,
	IDE9_MAJOR,
};
enum {
	DEV_PER_MAJOR = 2,
	PARTITION_SHIFT = 6,
};
static int major_to_index(int major)
{
	switch(major) {
	case IDE0_MAJOR: return 0;
	case IDE1_MAJOR: return 1;
	case IDE2_MAJOR: return 2;
	case IDE3_MAJOR: return 3;
	case IDE4_MAJOR: return 4;
	case IDE5_MAJOR: return 5;
	case IDE6_MAJOR: return 6;
	case IDE7_MAJOR: return 7;
	case IDE8_MAJOR: return 8;
	case IDE9_MAJOR: return 9;
	default:
		return -1;
	}
}
#define VIOD_DEVICE_NAME "ide"
#define VIOD_GENHD_NAME "hd"
#else				/* !CONFIG_VIODASD_IDE */
static const int major_table[] = {
	VIODASD_MAJOR,
};
enum {
	DEV_PER_MAJOR = 32,
	PARTITION_SHIFT = 3,
};
static int major_to_index(int major)
{
	if(major != VIODASD_MAJOR)
		return -1;
	return 0;
}
#define VIOD_DEVICE_NAME "viod"
#ifdef CONFIG_DEVFS_FS
#define VIOD_GENHD_NAME "viod"
#else
#define VIOD_GENHD_NAME "iseries/vd"
#endif
#endif				/* CONFIG_VIODASD_IDE */

#define DEVICE_NR(dev) (devt_to_diskno(dev))
#define LOCAL_END_REQUEST

#include <linux/sched.h>
#include <linux/timer.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/fd.h>
#include <linux/buffer_head.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/root_dev.h>
#include <linux/kdev_t.h>

#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/vio.h>
#include <asm/iSeries/iSeries_proc.h>

MODULE_DESCRIPTION("iSeries Virtual DASD");
MODULE_AUTHOR("Dave Boutcher");
MODULE_LICENSE("GPL");

#define VIODASD_VERS "1.50"

enum {
	NUM_MAJORS = sizeof(major_table) / sizeof(major_table[0]),
	MAX_DISKNO = DEV_PER_MAJOR * NUM_MAJORS,
	MAX_DISK_NAME = 16, /* maximum length of a gendisk->name */
};

static volatile int	viodasd_max_disk = MAX_DISKNO - 1;
static spinlock_t	viodasd_spinlock = SPIN_LOCK_UNLOCKED;

static inline int devt_to_diskno(dev_t dev)
{
	return major_to_index(MAJOR(dev)) * DEV_PER_MAJOR +
	    (MINOR(dev) >> PARTITION_SHIFT);
}

#define VIOMAXREQ	16
#define VIOMAXBLOCKDMA	12

#define DEVICE_NO(cell)	((struct viodasd_device *)(cell) - &viodasd_devices[0])

extern struct pci_dev *iSeries_vio_dev;

struct openData {
	u64 mDiskLen;
	u16 mMaxDisks;
	u16 mCylinders;
	u16 mTracks;
	u16 mSectors;
	u16 mBytesPerSector;
};

struct rwData {			// Used during rw
	u64 mOffset;
	struct {
		u32 mToken;
		u32 reserved;
		u64 mLen;
	} dmaInfo[VIOMAXBLOCKDMA];
};

struct vioblocklpevent {
	struct HvLpEvent event;
	u32 mReserved1;
	u16 mVersion;
	u16 mSubTypeRc;
	u16 mDisk;
	u16 mFlags;
	union {
		struct openData openData;
		struct rwData rwData;
		struct {
			u64 changed;
		} check;
	} u;
};

#define vioblockflags_ro   0x0001

enum vioblocksubtype {
	vioblockopen = 0x0001,
	vioblockclose = 0x0002,
	vioblockread = 0x0003,
	vioblockwrite = 0x0004,
	vioblockflush = 0x0005,
	vioblockcheck = 0x0007
};

static DECLARE_WAIT_QUEUE_HEAD(viodasd_wait);
struct viodasd_waitevent {
	struct semaphore *sem;
	int rc;
	union {
		int changed;	/* Used only for check_change */
		u16 subRC;
	} data;
};

static const struct vio_error_entry viodasd_err_table[] = {
	{ 0x0201, EINVAL, "Invalid Range" },
	{ 0x0202, EINVAL, "Invalid Token" },
	{ 0x0203, EIO, "DMA Error" },
	{ 0x0204, EIO, "Use Error" },
	{ 0x0205, EIO, "Release Error" },
	{ 0x0206, EINVAL, "Invalid Disk" },
	{ 0x0207, EBUSY, "Cant Lock" },
	{ 0x0208, EIO, "Already Locked" },
	{ 0x0209, EIO, "Already Unlocked" },
	{ 0x020A, EIO, "Invalid Arg" },
	{ 0x020B, EIO, "Bad IFS File" },
	{ 0x020C, EROFS, "Read Only Device" },
	{ 0x02FF, EIO, "Internal Error" },
	{ 0x0000, 0, NULL },
};

/*
 * Figure out the biggest I/O request (in sectors) we can accept
 */
#define VIODASD_MAXSECTORS (4096 / 512 * VIOMAXBLOCKDMA)

/*
 * Keep some statistics on what's happening for the PROC file system
 */
static struct {
	long tot;
	long nobh;
	long ntce[VIOMAXBLOCKDMA];
} viod_stats[MAX_DISKNO][2];

/*
 * Number of disk I/O requests we've sent to OS/400
 */
static int num_req_outstanding;

/*
 * This is our internal structure for keeping track of disk devices
 */
struct viodasd_device {
	int useCount;
	u16 cylinders;
	u16 tracks;
	u16 sectors;
	u16 bytesPerSector;
	u64 size;
	int readOnly;
        struct request_queue *queue;
	spinlock_t q_lock;
        struct gendisk *disk;
	struct block_device *bdev;
}	viodasd_devices[MAX_DISKNO];

static struct hd_struct *devt_to_partition(dev_t dev)
{
	return viodasd_devices[devt_to_diskno(dev)].disk->
		part[MINOR(dev) & ((1 << PARTITION_SHIFT) - 1)];
}

/*
 * When we get a disk I/O request we take it off the general request queue
 * and put it here.
 */
static LIST_HEAD(reqlist);

/*
 * Handle reads from the proc file system
 */
static int proc_read(char *buf, char **start, off_t offset,
		     int blen, int *eof, void *data)
{
	int len = 0;
	int i;
	int j;

#if defined(MODULE)
	len +=
	    sprintf(buf + len,
		    "viod Module opened %d times.  Major number %d\n",
		    MOD_IN_USE, major_table[0]);
#endif
	len +=
	    sprintf(buf + len, "viod %d possible devices\n", MAX_DISKNO);

	for (i = 0; i < 16; i++) {
		if (viod_stats[i][0].tot || viod_stats[i][1].tot) {
			len +=
			    sprintf(buf + len,
				    "DISK %2.2d: rd %-10.10ld wr %-10.10ld (no buffer list rd %-10.10ld wr %-10.10ld\n",
				    i, viod_stats[i][0].tot,
				    viod_stats[i][1].tot,
				    viod_stats[i][0].nobh,
				    viod_stats[i][1].nobh);

			len += sprintf(buf + len, "rd DMA: ");

			for (j = 0; j < VIOMAXBLOCKDMA; j++)
				len += sprintf(buf + len, " [%2.2d] %ld",
					       j,
					       viod_stats[i][0].ntce[j]);

			len += sprintf(buf + len, "\nwr DMA: ");

			for (j = 0; j < VIOMAXBLOCKDMA; j++)
				len += sprintf(buf + len, " [%2.2d] %ld",
					       j,
					       viod_stats[i][1].ntce[j]);
			len += sprintf(buf + len, "\n");
		}
	}

	*eof = 1;
	return len;
}

/*
 * Handle writes to our proc file system
 */
static int proc_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	return count;
}

/*
 * setup our proc file system entries
 */
void viodasd_proc_init(struct proc_dir_entry *iSeries_proc)
{
	struct proc_dir_entry *ent;

	ent = create_proc_entry("viodasd", S_IFREG | S_IRUSR, iSeries_proc);
	if (!ent)
		return;
	ent->nlink = 1;
	ent->data = NULL;
	ent->read_proc = proc_read;
	ent->write_proc = proc_write;
}

/*
 * clean up our proc file system entries
 */
void viodasd_proc_delete(struct proc_dir_entry *iSeries_proc)
{
	remove_proc_entry("viodasd", iSeries_proc);
}

/*
 * End a request
 */
static void viodasd_end_request(struct request *req, int uptodate,
		int num_sectors)
{
	if (!uptodate)
		num_sectors = req->current_nr_sectors;
	if (end_that_request_first(req, uptodate, num_sectors))
		return;
        add_disk_randomness(req->rq_disk);
	end_that_request_last(req);
}

/*
 * This rebuilds the partition information for a single disk device
 */
static int viodasd_revalidate(struct gendisk *gendisk)
{
    struct viodasd_device *device =
	    (struct viodasd_device *)gendisk->private_data;

    set_capacity(gendisk, device->size >> 9);
    return 0;
}

static u16 access_flags(mode_t mode)
{
	u16 flags = 0;
	if (!(mode & FMODE_WRITE))
		flags |= vioblockflags_ro;
	return flags;
}

static void internal_register_disk(int diskno)
{
	static int registered[MAX_DISKNO];
	struct gendisk *gendisk = viodasd_devices[diskno].disk;
	int i;

	if (registered[diskno])
		return;
	registered[diskno] = 1;

	printk(KERN_INFO_VIO
	       "%s: Disk %2.2d size %dM, sectors %d, heads %d, cylinders %d\n", 
               VIOD_DEVICE_NAME, diskno,
	       (int) viodasd_devices[diskno].size >> 20,
	       (int) viodasd_devices[diskno].sectors,
	       (int) viodasd_devices[diskno].tracks,
	       (int) viodasd_devices[diskno].cylinders);

	for (i = 1; i < (1 << PARTITION_SHIFT); ++i) {
		struct hd_struct *partition = gendisk->part[i - 1];
		if (partition && partition->nr_sects)
			printk(KERN_INFO_VIO
			       "%s: Disk %2.2d partition %2.2d start sector %ld, # sector %ld\n",
			       VIOD_DEVICE_NAME, diskno, i,
			       partition->start_sect, partition->nr_sects);
	}
}


/*
 * This is the actual open code.  It gets called from the external
 * open entry point, as well as from the init code when we're figuring
 * out what disks we have
 */
static int internal_open(int device_no, u16 flags)
{
	struct gendisk *gendisk;
	HvLpEvent_Rc hvrc;
	/* This semaphore is raised in the interrupt handler */
	DECLARE_MUTEX_LOCKED(Semaphore);
	struct viodasd_waitevent we = { .sem = &Semaphore };

	/* Check that we are dealing with a valid hosting partition */
	if (viopath_hostLp == HvLpIndexInvalid) {
		printk(KERN_WARNING_VIO "Invalid hosting partition\n");
		return -EIO;
	}

	/* Send the open event to OS/400 */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_blockio | vioblockopen,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)(unsigned long)&we, VIOVERSION << 16,
			((u64)device_no << 48) | ((u64)flags << 32), 0, 0, 0);
	if (hvrc != 0) {
		printk(KERN_WARNING_VIO "bad rc on signalLpEvent %d\n",
				(int)hvrc);
		return -EIO;
	}

	/* Wait for the interrupt handler to get the response */
	down(&Semaphore);

	/* Check the return code */
	if (we.rc != 0) {
		const struct vio_error_entry *err =
			vio_lookup_rc(viodasd_err_table, we.data.subRC);
		/*
		 * Temporary patch to quiet down the viodasd when drivers
		 * are probing for drives, especially lvm.  Collin is aware
		 * and is working on this.
		 */
#if 0
		printk(KERN_WARNING_VIO "bad rc opening disk: %d:0x%04x (%s)\n",
				(int) we.rc, we.data.subRC, err->msg);
#endif
		return -err->errno;
	}
	
	/* Bump the use count */
	viodasd_devices[device_no].useCount++;

	/*
	 * If this is the first open of this device, update the device
	 * information.  If this is NOT the first open, assume that it
	 * isn't changing
	 */
	gendisk = viodasd_devices[device_no].disk;
	if (gendisk == NULL)
		return 0;
	if (viodasd_devices[device_no].useCount == 0) {
		if (viodasd_devices[device_no].size > 0)
                        set_capacity(gendisk,
					viodasd_devices[device_no].size >> 9);
	} else if (get_capacity(gendisk) !=
			viodasd_devices[device_no].size >> 9)
		/*
		 * If the size of the device changed, weird things
		 * are happening!
		 */
		printk(KERN_WARNING_VIO
		       "disk size change (%d to %d sectors) for device %d\n",
		       (int)get_capacity(gendisk),
		       (int)viodasd_devices[device_no].size >> 9, device_no);

	internal_register_disk(device_no);

	return 0;
}

/*
 * This is the actual release code.  It gets called from the external
 * release entry point, as well as from the init code when we're figuring
 * out what disks we have.
 */
static int internal_release(int device_no, u16 flags)
{
	/* Send the event to OS/400.  We DON'T expect a response */
	HvLpEvent_Rc hvrc;

	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_blockio | vioblockclose,
			HvLpEvent_AckInd_NoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp), 0,
			VIOVERSION << 16,
			((u64)device_no << 48) | ((u64)flags << 32), 0, 0, 0);
	viodasd_devices[device_no].useCount--;
	if (hvrc != 0) {
		printk(KERN_WARNING_VIO
		       "bad rc sending event to OS/400 %d\n", (int)hvrc);
		return -EIO;
	}
	return 0;
}


/*
 * External open entry point.
 */
static int viodasd_open(struct inode *inode, struct file *fil)
{
	int device_no;
	int old_max_disk = viodasd_max_disk;

	/* Do a bunch of sanity checks */
	if (!inode) {
		printk(KERN_WARNING_VIO "no inode provided in open\n");
		return -ENODEV;
	}

	if (major_to_index(MAJOR(inode->i_rdev)) < 0) {
		printk(KERN_WARNING_VIO
		       "Weird error...wrong major number on open\n");
		return -ENODEV;
	}

	device_no = DEVICE_NR(inode->i_rdev);
	if ((device_no > MAX_DISKNO) || (device_no < 0)) {
		printk(KERN_WARNING_VIO
		       "Invalid device number %d in open\n", device_no);
		return -ENODEV;
	}
	if (!viodasd_devices[device_no].bdev)
		viodasd_devices[device_no].bdev = inode->i_bdev;

	/* Call the actual open code */
	if (internal_open(device_no, access_flags(fil ? fil->f_mode : 0)) == 0) {
		int i;
		MOD_INC_USE_COUNT;
		/* For each new disk: */
		/* update the disk's geometry via internal_open and register it */
		for (i = old_max_disk + 1; i <= viodasd_max_disk; ++i) {
			internal_open(i, vioblockflags_ro);
			internal_release(i, vioblockflags_ro);
		}
		return 0;
	} else
		return -EIO;
}

/* External release entry point.
 */
static int viodasd_release(struct inode *ino, struct file *fil)
{
	int device_no;

	/* Do a bunch of sanity checks */
	if (!ino) {
		printk(KERN_WARNING_VIO "no inode provided in release\n");
		return -ENODEV;
	}

	if (major_to_index(MAJOR(ino->i_rdev)) < 0) {
		printk(KERN_WARNING_VIO
		       "Weird error...wrong major number on release\n");
		return -ENODEV;
	}

	device_no = DEVICE_NR(ino->i_rdev);

	if (device_no > MAX_DISKNO || device_no < 0) {
		printk(KERN_WARNING_VIO
		       "Tried to release invalid disk number %d\n", device_no);
		return -ENODEV;
	}

	/* Call the actual release code */
	internal_release(device_no, access_flags(fil ? fil->f_mode : 0));

	MOD_DEC_USE_COUNT;
	return 0;
}

/* External ioctl entry point.
 */
static int viodasd_ioctl(struct inode *ino, struct file *fil,
			 unsigned int cmd, unsigned long arg)
{
	int device_no;
	int err;
	struct hd_struct *partition;

	/* Sanity checks */
	if (!ino) {
		printk(KERN_WARNING_VIO "no inode provided in ioctl\n");
		return -ENODEV;
	}

	if (major_to_index(MAJOR(ino->i_rdev)) < 0) {
		printk(KERN_WARNING_VIO
		       "Weird error...wrong major number on ioctl\n");
		return -ENODEV;
	}

	device_no = DEVICE_NR(ino->i_rdev);
	if (device_no > viodasd_max_disk) {
		printk(KERN_WARNING_VIO
		       "Invalid device number %d in ioctl\n", device_no);
		return -ENODEV;
	}

	partition = devt_to_partition(ino->i_rdev);

	switch (cmd) {
	case HDIO_GETGEO:
	{
		unsigned char sectors;
		unsigned char heads;
		unsigned short cylinders;

		struct hd_geometry *geo =
		    (struct hd_geometry *) arg;
		if (geo == NULL)
			return -EINVAL;

		err = verify_area(VERIFY_WRITE, geo, sizeof(*geo));
		if (err)
			return err;

		sectors = viodasd_devices[device_no].sectors;
		if (sectors == 0)
			sectors = 32;

		heads = viodasd_devices[device_no].tracks;
		if (heads == 0)
			heads = 64;

		cylinders = viodasd_devices[device_no].cylinders;
		if (cylinders == 0)
			cylinders =
			    partition->nr_sects / (sectors *
						   heads);

		put_user(sectors, &geo->sectors);
		put_user(heads, &geo->heads);
		put_user(cylinders, &geo->cylinders);

		if (partition)
			put_user(partition->start_sect, &geo->start);
		else
			put_user(0, &geo->start);

		return 0;
	}


#define PRTIOC(x)	\
	case x: printk(KERN_WARNING_VIO "got unsupported FD ioctl " #x "\n"); \
                          return -EINVAL;
	PRTIOC(FDCLRPRM);
	PRTIOC(FDSETPRM);
	PRTIOC(FDDEFPRM);
	PRTIOC(FDGETPRM);
	PRTIOC(FDMSGON);
	PRTIOC(FDMSGOFF);
	PRTIOC(FDFMTBEG);
	PRTIOC(FDFMTTRK);
	PRTIOC(FDFMTEND);
	PRTIOC(FDSETEMSGTRESH);
	PRTIOC(FDSETMAXERRS);
	PRTIOC(FDGETMAXERRS);
	PRTIOC(FDGETDRVTYP);
	PRTIOC(FDSETDRVPRM);
	PRTIOC(FDGETDRVPRM);
	PRTIOC(FDGETDRVSTAT);
	PRTIOC(FDPOLLDRVSTAT);
	PRTIOC(FDRESET);
	PRTIOC(FDGETFDCSTAT);
	PRTIOC(FDWERRORCLR);
	PRTIOC(FDWERRORGET);
	PRTIOC(FDRAWCMD);
	PRTIOC(FDEJECT);
	PRTIOC(FDTWADDLE);
	}

	return -EINVAL;
}

/*
 * Send an actual I/O request to OS/400
 */
static int send_request(struct request *req)
{
	u64 start;
	int direction;
	int nsg;
	u16 viocmd;
	HvLpEvent_Rc hvrc;
	struct vioblocklpevent *bevent;
	struct scatterlist sg[VIOMAXBLOCKDMA];
	int sgindex;
	int statindex;
        int device_no = DEVICE_NO(req->rq_disk->private_data);

	start = (u64)req->sector << 9;

	/* More paranoia checks */
	if ((req->sector + req->nr_sectors) > get_capacity(req->rq_disk)) {
		printk(KERN_WARNING_VIO "Invalid request offset & length\n");
		printk(KERN_WARNING_VIO
				"req->sector: %ld, req->nr_sectors: %ld\n",
				req->sector, req->nr_sectors);
		printk(KERN_WARNING_VIO "device: %s\n",
				req->rq_disk->disk_name);
		return -1;
	}

	if (rq_data_dir(req) == READ) {
		direction = PCI_DMA_FROMDEVICE;
		viocmd = viomajorsubtype_blockio | vioblockread;
		statindex = 0;
	} else {
		direction = PCI_DMA_TODEVICE;
		viocmd = viomajorsubtype_blockio | vioblockwrite;
		statindex = 1;
	}

	/* Update totals */
	viod_stats[device_no][statindex].tot++;

	/* Now build the scatter-gather list */
        nsg = blk_rq_map_sg(req->q, req, sg);
	nsg = pci_map_sg(iSeries_vio_dev, sg, nsg, direction);
	/* Update stats */
	viod_stats[device_no][statindex].ntce[nsg]++;

	/* This optimization handles a single DMA block */
	if (nsg == 1)
		hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
				HvLpEvent_Type_VirtualIo,
				viomajorsubtype_blockio | viocmd,
				HvLpEvent_AckInd_DoAck,
				HvLpEvent_AckType_ImmediateAck,
				viopath_sourceinst(viopath_hostLp),
				viopath_targetinst(viopath_hostLp),
				(u64)(unsigned long)req, VIOVERSION << 16,
				((u64)device_no << 48), start,
				((u64)sg[0].dma_address) << 32,
				sg[0].dma_length);
	else {
		bevent = (struct vioblocklpevent *)
			vio_get_event_buffer(viomajorsubtype_blockio);
		if (bevent == NULL) {
			printk(KERN_WARNING_VIO
			       "error allocating disk event buffer\n");
			return -1;
		}

		/*
		 * Now build up the actual request.  Note that we store
		 * the pointer to the request buffer in the correlation
		 * token so we can match this response up later
		 */
		memset(bevent, 0, sizeof(struct vioblocklpevent));
		bevent->event.xFlags.xValid = 1;
		bevent->event.xFlags.xFunction = HvLpEvent_Function_Int;
		bevent->event.xFlags.xAckInd = HvLpEvent_AckInd_DoAck;
		bevent->event.xFlags.xAckType = HvLpEvent_AckType_ImmediateAck;
		bevent->event.xType = HvLpEvent_Type_VirtualIo;
		bevent->event.xSubtype = viocmd;
		bevent->event.xSourceLp = HvLpConfig_getLpIndex();
		bevent->event.xTargetLp = viopath_hostLp;
		bevent->event.xSizeMinus1 =
			offsetof(struct vioblocklpevent, u.rwData.dmaInfo) +
			(sizeof(bevent->u.rwData.dmaInfo[0]) * nsg) - 1;
		bevent->event.xSourceInstanceId =
			viopath_sourceinst(viopath_hostLp);
		bevent->event.xTargetInstanceId =
			viopath_targetinst(viopath_hostLp);
		bevent->event.xCorrelationToken = (u64)(unsigned long)req;
		bevent->mVersion = VIOVERSION;
		bevent->mDisk = device_no;
		bevent->u.rwData.mOffset = start;

		/*
		 * Copy just the dma information from the sg list
		 * into the request
		 */
		for (sgindex = 0; sgindex < nsg; sgindex++) {
			bevent->u.rwData.dmaInfo[sgindex].mToken =
				sg[sgindex].dma_address;
			bevent->u.rwData.dmaInfo[sgindex].mLen =
				sg[sgindex].dma_length;
		}

		/* Send the request */
		hvrc = HvCallEvent_signalLpEvent(&bevent->event);
		vio_free_event_buffer(viomajorsubtype_blockio, bevent);
	}

	if (hvrc != HvLpEvent_Rc_Good) {
		printk(KERN_WARNING_VIO
		       "error sending disk event to OS/400 (rc %d)\n",
		       (int)hvrc);
		return -1;
	}
	/* If the request was successful, bump the number of outstanding */
	num_req_outstanding++;
	return 0;
}

/*
 * This is the external request processing routine
 */
static void do_viodasd_request(request_queue_t *q)
{
	if (q == NULL)
		return;
	for (;;) {
		struct request *req;
		struct gendisk *gendisk;

		/*
		 * inlined INIT_REQUEST here because we don't define
		 * MAJOR_NR before blk.h
		 */
                if ((req = elv_next_request(q)) == NULL)
                	return;
		/* check that request contains a valid command */
		if (!blk_fs_request(req)) {
			viodasd_end_request(req, 0, 0);
			continue;
		}

		gendisk = req->rq_disk;
		if (major_to_index(gendisk->major) < 0)
			panic(VIOD_DEVICE_NAME ": request list destroyed");

		if (get_capacity(gendisk) == 0) {
			printk(KERN_WARNING_VIO
					"Ouch! get_capacity(gendisk) is 0\n");
			viodasd_end_request(req, 0, 0);
			continue;
		}

		/* If the queue is plugged, don't dequeue anything right now */
		if (blk_queue_plugged(q))
			return;

		/*
		 * If we already have the maximum number of requests
		 * outstanding to OS/400 just bail out. We'll come
		 * back later.
		 */
		if (num_req_outstanding >= VIOMAXREQ)
			return;

		/* get the current request, then dequeue it from the queue */
		blkdev_dequeue_request(req);

		/* Try sending the request */
		if (send_request(req) == 0)
			list_add_tail(&req->queuelist, &reqlist);
		else
			viodasd_end_request(req, 0, 0);
	}
}

/*
 * Check for changed disks
 */
static int viodasd_check_change(struct gendisk *gendisk)
{
	struct viodasd_waitevent we;
	HvLpEvent_Rc hvrc;
	int device_no = major_to_index(gendisk->major);

	/* This semaphore is raised in the interrupt handler */
	DECLARE_MUTEX_LOCKED(Semaphore);

	/* Check that we are dealing with a valid hosting partition */
	if (viopath_hostLp == HvLpIndexInvalid) {
		printk(KERN_WARNING_VIO "Invalid hosting partition\n");
		return -EIO;
	}

	we.sem = &Semaphore;

	/* Send the open event to OS/400 */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
			HvLpEvent_Type_VirtualIo,
			viomajorsubtype_blockio | vioblockcheck,
			HvLpEvent_AckInd_DoAck, HvLpEvent_AckType_ImmediateAck,
			viopath_sourceinst(viopath_hostLp),
			viopath_targetinst(viopath_hostLp),
			(u64)(unsigned long)&we, VIOVERSION << 16,
			((u64)device_no << 48), 0, 0, 0);
	if (hvrc != 0) {
		printk(KERN_WARNING_VIO "bad rc on signalLpEvent %d\n",
				(int)hvrc);
		return -EIO;
	}

	/* Wait for the interrupt handler to get the response */
	down(&Semaphore);

	/* Check the return code.  If bad, assume no change */
	if (we.rc != 0) {
		printk(KERN_WARNING_VIO
				"bad rc %d on check_change. Assuming no change\n",
				(int)we.rc);
		return 0;
	}

	return we.data.changed;
}

/*
 * Our file operations table
 */
static struct block_device_operations viodasd_fops = {
	.open = viodasd_open,
	.release = viodasd_release,
	.ioctl = viodasd_ioctl,
	.media_changed = viodasd_check_change,
	.revalidate_disk = viodasd_revalidate
};

/* returns the total number of scatterlist elements converted */
static int block_event_to_scatterlist(const struct vioblocklpevent *bevent,
		struct scatterlist *sg, int *total_len)
{
	int i, numsg;
	const struct rwData *rwData = &bevent->u.rwData;
	static const int offset =
		offsetof(struct vioblocklpevent, u.rwData.dmaInfo);
	static const int element_size = sizeof(rwData->dmaInfo[0]);

	numsg = ((bevent->event.xSizeMinus1 + 1) - offset) / element_size;
	if (numsg > VIOMAXBLOCKDMA)
		numsg = VIOMAXBLOCKDMA;

	*total_len = 0;
	memset(sg, 0, sizeof(sg[0]) * VIOMAXBLOCKDMA);

	for (i = 0; (i < numsg) && (rwData->dmaInfo[i].mLen > 0); ++i) {
		sg[i].dma_address = rwData->dmaInfo[i].mToken;
		sg[i].dma_length = rwData->dmaInfo[i].mLen;
		*total_len += rwData->dmaInfo[i].mLen;
	}
	return i;
}

static struct request *find_request_with_token(u64 token)
{
	struct request *req = blkdev_entry_to_request(reqlist.next);
	while ((&req->queuelist != &reqlist) &&
			((u64) (unsigned long)req != token))
		req = blkdev_entry_to_request(req->queuelist.next);
	if (&req->queuelist == &reqlist)
		return NULL;
	return req;
}

/*
 * Restart all queues, starting with the one _after_ the major given,
 * thus reducing the chance of starvation of disks with late majors.
 */
static void viodasd_restart_all_queues_starting_from(int first_major)
{
	int i, first_index = major_to_index(first_major);
	for(i = first_index + 1; i < NUM_MAJORS; ++i)
		do_viodasd_request(viodasd_devices[i].queue);
	for(i = 0; i <= first_index; ++i)
		do_viodasd_request(viodasd_devices[i].queue);
}

/*
 * For read and write requests, decrement the number of outstanding requests,
 * Free the DMA buffers we allocated, and find the matching request by
 * using the buffer pointer we stored in the correlation token.
 */
static int viodasd_handleReadWrite(struct vioblocklpevent *bevent)
{
	int num_sg, num_sect, pci_direction, total_len, major;
	struct request *req;
	struct scatterlist sg[VIOMAXBLOCKDMA];
	struct HvLpEvent *event = &bevent->event;
	unsigned long irq_flags;

	num_sg = block_event_to_scatterlist(bevent, sg, &total_len);
	num_sect = total_len >> 9;
	if (event->xSubtype == (viomajorsubtype_blockio | vioblockread))
		pci_direction = PCI_DMA_FROMDEVICE;
	else
		pci_direction = PCI_DMA_TODEVICE;
	pci_unmap_sg(iSeries_vio_dev, sg, num_sg, pci_direction);


	/*
	 * Since this is running in interrupt mode, we need to make sure
	 * we're not stepping on any global I/O operations
	 */
	spin_lock_irqsave(&viodasd_spinlock, irq_flags);

	num_req_outstanding--;

	/*
	 * Now find the matching request in OUR list (remember we moved
	 * the request from the global list to our list when we got it)
	 */
	req = find_request_with_token(bevent->event.xCorrelationToken);
	if (req == NULL) {
		printk(KERN_WARNING_VIO
		       "Yikes! No request matching 0x%lx found\n",
		       bevent->event.xCorrelationToken);
		spin_unlock_irqrestore(&viodasd_spinlock, irq_flags);
		return -1;
	}

	/* Remove the request from our list */
	list_del_init(&req->queuelist);
	major = req->rq_disk->major;

	if (event->xRc != HvLpEvent_Rc_Good) {
		const struct vio_error_entry *err;
		err = vio_lookup_rc(viodasd_err_table, bevent->mSubTypeRc);
		printk(KERN_WARNING_VIO "read/write error %d:0x%04x (%s)\n",
				event->xRc, bevent->mSubTypeRc, err->msg);
		viodasd_end_request(req, 0, 0);
	} else
		viodasd_end_request(req, 1, num_sect);

	/* Finally, try to get more requests off of this device's queue */
	viodasd_restart_all_queues_starting_from(major);

	spin_unlock_irqrestore(&viodasd_spinlock, irq_flags);

	return 0;
}

/* This routine handles incoming block LP events */
static void vioHandleBlockEvent(struct HvLpEvent *event)
{
	struct vioblocklpevent *bevent = (struct vioblocklpevent *)event;
	struct viodasd_waitevent *pwe;

	if (event == NULL)
		/* Notification that a partition went away! */
		return;
	/* First, we should NEVER get an int here...only acks */
	if (event->xFlags.xFunction == HvLpEvent_Function_Int) {
		printk(KERN_WARNING_VIO
		       "Yikes! got an int in viodasd event handler!\n");
		if (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}

	switch (event->xSubtype & VIOMINOR_SUBTYPE_MASK) {
	case vioblockopen:
		/*
		 * Handle a response to an open request.  We get all the
		 * disk information in the response, so update it.  The
		 * correlation token contains a pointer to a waitevent
		 * structure that has a semaphore in it.  update the
		 * return code in the waitevent structure and post the
		 * semaphore to wake up the guy who sent the request
		 */
		pwe = (struct viodasd_waitevent *)(unsigned long)event->
			xCorrelationToken;
		pwe->rc = event->xRc;
		pwe->data.subRC = bevent->mSubTypeRc;
		if (event->xRc == HvLpEvent_Rc_Good) {
			const struct openData *data = &bevent->u.openData;
			struct viodasd_device *device =
				&viodasd_devices[bevent->mDisk];
			device->readOnly =
				bevent->mFlags & vioblockflags_ro;
			device->size = data->mDiskLen;
			device->cylinders = data->mCylinders;
			device->tracks = data->mTracks;
			device->sectors = data->mSectors;
			device->bytesPerSector = data->mBytesPerSector;
			viodasd_max_disk = data->mMaxDisks;
		}
		up(pwe->sem);
		break;
	case vioblockclose:
		break;
	case vioblockcheck:
		pwe = (struct viodasd_waitevent *)(unsigned long)event->
			xCorrelationToken;
		pwe->rc = event->xRc;
		pwe->data.changed = bevent->u.check.changed;
		up(pwe->sem);
		break;
	case vioblockflush:
		up((void *)(unsigned long)event->xCorrelationToken);
		break;
	case vioblockread:
	case vioblockwrite:
		viodasd_handleReadWrite(bevent);
		break;

	default:
		printk(KERN_WARNING_VIO "invalid subtype!");
		if (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

static const char *major_name(int major)
{
	static char major_names[NUM_MAJORS][MAX_DISK_NAME];
	int index = major_to_index(major);

	if (index < 0)
		return NULL;
	if (major_names[index][0] == '\0') {
		if (index == 0)
			strcpy(major_names[index], VIOD_GENHD_NAME);
		else
			sprintf(major_names[index], VIOD_GENHD_NAME"%d", index);
	}
	return major_names[index];
}

static const char *disk_names(int diskno)
{
        static char name[MAX_DISK_NAME];
        char suffix[MAX_DISK_NAME];
        int idx = 0;
        int i, j;

        /* convert the disk number to a letter(s) */
        /* 0=a 25=z 26=aa 27=a, ....              */
        do {
                suffix[idx++] = 'a' + (diskno % 26);
                diskno /= 26;
        } while (diskno-- && (idx < MAX_DISK_NAME - 1));

        suffix[idx] = 0;
        /* reverse the array */
        for (i = 0, j = idx - 1; i < j; i++, j--) {
                char c = suffix[i];
		suffix[i] = suffix[j];
		suffix[j] = c;
	}

        /* put the name all together and return it */
        snprintf(name, MAX_DISK_NAME, "%s%s", VIOD_GENHD_NAME, suffix);
        return name;
}


static void viodasd_cleanup_major(int major)
{
	int i;

        for (i = 0; i < DEV_PER_MAJOR; i++) {
		int device_no = DEV_PER_MAJOR * major_to_index(major) + i;

		blk_cleanup_queue(viodasd_devices[device_no].disk->queue);
		del_gendisk(viodasd_devices[device_no].disk);
		put_disk(viodasd_devices[device_no].disk);
		kfree(viodasd_devices[device_no].disk);
		unregister_blkdev(major, major_name(major));
		viodasd_devices[device_no].disk = NULL;
	}
}

/* in case of bad return code, caller must cleanup2() for this device */
static int __init viodasd_init_major(int major)
{
	struct gendisk *gendisk;
        struct request_queue *q;
	int i;

        /* register the block device */
	if (register_blkdev(major, major_name(major))) {
		printk(KERN_WARNING "Unable to get major number %s\n",
				major_name(major));
		return -1;
	}
	for (i = 0; i < DEV_PER_MAJOR; i++) {
		int deviceno = DEV_PER_MAJOR * major_to_index(major) + i;

		/* create the request queue for the disk */
		spin_lock_init(&viodasd_devices[deviceno].q_lock);
		q = blk_init_queue(do_viodasd_request,
				&viodasd_devices[deviceno].q_lock);
		if (q == NULL)
			return -ENOMEM;
		blk_queue_max_hw_segments(q, VIOMAXBLOCKDMA);
		viodasd_devices[deviceno].queue = q;

		/* inialize the struct */
        	gendisk = alloc_disk(1 << PARTITION_SHIFT);
		if (gendisk == NULL)
			return -ENOMEM;
        	viodasd_devices[deviceno].disk = gendisk;
		gendisk->major = major;
        	gendisk->first_minor = i * (1 << PARTITION_SHIFT);
        	strncpy(gendisk->disk_name, disk_names(deviceno),
				MAX_DISK_NAME);
        	strncpy(gendisk->devfs_name, disk_names(deviceno),
				MAX_DISK_NAME);
        	gendisk->fops = &viodasd_fops;
		gendisk->queue = q;
        	gendisk->private_data = (void *)&viodasd_devices[deviceno];
		set_capacity(gendisk, viodasd_devices[deviceno].size >> 9);
	
		/* register us in the global list */
		add_disk(gendisk);
	}

	return 0;
}

/*
 * Initialize the whole device driver.  Handle module and non-module
 * versions
 */
static int __init viodasd_init(void)
{
	int i, j;

	/* Try to open to our host lp */
	if (viopath_hostLp == HvLpIndexInvalid)
		vio_set_hostlp();

	if (viopath_hostLp == HvLpIndexInvalid) {
		printk(KERN_WARNING_VIO "%s: invalid hosting partition\n",
		       VIOD_DEVICE_NAME);
		return -EIO;
	}

	printk(KERN_INFO_VIO
	       "%s: Disk vers %s, major %d, max disks %d, hosting partition %d\n",
	       VIOD_DEVICE_NAME, VIODASD_VERS, major_table[0], MAX_DISKNO,
	       viopath_hostLp);

	/* Actually open the path to the hosting partition */
	if (viopath_open(viopath_hostLp, viomajorsubtype_blockio,
				VIOMAXREQ + 2)) {
		printk(KERN_WARNING_VIO
		       "error opening path to host partition %d\n",
		       viopath_hostLp);
		return -EIO;
	} else
		printk("%s: opened path to hosting partition %d\n",
		       VIOD_DEVICE_NAME, viopath_hostLp);

	/*
	 * Initialize our request handler
	 */
	vio_setHandler(viomajorsubtype_blockio, vioHandleBlockEvent);

	viodasd_max_disk = MAX_DISKNO - 1;
	for (i = 0; (i <= viodasd_max_disk) && (i < MAX_DISKNO); i++) {
		// Note that internal_open has side effects:
		//  a) it updates the size of the disk
		//  b) it updates viodasd_max_disk
		//  c) it registers the disk if it has not done so already
		if (internal_open(i, vioblockflags_ro) == 0)
			internal_release(i, vioblockflags_ro);
	}

	printk(KERN_INFO_VIO "%s: Currently %d disks connected\n",
	       VIOD_DEVICE_NAME, (int)viodasd_max_disk + 1);
	if (viodasd_max_disk > (MAX_DISKNO - 1))
		printk(KERN_INFO_VIO "Only examining the first %d\n",
		       MAX_DISKNO);

	for (i = 0; i < NUM_MAJORS; ++i) {
		int init_rc = viodasd_init_major(major_table[i]);
		if (init_rc < 0) {
			for (j = 0; j <= i; ++j)
				viodasd_cleanup_major(major_table[j]);
			return init_rc;
		}
	}

	/* 
	 * Create the proc entry
	 */
	iSeries_proc_callback(&viodasd_proc_init);

	return 0;
}
module_init(viodasd_init);

#if 0
void viodasd_exit(void)
{
	int i;
	for(i = 0; i < NUM_MAJORS; ++i)
		viodasd_cleanup_major(major_table[i]);

	CLEANIT(viodasd_devices);

	viopath_close(viopath_hostLp, viomajorsubtype_blockio, VIOMAXREQ + 2);
	iSeries_proc_callback(&viodasd_proc_delete);

}

module_exit(viodasd_exit);
#endif
