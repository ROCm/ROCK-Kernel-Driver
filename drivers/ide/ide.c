/*
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
 *
 *  Mostly written by Mark Lord  <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the multiple IDE interface driver, as evolved from hd.c.
 * It supports up to MAX_HWIFS IDE interfaces, on one or more IRQs (usually 14 & 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * Primary:    ide0, port 0x1f0; major=3;  hda is minor=0; hdb is minor=64
 * Secondary:  ide1, port 0x170; major=22; hdc is minor=0; hdd is minor=64
 * Tertiary:   ide2, port 0x???; major=33; hde is minor=0; hdf is minor=64
 * Quaternary: ide3, port 0x???; major=34; hdg is minor=0; hdh is minor=64
 * ...
 *
 *  From hd.c:
 *  |
 *  | It traverses the request-list, using interrupts to jump between functions.
 *  | As nearly all functions can be called within interrupts, we may not sleep.
 *  | Special care is recommended.  Have Fun!
 *  |
 *  | modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *  |
 *  | Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  | in the early extended-partition checks and added DM partitions.
 *  |
 *  | Early work on error handling by Mika Liljeberg (liljeber@cs.Helsinki.FI).
 *  |
 *  | IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  | and general streamlining by Mark Lord (mlord@pobox.com).
 *
 *  October, 1994 -- Complete line-by-line overhaul for linux 1.1.x, by:
 *
 *	Mark Lord	(mlord@pobox.com)		(IDE Perf.Pkg)
 *	Delman Lee	(delman@ieee.org)		("Mr. atdisk2")
 *	Scott Snyder	(snyder@fnald0.fnal.gov)	(ATAPI IDE cd-rom)
 *
 *  This was a rewrite of just about everything from hd.c, though some original
 *  code is still sprinkled about.  Think of it as a major evolution, with
 *  inspiration from lots of linux users, esp.  hamish@zot.apana.org.au
 *
 *  Version 1.0 ALPHA	initial code, primary i/f working okay
 *  Version 1.3 BETA	dual i/f on shared irq tested & working!
 *  Version 1.4 BETA	added auto probing for irq(s)
 *  Version 1.5 BETA	added ALPHA (untested) support for IDE cd-roms,
 *  ...
 * Version 5.50		allow values as small as 20 for idebus=
 * Version 5.51		force non io_32bit in drive_cmd_intr()
 *			change delay_10ms() to delay_50ms() to fix problems
 * Version 5.52		fix incorrect invalidation of removable devices
 *			add "hdx=slow" command line option
 * Version 5.60		start to modularize the driver; the disk and ATAPI
 *			 drivers can be compiled as loadable modules.
 *			move IDE probe code to ide-probe.c
 *			move IDE disk code to ide-disk.c
 *			add support for generic IDE device subdrivers
 *			add m68k code from Geert Uytterhoeven
 *			probe all interfaces by default
 *			add ioctl to (re)probe an interface
 * Version 6.00		use per device request queues
 *			attempt to optimize shared hwgroup performance
 *			add ioctl to manually adjust bandwidth algorithms
 *			add kerneld support for the probe module
 *			fix bug in ide_error()
 *			fix bug in the first ide_get_lock() call for Atari
 *			don't flush leftover data for ATAPI devices
 * Version 6.01		clear hwgroup->active while the hwgroup sleeps
 *			support HDIO_GETGEO for floppies
 * Version 6.02		fix ide_ack_intr() call
 *			check partition table on floppies
 * Version 6.03		handle bad status bit sequencing in ide_wait_stat()
 * Version 6.10		deleted old entries from this list of updates
 *			replaced triton.c with ide-dma.c generic PCI DMA
 *			added support for BIOS-enabled UltraDMA
 *			rename all "promise" things to "pdc4030"
 *			fix EZ-DRIVE handling on small disks
 * Version 6.11		fix probe error in ide_scan_devices()
 *			fix ancient "jiffies" polling bugs
 *			mask all hwgroup interrupts on each irq entry
 * Version 6.12		integrate ioctl and proc interfaces
 *			fix parsing of "idex=" command line parameter
 * Version 6.13		add support for ide4/ide5 courtesy rjones@orchestream.com
 * Version 6.14		fixed IRQ sharing among PCI devices
 * Version 6.15		added SMP awareness to IDE drivers
 * Version 6.16		fixed various bugs; even more SMP friendly
 * Version 6.17		fix for newest EZ-Drive problem
 * Version 6.18		default unpartitioned-disk translation now "BIOS LBA"
 * Version 6.19		Re-design for a UNIFORM driver for all platforms,
 *			  model based on suggestions from Russell King and
 *			  Geert Uytterhoeven
 *			Promise DC4030VL now supported.
 *			add support for ide6/ide7
 *			delay_50ms() changed to ide_delay_50ms() and exported.
 * Version 6.20		Added/Fixed Generic ATA-66 support and hwif detection.
 *			Added hdx=flash to allow for second flash disk
 *			  detection w/o the hang loop.
 *			Added support for ide8/ide9
 *			Added idex=ata66 for the quirky chipsets that are
 *			  ATA-66 compliant, but have yet to determine a method
 *			  of verification of the 80c cable presence.
 *			  Specifically Promise's PDC20262 chipset.
 * Version 6.21		Fixing/Fixed SMP spinlock issue with insight from an old
 *			  hat that clarified original low level driver design.
 * Version 6.30		Added SMP support; fixed multmode issues.  -ml
 * Version 6.31		Debug Share INTR's and request queue streaming
 *			Native ATA-100 support
 *			Prep for Cascades Project
 * Version 6.32		4GB highmem support for DMA, and mapping of those for
 *			PIO transfer (Jens Axboe)
 *
 *  Some additional driver compile-time options are in ./include/linux/ide.h
 */

#define	VERSION	"7.0.0"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#ifndef MODULE
# include <linux/init.h>
#endif
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/cdrom.h>
#include <linux/device.h>
#include <linux/kmod.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#include "ata-timing.h"

/*
 * Those will be moved into separate header files eventually.
 */
#ifdef CONFIG_BLK_DEV_RZ1000
extern void ide_probe_for_rz100x(void);
#endif
#ifdef CONFIG_ETRAX_IDE
extern void init_e100_ide(void);
#endif
#ifdef CONFIG_BLK_DEV_CMD640
extern void ide_probe_for_cmd640x(void);
#endif
#ifdef CONFIG_BLK_DEV_PDC4030
extern int ide_probe_for_pdc4030(void);
#endif
#ifdef CONFIG_BLK_DEV_IDE_PMAC
extern void pmac_ide_probe(void);
#endif
#ifdef CONFIG_BLK_DEV_IDE_ICSIDE
extern void icside_init(void);
#endif
#ifdef CONFIG_BLK_DEV_IDE_RAPIDE
extern void rapide_init(void);
#endif
#ifdef CONFIG_BLK_DEV_GAYLE
extern void gayle_init(void);
#endif
#ifdef CONFIG_BLK_DEV_FALCON_IDE
extern void falconide_init(void);
#endif
#ifdef CONFIG_BLK_DEV_MAC_IDE
extern void macide_init(void);
#endif
#ifdef CONFIG_BLK_DEV_Q40IDE
extern void q40ide_init(void);
#endif
#ifdef CONFIG_BLK_DEV_BUDDHA
extern void buddha_init(void);
#endif
#if defined(CONFIG_BLK_DEV_ISAPNP) && defined(CONFIG_ISAPNP)
extern void pnpide_init(int);
#endif

/* default maximum number of failures */
#define IDE_DEFAULT_MAX_FAILURES	1

static int idebus_parameter;	/* holds the "idebus=" parameter */
int system_bus_speed;		/* holds what we think is VESA/PCI bus speed */
static int initializing;	/* set while initializing built-in drivers */

/*
 * Protects access to global structures etc.
 */
spinlock_t ide_lock __cacheline_aligned = SPIN_LOCK_UNLOCKED;

#ifdef CONFIG_BLK_DEV_IDEPCI
static int ide_scan_direction;	/* THIS was formerly 2.2.x pci=reverse */
#endif

#if defined(__mc68000__) || defined(CONFIG_APUS)
/*
 * This is used by the Atari code to obtain access to the IDE interrupt,
 * which is shared between several drivers.
 */
static int	ide_intr_lock;
#endif

int noautodma = 0;

/*
 * This is declared extern in ide.h, for access by other IDE modules:
 */
struct ata_channel ide_hwifs[MAX_HWIFS];	/* master data repository */

#if (DISK_RECOVERY_TIME > 0)
/*
 * For really screwed hardware (hey, at least it *can* be used with Linux)
 * we can enforce a minimum delay time between successive operations.
 */
static unsigned long read_timer (void)
{
	unsigned long t, flags;
	int i;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */
	t = jiffies * 11932;
	outb_p(0, 0x43);
	i = inb_p(0x40);
	i |= inb(0x40) << 8;
	__restore_flags(flags);	/* local CPU only */
	return (t - i);
}
#endif

static inline void set_recovery_timer(struct ata_channel *channel)
{
#if (DISK_RECOVERY_TIME > 0)
	channel->last_time = read_timer();
#endif
}

/*
 * Do not even *think* about calling this!
 */
static void init_hwif_data(struct ata_channel *hwif, unsigned int index)
{
	static const byte ide_major[] = {
		IDE0_MAJOR, IDE1_MAJOR, IDE2_MAJOR, IDE3_MAJOR, IDE4_MAJOR,
		IDE5_MAJOR, IDE6_MAJOR, IDE7_MAJOR, IDE8_MAJOR, IDE9_MAJOR
	};

	unsigned int unit;
	hw_regs_t hw;

	/* bulk initialize hwif & drive info with zeros */
	memset(hwif, 0, sizeof(struct ata_channel));
	memset(&hw, 0, sizeof(hw_regs_t));

	/* fill in any non-zero initial values */
	hwif->index     = index;
	ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, &hwif->irq);
	memcpy(&hwif->hw, &hw, sizeof(hw));
	memcpy(hwif->io_ports, hw.io_ports, sizeof(hw.io_ports));
	hwif->noprobe	= !hwif->io_ports[IDE_DATA_OFFSET];
#ifdef CONFIG_BLK_DEV_HD
	if (hwif->io_ports[IDE_DATA_OFFSET] == HD_DATA)
		hwif->noprobe = 1; /* may be overridden by ide_setup() */
#endif /* CONFIG_BLK_DEV_HD */
	hwif->major = ide_major[index];
	sprintf(hwif->name, "ide%d", index);
	hwif->bus_state = BUSSTATE_ON;
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		drive->type			= ATA_DISK;
		drive->select.all		= (unit<<4)|0xa0;
		drive->channel			= hwif;
		drive->ctl			= 0x08;
		drive->ready_stat		= READY_STAT;
		drive->bad_wstat		= BAD_W_STAT;
		drive->special.b.recalibrate	= 1;
		drive->special.b.set_geometry	= 1;
		sprintf(drive->name, "hd%c", 'a' + (index * MAX_DRIVES) + unit);
		drive->max_failures		= IDE_DEFAULT_MAX_FAILURES;
		init_waitqueue_head(&drive->wqueue);
	}
}

/*
 * init_ide_data() sets reasonable default values into all fields
 * of all instances of the hwifs and drives, but only on the first call.
 * Subsequent calls have no effect (they don't wipe out anything).
 *
 * This routine is normally called at driver initialization time,
 * but may also be called MUCH earlier during kernel "command-line"
 * parameter processing.  As such, we cannot depend on any other parts
 * of the kernel (such as memory allocation) to be functioning yet.
 *
 * This is too bad, as otherwise we could dynamically allocate the
 * ide_drive_t structs as needed, rather than always consuming memory
 * for the max possible number (MAX_HWIFS * MAX_DRIVES) of them.
 */
#define MAGIC_COOKIE 0x12345678
static void __init init_ide_data (void)
{
	unsigned int h;
	static unsigned long magic_cookie = MAGIC_COOKIE;

	if (magic_cookie != MAGIC_COOKIE)
		return;		/* already initialized */
	magic_cookie = 0;

	/* Initialize all interface structures */
	for (h = 0; h < MAX_HWIFS; ++h)
		init_hwif_data(&ide_hwifs[h], h);

	/* Add default hw interfaces */
	ide_init_default_hwifs();

	idebus_parameter = 0;
}

/*
 * CompactFlash cards and their relatives pretend to be removable hard disks, except:
 *	(1) they never have a slave unit, and
 *	(2) they don't have a door lock mechanisms.
 * This test catches them, and is invoked elsewhere when setting appropriate config bits.
 *
 * FIXME FIXME: Yes this is for certain applicable for all of them as time has shown.
 *
 * FIXME: This treatment is probably applicable for *all* PCMCIA (PC CARD) devices,
 * so in linux 2.3.x we should change this to just treat all PCMCIA drives this way,
 * and get rid of the model-name tests below (too big of an interface change for 2.2.x).
 * At that time, we might also consider parameterizing the timeouts and retries,
 * since these are MUCH faster than mechanical drives.	-M.Lord
 */
int drive_is_flashcard (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	if (drive->removable && id != NULL) {
		if (id->config == 0x848a)
			return 1;	/* CompactFlash */
		if (!strncmp(id->model, "KODAK ATA_FLASH", 15)	/* Kodak */
		 || !strncmp(id->model, "Hitachi CV", 10)	/* Hitachi */
		 || !strncmp(id->model, "SunDisk SDCFB", 13)	/* SunDisk */
		 || !strncmp(id->model, "HAGIWARA HPC", 12)	/* Hagiwara */
		 || !strncmp(id->model, "LEXAR ATA_FLASH", 15)	/* Lexar */
		 || !strncmp(id->model, "ATA_FLASH", 9))	/* Simple Tech */
		{
			return 1;	/* yes, it is a flash memory card */
		}
	}
	return 0;	/* no, it is not a flash memory card */
}

void ide_end_queued_request(ide_drive_t *drive, int uptodate, struct request *rq)
{
	unsigned long flags;

	BUG_ON(!(rq->flags & REQ_STARTED));
	BUG_ON(!rq->special);

	if (!end_that_request_first(rq, uptodate, rq->hard_nr_sectors)) {
		struct ata_request *ar = rq->special;

		add_blkdev_randomness(major(rq->rq_dev));

		spin_lock_irqsave(&ide_lock, flags);

		if ((jiffies - ar->ar_time > ATA_AR_MAX_TURNAROUND) && drive->queue_depth > 1) {
			printk(KERN_INFO "%s: exceeded max command turn-around time (%d seconds)\n", drive->name, ATA_AR_MAX_TURNAROUND / HZ);
			drive->queue_depth >>= 1;
		}

		if (jiffies - ar->ar_time > drive->tcq->oldest_command)
			drive->tcq->oldest_command = jiffies - ar->ar_time;

		ata_ar_put(drive, ar);
		end_that_request_last(rq);
		/*
		 * IDE_SET_CUR_TAG(drive, IDE_INACTIVE_TAG) will do this
		 * too, but it really belongs here. assumes that the
		 * ended request is the active one.
		 */
		HWGROUP(drive)->rq = NULL;
		spin_unlock_irqrestore(&ide_lock, flags);
	}
}

int __ide_end_request(ide_drive_t *drive, int uptodate, int nr_secs)
{
	struct request *rq;
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&ide_lock, flags);
	rq = HWGROUP(drive)->rq;

	BUG_ON(!(rq->flags & REQ_STARTED));

	/*
	 * small hack to eliminate locking from ide_end_request to grab
	 * the first segment number of sectors
	 */
	if (!nr_secs)
		nr_secs = rq->hard_cur_sectors;

	/*
	 * decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO
	 */
	if (drive->state == DMA_PIO_RETRY && drive->retry_pio <= 3) {
		drive->state = 0;
		HWGROUP(drive)->hwif->dmaproc(ide_dma_on, drive);
	}

	if (!end_that_request_first(rq, uptodate, nr_secs)) {
		struct ata_request *ar = rq->special;

		add_blkdev_randomness(major(rq->rq_dev));
		/*
		 * request with ATA_AR_QUEUED set have already been
		 * dequeued, but doing it twice is ok
		 */
		blkdev_dequeue_request(rq);
		HWGROUP(drive)->rq = NULL;
		if (ar)
			ata_ar_put(drive, ar);
		end_that_request_last(rq);
		ret = 0;
	}

	spin_unlock_irqrestore(&ide_lock, flags);
	return ret;
}

