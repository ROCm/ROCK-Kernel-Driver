/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 *
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 *
 *  IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  and general streamlining by Mark Lord.
 *
 *  Removed 99% of above. Use Mark's ide driver for those options.
 *  This is now a lightweight ST-506 driver. (Paul Gortmaker)
 *
 *  Modified 1995 Russell King for ARM processor.
 *
 *  Bugfix: max_sectors must be <= 255 or the wheels tend to come
 *  off in a hurry once you queue things up - Paul G. 02/2001
 */

/* Uncomment the following if you want verbose error reports. */
/* #define VERBOSE_ERRORS */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/mc146818rtc.h> /* CMOS defines */
#include <linux/init.h>
#include <linux/blkpg.h>
#include <linux/hdreg.h>

#define REALLY_SLOW_IO
#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define MAJOR_NR HD_MAJOR
#define DEVICE_NR(device) (minor(device)>>6)
#include <linux/blk.h>

/* ATA commands we use.
 */
#define WIN_SPECIFY	0x91 /* set drive geometry translation */
#define WIN_RESTORE	0x10
#define WIN_READ	0x20 /* 28-Bit */
#define WIN_WRITE	0x30 /* 28-Bit */

#define HD_IRQ 14	/* the standard disk interrupt */

#ifdef __arm__
#undef  HD_IRQ
#endif
#include <asm/irq.h>
#ifdef __arm__
#define HD_IRQ IRQ_HARDDISK
#endif

/* Hd controller regster ports */

#define HD_DATA		0x1f0		/* _CTL when writing */
#define HD_ERROR	0x1f1		/* see err-bits */
#define HD_NSECTOR	0x1f2		/* nr of sectors to read/write */
#define HD_SECTOR	0x1f3		/* starting sector */
#define HD_LCYL		0x1f4		/* starting cylinder */
#define HD_HCYL		0x1f5		/* high byte of starting cyl */
#define HD_CURRENT	0x1f6		/* 101dhhhh , d=drive, hhhh=head */
#define HD_STATUS	0x1f7		/* see status-bits */
#define HD_FEATURE	HD_ERROR	/* same io address, read=error, write=feature */
#define HD_PRECOMP	HD_FEATURE	/* obsolete use of this port - predates IDE */
#define HD_COMMAND	HD_STATUS	/* same io address, read=status, write=cmd */

#define HD_CMD		0x3f6		/* used for resets */
#define HD_ALTSTATUS	0x3f6		/* same as HD_STATUS but doesn't clear irq */

/* Bits of HD_STATUS */
#define ERR_STAT		0x01
#define INDEX_STAT		0x02
#define ECC_STAT		0x04	/* Corrected error */
#define DRQ_STAT		0x08
#define SEEK_STAT		0x10
#define SERVICE_STAT		SEEK_STAT
#define WRERR_STAT		0x20
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits for HD_ERROR */
#define MARK_ERR		0x01	/* Bad address mark */
#define TRK0_ERR		0x02	/* couldn't find track 0 */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR			0x08	/* media change request */
#define ID_ERR			0x10	/* ID field not found */
#define MC_ERR			0x20	/* media changed */
#define ECC_ERR			0x40	/* Uncorrectable ECC error */
#define BBD_ERR			0x80	/* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR		0x80	/* new meaning:  CRC error during transfer */

static spinlock_t hd_lock = SPIN_LOCK_UNLOCKED;

static int revalidate_hddisk(kdev_t, int);

#define TIMEOUT_VALUE	(6*HZ)
#define	HD_DELAY	0

#define MAX_ERRORS     16	/* Max read/write errors/sector */
#define RESET_FREQ      8	/* Reset controller every 8th retry */
#define RECAL_FREQ      4	/* Recalibrate every 4th retry */
#define MAX_HD		2

#define STAT_OK		(READY_STAT|SEEK_STAT)
#define OK_STATUS(s)	(((s)&(STAT_OK|(BUSY_STAT|WRERR_STAT|ERR_STAT)))==STAT_OK)

static void recal_intr(void);
static void bad_rw_intr(void);

static char recalibrate[MAX_HD];
static char special_op[MAX_HD];

static int reset;
static int hd_error;

#define SUBSECTOR(block) (CURRENT->current_nr_sectors > 0)

/*
 *  This struct defines the HD's and their types.
 */
struct hd_i_struct {
	unsigned int head,sect,cyl,wpcom,lzone,ctl;
};
	