/*
 * This should get invoked any time we exit the driver to
 * wait for an interrupt response from a drive.  handler() points
 * at the appropriate code to handle the next interrupt, and a
 * timer is started to prevent us from waiting forever in case
 * something goes wrong (see the ide_timer_expiry() handler later on).
 */
void ide_set_handler (ide_drive_t *drive, ide_handler_t *handler,
		      unsigned int timeout, ide_expiry_t *expiry)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);

	spin_lock_irqsave(&ide_lock, flags);
	if (hwgroup->handler != NULL) {
		printk("%s: ide_set_handler: handler not null; old=%p, new=%p, from %p\n",
			drive->name, hwgroup->handler, handler, __builtin_return_address(0));
	}
	hwgroup->handler	= handler;
	hwgroup->expiry		= expiry;
	hwgroup->timer.expires	= jiffies + timeout;
	add_timer(&hwgroup->timer);
	spin_unlock_irqrestore(&ide_lock, flags);
}

static void ata_pre_reset(ide_drive_t *drive)
{
	if (ata_ops(drive) && ata_ops(drive)->pre_reset)
		ata_ops(drive)->pre_reset(drive);

	if (!drive->keep_settings && !drive->using_dma) {
		drive->unmask = 0;
		drive->io_32bit = 0;
	}

	if (drive->using_dma) {
		/* check the DMA crc count */
		if (drive->crc_count) {
			drive->channel->dmaproc(ide_dma_off_quietly, drive);
			if ((drive->channel->speedproc) != NULL)
				drive->channel->speedproc(drive, ide_auto_reduce_xfer(drive));
			if (drive->current_speed >= XFER_SW_DMA_0)
				drive->channel->dmaproc(ide_dma_on, drive);
		} else
			drive->channel->dmaproc(ide_dma_off, drive);
	}
}

/*
 * The capacity of a drive according to its current geometry/LBA settings in
 * sectors.
 */
unsigned long ata_capacity(ide_drive_t *drive)
{
	if (!drive->present || !drive->driver)
		return 0;

	if (ata_ops(drive) && ata_ops(drive)->capacity)
		return ata_ops(drive)->capacity(drive);

	/* This used to be 0x7fffffff, but since now we use the maximal drive
	 * capacity value used by other kernel subsystems as well.
	 */

	return ~0UL;
}

/*
 * This is used to issue WIN_SPECIFY, WIN_RESTORE, and WIN_SETMULT commands to
 * a drive.  It used to do much more, but has been scaled back.
 */
static ide_startstop_t ata_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

#ifdef DEBUG
	printk("%s: ata_special: 0x%02x\n", drive->name, s->all);
#endif
	if (s->b.set_tune) {
		s->b.set_tune = 0;
		if (drive->channel->tuneproc != NULL)
			drive->channel->tuneproc(drive, drive->tune_req);
	} else if (drive->driver != NULL) {
		if (ata_ops(drive)->special)
			return ata_ops(drive)->special(drive);
		else {
			drive->special.all = 0;
			drive->mult_req = 0;

			return ide_stopped;
		}
	} else if (s->all) {
		printk("%s: bad special flag: 0x%02x\n", drive->name, s->all);
		s->all = 0;
	}

	return ide_stopped;
}

extern struct block_device_operations ide_fops[];

/*
 * This is called exactly *once* for each channel.
 */
void ide_geninit(struct ata_channel *hwif)
{
	unsigned int unit;
	struct gendisk *gd = hwif->gd;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		if (!drive->present)
			continue;
		if (drive->type != ATA_DISK && drive->type != ATA_FLOPPY)
			continue;
		register_disk(gd,mk_kdev(hwif->major,unit<<PARTN_BITS),
#ifdef CONFIG_BLK_DEV_ISAPNP
			(drive->forced_geom && drive->noprobe) ? 1 :
#endif
			1 << PARTN_BITS, ide_fops, ata_capacity(drive));
	}
}

static ide_startstop_t do_reset1(ide_drive_t *, int);		/* needed below */

/*
 * Poll the interface for completion every 50ms during an ATAPI drive reset
 * operation. If the drive has not yet responded, and we have not yet hit our
 * maximum waiting time, then the timer is restarted for another 50ms.
 */
static ide_startstop_t atapi_reset_pollfunc (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	byte stat;

	SELECT_DRIVE(drive->channel,drive);
	udelay (10);

	if (OK_STAT(stat=GET_STAT(), 0, BUSY_STAT)) {
		printk("%s: ATAPI reset complete\n", drive->name);
	} else {
		if (time_before(jiffies, hwgroup->poll_timeout)) {
			BUG_ON(HWGROUP(drive)->handler);
			ide_set_handler (drive, &atapi_reset_pollfunc, HZ/20, NULL);
			return ide_started;	/* continue polling */
		}
		hwgroup->poll_timeout = 0;	/* end of polling */
		printk("%s: ATAPI reset timed-out, status=0x%02x\n", drive->name, stat);
		return do_reset1 (drive, 1);	/* do it the old fashioned way */
	}
	hwgroup->poll_timeout = 0;	/* done polling */
	return ide_stopped;
}

/*
 * Poll the interface for completion every 50ms during an ata reset operation.
 * If the drives have not yet responded, and we have not yet hit our maximum
 * waiting time, then the timer is restarted for another 50ms.
 */
static ide_startstop_t reset_pollfunc (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	struct ata_channel *hwif = drive->channel;
	byte tmp;

	if (!OK_STAT(tmp=GET_STAT(), 0, BUSY_STAT)) {
		if (time_before(jiffies, hwgroup->poll_timeout)) {
			BUG_ON(HWGROUP(drive)->handler);
			ide_set_handler (drive, &reset_pollfunc, HZ/20, NULL);
			return ide_started;	/* continue polling */
		}
		printk("%s: reset timed-out, status=0x%02x\n", hwif->name, tmp);
		drive->failures++;
	} else  {
		printk("%s: reset: ", hwif->name);
		if ((tmp = GET_ERR()) == 1) {
			printk("success\n");
			drive->failures = 0;
		} else {
			drive->failures++;
#if FANCY_STATUS_DUMPS
			printk("master: ");
			switch (tmp & 0x7f) {
				case 1: printk("passed");
					break;
				case 2: printk("formatter device error");
					break;
				case 3: printk("sector buffer error");
					break;
				case 4: printk("ECC circuitry error");
					break;
				case 5: printk("controlling MPU error");
					break;
				default:printk("error (0x%02x?)", tmp);
			}
			if (tmp & 0x80)
				printk("; slave: failed");
			printk("\n");
#else
			printk("failed\n");
#endif
		}
	}
	hwgroup->poll_timeout = 0;	/* done polling */
	return ide_stopped;
}

/*
 * do_reset1() attempts to recover a confused drive by resetting it.
 * Unfortunately, resetting a disk drive actually resets all devices on
 * the same interface, so it can really be thought of as resetting the
 * interface rather than resetting the drive.
 *
 * ATAPI devices have their own reset mechanism which allows them to be
 * individually reset without clobbering other devices on the same interface.
 *
 * Unfortunately, the IDE interface does not generate an interrupt to let
 * us know when the reset operation has finished, so we must poll for this.
 * Equally poor, though, is the fact that this may a very long time to complete,
 * (up to 30 seconds worst case).  So, instead of busy-waiting here for it,
 * we set a timer to poll at 50ms intervals.
 */
static ide_startstop_t do_reset1 (ide_drive_t *drive, int do_not_try_atapi)
{
	unsigned int unit;
	unsigned long flags;
	struct ata_channel *hwif = drive->channel;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	/* For an ATAPI device, first try an ATAPI SRST. */
	if (drive->type != ATA_DISK && !do_not_try_atapi) {
		ata_pre_reset(drive);
		SELECT_DRIVE(hwif,drive);
		udelay (20);
		OUT_BYTE (WIN_SRST, IDE_COMMAND_REG);
		hwgroup->poll_timeout = jiffies + WAIT_WORSTCASE;
		BUG_ON(HWGROUP(drive)->handler);
		ide_set_handler (drive, &atapi_reset_pollfunc, HZ/20, NULL);
		__restore_flags (flags);	/* local CPU only */
		return ide_started;
	}

	/*
	 * First, reset any device state data we were maintaining
	 * for any of the drives on this interface.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit)
		ata_pre_reset(&hwif->drives[unit]);

#if OK_TO_RESET_CONTROLLER
	if (!IDE_CONTROL_REG) {
		__restore_flags(flags);
		return ide_stopped;
	}
	/*
	 * Note that we also set nIEN while resetting the device,
	 * to mask unwanted interrupts from the interface during the reset.
	 * However, due to the design of PC hardware, this will cause an
	 * immediate interrupt due to the edge transition it produces.
	 * This single interrupt gives us a "fast poll" for drives that
	 * recover from reset very quickly, saving us the first 50ms wait time.
	 */
	OUT_BYTE(drive->ctl|6,IDE_CONTROL_REG);	/* set SRST and nIEN */
	udelay(10);			/* more than enough time */
	if (drive->quirk_list == 2) {
		OUT_BYTE(drive->ctl,IDE_CONTROL_REG);	/* clear SRST and nIEN */
	} else {
		OUT_BYTE(drive->ctl|2,IDE_CONTROL_REG);	/* clear SRST, leave nIEN */
	}
	udelay(10);			/* more than enough time */
	hwgroup->poll_timeout = jiffies + WAIT_WORSTCASE;
	BUG_ON(HWGROUP(drive)->handler);
	ide_set_handler(drive, &reset_pollfunc, HZ/20, NULL);

	/*
	 * Some weird controller like resetting themselves to a strange
	 * state when the disks are reset this way. At least, the Winbond
	 * 553 documentation says that
	 */
	if (hwif->resetproc != NULL)
		hwif->resetproc(drive);

#endif

	__restore_flags (flags);	/* local CPU only */
	return ide_started;
}

static inline u32 read_24 (ide_drive_t *drive)
{
	return  (IN_BYTE(IDE_HCYL_REG)<<16) |
		(IN_BYTE(IDE_LCYL_REG)<<8) |
		 IN_BYTE(IDE_SECTOR_REG);
}

/*
 * Clean up after success/failure of an explicit drive cmd
 */
void ide_end_drive_cmd(ide_drive_t *drive, byte stat, byte err)
{
	unsigned long flags;
	struct request *rq;

	spin_lock_irqsave(&ide_lock, flags);
	rq = HWGROUP(drive)->rq;

	if (rq->flags & REQ_DRIVE_CMD) {
		byte *args = (byte *) rq->buffer;
		rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);
		if (args) {
			args[0] = stat;
			args[1] = err;
			args[2] = IN_BYTE(IDE_NSECTOR_REG);
		}
	} else if (rq->flags & REQ_DRIVE_TASK) {
		byte *args = (byte *) rq->buffer;
		rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);
		if (args) {
			args[0] = stat;
			args[1] = err;
			args[2] = IN_BYTE(IDE_NSECTOR_REG);
			args[3] = IN_BYTE(IDE_SECTOR_REG);
			args[4] = IN_BYTE(IDE_LCYL_REG);
			args[5] = IN_BYTE(IDE_HCYL_REG);
			args[6] = IN_BYTE(IDE_SELECT_REG);
		}
	} else if (rq->flags & REQ_DRIVE_TASKFILE) {
		struct ata_request *ar = rq->special;
		struct ata_taskfile *args = &ar->ar_task;

		rq->errors = !OK_STAT(stat, READY_STAT, BAD_STAT);

		if (args) {
			args->taskfile.feature = err;
			args->taskfile.sector_count = IN_BYTE(IDE_NSECTOR_REG);
			args->taskfile.sector_number = IN_BYTE(IDE_SECTOR_REG);
			args->taskfile.low_cylinder = IN_BYTE(IDE_LCYL_REG);
			args->taskfile.high_cylinder = IN_BYTE(IDE_HCYL_REG);
			args->taskfile.device_head = IN_BYTE(IDE_SELECT_REG);
			args->taskfile.command = stat;
			if ((drive->id->command_set_2 & 0x0400) &&
			    (drive->id->cfs_enable_2 & 0x0400) &&
			    (drive->addressing == 1)) {
				/* The following command goes to the hob file! */

				OUT_BYTE(drive->ctl|0x80, IDE_CONTROL_REG);
				args->hobfile.feature = IN_BYTE(IDE_FEATURE_REG);
				args->hobfile.sector_count = IN_BYTE(IDE_NSECTOR_REG);

				args->hobfile.sector_number = IN_BYTE(IDE_SECTOR_REG);
				args->hobfile.low_cylinder = IN_BYTE(IDE_LCYL_REG);
				args->hobfile.high_cylinder = IN_BYTE(IDE_HCYL_REG);
			}
		}
		if (ar->ar_flags & ATA_AR_RETURN)
			ata_ar_put(drive, ar);
	}

	blkdev_dequeue_request(rq);
	HWGROUP(drive)->rq = NULL;
	end_that_request_last(rq);

	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
byte ide_dump_status (ide_drive_t *drive, const char *msg, byte stat)
{
	unsigned long flags;
	byte err = 0;

	__save_flags (flags);	/* local CPU only */
	ide__sti();		/* local CPU only */
	printk("%s: %s: status=0x%02x", drive->name, msg, stat);
#if FANCY_STATUS_DUMPS
	printk(" { ");
	if (stat & BUSY_STAT)
		printk("Busy ");
	else {
		if (stat & READY_STAT)	printk("DriveReady ");
		if (stat & WRERR_STAT)	printk("DeviceFault ");
		if (stat & SEEK_STAT)	printk("SeekComplete ");
		if (stat & DRQ_STAT)	printk("DataRequest ");
		if (stat & ECC_STAT)	printk("CorrectedError ");
		if (stat & INDEX_STAT)	printk("Index ");
		if (stat & ERR_STAT)	printk("Error ");
	}
	printk("}");
#endif	/* FANCY_STATUS_DUMPS */
	printk("\n");
	if ((stat & (BUSY_STAT|ERR_STAT)) == ERR_STAT) {
		err = GET_ERR();
		printk("%s: %s: error=0x%02x", drive->name, msg, err);
#if FANCY_STATUS_DUMPS
		if (drive->type == ATA_DISK) {
			printk(" { ");
			if (err & ABRT_ERR)	printk("DriveStatusError ");
			if (err & ICRC_ERR)	printk("%s", (err & ABRT_ERR) ? "BadCRC " : "BadSector ");
			if (err & ECC_ERR)	printk("UncorrectableError ");
			if (err & ID_ERR)	printk("SectorIdNotFound ");
			if (err & TRK0_ERR)	printk("TrackZeroNotFound ");
			if (err & MARK_ERR)	printk("AddrMarkNotFound ");
			printk("}");
			if ((err & (BBD_ERR | ABRT_ERR)) == BBD_ERR || (err & (ECC_ERR|ID_ERR|MARK_ERR))) {
				if ((drive->id->command_set_2 & 0x0400) &&
				    (drive->id->cfs_enable_2 & 0x0400) &&
				    (drive->addressing == 1)) {
					__u64 sectors = 0;
					u32 low = 0, high = 0;
					low = read_24(drive);
					OUT_BYTE(drive->ctl|0x80, IDE_CONTROL_REG);
					high = read_24(drive);

					sectors = ((__u64)high << 24) | low;
					printk(", LBAsect=%lld, high=%d, low=%d", (long long) sectors, high, low);
				} else {
					byte cur = IN_BYTE(IDE_SELECT_REG);
					if (cur & 0x40) {	/* using LBA? */
						printk(", LBAsect=%ld", (unsigned long)
						 ((cur&0xf)<<24)
						 |(IN_BYTE(IDE_HCYL_REG)<<16)
						 |(IN_BYTE(IDE_LCYL_REG)<<8)
						 | IN_BYTE(IDE_SECTOR_REG));
					} else {
						printk(", CHS=%d/%d/%d",
						 (IN_BYTE(IDE_HCYL_REG)<<8) +
						  IN_BYTE(IDE_LCYL_REG),
						  cur & 0xf,
						  IN_BYTE(IDE_SECTOR_REG));
					}
				}
				if (HWGROUP(drive) && HWGROUP(drive)->rq)
					printk(", sector=%ld", HWGROUP(drive)->rq->sector);
			}
		}
#endif	/* FANCY_STATUS_DUMPS */
		printk("\n");
	}
	__restore_flags (flags);	/* local CPU only */
	return err;
}

/*
 * This gets invoked in response to a drive unexpectedly having its DRQ_STAT
 * bit set.  As an alternative to resetting the drive, it tries to clear the
 * condition by reading a sector's worth of data from the drive.  Of course,
 * this may not help if the drive is *waiting* for data from *us*.
 */
static void try_to_flush_leftover_data (ide_drive_t *drive)
{
	int i = (drive->mult_count ? drive->mult_count : 1);

	if (drive->type != ATA_DISK)
		return;

	while (i > 0) {
		u32 buffer[SECTOR_WORDS];
		unsigned int count = (i > 1) ? 1 : i;

		ata_read(drive, buffer, count * SECTOR_WORDS);
		i -= count;
	}
}

/*
 * Take action based on the error returned by the drive.
 */
ide_startstop_t ide_error(ide_drive_t *drive, const char *msg, byte stat)
{
	struct request *rq;
	byte err;

	/*
	 * FIXME: remember to invalidate tcq queue when drive->using_tcq
	 * and atomic_read(&drive->tcq->queued) /jens
	 */

	err = ide_dump_status(drive, msg, stat);
	if (drive == NULL || (rq = HWGROUP(drive)->rq) == NULL)
		return ide_stopped;
	/* retry only "normal" I/O: */
	if (!(rq->flags & REQ_CMD)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, stat, err);
		return ide_stopped;
	}

	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr)) { /* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else {
		if (drive->type == ATA_DISK && (stat & ERR_STAT)) {
			/* err has different meaning on cdrom and tape */
			if (err == ABRT_ERR) {
				if (drive->select.b.lba && IN_BYTE(IDE_COMMAND_REG) == WIN_SPECIFY)
					return ide_stopped; /* some newer drives don't support WIN_SPECIFY */
			} else if ((err & (ABRT_ERR | ICRC_ERR)) == (ABRT_ERR | ICRC_ERR)) {
				drive->crc_count++; /* UDMA crc error -- just retry the operation */
			} else if (err & (BBD_ERR | ECC_ERR))	/* retries won't help these */
				rq->errors = ERROR_MAX;
			else if (err & TRK0_ERR)	/* help it find track zero */
				rq->errors |= ERROR_RECAL;
		}
		/* pre bio (rq->cmd != WRITE) */
		if ((stat & DRQ_STAT) && rq_data_dir(rq) == READ)
			try_to_flush_leftover_data(drive);
	}
	if (GET_STAT() & (BUSY_STAT|DRQ_STAT))
		OUT_BYTE(WIN_IDLEIMMEDIATE,IDE_COMMAND_REG);	/* force an abort */

	if (rq->errors >= ERROR_MAX) {
		/* ATA-PATTERN */
		if (ata_ops(drive) && ata_ops(drive)->end_request)
			ata_ops(drive)->end_request(drive, 0);
		else
			ide_end_request(drive, 0);
	} else {
		if ((rq->errors & ERROR_RESET) == ERROR_RESET) {
			++rq->errors;
			return do_reset1(drive, 0);
		}
		if ((rq->errors & ERROR_RECAL) == ERROR_RECAL)
			drive->special.b.recalibrate = 1;
		++rq->errors;
	}
	return ide_stopped;
}

/*
 * Issue a simple drive command.  The drive must be selected beforehand.
 */
void ide_cmd(ide_drive_t *drive, byte cmd, byte nsect, ide_handler_t *handler)
{
	BUG_ON(HWGROUP(drive)->handler);
	ide_set_handler (drive, handler, WAIT_CMD, NULL);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl,IDE_CONTROL_REG);	/* clear nIEN */
	SELECT_MASK(drive->channel, drive, 0);
	OUT_BYTE(nsect,IDE_NSECTOR_REG);
	OUT_BYTE(cmd,IDE_COMMAND_REG);
}

/*
 * Invoked on completion of a special DRIVE_CMD.
 */
static ide_startstop_t drive_cmd_intr (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	u8 *args = rq->buffer;
	u8 stat = GET_STAT();
	int retries = 10;

	ide__sti();	/* local CPU only */
	if ((stat & DRQ_STAT) && args && args[3]) {
		int io_32bit = drive->io_32bit;

		drive->io_32bit = 0;
		ata_read(drive, &args[4], args[3] * SECTOR_WORDS);
		drive->io_32bit = io_32bit;

		while (((stat = GET_STAT()) & BUSY_STAT) && retries--)
			udelay(100);
	}

	if (!OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_error(drive, "drive_cmd", stat); /* calls ide_end_drive_cmd */
	ide_end_drive_cmd (drive, stat, GET_ERR());

	return ide_stopped;
}

/*
 * Busy-wait for the drive status to be not "busy".  Check then the status for
 * all of the "good" bits and none of the "bad" bits, and if all is okay it
 * returns 0.  All other cases return 1 after invoking ide_error() -- caller
 * should just return.
 *
 * This routine should get fixed to not hog the cpu during extra long waits..
 * That could be done by busy-waiting for the first jiffy or two, and then
 * setting a timer to wake up at half second intervals thereafter, until
 * timeout is achieved, before timing out.
 */
int ide_wait_stat(ide_startstop_t *startstop, ide_drive_t *drive, byte good, byte bad, unsigned long timeout) {
	byte stat;
	int i;
	unsigned long flags;

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		*startstop = ide_stopped;
		return 1;
	}

	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */
	if ((stat = GET_STAT()) & BUSY_STAT) {
		__save_flags(flags);	/* local CPU only */
		ide__sti();		/* local CPU only */
		timeout += jiffies;
		while ((stat = GET_STAT()) & BUSY_STAT) {
			if (0 < (signed long)(jiffies - timeout)) {
				__restore_flags(flags);	/* local CPU only */
				*startstop = ide_error(drive, "status timeout", stat);
				return 1;
			}
		}
		__restore_flags(flags);	/* local CPU only */
	}
	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 * This fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		if (OK_STAT((stat = GET_STAT()), good, bad))
			return 0;
	}
	*startstop = ide_error(drive, "status error", stat);
	return 1;
}

/*
 * This initiates handling of a new I/O request.
 */
static ide_startstop_t start_request(ide_drive_t *drive, struct request *rq)
{
	ide_startstop_t startstop;
	unsigned long block;
	unsigned int minor = minor(rq->rq_dev), unit = minor >> PARTN_BITS;
	struct ata_channel *hwif = drive->channel;

	BUG_ON(!(rq->flags & REQ_STARTED));

#ifdef DEBUG
	printk("%s: start_request: current=0x%08lx\n", hwif->name, (unsigned long) rq);
#endif

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		goto kill_rq;
	}

	if (unit >= MAX_DRIVES) {
		printk("%s: bad device number: %s\n", hwif->name, kdevname(rq->rq_dev));
		goto kill_rq;
	}
	block    = rq->sector;

	/* Strange disk manager remap */
	if ((rq->flags & REQ_CMD) &&
	    (drive->type == ATA_DISK || drive->type == ATA_FLOPPY)) {
		block += drive->sect0;
	}
	/* Yecch - this will shift the entire interval,
	   possibly killing some innocent following sector */
	if (block == 0 && drive->remap_0_to_1 == 1)
		block = 1;  /* redirect MBR access to EZ-Drive partn table */

#if (DISK_RECOVERY_TIME > 0)
	while ((read_timer() - hwif->last_time) < DISK_RECOVERY_TIME);
#endif

	SELECT_DRIVE(hwif, drive);
	if (ide_wait_stat(&startstop, drive, drive->ready_stat,
			  BUSY_STAT|DRQ_STAT, WAIT_READY)) {
		printk(KERN_WARNING "%s: drive not ready for command\n", drive->name);
		return startstop;
	}

	/* FIXME: We can see nicely here that all commands should be submitted
	 * through the request queue and that the special field in drive should
	 * go as soon as possible!
	 */

	if (!drive->special.all) {
		if (rq->flags & (REQ_DRIVE_CMD | REQ_DRIVE_TASK | REQ_DRIVE_TASKFILE)) {
			/* This issues a special drive command, usually
			 * initiated by ioctl() from the external hdparm
			 * program.
			 */

			if (rq->flags & REQ_DRIVE_TASKFILE) {
				struct ata_request *ar = rq->special;
				struct ata_taskfile *args;

				if (!ar)
					goto args_error;

				args = &ar->ar_task;

				ata_taskfile(drive, args, NULL);

				if (((args->command_type == IDE_DRIVE_TASK_RAW_WRITE) ||
					(args->command_type == IDE_DRIVE_TASK_OUT)) &&
						args->prehandler && args->handler)
					return args->prehandler(drive, rq);
				return ide_started;

			} else if (rq->flags & REQ_DRIVE_TASK) {
				byte *args = rq->buffer;
				byte sel;

				if (!(args)) goto args_error;
#ifdef DEBUG
				printk("%s: DRIVE_TASK_CMD ", drive->name);
				printk("cmd=0x%02x ", args[0]);
				printk("fr=0x%02x ", args[1]);
				printk("ns=0x%02x ", args[2]);
				printk("sc=0x%02x ", args[3]);
				printk("lcyl=0x%02x ", args[4]);
				printk("hcyl=0x%02x ", args[5]);
				printk("sel=0x%02x\n", args[6]);
#endif
				OUT_BYTE(args[1], IDE_FEATURE_REG);
				OUT_BYTE(args[3], IDE_SECTOR_REG);
				OUT_BYTE(args[4], IDE_LCYL_REG);
				OUT_BYTE(args[5], IDE_HCYL_REG);
				sel = (args[6] & ~0x10);
				if (drive->select.b.unit)
					sel |= 0x10;
				OUT_BYTE(sel, IDE_SELECT_REG);
				ide_cmd(drive, args[0], args[2], &drive_cmd_intr);
				return ide_started;
			} else if (rq->flags & REQ_DRIVE_CMD) {
				byte *args = rq->buffer;
				if (!(args)) goto args_error;
#ifdef DEBUG
				printk("%s: DRIVE_CMD ", drive->name);
				printk("cmd=0x%02x ", args[0]);
				printk("sc=0x%02x ", args[1]);
				printk("fr=0x%02x ", args[2]);
				printk("xx=0x%02x\n", args[3]);
#endif
				if (args[0] == WIN_SMART) {
					OUT_BYTE(0x4f, IDE_LCYL_REG);
					OUT_BYTE(0xc2, IDE_HCYL_REG);
					OUT_BYTE(args[2],IDE_FEATURE_REG);
					OUT_BYTE(args[1],IDE_SECTOR_REG);
					ide_cmd(drive, args[0], args[3], &drive_cmd_intr);

					return ide_started;
				}
				OUT_BYTE(args[2],IDE_FEATURE_REG);
				ide_cmd(drive, args[0], args[1], &drive_cmd_intr);
				return ide_started;
			}

args_error:
			/*
			 * NULL is actually a valid way of waiting for all
			 * current requests to be flushed from the queue.
			 */
#ifdef DEBUG
			printk("%s: DRIVE_CMD (null)\n", drive->name);
#endif
			ide_end_drive_cmd(drive, GET_STAT(), GET_ERR());
			return ide_stopped;
		}

		/* The normal way of execution is to pass execute the request
		 * handler.
		 */

		if (ata_ops(drive)) {
			if (ata_ops(drive)->do_request)
				return ata_ops(drive)->do_request(drive, rq, block);
			else {
				ide_end_request(drive, 0);
				return ide_stopped;
			}
		}
		printk(KERN_WARNING "%s: device type %d not supported\n",
		       drive->name, drive->type);
		goto kill_rq;
	}
	return ata_special(drive);
kill_rq:
	if (ata_ops(drive) && ata_ops(drive)->end_request)
		ata_ops(drive)->end_request(drive, 0);
	else
		ide_end_request(drive, 0);
	return ide_stopped;
}

ide_startstop_t restart_request(ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned long flags;
	struct request *rq;

	spin_lock_irqsave(&ide_lock, flags);
	hwgroup->handler = NULL;
	del_timer(&hwgroup->timer);
	rq = hwgroup->rq;
	spin_unlock_irqrestore(&ide_lock, flags);

	return start_request(drive, rq);
}

/*
 * This is used by a drive to give excess bandwidth back to the hwgroup by
 * sleeping for timeout jiffies.
 */
void ide_stall_queue(ide_drive_t *drive, unsigned long timeout)
{
	if (timeout > WAIT_WORSTCASE)
		timeout = WAIT_WORSTCASE;
	drive->PADAM_sleep = timeout + jiffies;
}

/*
 * Select the next drive which will be serviced.
 */
static ide_drive_t *choose_drive(ide_hwgroup_t *hwgroup)
{
	ide_drive_t *tmp;
	ide_drive_t *drive = NULL;
	unsigned long sleep = 0;

	tmp = hwgroup->drive;
	do {
		if (!list_empty(&tmp->queue.queue_head)
		&& (!tmp->PADAM_sleep || time_after_eq(tmp->PADAM_sleep, jiffies))) {
			if (!drive
			 || (tmp->PADAM_sleep && (!drive->PADAM_sleep || time_after(drive->PADAM_sleep, tmp->PADAM_sleep)))
			 || (!drive->PADAM_sleep && time_after(drive->PADAM_service_start + 2 * drive->PADAM_service_time, tmp->PADAM_service_start + 2 * tmp->PADAM_service_time)))
			{
				if (!blk_queue_plugged(&tmp->queue))
					drive = tmp;
			}
		}
		tmp = tmp->next;
	} while (tmp != hwgroup->drive);

	if (drive)
		return drive;

	hwgroup->rq = NULL;
	drive = hwgroup->drive;
	do {
		if (drive->PADAM_sleep && (!sleep || time_after(sleep, drive->PADAM_sleep)))
			sleep = drive->PADAM_sleep;
	} while ((drive = drive->next) != hwgroup->drive);

	if (sleep) {
		/*
		 * Take a short snooze, and then wake up this hwgroup
		 * again.  This gives other hwgroups on the same a
		 * chance to play fairly with us, just in case there
		 * are big differences in relative throughputs.. don't
		 * want to hog the cpu too much.
		 */
		if (0 < (signed long)(jiffies + WAIT_MIN_SLEEP - sleep))
			sleep = jiffies + WAIT_MIN_SLEEP;

		if (timer_pending(&hwgroup->timer))
			printk("ide_set_handler: timer already active\n");

		set_bit(IDE_SLEEP, &hwgroup->flags);
		mod_timer(&hwgroup->timer, sleep);
		/* we purposely leave hwgroup busy while
		 * sleeping */
	} else {
		/* Ugly, but how can we sleep for the lock
		 * otherwise? perhaps from tq_disk? */
		ide_release_lock(&ide_intr_lock);/* for atari only */
		clear_bit(IDE_BUSY, &hwgroup->flags);
	}

	return NULL;
}

#ifdef CONFIG_BLK_DEV_IDE_TCQ
ide_startstop_t ide_check_service(ide_drive_t *drive);
#else
#define ide_check_service(drive)	(ide_stopped)
#endif

/*
 * feed commands to a drive until it barfs. used to be part of ide_do_request.
 * called with ide_lock/DRIVE_LOCK held and busy hwgroup
 */