#ifdef HD_TYPE
static struct hd_i_struct hd_info[] = { HD_TYPE };
static int NR_HD = ((sizeof (hd_info))/(sizeof (struct hd_i_struct)));
#else
static struct hd_i_struct hd_info[MAX_HD];
static int NR_HD;
#endif

static struct hd_struct hd[MAX_HD<<6];

static struct timer_list device_timer;

#define TIMEOUT_VALUE (6*HZ)

#define SET_TIMER							\
	do {								\
		mod_timer(&device_timer, jiffies + TIMEOUT_VALUE);	\
	} while (0)

static void (*do_hd)(void) = NULL;
#define SET_HANDLER(x) \
if ((do_hd = (x)) != NULL) \
	SET_TIMER; \
else \
	del_timer(&device_timer);


#if (HD_DELAY > 0)
unsigned long last_req;

unsigned long read_timer(void)
{
        extern spinlock_t i8253_lock;
	unsigned long t, flags;
	int i;

	spin_lock_irqsave(&i8253_lock, flags);
	t = jiffies * 11932;
    	outb_p(0, 0x43);
	i = inb_p(0x40);
	i |= inb(0x40) << 8;
	spin_unlock_irqrestore(&i8253_lock, flags);
	return(t - i);
}
#endif

void __init hd_setup(char *str, int *ints)
{
	int hdind = 0;

	if (ints[0] != 3)
		return;
	if (hd_info[0].head != 0)
		hdind=1;
	hd_info[hdind].head = ints[2];
	hd_info[hdind].sect = ints[3];
	hd_info[hdind].cyl = ints[1];
	hd_info[hdind].wpcom = 0;
	hd_info[hdind].lzone = ints[1];
	hd_info[hdind].ctl = (ints[2] > 8 ? 8 : 0);
	NR_HD = hdind+1;
}

static void dump_status (const char *msg, unsigned int stat)
{
	char devc;

	devc = !blk_queue_empty(QUEUE) ? 'a' + DEVICE_NR(CURRENT->rq_dev) : '?';
#ifdef VERBOSE_ERRORS
	printk("hd%c: %s: status=0x%02x { ", devc, msg, stat & 0xff);
	if (stat & BUSY_STAT)	printk("Busy ");
	if (stat & READY_STAT)	printk("DriveReady ");
	if (stat & WRERR_STAT)	printk("WriteFault ");
	if (stat & SEEK_STAT)	printk("SeekComplete ");
	if (stat & DRQ_STAT)	printk("DataRequest ");
	if (stat & ECC_STAT)	printk("CorrectedError ");
	if (stat & INDEX_STAT)	printk("Index ");
	if (stat & ERR_STAT)	printk("Error ");
	printk("}\n");
	if ((stat & ERR_STAT) == 0) {
		hd_error = 0;
	} else {
		hd_error = inb(HD_ERROR);
		printk("hd%c: %s: error=0x%02x { ", devc, msg, hd_error & 0xff);
		if (hd_error & BBD_ERR)		printk("BadSector ");
		if (hd_error & ECC_ERR)		printk("UncorrectableError ");
		if (hd_error & ID_ERR)		printk("SectorIdNotFound ");
		if (hd_error & ABRT_ERR)	printk("DriveStatusError ");
		if (hd_error & TRK0_ERR)	printk("TrackZeroNotFound ");
		if (hd_error & MARK_ERR)	printk("AddrMarkNotFound ");
		printk("}");
		if (hd_error & (BBD_ERR|ECC_ERR|ID_ERR|MARK_ERR)) {
			printk(", CHS=%d/%d/%d", (inb(HD_HCYL)<<8) + inb(HD_LCYL),
				inb(HD_CURRENT) & 0xf, inb(HD_SECTOR));
			if (!blk_queue_empty(QUEUE))
				printk(", sector=%ld", CURRENT->sector);
		}
		printk("\n");
	}
#else
	printk("hd%c: %s: status=0x%02x.\n", devc, msg, stat & 0xff);
	if ((stat & ERR_STAT) == 0) {
		hd_error = 0;
	} else {
		hd_error = inb(HD_ERROR);
		printk("hd%c: %s: error=0x%02x.\n", devc, msg, hd_error & 0xff);
	}
#endif
}

void check_status(void)
{
	int i = inb_p(HD_STATUS);

	if (!OK_STATUS(i)) {
		dump_status("check_status", i);
		bad_rw_intr();
	}
}

static int controller_busy(void)
{
	int retries = 100000;
	unsigned char status;

	do {
		status = inb_p(HD_STATUS);
	} while ((status & BUSY_STAT) && --retries);
	return status;
}