static void ide_queue_commands(ide_drive_t *drive, int masked_irq)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	ide_startstop_t startstop = -1;
	struct request *rq;
	int do_service = 0;

	do {
		rq = NULL;

		if (!test_bit(IDE_BUSY, &hwgroup->flags))
			printk("%s: hwgroup not busy while queueing\n", drive->name);

		/*
		 * abort early if we can't queue another command. for non
		 * tcq, ide_can_queue is always 1 since we never get here
		 * unless the drive is idle.
		 */
		if (!ide_can_queue(drive)) {
			if (!ide_pending_commands(drive))
				clear_bit(IDE_BUSY, &hwgroup->flags);
			break;
		}

		drive->PADAM_sleep = 0;
		drive->PADAM_service_start = jiffies;

		if (test_bit(IDE_DMA, &hwgroup->flags)) {
			printk("ide_do_request: DMA in progress...\n");
			break;
		}

		/*
		 * there's a small window between where the queue could be
		 * replugged while we are in here when using tcq (in which
		 * case the queue is probably empty anyways...), so check
		 * and leave if appropriate. When not using tcq, this is
		 * still a severe BUG!
		 */
		if (blk_queue_plugged(&drive->queue)) {
			BUG_ON(!drive->using_tcq);
			break;
		}

		if (!(rq = elv_next_request(&drive->queue))) {
			if (!ide_pending_commands(drive))
				clear_bit(IDE_BUSY, &hwgroup->flags);
			hwgroup->rq = NULL;
			break;
		}

		/*
		 * if there are queued commands, we can't start a non-fs
		 * request (really, a non-queuable command) until the
		 * queue is empty
		 */
		if (!(rq->flags & REQ_CMD) && ide_pending_commands(drive))
			break;

		hwgroup->rq = rq;

service:
		/*
		 * Some systems have trouble with IDE IRQs arriving while
		 * the driver is still setting things up.  So, here we disable
		 * the IRQ used by this interface while the request is being
		 * started.  This may look bad at first, but pretty much the
		 * same thing happens anyway when any interrupt comes in, IDE
		 * or otherwise -- the kernel masks the IRQ while it is being
		 * handled.
		 */
		if (masked_irq && HWIF(drive)->irq != masked_irq)
			disable_irq_nosync(HWIF(drive)->irq);

		spin_unlock(&ide_lock);
		ide__sti();	/* allow other IRQs while we start this request */
		if (!do_service)
			startstop = start_request(drive, rq);
		else
			startstop = ide_check_service(drive);

		spin_lock_irq(&ide_lock);
		if (masked_irq && HWIF(drive)->irq != masked_irq)
			enable_irq(HWIF(drive)->irq);

		/*
		 * command started, we are busy
		 */
		if (startstop == ide_started)
			break;

		/*
		 * start_request() can return either ide_stopped (no command
		 * was started), ide_started (command started, don't queue
		 * more), or ide_released (command started, try and queue
		 * more).
		 */
#if 0
		if (startstop == ide_stopped)
			set_bit(IDE_BUSY, &hwgroup->flags);
#endif

	} while (1);

	if (startstop == ide_started)
		return;

	if ((do_service = drive->service_pending))
		goto service;
}

/*
 * Issue a new request to a drive from hwgroup
 * Caller must have already done spin_lock_irqsave(&ide_lock, ...)
 *
 * A hwgroup is a serialized group of IDE interfaces.  Usually there is
 * exactly one hwif (interface) per hwgroup, but buggy controllers (eg. CMD640)
 * may have both interfaces in a single hwgroup to "serialize" access.
 * Or possibly multiple ISA interfaces can share a common IRQ by being grouped
 * together into one hwgroup for serialized access.
 *
 * Note also that several hwgroups can end up sharing a single IRQ,
 * possibly along with many other devices.  This is especially common in
 * PCI-based systems with off-board IDE controller cards.
 *
 * The IDE driver uses the queue spinlock to protect access to the request
 * queues.
 *
 * The first thread into the driver for a particular hwgroup sets the
 * hwgroup->flags IDE_BUSY flag to indicate that this hwgroup is now active,
 * and then initiates processing of the top request from the request queue.
 *
 * Other threads attempting entry notice the busy setting, and will simply
 * queue their new requests and exit immediately.  Note that hwgroup->flags
 * remains busy even when the driver is merely awaiting the next interrupt.
 * Thus, the meaning is "this hwgroup is busy processing a request".
 *
 * When processing of a request completes, the completing thread or IRQ-handler
 * will start the next request from the queue.  If no more work remains,
 * the driver will clear the hwgroup->flags IDE_BUSY flag and exit.
 */
static void ide_do_request(ide_hwgroup_t *hwgroup, int masked_irq)
{
	ide_drive_t *drive;
	struct ata_channel *hwif;

	ide_get_lock(&ide_intr_lock, ide_intr, hwgroup);/* for atari only: POSSIBLY BROKEN HERE(?) */

	__cli();	/* necessary paranoia: ensure IRQs are masked on local CPU */

	while (!test_and_set_bit(IDE_BUSY, &hwgroup->flags)) {

		/*
		 * will clear IDE_BUSY, if appropriate
		 */
		if ((drive = choose_drive(hwgroup)) == NULL)
			break;

		hwif = drive->channel;
		if (hwgroup->hwif->sharing_irq && hwif != hwgroup->hwif && IDE_CONTROL_REG) {
			/* set nIEN for previous hwif */
			if (hwif->intrproc)
				hwif->intrproc(drive);
			else
				OUT_BYTE(drive->ctl|2, IDE_CONTROL_REG);
		}
		hwgroup->hwif = hwif;
		hwgroup->drive = drive;

		/*
		 * main queueing loop
		 */
		ide_queue_commands(drive, masked_irq);
	}
}

/*
 * Returns the queue which corresponds to a given device.
 */
request_queue_t *ide_get_queue(kdev_t dev)
{
	struct ata_channel *channel = (struct ata_channel *)blk_dev[major(dev)].data;

	/* FIXME: ALLERT: This discriminates between master and slave! */
	return &channel->drives[DEVICE_NR(dev) & 1].queue;
}

/*
 * Passes the stuff to ide_do_request
 */
void do_ide_request(request_queue_t *q)
{
	ide_do_request(q->queuedata, 0);
}

/*
 * un-busy the hwgroup etc, and clear any pending DMA status. we want to
 * retry the current request in PIO mode instead of risking tossing it
 * all away
 *
 * FIXME: needs a bit of tcq work
 */
void ide_dma_timeout_retry(ide_drive_t *drive)
{
	struct ata_channel *hwif = drive->channel;
	struct request *rq = NULL;
	struct ata_request *ar = NULL;

	if (drive->using_tcq) {
		if (drive->tcq->active_tag != -1) {
			ar = IDE_CUR_AR(drive);
			rq = ar->ar_rq;
		}
	} else {
		rq = HWGROUP(drive)->rq;
		ar = rq->special;
	}

	/*
	 * end current dma transaction
	 */
	if (rq)
		hwif->dmaproc(ide_dma_end, drive);

	/*
	 * complain a little, later we might remove some of this verbosity
	 */
	printk("%s: timeout waiting for DMA", drive->name);
	if (drive->using_tcq)
		printk(" queued, active tag %d", drive->tcq->active_tag);
	printk("\n");

	hwif->dmaproc(ide_dma_timeout, drive);

	/*
	 * Disable dma for now, but remember that we did so because of
	 * a timeout -- we'll reenable after we finish this next request
	 * (or rather the first chunk of it) in pio.
	 */
	drive->retry_pio++;
	drive->state = DMA_PIO_RETRY;
	hwif->dmaproc(ide_dma_off_quietly, drive);

	/*
	 * un-busy drive etc (hwgroup->busy is cleared on return) and
	 * make sure request is sane
	 */
	HWGROUP(drive)->rq = NULL;

	if (!rq)
		return;

	rq->errors = 0;
	if (rq->bio) {
		rq->sector = rq->bio->bi_sector;
		rq->current_nr_sectors = bio_iovec(rq->bio)->bv_len >> 9;
		rq->buffer = NULL;
	}

	/*
	 *  this request was not on the queue any more
	 */
	if (ar->ar_flags & ATA_AR_QUEUED) {
		ata_ar_put(drive, ar);
		_elv_add_request(&drive->queue, rq, 0, 0);
	}
}

/*
 * This is our timeout function for all drive operations.  But note that it can
 * also be invoked as a result of a "sleep" operation triggered by the
 * mod_timer() call in ide_do_request.
 */
void ide_timer_expiry(unsigned long data)
{
	ide_hwgroup_t	*hwgroup = (ide_hwgroup_t *) data;
	ide_handler_t	*handler;
	ide_expiry_t	*expiry;
	unsigned long	flags;
	unsigned long	wait;

	/*
	 * a global lock protects timers etc -- shouldn't get contention
	 * worth mentioning
	 */
	spin_lock_irqsave(&ide_lock, flags);
	del_timer(&hwgroup->timer);

	if ((handler = hwgroup->handler) == NULL) {
		/*
		 * Either a marginal timeout occurred
		 * (got the interrupt just as timer expired),
		 * or we were "sleeping" to give other devices a chance.
		 * Either way, we don't really want to complain about anything.
		 */
		if (test_and_clear_bit(IDE_SLEEP, &hwgroup->flags))
			clear_bit(IDE_BUSY, &hwgroup->flags);
	} else {
		ide_drive_t *drive = hwgroup->drive;
		if (!drive) {
			printk("ide_timer_expiry: hwgroup->drive was NULL\n");
			hwgroup->handler = NULL;
		} else {
			struct ata_channel *hwif;
			ide_startstop_t startstop;
			/* paranoia */
			if (!test_and_set_bit(IDE_BUSY, &hwgroup->flags))
				printk("%s: ide_timer_expiry: hwgroup was not busy??\n", drive->name);
			if ((expiry = hwgroup->expiry) != NULL) {
				/* continue */
				if ((wait = expiry(drive)) != 0) {
					/* reengage timer */
					hwgroup->timer.expires  = jiffies + wait;
					add_timer(&hwgroup->timer);
					spin_unlock_irqrestore(&ide_lock, flags);
					return;
				}
			}
			hwgroup->handler = NULL;
			/*
			 * We need to simulate a real interrupt when invoking
			 * the handler() function, which means we need to globally
			 * mask the specific IRQ:
			 */
			spin_unlock(&ide_lock);
			hwif  = drive->channel;
#if DISABLE_IRQ_NOSYNC
			disable_irq_nosync(hwif->irq);
#else
			disable_irq(hwif->irq);	/* disable_irq_nosync ?? */
#endif
			__cli();	/* local CPU only, as if we were handling an interrupt */
			if (hwgroup->poll_timeout != 0) {
				startstop = handler(drive);
			} else if (drive_is_ready(drive)) {
				if (drive->waiting_for_dma)
					(void) hwgroup->hwif->dmaproc(ide_dma_lostirq, drive);
				(void)ide_ack_intr(hwif);
				printk("%s: lost interrupt\n", drive->name);
				startstop = handler(drive);
			} else {
				if (drive->waiting_for_dma) {
					startstop = ide_stopped;
					ide_dma_timeout_retry(drive);
				} else
					startstop = ide_error(drive, "irq timeout", GET_STAT());
			}
			set_recovery_timer(hwif);
			drive->PADAM_service_time = jiffies - drive->PADAM_service_start;
			enable_irq(hwif->irq);
			spin_lock_irq(&ide_lock);
			if (startstop == ide_stopped)
				clear_bit(IDE_BUSY, &hwgroup->flags);
		}
	}
	ide_do_request(hwgroup, 0);
	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * There's nothing really useful we can do with an unexpected interrupt,
 * other than reading the status register (to clear it), and logging it.
 * There should be no way that an irq can happen before we're ready for it,
 * so we needn't worry much about losing an "important" interrupt here.
 *
 * On laptops (and "green" PCs), an unexpected interrupt occurs whenever the
 * drive enters "idle", "standby", or "sleep" mode, so if the status looks
 * "good", we just ignore the interrupt completely.
 *
 * This routine assumes __cli() is in effect when called.
 *
 * If an unexpected interrupt happens on irq15 while we are handling irq14
 * and if the two interfaces are "serialized" (CMD640), then it looks like
 * we could screw up by interfering with a new request being set up for irq15.
 *
 * In reality, this is a non-issue.  The new command is not sent unless the
 * drive is ready to accept one, in which case we know the drive is not
 * trying to interrupt us.  And ide_set_handler() is always invoked before
 * completing the issuance of any new drive command, so we will not be
 * accidentally invoked as a result of any valid command completion interrupt.
 *
 */
static void unexpected_intr(int irq, ide_hwgroup_t *hwgroup)
{
	u8 stat;
	struct ata_channel *hwif = hwgroup->hwif;

	/*
	 * handle the unexpected interrupt
	 */
	do {
		if (hwif->irq != irq)
			continue;

		stat = IN_BYTE(hwif->io_ports[IDE_STATUS_OFFSET]);
		if (!OK_STAT(stat, READY_STAT, BAD_STAT)) {
			/* Try to not flood the console with msgs */
			static unsigned long last_msgtime;
			static int count;
			++count;
			if (time_after(jiffies, last_msgtime + HZ)) {
				last_msgtime = jiffies;
				printk("%s%s: unexpected interrupt, status=0x%02x, count=%d\n",
						hwif->name, (hwif->next == hwgroup->hwif) ? "" : "(?)", stat, count);
			}
		}
		hwif = hwif->next;
	} while (hwif != hwgroup->hwif);
}

/*
 * entry point for all interrupts, caller does __cli() for us
 */
void ide_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = (ide_hwgroup_t *)dev_id;
	struct ata_channel *hwif;
	ide_drive_t *drive;
	ide_handler_t *handler;
	ide_startstop_t startstop;

	spin_lock_irqsave(&ide_lock, flags);
	hwif = hwgroup->hwif;

	if (!ide_ack_intr(hwif))
		goto out_lock;

	if ((handler = hwgroup->handler) == NULL || hwgroup->poll_timeout != 0) {
		printk(KERN_INFO "ide: unexpected interrupt %d %d\n", hwif->unit, irq);

		/*
		 * Not expecting an interrupt from this drive.
		 * That means this could be:
		 *	(1) an interrupt from another PCI device
		 *	sharing the same PCI INT# as us.
		 * or	(2) a drive just entered sleep or standby mode,
		 *	and is interrupting to let us know.
		 * or	(3) a spurious interrupt of unknown origin.
		 *
		 * For PCI, we cannot tell the difference,
		 * so in that case we just ignore it and hope it goes away.
		 */
#ifdef CONFIG_BLK_DEV_IDEPCI
		if (hwif->pci_dev && !hwif->pci_dev->vendor)
#endif
		{
			/*
			 * Probably not a shared PCI interrupt,
			 * so we can safely try to do something about it:
			 */
			unexpected_intr(irq, hwgroup);
#ifdef CONFIG_BLK_DEV_IDEPCI
		} else {
			/*
			 * Whack the status register, just in case we have a leftover pending IRQ.
			 */
			IN_BYTE(hwif->io_ports[IDE_STATUS_OFFSET]);
#endif
		}
		goto out_lock;
	}
	drive = hwgroup->drive;
	if (!drive) {
		/*
		 * This should NEVER happen, and there isn't much we could do
		 * about it here.
		 */
		goto out_lock;
	}
	if (!drive_is_ready(drive)) {
		/*
		 * This happens regularly when we share a PCI IRQ with another device.
		 * Unfortunately, it can also happen with some buggy drives that trigger
		 * the IRQ before their status register is up to date.  Hopefully we have
		 * enough advance overhead that the latter isn't a problem.
		 */
		goto out_lock;
	}
	/* paranoia */
	if (!test_and_set_bit(IDE_BUSY, &hwgroup->flags))
		printk("%s: ide_intr: hwgroup was not busy??\n", drive->name);
	hwgroup->handler = NULL;
	del_timer(&hwgroup->timer);
	spin_unlock(&ide_lock);

	if (drive->unmask)
		ide__sti();	/* local CPU only */
	startstop = handler(drive);		/* service this interrupt, may set handler for next interrupt */
	spin_lock_irq(&ide_lock);

	/*
	 * Note that handler() may have set things up for another
	 * interrupt to occur soon, but it cannot happen until
	 * we exit from this routine, because it will be the
	 * same irq as is currently being serviced here, and Linux
	 * won't allow another of the same (on any CPU) until we return.
	 */
	set_recovery_timer(drive->channel);
	drive->PADAM_service_time = jiffies - drive->PADAM_service_start;
	if (startstop == ide_stopped) {
		if (hwgroup->handler == NULL) { /* paranoia */
			clear_bit(IDE_BUSY, &hwgroup->flags);
			if (test_bit(IDE_DMA, &hwgroup->flags))
				printk("ide_intr: illegal clear\n");
			ide_do_request(hwgroup, hwif->irq);
		} else {
			printk("%s: ide_intr: huh? expected NULL handler on exit\n", drive->name);
		}
	} else if (startstop == ide_released)
		ide_queue_commands(drive, hwif->irq);

out_lock:
	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * get_info_ptr() returns the (ide_drive_t *) for a given device number.
 * It returns NULL if the given device number does not match any present drives.
 */
ide_drive_t *get_info_ptr(kdev_t i_rdev)
{
	unsigned int major = major(i_rdev);
	int h;

	for (h = 0; h < MAX_HWIFS; ++h) {
		struct ata_channel *hwif = &ide_hwifs[h];
		if (hwif->present && major == hwif->major) {
			int unit = DEVICE_NR(i_rdev);
			if (unit < MAX_DRIVES) {
				ide_drive_t *drive = &hwif->drives[unit];
				if (drive->present)
					return drive;
			}
			break;
		}
	}
	return NULL;
}

/*
 * This function is intended to be used prior to invoking ide_do_drive_cmd().
 */
void ide_init_drive_cmd (struct request *rq)
{
	memset(rq, 0, sizeof(*rq));
	rq->flags = REQ_DRIVE_CMD;
}

/*
 * This function issues a special IDE device request onto the request queue.
 *
 * If action is ide_wait, then the rq is queued at the end of the request
 * queue, and the function sleeps until it has been processed.  This is for use
 * when invoked from an ioctl handler.
 *
 * If action is ide_preempt, then the rq is queued at the head of the request
 * queue, displacing the currently-being-processed request and this function
 * returns immediately without waiting for the new rq to be completed.  This is
 * VERY DANGEROUS, and is intended for careful use by the ATAPI tape/cdrom
 * driver code.
 *
 * If action is ide_next, then the rq is queued immediately after the
 * currently-being-processed-request (if any), and the function returns without
 * waiting for the new rq to be completed.  As above, This is VERY DANGEROUS,
 * and is intended for careful use by the ATAPI tape/cdrom driver code.
 *
 * If action is ide_end, then the rq is queued at the end of the request queue,
 * and the function returns immediately without waiting for the new rq to be
 * completed. This is again intended for careful use by the ATAPI tape/cdrom
 * driver code.
 */
int ide_do_drive_cmd(ide_drive_t *drive, struct request *rq, ide_action_t action)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned int major = drive->channel->major;
	request_queue_t *q = &drive->queue;
	struct list_head *queue_head = &q->queue_head;
	DECLARE_COMPLETION(wait);

#ifdef CONFIG_BLK_DEV_PDC4030
	if (drive->channel->chipset == ide_pdc4030 && rq->buffer != NULL)
		return -ENOSYS;  /* special drive cmds not supported */
#endif
	rq->flags |= REQ_STARTED;
	rq->errors = 0;
	rq->rq_status = RQ_ACTIVE;
	rq->rq_dev = mk_kdev(major,(drive->select.b.unit)<<PARTN_BITS);
	if (action == ide_wait)
		rq->waiting = &wait;
	spin_lock_irqsave(&ide_lock, flags);
	if (blk_queue_empty(&drive->queue) || action == ide_preempt) {
		if (action == ide_preempt)
			hwgroup->rq = NULL;
	} else {
		if (action == ide_wait || action == ide_end)
			queue_head = queue_head->prev;
		else
			queue_head = queue_head->next;
	}
	q->elevator.elevator_add_req_fn(q, rq, queue_head);
	ide_do_request(hwgroup, 0);
	spin_unlock_irqrestore(&ide_lock, flags);
	if (action == ide_wait) {
		wait_for_completion(&wait);	/* wait for it to be serviced */
		return rq->errors ? -EIO : 0;	/* return -EIO if errors */
	}
	return 0;

}

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
int ide_revalidate_disk (kdev_t i_rdev)
{
	ide_drive_t *drive;
	ide_hwgroup_t *hwgroup;
	unsigned long flags;
	int res;

	if ((drive = get_info_ptr(i_rdev)) == NULL)
		return -ENODEV;
	hwgroup = HWGROUP(drive);
	spin_lock_irqsave(&ide_lock, flags);
	if (drive->busy || (drive->usage > 1)) {
		spin_unlock_irqrestore(&ide_lock, flags);
		return -EBUSY;
	}
	drive->busy = 1;
	MOD_INC_USE_COUNT;
	spin_unlock_irqrestore(&ide_lock, flags);

	res = wipe_partitions(i_rdev);
	if (!res) {
		if (ata_ops(drive) && ata_ops(drive)->revalidate) {
			ata_get(ata_ops(drive));
			/* this is a no-op for tapes and SCSI based access */
			ata_ops(drive)->revalidate(drive);
			ata_put(ata_ops(drive));
		} else
			grok_partitions(i_rdev, ata_capacity(drive));
	}

	drive->busy = 0;
	wake_up(&drive->wqueue);
	MOD_DEC_USE_COUNT;
	return res;
}

/*
 * Look again for all drives in the system on all interfaces.  This is used
 * after a new driver category has been loaded as module.
 */
void revalidate_drives(void)
{
	struct ata_channel *hwif;
	ide_drive_t *drive;
	int h;

	for (h = 0; h < MAX_HWIFS; ++h) {
		int unit;
		hwif = &ide_hwifs[h];
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			drive = &ide_hwifs[h].drives[unit];
			if (drive->revalidate) {
				drive->revalidate = 0;
				if (!initializing)
					ide_revalidate_disk(mk_kdev(hwif->major, unit<<PARTN_BITS));
			}
		}
	}
}

static void ide_probe_module(void)
{
	ideprobe_init();
	revalidate_drives();
}

static void ide_driver_module (void)
{
	int index;

	for (index = 0; index < MAX_HWIFS; ++index)
		if (ide_hwifs[index].present)
			goto search;
	ide_probe_module();
search:

	revalidate_drives();
}

static int ide_open(struct inode * inode, struct file * filp)
{
	ide_drive_t *drive;

	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENXIO;
	if (drive->driver == NULL)
		ide_driver_module();

	/* Request a particular device type module.
	 *
	 * FIXME: The function which should rather requests the drivers in
	 * ide_driver_module(), since it seems illogical and even a bit
	 * dangerous to delay this until open time!
	 */

#ifdef CONFIG_KMOD
	if (drive->driver == NULL) {
		char *module = NULL;

		switch (drive->type) {
			case ATA_DISK:
				module = "ide-disk";
				break;
			case ATA_ROM:
				module = "ide-cd";
				break;
			case ATA_TAPE:
				module = "ide-tape";
				break;
			case ATA_FLOPPY:
				module = "ide-floppy";
				break;
			case ATA_SCSI:
				module = "ide-scsi";
				break;
			default:
				/* nothing we can do about it */ ;
		}
		if (module)
			request_module(module);
	}
#endif
	while (drive->busy)
		sleep_on(&drive->wqueue);
	++drive->usage;
	if (ata_ops(drive) && ata_ops(drive)->open)
		return ata_ops(drive)->open(inode, filp, drive);
	else {
		--drive->usage;
		return -ENODEV;
	}

	printk(KERN_INFO "%s: driver not present\n", drive->name);
	drive->usage--;
	return -ENXIO;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */
static int ide_release(struct inode * inode, struct file * file)
{
	ide_drive_t *drive;

	if (!(drive = get_info_ptr(inode->i_rdev)))
		return 0;

	drive->usage--;
	if (ata_ops(drive) && ata_ops(drive)->release)
		ata_ops(drive)->release(inode, file, drive);
	return 0;
}

#ifdef CONFIG_PROC_FS
ide_proc_entry_t generic_subdriver_entries[] = {
	{ "capacity",	S_IFREG|S_IRUGO,	proc_ide_read_capacity,	NULL },
	{ NULL, 0, NULL, NULL }
};
#endif

/*
 * Note that we only release the standard ports, and do not even try to handle
 * any extra ports allocated for weird IDE interface chipsets.
 */
static void hwif_unregister(struct ata_channel *hwif)
{
	if (hwif->straight8) {
		ide_release_region(hwif->io_ports[IDE_DATA_OFFSET], 8);
	} else {
		if (hwif->io_ports[IDE_DATA_OFFSET])
			ide_release_region(hwif->io_ports[IDE_DATA_OFFSET], 1);
		if (hwif->io_ports[IDE_ERROR_OFFSET])
			ide_release_region(hwif->io_ports[IDE_ERROR_OFFSET], 1);
		if (hwif->io_ports[IDE_NSECTOR_OFFSET])
			ide_release_region(hwif->io_ports[IDE_NSECTOR_OFFSET], 1);
		if (hwif->io_ports[IDE_SECTOR_OFFSET])
			ide_release_region(hwif->io_ports[IDE_SECTOR_OFFSET], 1);
		if (hwif->io_ports[IDE_LCYL_OFFSET])
			ide_release_region(hwif->io_ports[IDE_LCYL_OFFSET], 1);
		if (hwif->io_ports[IDE_HCYL_OFFSET])
			ide_release_region(hwif->io_ports[IDE_HCYL_OFFSET], 1);
		if (hwif->io_ports[IDE_SELECT_OFFSET])
			ide_release_region(hwif->io_ports[IDE_SELECT_OFFSET], 1);
		if (hwif->io_ports[IDE_STATUS_OFFSET])
			ide_release_region(hwif->io_ports[IDE_STATUS_OFFSET], 1);
	}
	if (hwif->io_ports[IDE_CONTROL_OFFSET])
		ide_release_region(hwif->io_ports[IDE_CONTROL_OFFSET], 1);
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (hwif->io_ports[IDE_IRQ_OFFSET])
		ide_release_region(hwif->io_ports[IDE_IRQ_OFFSET], 1);
#endif
}

void ide_unregister(struct ata_channel *channel)
{
	struct gendisk *gd;
	ide_drive_t *drive, *d;
	ide_hwgroup_t *hwgroup;
	int unit, i;
	unsigned long flags;
	unsigned int p, minor;
	struct ata_channel old_hwif;

	spin_lock_irqsave(&ide_lock, flags);
	if (!channel->present)
		goto abort;
	put_device(&channel->dev);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		drive = &channel->drives[unit];
		if (!drive->present)
			continue;
		if (drive->busy || drive->usage)
			goto abort;
		if (ata_ops(drive)) {
			if (ata_ops(drive)->cleanup) {
				if (ata_ops(drive)->cleanup(drive))
					goto abort;
			} else
				ide_unregister_subdriver(drive);
		}
	}
	channel->present = 0;

	/*
	 * All clear?  Then blow away the buffer cache
	 */
	spin_unlock_irqrestore(&ide_lock, flags);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		drive = &channel->drives[unit];
		if (!drive->present)
			continue;
		minor = drive->select.b.unit << PARTN_BITS;
		for (p = 0; p < (1<<PARTN_BITS); ++p) {
			if (drive->part[p].nr_sects > 0) {
				kdev_t devp = mk_kdev(channel->major, minor+p);
				invalidate_device(devp, 0);
			}
		}
	}
#ifdef CONFIG_PROC_FS
	destroy_proc_ide_drives(channel);
#endif
	spin_lock_irqsave(&ide_lock, flags);
	hwgroup = channel->hwgroup;

	/*
	 * free the irq if we were the only hwif using it
	 */
	{
		struct ata_channel *g;
		int irq_count = 0;

		g = hwgroup->hwif;
		do {
			if (g->irq == channel->irq)
				++irq_count;
			g = g->next;
		} while (g != hwgroup->hwif);
		if (irq_count == 1)
			free_irq(channel->irq, hwgroup);
	}
	hwif_unregister(channel);

	/*
	 * Remove us from the hwgroup, and free
	 * the hwgroup if we were the only member
	 */
	d = hwgroup->drive;
	for (i = 0; i < MAX_DRIVES; ++i) {
		drive = &channel->drives[i];
		if (drive->de) {
			devfs_unregister (drive->de);
			drive->de = NULL;
		}
		if (!drive->present)
			continue;
		while (hwgroup->drive->next != drive)
			hwgroup->drive = hwgroup->drive->next;
		hwgroup->drive->next = drive->next;
		if (hwgroup->drive == drive)
			hwgroup->drive = NULL;
		if (drive->id != NULL) {
			kfree(drive->id);
			drive->id = NULL;
		}
		drive->present = 0;
		blk_cleanup_queue(&drive->queue);
		ide_teardown_commandlist(drive);
	}
	if (d->present)
		hwgroup->drive = d;
	while (hwgroup->hwif->next != channel)
		hwgroup->hwif = hwgroup->hwif->next;
	hwgroup->hwif->next = channel->next;
	if (hwgroup->hwif == channel)
		kfree(hwgroup);
	else
		hwgroup->hwif = hwgroup->drive->channel;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
	ide_release_dma(channel);
#endif

	/*
	 * Remove us from the kernel's knowledge.
	 */
	unregister_blkdev(channel->major, channel->name);
	kfree(blksize_size[channel->major]);
	blk_dev[channel->major].data = NULL;
	blk_dev[channel->major].queue = NULL;
	blk_clear(channel->major);
	gd = channel->gd;
	if (gd) {
		del_gendisk(gd);
		kfree(gd->sizes);
		kfree(gd->part);
		if (gd->de_arr)
			kfree (gd->de_arr);
		if (gd->flags)
			kfree (gd->flags);
		kfree(gd);
		channel->gd = NULL;
	}

	/*
	 * Reinitialize the channel handler, but preserve any special methods for
	 * it.
	 */

	old_hwif = *channel;
	init_hwif_data(channel, channel->index);
	channel->hwgroup = old_hwif.hwgroup;
	channel->tuneproc = old_hwif.tuneproc;
	channel->speedproc = old_hwif.speedproc;
	channel->selectproc = old_hwif.selectproc;
	channel->resetproc = old_hwif.resetproc;
	channel->intrproc = old_hwif.intrproc;
	channel->maskproc = old_hwif.maskproc;
	channel->quirkproc = old_hwif.quirkproc;
	channel->rwproc	= old_hwif.rwproc;
	channel->ata_read = old_hwif.ata_read;
	channel->ata_write = old_hwif.ata_write;
	channel->atapi_read = old_hwif.atapi_read;
	channel->atapi_write = old_hwif.atapi_write;
	channel->dmaproc = old_hwif.dmaproc;
	channel->busproc = old_hwif.busproc;
	channel->bus_state = old_hwif.bus_state;
	channel->dma_base = old_hwif.dma_base;
	channel->dma_extra = old_hwif.dma_extra;
	channel->config_data = old_hwif.config_data;
	channel->select_data = old_hwif.select_data;
	channel->proc = old_hwif.proc;
#ifndef CONFIG_BLK_DEV_IDECS
	channel->irq = old_hwif.irq;
#endif
	channel->major = old_hwif.major;
	channel->chipset = old_hwif.chipset;
	channel->autodma = old_hwif.autodma;
	channel->udma_four = old_hwif.udma_four;
#ifdef CONFIG_BLK_DEV_IDEPCI
	channel->pci_dev = old_hwif.pci_dev;
#else
	channel->pci_dev = NULL;
#endif
	channel->straight8 = old_hwif.straight8;
abort:
	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * Setup hw_regs_t structure described by parameters.  You
 * may set up the hw structure yourself OR use this routine to
 * do it for you.
 */
void ide_setup_ports (	hw_regs_t *hw,
			ide_ioreg_t base, int *offsets,
			ide_ioreg_t ctrl, ide_ioreg_t intr,
			ide_ack_intr_t *ack_intr, int irq)
{
	int i;

	for (i = 0; i < IDE_NR_PORTS; i++) {
		if (offsets[i] == -1) {
			switch(i) {
				case IDE_CONTROL_OFFSET:
					hw->io_ports[i] = ctrl;
					break;
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
				case IDE_IRQ_OFFSET:
					hw->io_ports[i] = intr;
					break;
#endif
				default:
					hw->io_ports[i] = 0;
					break;
			}
		} else {
			hw->io_ports[i] = base + offsets[i];
		}
	}
	hw->irq = irq;
	hw->dma = NO_DMA;
	hw->ack_intr = ack_intr;
}