static int status_ok(void)
{
	unsigned char status = inb_p(HD_STATUS);

	if (status & BUSY_STAT)
		return 1;	/* Ancient, but does it make sense??? */
	if (status & WRERR_STAT)
		return 0;
	if (!(status & READY_STAT))
		return 0;
	if (!(status & SEEK_STAT))
		return 0;
	return 1;
}

static int controller_ready(unsigned int drive, unsigned int head)
{
	int retry = 100;

	do {
		if (controller_busy() & BUSY_STAT)
			return 0;
		outb_p(0xA0 | (drive<<4) | head, HD_CURRENT);
		if (status_ok())
			return 1;
	} while (--retry);
	return 0;
}

static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	unsigned short port;

#if (HD_DELAY > 0)
	while (read_timer() - last_req < HD_DELAY)
		/* nothing */;
#endif
	if (reset)
		return;
	if (!controller_ready(drive, head)) {
		reset = 1;
		return;
	}
	SET_HANDLER(intr_addr);
	outb_p(hd_info[drive].ctl,HD_CMD);
	port=HD_DATA;
	outb_p(hd_info[drive].wpcom>>2,++port);
	outb_p(nsect,++port);
	outb_p(sect,++port);
	outb_p(cyl,++port);
	outb_p(cyl>>8,++port);
	outb_p(0xA0|(drive<<4)|head,++port);
	outb_p(cmd,++port);
}

static void hd_request (void);

static int drive_busy(void)
{
	unsigned int i;
	unsigned char c;

	for (i = 0; i < 500000 ; i++) {
		c = inb_p(HD_STATUS);
		if ((c & (BUSY_STAT | READY_STAT | SEEK_STAT)) == STAT_OK)
			return 0;
	}
	dump_status("reset timed out", c);
	return 1;
}

static void reset_controller(void)
{
	int	i;

	outb_p(4,HD_CMD);
	for(i = 0; i < 1000; i++) barrier();
	outb_p(hd_info[0].ctl & 0x0f,HD_CMD);
	for(i = 0; i < 1000; i++) barrier();
	if (drive_busy())
		printk("hd: controller still busy\n");
	else if ((hd_error = inb(HD_ERROR)) != 1)
		printk("hd: controller reset failed: %02x\n",hd_error);
}

static void reset_hd(void)
{
	static int i;

repeat:
	if (reset) {
		reset = 0;
		i = -1;
		reset_controller();
	} else {
		check_status();
		if (reset)
			goto repeat;
	}
	if (++i < NR_HD) {
		special_op[i] = recalibrate[i] = 1;
		hd_out(i,hd_info[i].sect,hd_info[i].sect,hd_info[i].head-1,
			hd_info[i].cyl,WIN_SPECIFY,&reset_hd);
		if (reset)
			goto repeat;
	} else
		hd_request();
}

/*
 * Ok, don't know what to do with the unexpected interrupts: on some machines
 * doing a reset and a retry seems to result in an eternal loop. Right now I
 * ignore it, and just set the timeout.
 *
 * On laptops (and "green" PCs), an unexpected interrupt occurs whenever the
 * drive enters "idle", "standby", or "sleep" mode, so if the status looks
 * "good", we just ignore the interrupt completely.
 */
void unexpected_hd_interrupt(void)
{
	unsigned int stat = inb_p(HD_STATUS);

	if (stat & (BUSY_STAT|DRQ_STAT|ECC_STAT|ERR_STAT)) {
		dump_status ("unexpected interrupt", stat);
		SET_TIMER;
	}
}

/*
 * bad_rw_intr() now tries to be a bit smarter and does things
 * according to the error returned by the controller.
 * -Mika Liljeberg (liljeber@cs.Helsinki.FI)
 */
static void bad_rw_intr(void)
{
	int dev;

	if (blk_queue_empty(QUEUE))
		return;
	dev = DEVICE_NR(CURRENT->rq_dev);
	if (++CURRENT->errors >= MAX_ERRORS || (hd_error & BBD_ERR)) {
		end_request(CURRENT, 0);
		special_op[dev] = recalibrate[dev] = 1;
	} else if (CURRENT->errors % RESET_FREQ == 0)
		reset = 1;
	else if ((hd_error & TRK0_ERR) || CURRENT->errors % RECAL_FREQ == 0)
		special_op[dev] = recalibrate[dev] = 1;
	/* Otherwise just retry */
}