/*
 * Register an IDE interface, specifing exactly the registers etc
 * Set init=1 iff calling before probes have taken place.
 */
int ide_register_hw(hw_regs_t *hw, struct ata_channel **hwifp)
{
	int h;
	int retry = 1;
	struct ata_channel *hwif;

	do {
		for (h = 0; h < MAX_HWIFS; ++h) {
			hwif = &ide_hwifs[h];
			if (hwif->hw.io_ports[IDE_DATA_OFFSET] == hw->io_ports[IDE_DATA_OFFSET])
				goto found;
		}
		for (h = 0; h < MAX_HWIFS; ++h) {
			hwif = &ide_hwifs[h];
			if ((!hwif->present && (hwif->unit == ATA_PRIMARY) && !initializing) ||
			    (!hwif->hw.io_ports[IDE_DATA_OFFSET] && initializing))
				goto found;
		}
		for (h = 0; h < MAX_HWIFS; ++h)
			ide_unregister(&ide_hwifs[h]);
	} while (retry--);

	return -1;

found:
	ide_unregister(hwif);
	if (hwif->present)
		return -1;
	memcpy(&hwif->hw, hw, sizeof(*hw));
	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->hw.io_ports));
	hwif->irq = hw->irq;
	hwif->noprobe = 0;
	hwif->chipset = hw->chipset;

	if (!initializing) {
		ide_probe_module();
#ifdef CONFIG_PROC_FS
		create_proc_ide_interfaces();
#endif
		ide_driver_module();
	}

	if (hwifp)
		*hwifp = hwif;

	return (initializing || hwif->present) ? h : -1;
}

/*
 * Compatability function for existing drivers.  If you want
 * something different, use the function above.
 */
int ide_register(int arg1, int arg2, int irq)
{
	hw_regs_t hw;
	ide_init_hwif_ports(&hw, (ide_ioreg_t) arg1, (ide_ioreg_t) arg2, NULL);
	hw.irq = irq;
	return ide_register_hw(&hw, NULL);
}

void ide_add_setting(ide_drive_t *drive, const char *name, int rw, int read_ioctl, int write_ioctl, int data_type, int min, int max, int mul_factor, int div_factor, void *data, ide_procset_t *set)
{
	ide_settings_t **p = &drive->settings;
	ide_settings_t *setting = NULL;

	while ((*p) && strcmp((*p)->name, name) < 0)
		p = &((*p)->next);
	if ((setting = kmalloc(sizeof(*setting), GFP_KERNEL)) == NULL)
		goto abort;
	memset(setting, 0, sizeof(*setting));
	if ((setting->name = kmalloc(strlen(name) + 1, GFP_KERNEL)) == NULL)
		goto abort;
	strcpy(setting->name, name);		setting->rw = rw;
	setting->read_ioctl = read_ioctl;	setting->write_ioctl = write_ioctl;
	setting->data_type = data_type;		setting->min = min;
	setting->max = max;			setting->mul_factor = mul_factor;
	setting->div_factor = div_factor;	setting->data = data;
	setting->set = set;			setting->next = *p;
	if (drive->driver)
		setting->auto_remove = 1;
	*p = setting;
	return;
abort:
	if (setting)
		kfree(setting);
}

void ide_remove_setting (ide_drive_t *drive, char *name)
{
	ide_settings_t **p = &drive->settings, *setting;

	while ((*p) && strcmp((*p)->name, name))
		p = &((*p)->next);
	if ((setting = (*p)) == NULL)
		return;
	(*p) = setting->next;
	kfree(setting->name);
	kfree(setting);
}

static void auto_remove_settings (ide_drive_t *drive)
{
	ide_settings_t *setting;
repeat:
	setting = drive->settings;
	while (setting) {
		if (setting->auto_remove) {
			ide_remove_setting(drive, setting->name);
			goto repeat;
		}
		setting = setting->next;
	}
}

int ide_read_setting (ide_drive_t *drive, ide_settings_t *setting)
{
	int		val = -EINVAL;
	unsigned long	flags;

	if ((setting->rw & SETTING_READ)) {
		spin_lock_irqsave(&ide_lock, flags);
		switch(setting->data_type) {
			case TYPE_BYTE:
				val = *((u8 *) setting->data);
				break;
			case TYPE_SHORT:
				val = *((u16 *) setting->data);
				break;
			case TYPE_INT:
			case TYPE_INTA:
				val = *((u32 *) setting->data);
				break;
		}
		spin_unlock_irqrestore(&ide_lock, flags);
	}
	return val;
}

int ide_spin_wait_hwgroup (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned long timeout = jiffies + (3 * HZ);

	spin_lock_irq(&ide_lock);

	while (test_bit(IDE_BUSY, &hwgroup->flags)) {
		unsigned long lflags;
		spin_unlock_irq(&ide_lock);
		__save_flags(lflags);	/* local CPU only */
		__sti();		/* local CPU only; needed for jiffies */
		if (0 < (signed long)(jiffies - timeout)) {
			__restore_flags(lflags);	/* local CPU only */
			printk("%s: channel busy\n", drive->name);
			return -EBUSY;
		}
		__restore_flags(lflags);	/* local CPU only */
		spin_lock_irq(&ide_lock);
	}
	return 0;
}

/*
 * FIXME:  This should be changed to enqueue a special request
 * to the driver to change settings, and then wait on a semaphore for completion.
 * The current scheme of polling is kludgey, though safe enough.
 */
int ide_write_setting (ide_drive_t *drive, ide_settings_t *setting, int val)
{
	int i;
	u32 *p;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!(setting->rw & SETTING_WRITE))
		return -EPERM;
	if (val < setting->min || val > setting->max)
		return -EINVAL;
	if (setting->set)
		return setting->set(drive, val);
	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	switch (setting->data_type) {
		case TYPE_BYTE:
			*((u8 *) setting->data) = val;
			break;
		case TYPE_SHORT:
			*((u16 *) setting->data) = val;
			break;
		case TYPE_INT:
			*((u32 *) setting->data) = val;
			break;
		case TYPE_INTA:
			p = (u32 *) setting->data;
			for (i = 0; i < 1 << PARTN_BITS; i++, p++)
				*p = val;
			break;
	}
	spin_unlock_irq(&ide_lock);
	return 0;
}

static int set_io_32bit(struct ata_device *drive, int arg)
{
	if (drive->no_io_32bit)
		return -EIO;

	drive->io_32bit = arg;
#ifdef CONFIG_BLK_DEV_DTC2278
	if (drive->channel->chipset == ide_dtc2278)
		drive->channel->drives[!drive->select.b.unit].io_32bit = arg;
#endif

	return 0;
}

static int set_using_dma (ide_drive_t *drive, int arg)
{
	if (!drive->driver)
		return -EPERM;
	if (!drive->id || !(drive->id->capability & 1) || !drive->channel->dmaproc)
		return -EPERM;
	if (drive->channel->dmaproc(arg ? ide_dma_on : ide_dma_off, drive))
		return -EIO;
	return 0;
}

static int set_pio_mode (ide_drive_t *drive, int arg)
{
	struct request rq;

	if (!drive->channel->tuneproc)
		return -ENOSYS;
	if (drive->special.b.set_tune)
		return -EBUSY;
	ide_init_drive_cmd(&rq);
	drive->tune_req = (byte) arg;
	drive->special.b.set_tune = 1;
	ide_do_drive_cmd(drive, &rq, ide_wait);
	return 0;
}

void ide_add_generic_settings (ide_drive_t *drive)
{
/*			drive	setting name		read/write access				read ioctl		write ioctl		data type	min	max				mul_factor	div_factor	data pointer			set function */
	ide_add_setting(drive,	"io_32bit",		drive->no_io_32bit ? SETTING_READ : SETTING_RW,	HDIO_GET_32BIT,		HDIO_SET_32BIT,		TYPE_BYTE,	0,	1 + (SUPPORT_VLB_SYNC << 1),	1,		1,		&drive->io_32bit,		set_io_32bit);
	ide_add_setting(drive,	"keepsettings",		SETTING_RW,					HDIO_GET_KEEPSETTINGS,	HDIO_SET_KEEPSETTINGS,	TYPE_BYTE,	0,	1,				1,		1,		&drive->keep_settings,		NULL);
	ide_add_setting(drive,	"pio_mode",		SETTING_WRITE,					-1,			HDIO_SET_PIO_MODE,	TYPE_BYTE,	0,	255,				1,		1,		NULL,				set_pio_mode);
	ide_add_setting(drive,	"slow",			SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	1,				1,		1,		&drive->slow,			NULL);
	ide_add_setting(drive,	"unmaskirq",		drive->no_unmask ? SETTING_READ : SETTING_RW,	HDIO_GET_UNMASKINTR,	HDIO_SET_UNMASKINTR,	TYPE_BYTE,	0,	1,				1,		1,		&drive->unmask,			NULL);
	ide_add_setting(drive,	"using_dma",		SETTING_RW,					HDIO_GET_DMA,		HDIO_SET_DMA,		TYPE_BYTE,	0,	1,				1,		1,		&drive->using_dma,		set_using_dma);
	ide_add_setting(drive,	"ide_scsi",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	1,				1,		1,		&drive->scsi,			NULL);
	ide_add_setting(drive,	"init_speed",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	69,				1,		1,		&drive->init_speed,		NULL);
	ide_add_setting(drive,	"current_speed",	SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	69,				1,		1,		&drive->current_speed,		NULL);
	ide_add_setting(drive,	"number",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	3,				1,		1,		&drive->dn,			NULL);
}

/*
 * Delay for *at least* 50ms.  As we don't know how much time is left
 * until the next tick occurs, we wait an extra tick to be safe.
 * This is used only during the probing/polling for drives at boot time.
 *
 * However, its usefullness may be needed in other places, thus we export it now.
 * The future may change this to a millisecond setable delay.
 */
void ide_delay_50ms (void)
{
#ifndef CONFIG_BLK_DEV_IDECS
	mdelay(50);
#else
	__set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ/20);
#endif /* CONFIG_BLK_DEV_IDECS */
}

static int ide_ioctl (struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int err = 0, major, minor;
	ide_drive_t *drive;
	struct request rq;
	kdev_t dev;
	ide_settings_t *setting;

	dev = inode->i_rdev;
	major = major(dev); minor = minor(dev);
	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENODEV;

	/* Find setting by ioctl */

	setting = drive->settings;

	while (setting) {
		if (setting->read_ioctl == cmd || setting->write_ioctl == cmd)
			break;
		setting = setting->next;
	}

	if (setting != NULL) {
		if (cmd == setting->read_ioctl) {
			err = ide_read_setting(drive, setting);
			return err >= 0 ? put_user(err, (long *) arg) : err;
		} else {
			if ((minor(inode->i_rdev) & PARTN_MASK))
				return -EINVAL;
			return ide_write_setting(drive, setting, arg);
		}
	}

	ide_init_drive_cmd (&rq);
	switch (cmd) {
		case HDIO_GETGEO:
		{
			struct hd_geometry *loc = (struct hd_geometry *) arg;
			unsigned short bios_cyl = drive->bios_cyl; /* truncate */

			if (!loc || (drive->type != ATA_DISK && drive->type != ATA_FLOPPY))
				return -EINVAL;
			if (put_user(drive->bios_head, (byte *) &loc->heads)) return -EFAULT;
			if (put_user(drive->bios_sect, (byte *) &loc->sectors)) return -EFAULT;
			if (put_user(bios_cyl, (unsigned short *) &loc->cylinders)) return -EFAULT;
			if (put_user((unsigned)drive->part[minor(inode->i_rdev)&PARTN_MASK].start_sect,
				(unsigned long *) &loc->start)) return -EFAULT;
			return 0;
		}

		case HDIO_GETGEO_BIG:
		{
			struct hd_big_geometry *loc = (struct hd_big_geometry *) arg;

			if (!loc || (drive->type != ATA_DISK && drive->type != ATA_FLOPPY))
				return -EINVAL;

			if (put_user(drive->bios_head, (byte *) &loc->heads)) return -EFAULT;
			if (put_user(drive->bios_sect, (byte *) &loc->sectors)) return -EFAULT;
			if (put_user(drive->bios_cyl, (unsigned int *) &loc->cylinders)) return -EFAULT;
			if (put_user((unsigned)drive->part[minor(inode->i_rdev)&PARTN_MASK].start_sect,
				(unsigned long *) &loc->start)) return -EFAULT;
			return 0;
		}

		case HDIO_GETGEO_BIG_RAW:
		{
			struct hd_big_geometry *loc = (struct hd_big_geometry *) arg;
			if (!loc || (drive->type != ATA_DISK && drive->type != ATA_FLOPPY))
				return -EINVAL;
			if (put_user(drive->head, (byte *) &loc->heads)) return -EFAULT;
			if (put_user(drive->sect, (byte *) &loc->sectors)) return -EFAULT;
			if (put_user(drive->cyl, (unsigned int *) &loc->cylinders)) return -EFAULT;
			if (put_user((unsigned)drive->part[minor(inode->i_rdev)&PARTN_MASK].start_sect,
				(unsigned long *) &loc->start)) return -EFAULT;
			return 0;
		}

		case BLKRRPART: /* Re-read partition tables */
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			return ide_revalidate_disk(inode->i_rdev);

		case HDIO_OBSOLETE_IDENTITY:
		case HDIO_GET_IDENTITY:
			if (minor(inode->i_rdev) & PARTN_MASK)
				return -EINVAL;
			if (drive->id == NULL)
				return -ENOMSG;
			if (copy_to_user((char *)arg, (char *)drive->id, (cmd == HDIO_GET_IDENTITY) ? sizeof(*drive->id) : 142))
				return -EFAULT;
			return 0;

		case HDIO_GET_NICE:
			return put_user(drive->dsc_overlap	<<	IDE_NICE_DSC_OVERLAP	|
					drive->atapi_overlap	<<	IDE_NICE_ATAPI_OVERLAP,
					(long *) arg);

		case HDIO_DRIVE_CMD:
			if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
				return -EACCES;
			return ide_cmd_ioctl(drive, arg);

		case HDIO_DRIVE_TASK:
			if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
				return -EACCES;
			return ide_task_ioctl(drive, arg);

		case HDIO_SET_NICE:
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
			if (arg != (arg & ((1 << IDE_NICE_DSC_OVERLAP))))
				return -EPERM;
			drive->dsc_overlap = (arg >> IDE_NICE_DSC_OVERLAP) & 1;
			/* Only CD-ROM's and tapes support DSC overlap. */
			if (drive->dsc_overlap && !(drive->type == ATA_ROM || drive->type == ATA_TAPE)) {
				drive->dsc_overlap = 0;
				return -EPERM;
			}
			return 0;
		case BLKGETSIZE:
		case BLKGETSIZE64:
		case BLKROSET:
		case BLKROGET:
		case BLKFLSBUF:
		case BLKSSZGET:
		case BLKPG:
		case BLKELVGET:
		case BLKELVSET:
		case BLKBSZGET:
		case BLKBSZSET:
			return blk_ioctl(inode->i_bdev, cmd, arg);

		/*
		 * uniform packet command handling
		 */
		case CDROMEJECT:
		case CDROMCLOSETRAY:
			return block_ioctl(inode->i_rdev, cmd, arg);

		case HDIO_GET_BUSSTATE:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (put_user(drive->channel->bus_state, (long *)arg))
				return -EFAULT;
			return 0;

		case HDIO_SET_BUSSTATE:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (drive->channel->busproc)
				drive->channel->busproc(drive, (int)arg);
			return 0;

		default:
			if (ata_ops(drive) && ata_ops(drive)->ioctl)
				return ata_ops(drive)->ioctl(drive, inode, file, cmd, arg);
			return -EINVAL;
	}
}