static inline int wait_DRQ(void)
{
	int retries = 100000, stat;

	while (--retries > 0)
		if ((stat = inb_p(HD_STATUS)) & DRQ_STAT)
			return 0;
	dump_status("wait_DRQ", stat);
	return -1;
}

static void read_intr(void)
{
	int i, retries = 100000;

	do {
		i = (unsigned) inb_p(HD_STATUS);
		if (i & BUSY_STAT)
			continue;
		if (!OK_STATUS(i))
			break;
		if (i & DRQ_STAT)
			goto ok_to_read;
	} while (--retries > 0);
	dump_status("read_intr", i);
	bad_rw_intr();
	hd_request();
	return;
ok_to_read:
	insw(HD_DATA,CURRENT->buffer,256);
	CURRENT->sector++;
	CURRENT->buffer += 512;
	CURRENT->errors = 0;
	i = --CURRENT->nr_sectors;
	--CURRENT->current_nr_sectors;
#ifdef DEBUG
	printk("hd%c: read: sector %ld, remaining = %ld, buffer=0x%08lx\n",
		dev+'a', CURRENT->sector, CURRENT->nr_sectors,
		(unsigned long) CURRENT->buffer+512));
#endif
	if (CURRENT->current_nr_sectors <= 0)
		end_request(CURRENT, 1);
	if (i > 0) {
		SET_HANDLER(&read_intr);
		return;
	}
	(void) inb_p(HD_STATUS);
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	if (!blk_queue_empty(QUEUE))
		hd_request();
	return;
}

static void write_intr(void)
{
	int i;
	int retries = 100000;

	do {
		i = (unsigned) inb_p(HD_STATUS);
		if (i & BUSY_STAT)
			continue;
		if (!OK_STATUS(i))
			break;
		if ((CURRENT->nr_sectors <= 1) || (i & DRQ_STAT))
			goto ok_to_write;
	} while (--retries > 0);
	dump_status("write_intr", i);
	bad_rw_intr();
	hd_request();
	return;
ok_to_write:
	CURRENT->sector++;
	i = --CURRENT->nr_sectors;
	--CURRENT->current_nr_sectors;
	CURRENT->buffer += 512;
	if (!i || (CURRENT->bio && !SUBSECTOR(i)))
		end_request(CURRENT, 1);
	if (i > 0) {
		SET_HANDLER(&write_intr);
		outsw(HD_DATA,CURRENT->buffer,256);
		local_irq_enable();
	} else {
#if (HD_DELAY > 0)
		last_req = read_timer();
#endif
		hd_request();
	}
	return;
}

static void recal_intr(void)
{
	check_status();
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	hd_request();
}

/*
 * This is another of the error-routines I don't know what to do with. The
 * best idea seems to just set reset, and start all over again.
 */
static void hd_times_out(unsigned long dummy)
{
	unsigned int dev;

	do_hd = NULL;

	if (blk_queue_empty(QUEUE))
		return;

	disable_irq(HD_IRQ);
	local_irq_enable();
	reset = 1;
	dev = DEVICE_NR(CURRENT->rq_dev);
	printk("hd%c: timeout\n", dev+'a');
	if (++CURRENT->errors >= MAX_ERRORS) {
#ifdef DEBUG
		printk("hd%c: too many errors\n", dev+'a');
#endif
		end_request(CURRENT, 0);
	}
	local_irq_disable();
	hd_request();
	enable_irq(HD_IRQ);
}

int do_special_op (unsigned int dev)
{
	if (recalibrate[dev]) {
		recalibrate[dev] = 0;
		hd_out(dev,hd_info[dev].sect,0,0,0,WIN_RESTORE,&recal_intr);
		return reset;
	}
	if (hd_info[dev].head > 16) {
		printk ("hd%c: cannot handle device with more than 16 heads - giving up\n", dev+'a');
		end_request(CURRENT, 0);
	}
	special_op[dev] = 0;
	return 1;
}

/*
 * The driver enables interrupts as much as possible.  In order to do this,
 * (a) the device-interrupt is disabled before entering hd_request(),
 * and (b) the timeout-interrupt is disabled before the sti().
 *
 * Interrupts are still masked (by default) whenever we are exchanging
 * data/cmds with a drive, because some drives seem to have very poor
 * tolerance for latency during I/O. The IDE driver has support to unmask
 * interrupts for non-broken hardware, so use that driver if required.
 */