int ide_build_commandlist(ide_drive_t *drive)
{
#ifdef CONFIG_BLK_DEV_IDEPCI
	struct pci_dev *pdev = drive->channel->pci_dev;
#else
	struct pci_dev *pdev = NULL;
#endif
	struct list_head *p;
	unsigned long flags;
	struct ata_request *ar;
	int i, cur;

	spin_lock_irqsave(&ide_lock, flags);

	cur = 0;
	list_for_each(p, &drive->free_req)
		cur++;

	/*
	 * for now, just don't shrink it...
	 */
	if (drive->queue_depth <= cur) {
		spin_unlock_irqrestore(&ide_lock, flags);
		return 0;
	}

	for (i = cur; i < drive->queue_depth; i++) {
		ar = kmalloc(sizeof(*ar), GFP_ATOMIC);
		if (!ar)
			break;

		memset(ar, 0, sizeof(*ar));
		INIT_LIST_HEAD(&ar->ar_queue);

		ar->ar_sg_table = kmalloc(PRD_SEGMENTS * sizeof(struct scatterlist), GFP_ATOMIC);
		if (!ar->ar_sg_table) {
			kfree(ar);
			break;
		}

		ar->ar_dmatable_cpu = pci_alloc_consistent(pdev, PRD_SEGMENTS * PRD_BYTES, &ar->ar_dmatable);
		if (!ar->ar_dmatable_cpu) {
			kfree(ar->ar_sg_table);
			kfree(ar);
			break;
		}

		/*
		 * pheew, all done, add to list
		 */
		list_add_tail(&ar->ar_queue, &drive->free_req);
		++cur;
	}
	drive->queue_depth = cur;
	spin_unlock_irqrestore(&ide_lock, flags);
	return 0;
}

int ide_init_commandlist(ide_drive_t *drive)
{
	INIT_LIST_HEAD(&drive->free_req);

	return ide_build_commandlist(drive);
}

void ide_teardown_commandlist(ide_drive_t *drive)
{
	struct pci_dev *pdev= drive->channel->pci_dev;
	struct list_head *entry;

	list_for_each(entry, &drive->free_req) {
		struct ata_request *ar = list_ata_entry(entry);

		list_del(&ar->ar_queue);
		kfree(ar->ar_sg_table);
		pci_free_consistent(pdev, PRD_SEGMENTS * PRD_BYTES, ar->ar_dmatable_cpu, ar->ar_dmatable);
		kfree(ar);
	}
}

static int ide_check_media_change (kdev_t i_rdev)
{
	ide_drive_t *drive;
	int res = 0; /* not changed */

	drive = get_info_ptr(i_rdev);
	if (!drive)
		return -ENODEV;

	if (ata_ops(drive)) {
		ata_get(ata_ops(drive));
		if (ata_ops(drive)->check_media_change)
			res = ata_ops(drive)->check_media_change(drive);
		else
			res = 1; /* assume it was changed */
		ata_put(ata_ops(drive));
	}
	return res;
}

void ide_fixstring (byte *s, const int bytecount, const int byteswap)
{
	byte *p = s, *end = &s[bytecount & ~1]; /* bytecount must be even */

	if (byteswap) {
		/* convert from big-endian to host byte order */
		for (p = end ; p != s;) {
			unsigned short *pp = (unsigned short *) (p -= 2);
			*pp = ntohs(*pp);
		}
	}

	/* strip leading blanks */
	while (s != end && *s == ' ')
		++s;

	/* compress internal blanks and strip trailing blanks */
	while (s != end && *s) {
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	}

	/* wipe out trailing garbage */
	while (p != end)
		*p++ = '\0';
}

/****************************************************************************
 * FIXME: rewrite the following crap:
 */

/*
 * stridx() returns the offset of c within s,
 * or -1 if c is '\0' or not found within s.
 */
static int __init stridx (const char *s, char c)
{
	char *i = strchr(s, c);

	return (i && c) ? i - s : -1;
}

/*
 * Parsing for ide_setup():
 *
 * 1. the first char of s must be '='.
 * 2. if the remainder matches one of the supplied keywords,
 *     the index (1 based) of the keyword is negated and returned.
 * 3. if the remainder is a series of no more than max_vals numbers
 *     separated by commas, the numbers are saved in vals[] and a
 *     count of how many were saved is returned.  Base10 is assumed,
 *     and base16 is allowed when prefixed with "0x".
 * 4. otherwise, zero is returned.
 */
static int __init match_parm (char *s, const char *keywords[], int vals[], int max_vals)
{
	static const char *decimal = "0123456789";
	static const char *hex = "0123456789abcdef";
	int i, n;

	if (*s++ == '=') {
		/*
		 * Try matching against the supplied keywords,
		 * and return -(index+1) if we match one
		 */
		if (keywords != NULL) {
			for (i = 0; *keywords != NULL; ++i) {
				if (!strcmp(s, *keywords++))
					return -(i+1);
			}
		}
		/*
		 * Look for a series of no more than "max_vals"
		 * numeric values separated by commas, in base10,
		 * or base16 when prefixed with "0x".
		 * Return a count of how many were found.
		 */
		for (n = 0; (i = stridx(decimal, *s)) >= 0;) {
			vals[n] = i;
			while ((i = stridx(decimal, *++s)) >= 0)
				vals[n] = (vals[n] * 10) + i;
			if (*s == 'x' && !vals[n]) {
				while ((i = stridx(hex, *++s)) >= 0)
					vals[n] = (vals[n] * 0x10) + i;
			}
			if (++n == max_vals)
				break;
			if (*s == ',' || *s == ';')
				++s;
		}
		if (!*s)
			return n;
	}
	return 0;	/* zero = nothing matched */
}

/*
 * This gets called VERY EARLY during initialization, to handle kernel "command
 * line" strings beginning with "hdx=" or "ide".It gets called even before the
 * actual module gets initialized.
 *
 * Here is the complete set currently supported comand line options:
 *
 * "hdx="  is recognized for all "x" from "a" to "h", such as "hdc".
 * "idex=" is recognized for all "x" from "0" to "3", such as "ide1".
 *
 * "hdx=noprobe"	: drive may be present, but do not probe for it
 * "hdx=none"		: drive is NOT present, ignore cmos and do not probe
 * "hdx=nowerr"		: ignore the WRERR_STAT bit on this drive
 * "hdx=cdrom"		: drive is present, and is a cdrom drive
 * "hdx=cyl,head,sect"	: disk drive is present, with specified geometry
 * "hdx=noremap"	: do not remap 0->1 even though EZD was detected
 * "hdx=autotune"	: driver will attempt to tune interface speed
 *				to the fastest PIO mode supported,
 *				if possible for this drive only.
 *				Not fully supported by all chipset types,
 *				and quite likely to cause trouble with
 *				older/odd IDE drives.
 *
 * "hdx=slow"		: insert a huge pause after each access to the data
 *				port. Should be used only as a last resort.
 *
 * "hdxlun=xx"          : set the drive last logical unit.
 * "hdx=flash"		: allows for more than one ata_flash disk to be
 *				registered. In most cases, only one device
 *				will be present.
 * "hdx=scsi"		: the return of the ide-scsi flag, this is useful for
 *				allowwing ide-floppy, ide-tape, and ide-cdrom|writers
 *				to use ide-scsi emulation on a device specific option.
 * "idebus=xx"		: inform IDE driver of VESA/PCI bus speed in MHz,
 *				where "xx" is between 20 and 66 inclusive,
 *				used when tuning chipset PIO modes.
 *				For PCI bus, 25 is correct for a P75 system,
 *				30 is correct for P90,P120,P180 systems,
 *				and 33 is used for P100,P133,P166 systems.
 *				If in doubt, use idebus=33 for PCI.
 *				As for VLB, it is safest to not specify it.
 *
 * "idex=noprobe"	: do not attempt to access/use this interface
 * "idex=base"		: probe for an interface at the address specified,
 *				where "base" is usually 0x1f0 or 0x170
 *				and "ctl" is assumed to be "base"+0x206
 * "idex=base,ctl"	: specify both base and ctl
 * "idex=base,ctl,irq"	: specify base, ctl, and irq number
 * "idex=autotune"	: driver will attempt to tune interface speed
 *				to the fastest PIO mode supported,
 *				for all drives on this interface.
 *				Not fully supported by all chipset types,
 *				and quite likely to cause trouble with
 *				older/odd IDE drives.
 * "idex=noautotune"	: driver will NOT attempt to tune interface speed
 *				This is the default for most chipsets,
 *				except the cmd640.
 * "idex=serialize"	: do not overlap operations on idex and ide(x^1)
 * "idex=four"		: four drives on idex and ide(x^1) share same ports
 * "idex=reset"		: reset interface before first use
 * "idex=dma"		: enable DMA by default on both drives if possible
 * "idex=ata66"		: informs the interface that it has an 80c cable
 *				for chipsets that are ATA-66 capable, but
 *				the ablity to bit test for detection is
 *				currently unknown.
 * "ide=reverse"	: Formerly called to pci sub-system, but now local.
 *
 * The following are valid ONLY on ide0, (except dc4030)
 * and the defaults for the base,ctl ports must not be altered.
 *
 * "ide0=dtc2278"	: probe/support DTC2278 interface
 * "ide0=ht6560b"	: probe/support HT6560B interface
 * "ide0=cmd640_vlb"	: *REQUIRED* for VLB cards with the CMD640 chip
 *			  (not for PCI -- automatically detected)
 * "ide0=qd65xx"	: probe/support qd65xx interface
 * "ide0=ali14xx"	: probe/support ali14xx chipsets (ALI M1439, M1443, M1445)
 * "ide0=umc8672"	: probe/support umc8672 chipsets
 * "idex=dc4030"	: probe/support Promise DC4030VL interface
 * "ide=doubler"	: probe/support IDE doublers on Amiga
 */