static void hd_request(void)
{
	unsigned int dev, block, nsect, sec, track, head, cyl;

	if (do_hd)
		return;
repeat:
	del_timer(&device_timer);
	local_irq_enable();

	if (blk_queue_empty(QUEUE)) {
		do_hd = NULL;
		return;
	}

	if (reset) {
		local_irq_disable();
		reset_hd();
		return;
	}
	dev = minor(CURRENT->rq_dev);
	block = CURRENT->sector;
	nsect = CURRENT->nr_sectors;
	if (dev >= (NR_HD<<6) || (dev & 0x3f) ||
	    block >= hd[dev].nr_sects || ((block+nsect) > hd[dev].nr_sects)) {
		if (dev >= (NR_HD<<6) || (dev & 0x3f))
			printk("hd: bad minor number: device=%s\n",
			       kdevname(CURRENT->rq_dev));
		else
			printk("hd%c: bad access: block=%d, count=%d\n",
				(minor(CURRENT->rq_dev)>>6)+'a', block, nsect);
		end_request(CURRENT, 0);
		goto repeat;
	}

	dev >>= 6;
	if (special_op[dev]) {
		if (do_special_op(dev))
			goto repeat;
		return;
	}
	sec   = block % hd_info[dev].sect + 1;
	track = block / hd_info[dev].sect;
	head  = track % hd_info[dev].head;
	cyl   = track / hd_info[dev].head;
#ifdef DEBUG
	printk("hd%c: %sing: CHS=%d/%d/%d, sectors=%d, buffer=0x%08lx\n",
		dev+'a', (CURRENT->cmd == READ)?"read":"writ",
		cyl, head, sec, nsect, (unsigned long) CURRENT->buffer);
#endif
	if(CURRENT->flags & REQ_CMD) {
		switch (rq_data_dir(CURRENT)) {
		case READ:
			hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
			if (reset)
				goto repeat;
			break;
		case WRITE:
			hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
			if (reset)
				goto repeat;
			if (wait_DRQ()) {
				bad_rw_intr();
				goto repeat;
			}
			outsw(HD_DATA,CURRENT->buffer,256);
			break;
		default:
			printk("unknown hd-command\n");
			end_request(CURRENT, 0);
			break;
		}
	}
}

static void do_hd_request (request_queue_t * q)
{
	disable_irq(HD_IRQ);
	hd_request();
	enable_irq(HD_IRQ);
}

static int hd_ioctl(struct inode * inode, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	struct hd_geometry *loc = (struct hd_geometry *) arg;
	int dev;

	if ((!inode) || kdev_none(inode->i_rdev))
		return -EINVAL;
	dev = DEVICE_NR(inode->i_rdev);
	if (dev >= NR_HD)
		return -EINVAL;
	switch (cmd) {
		case HDIO_GETGEO:
		{
			struct hd_geometry g; 
			if (!loc)  return -EINVAL;
			g.heads = hd_info[dev].head;
			g.sectors = hd_info[dev].sect;
			g.cylinders = hd_info[dev].cyl;
			g.start = get_start_sect(inode->i_bdev);
			return copy_to_user(loc, &g, sizeof g) ? -EFAULT : 0; 
		}

		case BLKRRPART: /* Re-read partition tables */
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			return revalidate_hddisk(inode->i_rdev, 1);

		default:
			return -EINVAL;
	}
}

static int hd_open(struct inode * inode, struct file * filp)
{
	int target =  DEVICE_NR(inode->i_rdev);
	if (target >= NR_HD)
		return -ENODEV;
	return 0;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */

extern struct block_device_operations hd_fops;

static struct gendisk hd_gendisk = {
	.major =	MAJOR_NR,
	.major_name =	"hd",
	.minor_shift =	6,
	.part =		hd,
	.fops =		&hd_fops,
};
	
static void hd_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	void (*handler)(void) = do_hd;

	do_hd = NULL;
	del_timer(&device_timer);
	if (!handler)
		handler = unexpected_hd_interrupt;
	handler();
	local_irq_enable();
}

static struct block_device_operations hd_fops = {
	.open =		hd_open,
	.ioctl =	hd_ioctl,
};

/*
 * This is the hard disk IRQ description. The SA_INTERRUPT in sa_flags
 * means we run the IRQ-handler with interrupts disabled:  this is bad for
 * interrupt latency, but anything else has led to problems on some
 * machines.
 *
 * We enable interrupts in some of the routines after making sure it's
 * safe.
 */