int __init ide_setup (char *s)
{
	int i, vals[3];
	struct ata_channel *hwif;
	ide_drive_t *drive;
	unsigned int hw, unit;
	const char max_drive = 'a' + ((MAX_HWIFS * MAX_DRIVES) - 1);
	const char max_hwif  = '0' + (MAX_HWIFS - 1);

	if (!strncmp(s, "hd=", 3))	/* hd= is for hd.c driver and not us */
		return 0;

	if (strncmp(s,"ide",3) &&
	    strncmp(s,"idebus",6) &&
	    strncmp(s,"hd",2))		/* hdx= & hdxlun= */
		return 0;

	printk("ide_setup: %s", s);
	init_ide_data ();

#ifdef CONFIG_BLK_DEV_IDEDOUBLER
	if (!strcmp(s, "ide=doubler")) {
		extern int ide_doubler;

		printk(" : Enabled support for IDE doublers\n");
		ide_doubler = 1;

		return 1;
	}
#endif

	if (!strcmp(s, "ide=nodma")) {
		printk("IDE: Prevented DMA\n");
		noautodma = 1;

		return 1;
	}

#ifdef CONFIG_BLK_DEV_IDEPCI
	if (!strcmp(s, "ide=reverse")) {
		ide_scan_direction = 1;
		printk(" : Enabled support for IDE inverse scan order.\n");

		return 1;
	}
#endif

	/*
	 * Look for drive options:  "hdx="
	 */
	if (!strncmp(s, "hd", 2) && s[2] >= 'a' && s[2] <= max_drive) {
		const char *hd_words[] = {"none", "noprobe", "nowerr", "cdrom",
				"serialize", "autotune", "noautotune",
				"slow", "flash", "remap", "noremap", "scsi", NULL};
		unit = s[2] - 'a';
		hw   = unit / MAX_DRIVES;
		unit = unit % MAX_DRIVES;
		hwif = &ide_hwifs[hw];
		drive = &hwif->drives[unit];
		if (!strncmp(s + 4, "ide-", 4)) {
			strncpy(drive->driver_req, s + 4, 9);
			goto done;
		}
		/*
		 * Look for last lun option:  "hdxlun="
		 */
		if (!strncmp(&s[3], "lun", 3)) {
			if (match_parm(&s[6], NULL, vals, 1) != 1)
				goto bad_option;
			if (vals[0] >= 0 && vals[0] <= 7) {
				drive->last_lun = vals[0];
				drive->forced_lun = 1;
			} else
				printk(" -- BAD LAST LUN! Expected value from 0 to 7");
			goto done;
		}
		switch (match_parm(&s[3], hd_words, vals, 3)) {
			case -1: /* "none" */
				drive->nobios = 1;  /* drop into "noprobe" */
			case -2: /* "noprobe" */
				drive->noprobe = 1;
				goto done;
			case -3: /* "nowerr" */
				drive->bad_wstat = BAD_R_STAT;
				hwif->noprobe = 0;
				goto done;
			case -4: /* "cdrom" */
				drive->present = 1;
				drive->type = ATA_ROM;
				hwif->noprobe = 0;
				goto done;
			case -5: /* "serialize" */
				printk(" -- USE \"ide%d=serialize\" INSTEAD", hw);
				goto do_serialize;
			case -6: /* "autotune" */
				drive->autotune = 1;
				goto done;
			case -7: /* "noautotune" */
				drive->autotune = 2;
				goto done;
			case -8: /* "slow" */
				drive->slow = 1;
				goto done;
			case -9: /* "flash" */
				drive->ata_flash = 1;
				goto done;
			case -10: /* "remap" */
				drive->remap_0_to_1 = 1;
				goto done;
			case -11: /* "noremap" */
				drive->remap_0_to_1 = 2;
				goto done;
			case -12: /* "scsi" */
#if defined(CONFIG_BLK_DEV_IDESCSI) && defined(CONFIG_SCSI)
				drive->scsi = 1;
				goto done;
#else
				drive->scsi = 0;
				goto bad_option;
#endif
			case 3: /* cyl,head,sect */
				drive->type	= ATA_DISK;
				drive->cyl	= drive->bios_cyl  = vals[0];
				drive->head	= drive->bios_head = vals[1];
				drive->sect	= drive->bios_sect = vals[2];
				drive->present	= 1;
				drive->forced_geom = 1;
				hwif->noprobe = 0;
				goto done;
			default:
				goto bad_option;
		}
	}

	/*
	 * Look for bus speed option:  "idebus="
	 */
	if (!strncmp(s, "idebus", 6)) {
		if (match_parm(&s[6], NULL, vals, 1) != 1)
			goto bad_option;
		if (vals[0] >= 20 && vals[0] <= 66) {
			idebus_parameter = vals[0];
		} else
			printk(" -- BAD BUS SPEED! Expected value from 20 to 66");
		goto done;
	}

	/*
	 * Look for interface options:  "idex="
	 */
	if (!strncmp(s, "ide", 3) && s[3] >= '0' && s[3] <= max_hwif) {
		/*
		 * Be VERY CAREFUL changing this: note hardcoded indexes below
		 * -8,-9,-10. -11 : are reserved for future idex calls to ease the hardcoding.
		 */
		const char *ide_words[] = {
			"noprobe", "serialize", "autotune", "noautotune", "reset", "dma", "ata66",
			"minus8", "minus9", "minus10", "minus11",
			"qd65xx", "ht6560b", "cmd640_vlb", "dtc2278", "umc8672", "ali14xx", "dc4030", NULL };
		hw = s[3] - '0';
		hwif = &ide_hwifs[hw];

		i = match_parm(&s[4], ide_words, vals, 3);

		/*
		 * Cryptic check to ensure chipset not already set for hwif:
		 */
		if (i > 0 || i <= -11) {			/* is parameter a chipset name? */
			if (hwif->chipset != ide_unknown)
				goto bad_option;	/* chipset already specified */
			if (i <= -11 && i != -18 && hw != 0)
				goto bad_hwif;		/* chipset drivers are for "ide0=" only */
			if (i <= -11 && i != -18 && ide_hwifs[hw+1].chipset != ide_unknown)
				goto bad_option;	/* chipset for 2nd port already specified */
			printk("\n");
		}

		switch (i) {
#ifdef CONFIG_BLK_DEV_PDC4030
			case -18: /* "dc4030" */
			{
				extern void init_pdc4030(void);
				init_pdc4030();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_PDC4030 */
#ifdef CONFIG_BLK_DEV_ALI14XX
			case -17: /* "ali14xx" */
			{
				extern void init_ali14xx (void);
				init_ali14xx();
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_UMC8672
			case -16: /* "umc8672" */
			{
				extern void init_umc8672 (void);
				init_umc8672();
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_DTC2278
			case -15: /* "dtc2278" */
			{
				extern void init_dtc2278 (void);
				init_dtc2278();
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_CMD640
			case -14: /* "cmd640_vlb" */
			{
				extern int cmd640_vlb; /* flag for cmd640.c */
				cmd640_vlb = 1;
				goto done;
			}
#endif
#ifdef CONFIG_BLK_DEV_HT6560B
			case -13: /* "ht6560b" */
			{
				extern void init_ht6560b (void);
				init_ht6560b();
				goto done;
			}
#endif
#if CONFIG_BLK_DEV_QD65XX
			case -12: /* "qd65xx" */
			{
				extern void init_qd65xx (void);
				init_qd65xx();
				goto done;
			}
#endif
			case -11: /* minus11 */
			case -10: /* minus10 */
			case -9: /* minus9 */
			case -8: /* minus8 */
				goto bad_option;
			case -7: /* ata66 */
#ifdef CONFIG_BLK_DEV_IDEPCI
				hwif->udma_four = 1;
				goto done;
#else
				hwif->udma_four = 0;
				goto bad_hwif;
#endif
			case -6: /* dma */
				hwif->autodma = 1;
				goto done;
			case -5: /* "reset" */
				hwif->reset = 1;
				goto done;
			case -4: /* "noautotune" */
				hwif->drives[0].autotune = 2;
				hwif->drives[1].autotune = 2;
				goto done;
			case -3: /* "autotune" */
				hwif->drives[0].autotune = 1;
				hwif->drives[1].autotune = 1;
				goto done;
			case -2: /* "serialize" */
			do_serialize:
				{
					struct ata_channel *mate;

					mate = &ide_hwifs[hw ^ 1];
					hwif->serialized = 1;
					mate->serialized = 1;
				}
				goto done;

			case -1: /* "noprobe" */
				hwif->noprobe = 1;
				goto done;

			case 1:	/* base */
				vals[1] = vals[0] + 0x206; /* default ctl */
			case 2: /* base,ctl */
				vals[2] = 0;	/* default irq = probe for it */
			case 3: /* base,ctl,irq */
				hwif->hw.irq = vals[2];
				ide_init_hwif_ports(&hwif->hw, (ide_ioreg_t) vals[0], (ide_ioreg_t) vals[1], &hwif->irq);
				memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
				hwif->irq      = vals[2];
				hwif->noprobe  = 0;
				hwif->chipset  = ide_generic;
				goto done;

			case 0: goto bad_option;
			default:
				printk(" -- SUPPORT NOT CONFIGURED IN THIS KERNEL\n");
				return 1;
		}
	}

bad_option:
	printk(" -- BAD OPTION\n");
	return 1;

bad_hwif:
	printk("-- NOT SUPPORTED ON ide%d", hw);

done:
	printk("\n");

	return 1;
}

/****************************************************************************/

/* This is the default end request function as well */
int ide_end_request(ide_drive_t *drive, int uptodate)
{
	return __ide_end_request(drive, uptodate, 0);
}

/*
 * Lookup IDE devices, which requested a particular driver
 */
ide_drive_t *ide_scan_devices(byte type, const char *name, struct ata_operations *driver, int n)
{
	unsigned int unit, index, i;

	for (index = 0, i = 0; index < MAX_HWIFS; ++index) {
		struct ata_channel *hwif = &ide_hwifs[index];
		if (!hwif->present)
			continue;
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			ide_drive_t *drive = &hwif->drives[unit];
			char *req = drive->driver_req;
			if (*req && !strstr(name, req))
				continue;
			if (drive->present && drive->type == type && drive->driver == driver && ++i > n)
				return drive;
		}
	}
	return NULL;
}

/*
 * This is in fact registering a drive not a driver.
 */
int ide_register_subdriver(ide_drive_t *drive, struct ata_operations *driver)
{
	unsigned long flags;

	save_flags(flags);		/* all CPUs */
	cli();				/* all CPUs */
	if (!drive->present || drive->driver != NULL || drive->busy || drive->usage) {
		restore_flags(flags);	/* all CPUs */
		return 1;
	}

	/* FIXME: This will be pushed to the drivers! Thus allowing us to
	 * save one parameter here separate this out.
	 */

	drive->driver = driver;

	restore_flags(flags);		/* all CPUs */
	/* FIXME: Check what this magic number is supposed to be about? */
	if (drive->autotune != 2) {
		if (drive->channel->dmaproc != NULL) {

			/*
			 * Force DMAing for the beginning of the check.  Some
			 * chipsets appear to do interesting things, if not
			 * checked and cleared.
			 *
			 *   PARANOIA!!!
			 */

			drive->channel->dmaproc(ide_dma_off_quietly, drive);
			drive->channel->dmaproc(ide_dma_check, drive);
#ifdef CONFIG_BLK_DEV_IDE_TCQ_DEFAULT
			drive->channel->dmaproc(ide_dma_queued_on, drive);
#endif /* CONFIG_BLK_DEV_IDE_TCQ_DEFAULT */
		}
		/* Only CD-ROMs and tape drives support DSC overlap. */
		drive->dsc_overlap = (drive->next != drive
				&& (drive->type == ATA_ROM || drive->type == ATA_TAPE));
	}
	drive->revalidate = 1;
	drive->suspend_reset = 0;
#ifdef CONFIG_PROC_FS
	ide_add_proc_entries(drive->proc, generic_subdriver_entries, drive);
	if (ata_ops(drive))
		ide_add_proc_entries(drive->proc, ata_ops(drive)->proc, drive);
#endif
	return 0;
}

/*
 * This is in fact the default cleanup routine.
 *
 * FIXME: Check whatever we maybe don't call it twice!.
 */
int ide_unregister_subdriver(ide_drive_t *drive)
{
	unsigned long flags;

	save_flags(flags);		/* all CPUs */
	cli();				/* all CPUs */

#if 0
	if (__MOD_IN_USE(ata_ops(drive)->owner)) {
		restore_flags(flags);
		return 1;
	}
#endif

	if (drive->usage || drive->busy || !ata_ops(drive)) {
		restore_flags(flags);	/* all CPUs */
		return 1;
	}

#if defined(CONFIG_BLK_DEV_ISAPNP) && defined(CONFIG_ISAPNP) && defined(MODULE)
	pnpide_init(0);
#endif
#ifdef CONFIG_PROC_FS
	if (ata_ops(drive))
		ide_remove_proc_entries(drive->proc, ata_ops(drive)->proc);
	ide_remove_proc_entries(drive->proc, generic_subdriver_entries);
#endif
	auto_remove_settings(drive);
	drive->driver = NULL;
	drive->present = 0;

	restore_flags(flags);		/* all CPUs */

	return 0;
}


/*
 * Register an ATA driver for a particular device type.
 */

int register_ata_driver(unsigned int type, struct ata_operations *driver)
{
	return 0;
}

EXPORT_SYMBOL(register_ata_driver);

/*
 * Unregister an ATA driver for a particular device type.
 */

int unregister_ata_driver(unsigned int type, struct ata_operations *driver)
{
	return 0;
}

EXPORT_SYMBOL(unregister_ata_driver);

struct block_device_operations ide_fops[] = {{
	owner:			THIS_MODULE,
	open:			ide_open,
	release:		ide_release,
	ioctl:			ide_ioctl,
	check_media_change:	ide_check_media_change,
	revalidate:		ide_revalidate_disk
}};

EXPORT_SYMBOL(ide_fops);
EXPORT_SYMBOL(ide_hwifs);
EXPORT_SYMBOL(ide_spin_wait_hwgroup);
EXPORT_SYMBOL(revalidate_drives);

/*
 * Probe module
 */
devfs_handle_t ide_devfs_handle;

EXPORT_SYMBOL(ide_lock);
EXPORT_SYMBOL(drive_is_flashcard);
EXPORT_SYMBOL(ide_timer_expiry);
EXPORT_SYMBOL(ide_intr);
EXPORT_SYMBOL(ide_get_queue);
EXPORT_SYMBOL(ide_add_generic_settings);
EXPORT_SYMBOL(do_ide_request);
/*
 * Driver module
 */
EXPORT_SYMBOL(ide_scan_devices);
EXPORT_SYMBOL(ide_register_subdriver);
EXPORT_SYMBOL(ide_unregister_subdriver);
EXPORT_SYMBOL(ide_set_handler);
EXPORT_SYMBOL(ide_dump_status);
EXPORT_SYMBOL(ide_error);
EXPORT_SYMBOL(ide_fixstring);
EXPORT_SYMBOL(ide_wait_stat);
EXPORT_SYMBOL(restart_request);
EXPORT_SYMBOL(ide_init_drive_cmd);
EXPORT_SYMBOL(ide_do_drive_cmd);
EXPORT_SYMBOL(ide_end_drive_cmd);
EXPORT_SYMBOL(__ide_end_request);
EXPORT_SYMBOL(ide_end_request);
EXPORT_SYMBOL(ide_revalidate_disk);
EXPORT_SYMBOL(ide_cmd);
EXPORT_SYMBOL(ide_delay_50ms);
EXPORT_SYMBOL(ide_stall_queue);
#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(ide_add_proc_entries);
EXPORT_SYMBOL(ide_remove_proc_entries);
EXPORT_SYMBOL(proc_ide_read_geometry);
#endif
EXPORT_SYMBOL(ide_add_setting);
EXPORT_SYMBOL(ide_remove_setting);

EXPORT_SYMBOL(ide_register_hw);
EXPORT_SYMBOL(ide_register);
EXPORT_SYMBOL(ide_unregister);
EXPORT_SYMBOL(ide_setup_ports);
EXPORT_SYMBOL(get_info_ptr);

static int ide_notify_reboot (struct notifier_block *this, unsigned long event, void *x)
{
	struct ata_channel *hwif;
	ide_drive_t *drive;
	int i, unit;

	switch (event) {
		case SYS_HALT:
		case SYS_POWER_OFF:
		case SYS_RESTART:
			break;
		default:
			return NOTIFY_DONE;
	}

	printk("flushing ide devices: ");

	for (i = 0; i < MAX_HWIFS; i++) {
		hwif = &ide_hwifs[i];
		if (!hwif->present)
			continue;

		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			drive = &hwif->drives[unit];
			if (!drive->present)
				continue;

			/* set the drive to standby */
			printk("%s ", drive->name);
			if (ata_ops(drive)) {
				if (event != SYS_RESTART)
					if (ata_ops(drive)->standby && ata_ops(drive)->standby(drive))
						continue;

				if (ata_ops(drive)->cleanup)
					ata_ops(drive)->cleanup(drive);
			}
		}
	}
	printk("\n");
	return NOTIFY_DONE;
}

static struct notifier_block ide_notifier = {
	ide_notify_reboot,
	NULL,
	5
};

/*
 * This is the global initialization entry point.
 */
static int __init ata_module_init(void)
{
	int h;

	printk(KERN_INFO "Uniform Multi-Platform E-IDE driver ver.:" VERSION "\n");

	ide_devfs_handle = devfs_mk_dir (NULL, "ide", NULL);

	/* Initialize system bus speed.
	 *
	 * This can be changed by a particular chipse initialization module.
	 * Otherwise we assume 33MHz as a safe value for PCI bus based systems.
	 * 50MHz will be assumed for abolitions like VESA, since higher values
	 * result in more conservative timing setups.
	 *
	 * The kernel parameter idebus=XX overrides the default settings.
	 */

	system_bus_speed = 50;
	if (idebus_parameter)
	    system_bus_speed = idebus_parameter;
#ifdef CONFIG_PCI
	else if (pci_present())
	    system_bus_speed = 33;
#endif

	printk("ide: system bus speed %dMHz\n", system_bus_speed);

	init_ide_data ();

	initializing = 1;

	/*
	 * Detect and initialize "known" IDE host chip types.
	 */
#ifdef CONFIG_PCI
	if (pci_present()) {
# ifdef CONFIG_BLK_DEV_IDEPCI
		ide_scan_pcibus(ide_scan_direction);
# else
#  ifdef CONFIG_BLK_DEV_RZ1000
		ide_probe_for_rz100x();
#  endif
# endif
	}
#endif

#ifdef CONFIG_ETRAX_IDE
	init_e100_ide();
#endif
#ifdef CONFIG_BLK_DEV_CMD640
	ide_probe_for_cmd640x();
#endif
#ifdef CONFIG_BLK_DEV_PDC4030
	ide_probe_for_pdc4030();
#endif
#ifdef CONFIG_BLK_DEV_IDE_PMAC
	pmac_ide_probe();
#endif
#ifdef CONFIG_BLK_DEV_IDE_ICSIDE
	icside_init();
#endif
#ifdef CONFIG_BLK_DEV_IDE_RAPIDE
	rapide_init();
#endif
#ifdef CONFIG_BLK_DEV_GAYLE
	gayle_init();
#endif
#ifdef CONFIG_BLK_DEV_FALCON_IDE
	falconide_init();
#endif
#ifdef CONFIG_BLK_DEV_MAC_IDE
	macide_init();
#endif
#ifdef CONFIG_BLK_DEV_Q40IDE
	q40ide_init();
#endif
#ifdef CONFIG_BLK_DEV_BUDDHA
	buddha_init();
#endif
#if defined(CONFIG_BLK_DEV_ISAPNP) && defined(CONFIG_ISAPNP)
	pnpide_init(1);
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
# if defined(__mc68000__) || defined(CONFIG_APUS)
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET]) {
		ide_get_lock(&ide_intr_lock, NULL, NULL);/* for atari only */
		disable_irq(ide_hwifs[0].irq);	/* disable_irq_nosync ?? */
//		disable_irq_nosync(ide_hwifs[0].irq);
	}
# endif

	ideprobe_init();

# if defined(__mc68000__) || defined(CONFIG_APUS)
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET]) {
		enable_irq(ide_hwifs[0].irq);
		ide_release_lock(&ide_intr_lock);/* for atari only */
	}
# endif
#endif

#ifdef CONFIG_PROC_FS
	proc_ide_create();
#endif

	/*
	 * Initialize all device type driver modules.
	 */
#ifdef CONFIG_BLK_DEV_IDEDISK
	idedisk_init();
#endif
#ifdef CONFIG_BLK_DEV_IDECD
	ide_cdrom_init();
#endif
#ifdef CONFIG_BLK_DEV_IDETAPE
	idetape_init();
#endif
#ifdef CONFIG_BLK_DEV_IDEFLOPPY
	idefloppy_init();
#endif
#ifdef CONFIG_BLK_DEV_IDESCSI
# ifdef CONFIG_SCSI
	idescsi_init();
# else
   #warning ATA SCSI emulation selected but no SCSI-subsystem in kernel
# endif
#endif

	initializing = 0;

	for (h = 0; h < MAX_HWIFS; ++h) {
		struct ata_channel *channel = &ide_hwifs[h];
		if (channel->present)
			ide_geninit(channel);
	}

	register_reboot_notifier(&ide_notifier);

	return 0;
}

static char *options = NULL;
MODULE_PARM(options,"s");
MODULE_LICENSE("GPL");

static void __init parse_options (char *line)
{
	char *next = line;

	if (line == NULL || !*line)
		return;
	while ((line = next) != NULL) {
		if ((next = strchr(line,' ')) != NULL)
			*next++ = 0;
		if (!ide_setup(line))
			printk ("Unknown option '%s'\n", line);
	}
}

static int __init init_ata (void)
{
	parse_options(options);
	return ata_module_init();
}

static void __exit cleanup_ata (void)
{
	int h;

	unregister_reboot_notifier(&ide_notifier);
	for (h = 0; h < MAX_HWIFS; ++h) {
		ide_unregister(&ide_hwifs[h]);
	}

# ifdef CONFIG_PROC_FS
	proc_ide_destroy();
# endif
	devfs_unregister(ide_devfs_handle);
}

module_init(init_ata);
module_exit(cleanup_ata);

#ifndef MODULE

/* command line option parser */
__setup("", ide_setup);

#endif