static void __init hd_geninit(void)
{
	int drive;

	blk_queue_hardsect_size(QUEUE, 512);

#ifdef __i386__
	if (!NR_HD) {
		extern struct drive_info drive_info;
		unsigned char *BIOS = (unsigned char *) &drive_info;
		unsigned long flags;
		int cmos_disks;

		for (drive=0 ; drive<2 ; drive++) {
			hd_info[drive].cyl = *(unsigned short *) BIOS;
			hd_info[drive].head = *(2+BIOS);
			hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
			hd_info[drive].ctl = *(8+BIOS);
			hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
			hd_info[drive].sect = *(14+BIOS);
#ifdef does_not_work_for_everybody_with_scsi_but_helps_ibm_vp
			if (hd_info[drive].cyl && NR_HD == drive)
				NR_HD++;
#endif
			BIOS += 16;
		}

	/*
		We query CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatible with ST-506, and thus showing up in our
		BIOS table, but not register compatible, and therefore
		not present in CMOS.

		Furthermore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and 
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19 
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have 
		an AT controller hard disk for that drive.

		Currently the rtc_lock is a bit academic since this
		driver is non-modular, but someday... ?         Paul G.
	*/

		spin_lock_irqsave(&rtc_lock, flags);
		cmos_disks = CMOS_READ(0x12);
		spin_unlock_irqrestore(&rtc_lock, flags);

		if (cmos_disks & 0xf0) {
			if (cmos_disks & 0x0f)
				NR_HD = 2;
			else
				NR_HD = 1;
		}
	}
#endif /* __i386__ */
#ifdef __arm__
	if (!NR_HD) {
		/* We don't know anything about the drive.  This means
		 * that you *MUST* specify the drive parameters to the
		 * kernel yourself.
		 */
		printk("hd: no drives specified - use hd=cyl,head,sectors"
			" on kernel command line\n");
	}
#endif

	for (drive=0 ; drive < NR_HD ; drive++) {
		hd[drive<<6].nr_sects = hd_info[drive].head *
			hd_info[drive].sect * hd_info[drive].cyl;
		printk ("hd%c: %ldMB, CHS=%d/%d/%d\n", drive+'a',
			hd[drive<<6].nr_sects / 2048, hd_info[drive].cyl,
			hd_info[drive].head, hd_info[drive].sect);
	}
	if (!NR_HD)
		return;

	if (request_irq(HD_IRQ, hd_interrupt, SA_INTERRUPT, "hd", NULL)) {
		printk("hd: unable to get IRQ%d for the hard disk driver\n",
			HD_IRQ);
		NR_HD = 0;
		return;
	}
	if (!request_region(HD_DATA, 8, "hd")) {
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_DATA);
		NR_HD = 0;
		free_irq(HD_IRQ, NULL);
		return;
	}
	if (!request_region(HD_CMD, 1, "hd(cmd)")) {
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_CMD);
		NR_HD = 0;
		free_irq(HD_IRQ, NULL);
		release_region(HD_DATA, 8);
		return;
	}

	hd_gendisk.nr_real = NR_HD;

	for(drive=0; drive < NR_HD; drive++)
		register_disk(&hd_gendisk, mk_kdev(MAJOR_NR,drive<<6), 1<<6,
			&hd_fops, hd_info[drive].head * hd_info[drive].sect *
			hd_info[drive].cyl);
}

int __init hd_init(void)
{
	if (register_blkdev(MAJOR_NR,"hd",&hd_fops)) {
		printk("hd: unable to get major %d for hard disk\n",MAJOR_NR);
		return -1;
	}
	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), do_hd_request, &hd_lock);
	blk_queue_max_sectors(BLK_DEFAULT_QUEUE(MAJOR_NR), 255);
	add_gendisk(&hd_gendisk);
	init_timer(&device_timer);
	device_timer.function = hd_times_out;
	hd_geninit();
	return 0;
}

#define CAPACITY (hd_info[target].head*hd_info[target].sect*hd_info[target].cyl)
/* We assume that the BIOS parameters do not change, so the disk capacity
   will not change */

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
static int revalidate_hddisk(kdev_t dev, int maxusage)
{
	int target = DEVICE_NR(dev);
	kdev_t device = mk_kdev(MAJOR_NR, target << 6);
	int res = dev_lock_part(device);
	if (res < 0)
		return res;
	res = wipe_partitions(device);
	if (!res)
		grok_partitions(device, CAPACITY);
	dev_unlock_part(device);
	return res;
}

static int parse_hd_setup (char *line) {
	int ints[6];

	(void) get_options(line, ARRAY_SIZE(ints), ints);
	hd_setup(NULL, ints);

	return 1;
}
__setup("hd=", parse_hd_setup);

