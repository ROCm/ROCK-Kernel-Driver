/*
 *  linux/drivers/ide/ide.c		Version 7.00alpha1	August 19 2002
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
 */

/*
 *  Mostly written by Mark Lord  <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the multiple IDE interface driver, as evolved from hd.c.
 * It supports up to MAX_HWIFS IDE interfaces, on one or more IRQs
 *   (usually 14 & 15).
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
 * Version 7.00alpha	First named revision of ide rearrange
 *
 *  Some additional driver compile-time options are in ./include/linux/ide.h
 *
 *  To do, in likely order of completion:
 *	- modify kernel to obtain BIOS geometry for drives on 2nd/3rd/4th i/f
 *
 */

#define	REVISION	"Revision: 7.00alpha2"
#define	VERSION		"Id: ide.c 7.00a2 20020906"

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#define _IDE_C			/* Tell ide.h it's really us */

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
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/cdrom.h>
#include <linux/seq_file.h>
#include <linux/device.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#include "ide_modes.h"

#include <linux/kmod.h>

/* default maximum number of failures */
#define IDE_DEFAULT_MAX_FAILURES 	1

static const u8 ide_hwif_to_major[] = { IDE0_MAJOR, IDE1_MAJOR,
					IDE2_MAJOR, IDE3_MAJOR,
					IDE4_MAJOR, IDE5_MAJOR,
					IDE6_MAJOR, IDE7_MAJOR,
					IDE8_MAJOR, IDE9_MAJOR };

static int idebus_parameter;	/* holds the "idebus=" parameter */
static int system_bus_speed;	/* holds what we think is VESA/PCI bus speed */
static int initializing;	/* set while initializing built-in drivers */

spinlock_t ide_lock __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;

static int ide_scan_direction; /* THIS was formerly 2.2.x pci=reverse */

#if defined(__mc68000__) || defined(CONFIG_APUS)
/*
 * ide_lock is used by the Atari code to obtain access to the IDE interrupt,
 * which is shared between several drivers.
 */
static int	ide_intr_lock;
#endif /* __mc68000__ || CONFIG_APUS */

#ifdef CONFIG_IDEDMA_AUTO
int noautodma = 0;
#else
int noautodma = 1;
#endif

EXPORT_SYMBOL(noautodma);

/*
 * ide_modules keeps track of the available IDE chipset/probe/driver modules.
 */
ide_module_t *ide_chipsets;
ide_module_t *ide_probe;

/*
 * This is declared extern in ide.h, for access by other IDE modules:
 */
ide_hwif_t ide_hwifs[MAX_HWIFS];	/* master data repository */

EXPORT_SYMBOL(ide_hwifs);

ide_devices_t *idedisk;
ide_devices_t *idecd;
ide_devices_t *idefloppy;
ide_devices_t *idetape;
ide_devices_t *idescsi;

EXPORT_SYMBOL(idedisk);
EXPORT_SYMBOL(idecd);
EXPORT_SYMBOL(idefloppy);
EXPORT_SYMBOL(idetape);
EXPORT_SYMBOL(idescsi);

#if (DISK_RECOVERY_TIME > 0)

Error So the User Has To Fix the Compilation And Stop Hacking Port 0x43
Does anyone ever use this anyway ??

/*
 * For really screwy hardware (hey, at least it *can* be used with Linux)
 * we can enforce a minimum delay time between successive operations.
 */
static unsigned long read_timer (ide_hwif_t *hwif)
{
	unsigned long t, flags;
	int i;
	
	/* FIXME this is completely unsafe! */
	local_irq_save(flags);
	t = jiffies * 11932;
	outb_p(0, 0x43);
	i = inb_p(0x40);
	i |= inb_p(0x40) << 8;
	local_irq_restore(flags);
	return (t - i);
}
#endif /* DISK_RECOVERY_TIME */

static inline void set_recovery_timer (ide_hwif_t *hwif)
{
#if (DISK_RECOVERY_TIME > 0)
	hwif->last_time = read_timer(hwif);
#endif /* DISK_RECOVERY_TIME */
}

/*
 * Do not even *think* about calling this!
 */
static void init_hwif_data (unsigned int index)
{
	unsigned int unit;
	hw_regs_t hw;
	ide_hwif_t *hwif = &ide_hwifs[index];

	/* bulk initialize hwif & drive info with zeros */
	memset(hwif, 0, sizeof(ide_hwif_t));
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
	hwif->major	= ide_hwif_to_major[index];
	hwif->name[0]	= 'i';
	hwif->name[1]	= 'd';
	hwif->name[2]	= 'e';
	hwif->name[3]	= '0' + index;
	hwif->bus_state = BUSSTATE_ON;
	hwif->reset_poll= NULL;
	hwif->pre_reset = NULL;

	hwif->atapi_dma = 0;		/* disable all atapi dma */ 
	hwif->ultra_mask = 0x80;	/* disable all ultra */
	hwif->mwdma_mask = 0x80;	/* disable all mwdma */
	hwif->swdma_mask = 0x80;	/* disable all swdma */

	default_hwif_iops(hwif);
	default_hwif_transport(hwif);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		drive->media			= ide_disk;
		drive->select.all		= (unit<<4)|0xa0;
		drive->hwif			= hwif;
		drive->ctl			= 0x08;
		drive->ready_stat		= READY_STAT;
		drive->bad_wstat		= BAD_W_STAT;
		drive->special.b.recalibrate	= 1;
		drive->special.b.set_geometry	= 1;
		drive->name[0]			= 'h';
		drive->name[1]			= 'd';
		drive->name[2]			= 'a' + (index * MAX_DRIVES) + unit;
		drive->max_failures		= IDE_DEFAULT_MAX_FAILURES;
		drive->using_dma		= 0;
		drive->is_flash			= 0;
		INIT_LIST_HEAD(&drive->list);
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
 *
 * FIXME: We should stuff the setup data into __init and copy the
 * relevant hwifs/allocate them properly during boot.
 */
#define MAGIC_COOKIE 0x12345678
static void __init init_ide_data (void)
{
	unsigned int index;
	static unsigned long magic_cookie = MAGIC_COOKIE;

	if (magic_cookie != MAGIC_COOKIE)
		return;		/* already initialized */
	magic_cookie = 0;

	/* Initialise all interface structures */
	for (index = 0; index < MAX_HWIFS; ++index)
		init_hwif_data(index);

	/* Add default hw interfaces */
	ide_init_default_hwifs();

	idebus_parameter = 0;
	system_bus_speed = 0;
}

/*
 * ide_system_bus_speed() returns what we think is the system VESA/PCI
 * bus speed (in MHz).  This is used for calculating interface PIO timings.
 * The default is 40 for known PCI systems, 50 otherwise.
 * The "idebus=xx" parameter can be used to override this value.
 * The actual value to be used is computed/displayed the first time through.
 */
int ide_system_bus_speed (void)
{
	if (!system_bus_speed) {
		if (idebus_parameter) {
			/* user supplied value */
			system_bus_speed = idebus_parameter;
		} else if (pci_present()) {
			/* safe default value for PCI */
			system_bus_speed = 33;
		} else {
			/* safe default value for VESA and PCI */
			system_bus_speed = 50;
		}
		printk("ide: Assuming %dMHz system bus speed "
			"for PIO modes%s\n", system_bus_speed,
			idebus_parameter ? "" : "; override with idebus=xx");
	}
	return system_bus_speed;
}

/*
 * This is our end_request replacement function.
 */
int ide_end_request (ide_drive_t *drive, int uptodate, int nr_sectors)
{
	struct request *rq;
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&ide_lock, flags);
	rq = HWGROUP(drive)->rq;

	BUG_ON(!(rq->flags & REQ_STARTED));

	if (!nr_sectors)
		nr_sectors = rq->hard_cur_sectors;

	/*
	 * decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO
	 */
	if (drive->state == DMA_PIO_RETRY && drive->retry_pio <= 3) {
		drive->state = 0;
		HWGROUP(drive)->hwif->ide_dma_on(drive);
	}

	if (!end_that_request_first(rq, uptodate, nr_sectors)) {
		add_blkdev_randomness(major(rq->rq_dev));
		if (!blk_rq_tagged(rq))
			blkdev_dequeue_request(rq);
		else
			blk_queue_end_tag(&drive->queue, rq);
		HWGROUP(drive)->rq = NULL;
		end_that_request_last(rq);
		ret = 0;
	}
	spin_unlock_irqrestore(&ide_lock, flags);
	return ret;
}

EXPORT_SYMBOL(ide_end_request);

/*
 * current_capacity() returns the capacity (in sectors) of a drive
 * according to its current geometry/LBA settings.
 */
unsigned long current_capacity (ide_drive_t *drive)
{
	if (!drive->present)
		return 0;
	if (drive->driver != NULL)
		return DRIVER(drive)->capacity(drive);
	return 0;
}

EXPORT_SYMBOL(current_capacity);

static inline u32 read_24 (ide_drive_t *drive)
{
	return  (HWIF(drive)->INB(IDE_HCYL_REG)<<16) |
		(HWIF(drive)->INB(IDE_LCYL_REG)<<8) |
		 HWIF(drive)->INB(IDE_SECTOR_REG);
}

/*
 * Clean up after success/failure of an explicit drive cmd
 */
void ide_end_drive_cmd (ide_drive_t *drive, u8 stat, u8 err)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	struct request *rq;

	spin_lock_irqsave(&ide_lock, flags);
	rq = HWGROUP(drive)->rq;
	spin_unlock_irqrestore(&ide_lock, flags);

	if (rq->flags & REQ_DRIVE_CMD) {
		u8 *args = (u8 *) rq->buffer;
		if (rq->errors == 0)
			rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);

		if (args) {
			args[0] = stat;
			args[1] = err;
			args[2] = hwif->INB(IDE_NSECTOR_REG);
		}
	} else if (rq->flags & REQ_DRIVE_TASK) {
		u8 *args = (u8 *) rq->buffer;
		if (rq->errors == 0)
			rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);

		if (args) {
			args[0] = stat;
			args[1] = err;
			args[2] = hwif->INB(IDE_NSECTOR_REG);
			args[3] = hwif->INB(IDE_SECTOR_REG);
			args[4] = hwif->INB(IDE_LCYL_REG);
			args[5] = hwif->INB(IDE_HCYL_REG);
			args[6] = hwif->INB(IDE_SELECT_REG);
		}
	} else if (rq->flags & REQ_DRIVE_TASKFILE) {
		ide_task_t *args = (ide_task_t *) rq->special;
		if (rq->errors == 0)
			rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);
			
		if (args) {
			if (args->tf_in_flags.b.data) {
				u16 data				= hwif->INW(IDE_DATA_REG);
				args->tfRegister[IDE_DATA_OFFSET]	= (data) & 0xFF;
				args->hobRegister[IDE_DATA_OFFSET_HOB]	= (data >> 8) & 0xFF;
			}
			args->tfRegister[IDE_ERROR_OFFSET]   = err;
			args->tfRegister[IDE_NSECTOR_OFFSET] = hwif->INB(IDE_NSECTOR_REG);
			args->tfRegister[IDE_SECTOR_OFFSET]  = hwif->INB(IDE_SECTOR_REG);
			args->tfRegister[IDE_LCYL_OFFSET]    = hwif->INB(IDE_LCYL_REG);
			args->tfRegister[IDE_HCYL_OFFSET]    = hwif->INB(IDE_HCYL_REG);
			args->tfRegister[IDE_SELECT_OFFSET]  = hwif->INB(IDE_SELECT_REG);
			args->tfRegister[IDE_STATUS_OFFSET]  = stat;

			if (drive->addressing == 1) {
				hwif->OUTB(drive->ctl|0x80, IDE_CONTROL_REG_HOB);
				args->hobRegister[IDE_FEATURE_OFFSET_HOB] = hwif->INB(IDE_FEATURE_REG);
				args->hobRegister[IDE_NSECTOR_OFFSET_HOB] = hwif->INB(IDE_NSECTOR_REG);
				args->hobRegister[IDE_SECTOR_OFFSET_HOB]  = hwif->INB(IDE_SECTOR_REG);
				args->hobRegister[IDE_LCYL_OFFSET_HOB]    = hwif->INB(IDE_LCYL_REG);
				args->hobRegister[IDE_HCYL_OFFSET_HOB]    = hwif->INB(IDE_HCYL_REG);
			}
		}
	}

	spin_lock_irqsave(&ide_lock, flags);
	blkdev_dequeue_request(rq);
	HWGROUP(drive)->rq = NULL;
	end_that_request_last(rq);
	spin_unlock_irqrestore(&ide_lock, flags);
}

EXPORT_SYMBOL(ide_end_drive_cmd);

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
u8 ide_dump_status (ide_drive_t *drive, const char *msg, u8 stat)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	u8 err = 0;

	local_irq_set(flags);
	printk("%s: %s: status=0x%02x", drive->name, msg, stat);
#if FANCY_STATUS_DUMPS
	printk(" { ");
	if (stat & BUSY_STAT) {
		printk("Busy ");
	} else {
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
		err = hwif->INB(IDE_ERROR_REG);
		printk("%s: %s: error=0x%02x", drive->name, msg, err);
#if FANCY_STATUS_DUMPS
		if (drive->media == ide_disk) {
			printk(" { ");
			if (err & ABRT_ERR)	printk("DriveStatusError ");
			if (err & ICRC_ERR)	printk("Bad%s ", (err & ABRT_ERR) ? "CRC" : "Sector");
			if (err & ECC_ERR)	printk("UncorrectableError ");
			if (err & ID_ERR)	printk("SectorIdNotFound ");
			if (err & TRK0_ERR)	printk("TrackZeroNotFound ");
			if (err & MARK_ERR)	printk("AddrMarkNotFound ");
			printk("}");
			if ((err & (BBD_ERR | ABRT_ERR)) == BBD_ERR || (err & (ECC_ERR|ID_ERR|MARK_ERR))) {
				if ((drive->id->command_set_2 & 0x0400) &&
				    (drive->id->cfs_enable_2 & 0x0400) &&
				    (drive->addressing == 1)) {
					u64 sectors = 0;
					u32 high = 0;
					u32 low = read_24(drive);
					hwif->OUTB(drive->ctl|0x80, IDE_CONTROL_REG);
					high = read_24(drive);

					sectors = ((u64)high << 24) | low;
					printk(", LBAsect=%llu, high=%d, low=%d",
					       (long long) sectors,
					       high, low);
				} else {
					u8 cur = hwif->INB(IDE_SELECT_REG);
					if (cur & 0x40) {	/* using LBA? */
						printk(", LBAsect=%ld", (unsigned long)
						 ((cur&0xf)<<24)
						 |(hwif->INB(IDE_HCYL_REG)<<16)
						 |(hwif->INB(IDE_LCYL_REG)<<8)
						 | hwif->INB(IDE_SECTOR_REG));
					} else {
						printk(", CHS=%d/%d/%d",
						 (hwif->INB(IDE_HCYL_REG)<<8) +
						  hwif->INB(IDE_LCYL_REG),
						  cur & 0xf,
						  hwif->INB(IDE_SECTOR_REG));
					}
				}
				if (HWGROUP(drive) && HWGROUP(drive)->rq)
					printk(", sector=%llu", (unsigned long long)HWGROUP(drive)->rq->sector);
			}
		}
#endif	/* FANCY_STATUS_DUMPS */
		printk("\n");
	}
	local_irq_restore(flags);
	return err;
}

EXPORT_SYMBOL(ide_dump_status);

/*
 * try_to_flush_leftover_data() is invoked in response to a drive
 * unexpectedly having its DRQ_STAT bit set.  As an alternative to
 * resetting the drive, this routine tries to clear the condition
 * by read a sector's worth of data from the drive.  Of course,
 * this may not help if the drive is *waiting* for data from *us*.
 */
void try_to_flush_leftover_data (ide_drive_t *drive)
{
	int i = (drive->mult_count ? drive->mult_count : 1) * SECTOR_WORDS;

	if (drive->media != ide_disk)
		return;
	while (i > 0) {
		u32 buffer[16];
		u32 wcount = (i > 16) ? 16 : i;

		i -= wcount;
		HWIF(drive)->ata_input_data(drive, buffer, wcount);
	}
}

EXPORT_SYMBOL(try_to_flush_leftover_data);

/*
 * FIXME Add an ATAPI error
 */

/*
 * ide_error() takes action based on the error returned by the drive.
 */
ide_startstop_t ide_error (ide_drive_t *drive, const char *msg, u8 stat)
{
	ide_hwif_t *hwif;
	struct request *rq;
	u8 err;

	err = ide_dump_status(drive, msg, stat);
	if (drive == NULL || (rq = HWGROUP(drive)->rq) == NULL)
		return ide_stopped;

	hwif = HWIF(drive);
	/* retry only "normal" I/O: */
	if (rq->flags & (REQ_DRIVE_CMD | REQ_DRIVE_TASK)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, stat, err);
		return ide_stopped;
	}
	if (rq->flags & REQ_DRIVE_TASKFILE) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, stat, err);
//		ide_end_taskfile(drive, stat, err);
		return ide_stopped;
	}

	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr)) {
		 /* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else {
		if (drive->media != ide_disk)
			goto media_out;

		if (stat & ERR_STAT) {
			/* err has different meaning on cdrom and tape */
			if (err == ABRT_ERR) {
				if (drive->select.b.lba &&
				    (hwif->INB(IDE_COMMAND_REG) == WIN_SPECIFY))
					/* some newer drives don't
					 * support WIN_SPECIFY
					 */
					return ide_stopped;
			} else if ((err & BAD_CRC) == BAD_CRC) {
				drive->crc_count++;
				/* UDMA crc error -- just retry the operation */
			} else if (err & (BBD_ERR | ECC_ERR)) {
				/* retries won't help these */
				rq->errors = ERROR_MAX;
			} else if (err & TRK0_ERR) {
				/* help it find track zero */
				rq->errors |= ERROR_RECAL;
			}
		}
media_out:
		if ((stat & DRQ_STAT) && rq_data_dir(rq) != WRITE)
			try_to_flush_leftover_data(drive);
	}
	if (hwif->INB(IDE_STATUS_REG) & (BUSY_STAT|DRQ_STAT)) {
		/* force an abort */
		hwif->OUTB(WIN_IDLEIMMEDIATE,IDE_COMMAND_REG);
	}
	if (rq->errors >= ERROR_MAX) {
		if (drive->driver != NULL)
			DRIVER(drive)->end_request(drive, 0, 0);
		else
	 		ide_end_request(drive, 0, 0);
	} else {
		if ((rq->errors & ERROR_RESET) == ERROR_RESET) {
			++rq->errors;
			return ide_do_reset(drive);
		}
		if ((rq->errors & ERROR_RECAL) == ERROR_RECAL)
			drive->special.b.recalibrate = 1;
		++rq->errors;
	}
	return ide_stopped;
}

EXPORT_SYMBOL(ide_error);

/*
 * Issue a simple drive command
 * The drive must be selected beforehand.
 */
void ide_cmd (ide_drive_t *drive, u8 cmd, u8 nsect, ide_handler_t *handler)
{
	ide_hwif_t *hwif = HWIF(drive);
	if (HWGROUP(drive)->handler != NULL)
		BUG();
	ide_set_handler(drive, handler, WAIT_CMD, NULL);
	if (IDE_CONTROL_REG)
		hwif->OUTB(drive->ctl,IDE_CONTROL_REG);	/* clear nIEN */
	SELECT_MASK(drive,0);
	hwif->OUTB(nsect,IDE_NSECTOR_REG);
	hwif->OUTB(cmd,IDE_COMMAND_REG);
}

EXPORT_SYMBOL(ide_cmd);

/*
 * drive_cmd_intr() is invoked on completion of a special DRIVE_CMD.
 */
ide_startstop_t drive_cmd_intr (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	ide_hwif_t *hwif = HWIF(drive);
	u8 *args = (u8 *) rq->buffer;
	u8 stat = hwif->INB(IDE_STATUS_REG);
	int retries = 10;

	local_irq_enable();
	if ((stat & DRQ_STAT) && args && args[3]) {
		u8 io_32bit = drive->io_32bit;
		drive->io_32bit = 0;
		hwif->ata_input_data(drive, &args[4], args[3] * SECTOR_WORDS);
		drive->io_32bit = io_32bit;
		while (((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) && retries--)
			udelay(100);
	}

	if (!OK_STAT(stat, READY_STAT, BAD_STAT))
		return DRIVER(drive)->error(drive, "drive_cmd", stat);
		/* calls ide_end_drive_cmd */
	ide_end_drive_cmd(drive, stat, hwif->INB(IDE_ERROR_REG));
	return ide_stopped;
}

EXPORT_SYMBOL(drive_cmd_intr);

/*
 * do_special() is used to issue WIN_SPECIFY, WIN_RESTORE, and WIN_SETMULT
 * commands to a drive.  It used to do much more, but has been scaled back.
 */
ide_startstop_t do_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

#ifdef DEBUG
	printk("%s: do_special: 0x%02x\n", drive->name, s->all);
#endif
	if (s->b.set_tune) {
		s->b.set_tune = 0;
		if (HWIF(drive)->tuneproc != NULL)
			HWIF(drive)->tuneproc(drive, drive->tune_req);
	} else if (drive->driver != NULL) {
		return DRIVER(drive)->special(drive);
	} else if (s->all) {
		printk("%s: bad special flag: 0x%02x\n", drive->name, s->all);
		s->all = 0;
	}
	return ide_stopped;
}

EXPORT_SYMBOL(do_special);

/*
 * execute_drive_cmd() issues a special drive command,
 * usually initiated by ioctl() from the external hdparm program.
 */
ide_startstop_t execute_drive_cmd (ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = HWIF(drive);
	if (rq->flags & REQ_DRIVE_TASKFILE) {
 		ide_task_t *args = rq->special;
 
		if (!args)
			goto done;
 
		if (args->tf_out_flags.all != 0) 
			return flagged_taskfile(drive, args);
		return do_rw_taskfile(drive, args);
	} else if (rq->flags & REQ_DRIVE_TASK) {
		u8 *args = rq->buffer;
		u8 sel;
 
		if (!args)
			goto done;
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
 		hwif->OUTB(args[1], IDE_FEATURE_REG);
 		hwif->OUTB(args[3], IDE_SECTOR_REG);
 		hwif->OUTB(args[4], IDE_LCYL_REG);
 		hwif->OUTB(args[5], IDE_HCYL_REG);
 		sel = (args[6] & ~0x10);
 		if (drive->select.b.unit)
 			sel |= 0x10;
 		hwif->OUTB(sel, IDE_SELECT_REG);
 		ide_cmd(drive, args[0], args[2], &drive_cmd_intr);
 		return ide_started;
 	} else if (rq->flags & REQ_DRIVE_CMD) {
 		u8 *args = rq->buffer;

		if (!args)
			goto done;
#ifdef DEBUG
 		printk("%s: DRIVE_CMD ", drive->name);
 		printk("cmd=0x%02x ", args[0]);
 		printk("sc=0x%02x ", args[1]);
 		printk("fr=0x%02x ", args[2]);
 		printk("xx=0x%02x\n", args[3]);
#endif
 		if (args[0] == WIN_SMART) {
 			hwif->OUTB(0x4f, IDE_LCYL_REG);
 			hwif->OUTB(0xc2, IDE_HCYL_REG);
 			hwif->OUTB(args[2],IDE_FEATURE_REG);
 			hwif->OUTB(args[1],IDE_SECTOR_REG);
 			ide_cmd(drive, args[0], args[3], &drive_cmd_intr);
 			return ide_started;
 		}
 		hwif->OUTB(args[2],IDE_FEATURE_REG);
 		ide_cmd(drive, args[0], args[1], &drive_cmd_intr);
 		return ide_started;
 	}

done:
 	/*
 	 * NULL is actually a valid way of waiting for
 	 * all current requests to be flushed from the queue.
 	 */
#ifdef DEBUG
 	printk("%s: DRIVE_CMD (null)\n", drive->name);
#endif
 	ide_end_drive_cmd(drive,
			hwif->INB(IDE_STATUS_REG),
			hwif->INB(IDE_ERROR_REG));
 	return ide_stopped;
}

EXPORT_SYMBOL(execute_drive_cmd);

/*
 * start_request() initiates handling of a new I/O request
 * needed to reverse the perverted changes anonymously made back
 * 2.3.99-pre6
 */
ide_startstop_t start_request (ide_drive_t *drive, struct request *rq)
{
	ide_startstop_t startstop;
	unsigned long block;
	unsigned int minor = minor(rq->rq_dev), unit = minor >> PARTN_BITS;
	ide_hwif_t *hwif = HWIF(drive);

	BUG_ON(!(rq->flags & REQ_STARTED));

#ifdef DEBUG
	printk("%s: start_request: current=0x%08lx\n",
		hwif->name, (unsigned long) rq);
#endif

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		goto kill_rq;
	}

	/*
	 * bail early if we've sent a device to sleep, however how to wake
	 * this needs to be a masked flag.  FIXME for proper operations.
	 */
	if (drive->suspend_reset) {
		goto kill_rq;
	}

	if (unit >= MAX_DRIVES) {
		printk("%s: bad device number: %s\n",
			hwif->name, kdevname(rq->rq_dev));
		goto kill_rq;
	}

	block    = rq->sector;
	if (blk_fs_request(rq) &&
	    (drive->media == ide_disk || drive->media == ide_floppy)) {
		block += drive->sect0;
	}
	/* Yecch - this will shift the entire interval,
	   possibly killing some innocent following sector */
	if (block == 0 && drive->remap_0_to_1 == 1)
		block = 1;  /* redirect MBR access to EZ-Drive partn table */

#if (DISK_RECOVERY_TIME > 0)
	while ((read_timer() - hwif->last_time) < DISK_RECOVERY_TIME);
#endif

	SELECT_DRIVE(drive);
	if (ide_wait_stat(&startstop, drive, drive->ready_stat, BUSY_STAT|DRQ_STAT, WAIT_READY)) {
		printk("%s: drive not ready for command\n", drive->name);
		return startstop;
	}
	if (!drive->special.all) {
		if (rq->flags & (REQ_DRIVE_CMD | REQ_DRIVE_TASK))
			return execute_drive_cmd(drive, rq);
		else if (rq->flags & REQ_DRIVE_TASKFILE)
			return execute_drive_cmd(drive, rq);

		if (drive->driver != NULL) {
			return (DRIVER(drive)->do_request(drive, rq, block));
		}
		printk("%s: media type %d not supported\n", drive->name, drive->media);
		goto kill_rq;
	}
	return do_special(drive);
kill_rq:
	if (drive->driver != NULL)
		DRIVER(drive)->end_request(drive, 0, 0);
	else
		ide_end_request(drive, 0, 0);
	return ide_stopped;
}

EXPORT_SYMBOL(start_request);

int restart_request (ide_drive_t *drive, struct request *rq)
{
	(void) start_request(drive, rq);
	return 0;
}

EXPORT_SYMBOL(restart_request);

/*
 * ide_stall_queue() can be used by a drive to give excess bandwidth back
 * to the hwgroup by sleeping for timeout jiffies.
 */
void ide_stall_queue (ide_drive_t *drive, unsigned long timeout)
{
	if (timeout > WAIT_WORSTCASE)
		timeout = WAIT_WORSTCASE;
	drive->sleep = timeout + jiffies;
}

EXPORT_SYMBOL(ide_stall_queue);

#define WAKEUP(drive)	((drive)->service_start + 2 * (drive)->service_time)

/*
 * choose_drive() selects the next drive which will be serviced.
 */
static inline ide_drive_t *choose_drive (ide_hwgroup_t *hwgroup)
{
	ide_drive_t *drive, *best;

repeat:	
	best = NULL;
	drive = hwgroup->drive;
	do {
		if (!blk_queue_empty(&drive->queue) && (!drive->sleep || time_after_eq(jiffies, drive->sleep))) {
			if (!best
			 || (drive->sleep && (!best->sleep || 0 < (signed long)(best->sleep - drive->sleep)))
			 || (!best->sleep && 0 < (signed long)(WAKEUP(best) - WAKEUP(drive))))
			{
				if (!blk_queue_plugged(&drive->queue))
					best = drive;
			}
		}
	} while ((drive = drive->next) != hwgroup->drive);
	if (best && best->nice1 && !best->sleep && best != hwgroup->drive && best->service_time > WAIT_MIN_SLEEP) {
		long t = (signed long)(WAKEUP(best) - jiffies);
		if (t >= WAIT_MIN_SLEEP) {
		/*
		 * We *may* have some time to spare, but first let's see if
		 * someone can potentially benefit from our nice mood today..
		 */
			drive = best->next;
			do {
				if (!drive->sleep
				 && 0 < (signed long)(WAKEUP(drive) - (jiffies - best->service_time))
				 && 0 < (signed long)((jiffies + t) - WAKEUP(drive)))
				{
					ide_stall_queue(best, IDE_MIN(t, 10 * WAIT_MIN_SLEEP));
					goto repeat;
				}
			} while ((drive = drive->next) != best);
		}
	}
	return best;
}

/*
 * Issue a new request to a drive from hwgroup
 * Caller must have already done spin_lock_irqsave(&ide_lock, ..);
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
 * The IDE driver uses the single global ide_lock spinlock to protect
 * access to the request queues, and to protect the hwgroup->busy flag.
 *
 * The first thread into the driver for a particular hwgroup sets the
 * hwgroup->busy flag to indicate that this hwgroup is now active,
 * and then initiates processing of the top request from the request queue.
 *
 * Other threads attempting entry notice the busy setting, and will simply
 * queue their new requests and exit immediately.  Note that hwgroup->busy
 * remains set even when the driver is merely awaiting the next interrupt.
 * Thus, the meaning is "this hwgroup is busy processing a request".
 *
 * When processing of a request completes, the completing thread or IRQ-handler
 * will start the next request from the queue.  If no more work remains,
 * the driver will clear the hwgroup->busy flag and exit.
 *
 * The ide_lock (spinlock) is used to protect all access to the
 * hwgroup->busy flag, but is otherwise not needed for most processing in
 * the driver.  This makes the driver much more friendlier to shared IRQs
 * than previous designs, while remaining 100% (?) SMP safe and capable.
 */
/* --BenH: made non-static as ide-pmac.c uses it to kick the hwgroup back
 *         into life on wakeup from machine sleep.
 */ 
void ide_do_request (ide_hwgroup_t *hwgroup, int masked_irq)
{
	ide_drive_t	*drive;
	ide_hwif_t	*hwif;
	struct request	*rq;
	ide_startstop_t	startstop;

	/* for atari only: POSSIBLY BROKEN HERE(?) */
	ide_get_lock(&ide_intr_lock, ide_intr, hwgroup);

	/* necessary paranoia: ensure IRQs are masked on local CPU */
	local_irq_disable();

	while (!hwgroup->busy) {
		hwgroup->busy = 1;
		drive = choose_drive(hwgroup);
		if (drive == NULL) {
			unsigned long sleep = 0;
			hwgroup->rq = NULL;
			drive = hwgroup->drive;
			do {
				if (drive->sleep && (!sleep || 0 < (signed long)(sleep - drive->sleep)))
					sleep = drive->sleep;
			} while ((drive = drive->next) != hwgroup->drive);
			if (sleep) {
		/*
		 * Take a short snooze, and then wake up this hwgroup again.
		 * This gives other hwgroups on the same a chance to
		 * play fairly with us, just in case there are big differences
		 * in relative throughputs.. don't want to hog the cpu too much.
		 */
				if (time_before(sleep, jiffies + WAIT_MIN_SLEEP))
					sleep = jiffies + WAIT_MIN_SLEEP;
#if 1
				if (timer_pending(&hwgroup->timer))
					printk("ide_set_handler: timer already active\n");
#endif
				/* so that ide_timer_expiry knows what to do */
				hwgroup->sleeping = 1;
				mod_timer(&hwgroup->timer, sleep);
				/* we purposely leave hwgroup->busy==1
				 * while sleeping */
			} else {
				/* Ugly, but how can we sleep for the lock
				 * otherwise? perhaps from tq_disk?
				 */

				/* for atari only */
				ide_release_lock(&ide_lock);
				hwgroup->busy = 0;
			}

			/* no more work for this hwgroup (for now) */
			return;
		}
		hwif = HWIF(drive);
		if (hwgroup->hwif->sharing_irq &&
		    hwif != hwgroup->hwif &&
		    hwif->io_ports[IDE_CONTROL_OFFSET]) {
			/* set nIEN for previous hwif */
			SELECT_INTERRUPT(drive);
		}
		hwgroup->hwif = hwif;
		hwgroup->drive = drive;
		drive->sleep = 0;
		drive->service_start = jiffies;

queue_next:
		if (!ata_can_queue(drive)) {
			if (!ata_pending_commands(drive))
				hwgroup->busy = 0;

			break;
		}

		if (blk_queue_plugged(&drive->queue)) {
			if (drive->using_tcq)
				break;

			printk("ide: huh? queue was plugged!\n");
			break;
		}

		rq = elv_next_request(&drive->queue);
		if (!rq)
			break;

		if (!rq->bio && ata_pending_commands(drive))
			break;

		hwgroup->rq = rq;

		/*
		 * Some systems have trouble with IDE IRQs arriving while
		 * the driver is still setting things up.  So, here we disable
		 * the IRQ used by this interface while the request is being started.
		 * This may look bad at first, but pretty much the same thing
		 * happens anyway when any interrupt comes in, IDE or otherwise
		 *  -- the kernel masks the IRQ while it is being handled.
		 */
		if (masked_irq && hwif->irq != masked_irq)
			disable_irq_nosync(hwif->irq);
		spin_unlock(&ide_lock);
		local_irq_enable();
			/* allow other IRQs while we start this request */
		startstop = start_request(drive, rq);
		spin_lock_irq(&ide_lock);
		if (masked_irq && hwif->irq != masked_irq)
			enable_irq(hwif->irq);
		if (startstop == ide_released)
			goto queue_next;
		if (startstop == ide_stopped)
			hwgroup->busy = 0;
	}
}

EXPORT_SYMBOL(ide_do_request);

/*
 * ide_get_queue() returns the queue which corresponds to a given device.
 */
request_queue_t *ide_get_queue (kdev_t dev)
{
	ide_hwif_t *hwif = (ide_hwif_t *)blk_dev[major(dev)].data;

	return &hwif->drives[DEVICE_NR(dev) & 1].queue;
}

EXPORT_SYMBOL(ide_get_queue);

/*
 * Passes the stuff to ide_do_request
 */
void do_ide_request(request_queue_t *q)
{
	ide_do_request(q->queuedata, 0);
}

/*
 * un-busy the hwgroup etc, and clear any pending DMA status. we want to
 * retry the current request in pio mode instead of risking tossing it
 * all away
 */
void ide_dma_timeout_retry(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct request *rq;

	/*
	 * end current dma transaction
	 */
	(void) hwif->ide_dma_end(drive);

	/*
	 * complain a little, later we might remove some of this verbosity
	 */
	printk("%s: timeout waiting for DMA\n", drive->name);
	(void) hwif->ide_dma_timeout(drive);

	/*
	 * disable dma for now, but remember that we did so because of
	 * a timeout -- we'll reenable after we finish this next request
	 * (or rather the first chunk of it) in pio.
	 */
	drive->retry_pio++;
	drive->state = DMA_PIO_RETRY;
	(void) hwif->ide_dma_off_quietly(drive);

	/*
	 * un-busy drive etc (hwgroup->busy is cleared on return) and
	 * make sure request is sane
	 */
	rq = HWGROUP(drive)->rq;
	HWGROUP(drive)->rq = NULL;

	rq->errors = 0;
	rq->sector = rq->bio->bi_sector;
	rq->current_nr_sectors = bio_iovec(rq->bio)->bv_len >> 9;
	rq->hard_cur_sectors = rq->current_nr_sectors;
	if (rq->bio)
		rq->buffer = NULL;
}

EXPORT_SYMBOL(ide_dma_timeout_retry);

/*
 * ide_timer_expiry() is our timeout function for all drive operations.
 * But note that it can also be invoked as a result of a "sleep" operation
 * triggered by the mod_timer() call in ide_do_request.
 */
void ide_timer_expiry (unsigned long data)
{
	ide_hwgroup_t	*hwgroup = (ide_hwgroup_t *) data;
	ide_handler_t	*handler;
	ide_expiry_t	*expiry;
 	unsigned long	flags;
	unsigned long	wait;

	spin_lock_irqsave(&ide_lock, flags);
	del_timer(&hwgroup->timer);

	if ((handler = hwgroup->handler) == NULL) {
		/*
		 * Either a marginal timeout occurred
		 * (got the interrupt just as timer expired),
		 * or we were "sleeping" to give other devices a chance.
		 * Either way, we don't really want to complain about anything.
		 */
		if (hwgroup->sleeping) {
			hwgroup->sleeping = 0;
			hwgroup->busy = 0;
		}
	} else {
		ide_drive_t *drive = hwgroup->drive;
		if (!drive) {
			printk("ide_timer_expiry: hwgroup->drive was NULL\n");
			hwgroup->handler = NULL;
		} else {
			ide_hwif_t *hwif;
			ide_startstop_t startstop = ide_stopped;
			if (!hwgroup->busy) {
				hwgroup->busy = 1;	/* paranoia */
				printk("%s: ide_timer_expiry: hwgroup->busy was 0 ??\n", drive->name);
			}
			if ((expiry = hwgroup->expiry) != NULL) {
				/* continue */
				if ((wait = expiry(drive)) != 0) {
					/* reset timer */
					hwgroup->timer.expires  = jiffies + wait;
					add_timer(&hwgroup->timer);
					spin_unlock_irqrestore(&ide_lock, flags);
					return;
				}
			}
			hwgroup->handler = NULL;
			/*
			 * We need to simulate a real interrupt when invoking
			 * the handler() function, which means we need to
			 * globally mask the specific IRQ:
			 */
			spin_unlock(&ide_lock);
			hwif  = HWIF(drive);
#if DISABLE_IRQ_NOSYNC
			disable_irq_nosync(hwif->irq);
#else
			/* disable_irq_nosync ?? */
			disable_irq(hwif->irq);
#endif /* DISABLE_IRQ_NOSYNC */
			/* local CPU only,
			 * as if we were handling an interrupt */
			local_irq_disable();
			if (hwgroup->poll_timeout != 0) {
				startstop = handler(drive);
			} else if (drive_is_ready(drive)) {
				if (drive->waiting_for_dma)
					(void) hwgroup->hwif->ide_dma_lostirq(drive);
				(void)ide_ack_intr(hwif);
				printk("%s: lost interrupt\n", drive->name);
				startstop = handler(drive);
			} else {
				if (drive->waiting_for_dma) {
					startstop = ide_stopped;
					ide_dma_timeout_retry(drive);
				} else
					startstop = DRIVER(drive)->error(drive, "irq timeout", hwif->INB(IDE_STATUS_REG));
			}
			set_recovery_timer(hwif);
			drive->service_time = jiffies - drive->service_start;
			enable_irq(hwif->irq);
			spin_lock_irq(&ide_lock);
			if (startstop == ide_stopped)
				hwgroup->busy = 0;
		}
	}
	ide_do_request(hwgroup, 0);
	spin_unlock_irqrestore(&ide_lock, flags);
}

EXPORT_SYMBOL(ide_timer_expiry);

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
static void unexpected_intr (int irq, ide_hwgroup_t *hwgroup)
{
	u8 stat;
	ide_hwif_t *hwif = hwgroup->hwif;

	/*
	 * handle the unexpected interrupt
	 */
	do {
		if (hwif->irq == irq) {
			stat = hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
			if (!OK_STAT(stat, READY_STAT, BAD_STAT)) {
				/* Try to not flood the console with msgs */
				static unsigned long last_msgtime, count;
				++count;
				if (time_after(jiffies, last_msgtime + HZ)) {
					last_msgtime = jiffies;
					printk("%s%s: unexpected interrupt, "
						"status=0x%02x, count=%ld\n",
						hwif->name,
						(hwif->next==hwgroup->hwif) ? "" : "(?)", stat, count);
				}
			}
		}
	} while ((hwif = hwif->next) != hwgroup->hwif);
}

/*
 * entry point for all interrupts, caller does __cli() for us
 */
void ide_intr (int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = (ide_hwgroup_t *)dev_id;
	ide_hwif_t *hwif;
	ide_drive_t *drive;
	ide_handler_t *handler;
	ide_startstop_t startstop;

	spin_lock_irqsave(&ide_lock, flags);
	hwif = hwgroup->hwif;

	if (!ide_ack_intr(hwif)) {
		spin_unlock_irqrestore(&ide_lock, flags);
		return;
	}

	if ((handler = hwgroup->handler) == NULL ||
	    hwgroup->poll_timeout != 0) {
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
#endif	/* CONFIG_BLK_DEV_IDEPCI */
		{
			/*
			 * Probably not a shared PCI interrupt,
			 * so we can safely try to do something about it:
			 */
			unexpected_intr(irq, hwgroup);
#ifdef CONFIG_BLK_DEV_IDEPCI
		} else {
			/*
			 * Whack the status register, just in case
			 * we have a leftover pending IRQ.
			 */
			(void) hwif->INB(hwif->io_ports[IDE_STATUS_OFFSET]);
#endif /* CONFIG_BLK_DEV_IDEPCI */
		}
		spin_unlock_irqrestore(&ide_lock, flags);
		return;
	}
	drive = hwgroup->drive;
	if (!drive) {
		/*
		 * This should NEVER happen, and there isn't much
		 * we could do about it here.
		 */
		spin_unlock_irqrestore(&ide_lock, flags);
		return;
	}
	if (!drive_is_ready(drive)) {
		/*
		 * This happens regularly when we share a PCI IRQ with
		 * another device.  Unfortunately, it can also happen
		 * with some buggy drives that trigger the IRQ before
		 * their status register is up to date.  Hopefully we have
		 * enough advance overhead that the latter isn't a problem.
		 */
		spin_unlock_irqrestore(&ide_lock, flags);
		return;
	}
	if (!hwgroup->busy) {
		hwgroup->busy = 1;	/* paranoia */
		printk("%s: ide_intr: hwgroup->busy was 0 ??\n", drive->name);
	}
	hwgroup->handler = NULL;
	del_timer(&hwgroup->timer);
	spin_unlock(&ide_lock);

	if (drive->unmask)
		local_irq_enable();
	/* service this interrupt, may set handler for next interrupt */
	startstop = handler(drive);
	spin_lock_irq(&ide_lock);

	/*
	 * Note that handler() may have set things up for another
	 * interrupt to occur soon, but it cannot happen until
	 * we exit from this routine, because it will be the
	 * same irq as is currently being serviced here, and Linux
	 * won't allow another of the same (on any CPU) until we return.
	 */
	set_recovery_timer(HWIF(drive));
	drive->service_time = jiffies - drive->service_start;
	if (startstop == ide_stopped) {
		if (hwgroup->handler == NULL) {	/* paranoia */
			hwgroup->busy = 0;
			ide_do_request(hwgroup, hwif->irq);
		} else {
			printk("%s: ide_intr: huh? expected NULL handler "
				"on exit\n", drive->name);
		}
	}
	spin_unlock_irqrestore(&ide_lock, flags);
}

EXPORT_SYMBOL(ide_intr);

/*
 * get_info_ptr() returns the (ide_drive_t *) for a given device number.
 * It returns NULL if the given device number does not match any present drives.
 */
ide_drive_t *get_info_ptr (kdev_t i_rdev)
{
	int		major = major(i_rdev);
	unsigned int	h;

	for (h = 0; h < MAX_HWIFS; ++h) {
		ide_hwif_t  *hwif = &ide_hwifs[h];
		if (hwif->present && major == hwif->major) {
			unsigned unit = DEVICE_NR(i_rdev);
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

EXPORT_SYMBOL(get_info_ptr);

/*
 * This function is intended to be used prior to invoking ide_do_drive_cmd().
 */
void ide_init_drive_cmd (struct request *rq)
{
	memset(rq, 0, sizeof(*rq));
	rq->flags = REQ_DRIVE_CMD;
}

EXPORT_SYMBOL(ide_init_drive_cmd);

/*
 * This function issues a special IDE device request
 * onto the request queue.
 *
 * If action is ide_wait, then the rq is queued at the end of the
 * request queue, and the function sleeps until it has been processed.
 * This is for use when invoked from an ioctl handler.
 *
 * If action is ide_preempt, then the rq is queued at the head of
 * the request queue, displacing the currently-being-processed
 * request and this function returns immediately without waiting
 * for the new rq to be completed.  This is VERY DANGEROUS, and is
 * intended for careful use by the ATAPI tape/cdrom driver code.
 *
 * If action is ide_next, then the rq is queued immediately after
 * the currently-being-processed-request (if any), and the function
 * returns without waiting for the new rq to be completed.  As above,
 * This is VERY DANGEROUS, and is intended for careful use by the
 * ATAPI tape/cdrom driver code.
 *
 * If action is ide_end, then the rq is queued at the end of the
 * request queue, and the function returns immediately without waiting
 * for the new rq to be completed. This is again intended for careful
 * use by the ATAPI tape/cdrom driver code.
 */
int ide_do_drive_cmd (ide_drive_t *drive, struct request *rq, ide_action_t action)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned int major = HWIF(drive)->major;
	request_queue_t *q = &drive->queue;
	struct list_head *queue_head = &q->queue_head;
	DECLARE_COMPLETION(wait);

#ifdef CONFIG_BLK_DEV_PDC4030
	if (HWIF(drive)->chipset == ide_pdc4030 && rq->buffer != NULL)
		return -ENOSYS;  /* special drive cmds not supported */
#endif
	rq->errors = 0;
	rq->rq_status = RQ_ACTIVE;
	rq->rq_dev = mk_kdev(major,(drive->select.b.unit)<<PARTN_BITS);
	if (action == ide_wait)
		rq->waiting = &wait;
	spin_lock_irqsave(&ide_lock, flags);
	if (blk_queue_empty(q) || action == ide_preempt) {
		if (action == ide_preempt)
			hwgroup->rq = NULL;
	} else {
		if (action == ide_wait || action == ide_end) {
			queue_head = queue_head->prev;
		} else
			queue_head = queue_head->next;
	}
	q->elevator.elevator_add_req_fn(q, rq, queue_head);
	ide_do_request(hwgroup, 0);
	spin_unlock_irqrestore(&ide_lock, flags);
	if (action == ide_wait) {
		/* wait for it to be serviced */
		wait_for_completion(&wait);
		/* return -EIO if errors */
		return rq->errors ? -EIO : 0;
	}
	return 0;

}

EXPORT_SYMBOL(ide_do_drive_cmd);

void ide_revalidate_drive (ide_drive_t *drive)
{
	set_capacity(drive->disk, current_capacity(drive));
}

EXPORT_SYMBOL(ide_revalidate_drive);

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
	if ((drive = get_info_ptr(i_rdev)) == NULL)
		return -ENODEV;
	if (DRIVER(drive)->revalidate)
		DRIVER(drive)->revalidate(drive);
	return  0;
}

EXPORT_SYMBOL(ide_revalidate_disk);

void ide_probe_module (void)
{
	if (!ide_probe) {
#if defined(CONFIG_KMOD) && defined(CONFIG_BLK_DEV_IDE_MODULE)
		(void) request_module("ide-probe-mod");
#endif /* (CONFIG_KMOD) && (CONFIG_BLK_DEV_IDE_MODULE) */
	} else {
		(void) ide_probe->init();
	}
}

EXPORT_SYMBOL(ide_probe_module);

static int ide_open (struct inode * inode, struct file * filp)
{
	ide_drive_t *drive;

	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENXIO;
	if (drive->driver == NULL) {
		if (drive->media == ide_disk)
			(void) request_module("ide-disk");
		if (drive->scsi)
			(void) request_module("ide-scsi");
		if (drive->media == ide_cdrom)
			(void) request_module("ide-cd");
		if (drive->media == ide_tape)
			(void) request_module("ide-tape");
		if (drive->media == ide_floppy)
			(void) request_module("ide-floppy");
	}
	drive->usage++;
	if (drive->driver != NULL)
		return DRIVER(drive)->open(inode, filp, drive);
	printk (KERN_WARNING "%s: driver not present\n", drive->name);
	drive->usage--;
	return -ENXIO;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */
static int ide_release (struct inode * inode, struct file * file)
{
	ide_drive_t *drive;

	if ((drive = get_info_ptr(inode->i_rdev)) != NULL) {
		drive->usage--;
		if (drive->driver != NULL)
			DRIVER(drive)->release(inode, file, drive);
	}
	return 0;
}

static LIST_HEAD(ata_unused);
static spinlock_t drives_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t drivers_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(drivers);

/* Iterator */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct list_head *p;
	loff_t l = *pos;
	spin_lock(&drivers_lock);
	list_for_each(p, &drivers)
		if (!l--)
			return list_entry(p, ide_driver_t, drivers);
	return NULL;
}
static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct list_head *p = ((ide_driver_t *)v)->drivers.next;
	(*pos)++;
	return p==&drivers ? NULL : list_entry(p, ide_driver_t, drivers);
}
static void m_stop(struct seq_file *m, void *v)
{
	spin_unlock(&drivers_lock);
}
static int show_driver(struct seq_file *m, void *v)
{
	ide_driver_t *driver = v;
	seq_printf(m, "%s version %s\n", driver->name, driver->version);
	return 0;
}
struct seq_operations ide_drivers_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_driver
};

#ifdef CONFIG_PROC_FS
ide_proc_entry_t generic_subdriver_entries[] = {
	{ "capacity",	S_IFREG|S_IRUGO,	proc_ide_read_capacity,	NULL },
	{ NULL, 0, NULL, NULL }
};
#endif


#define hwif_release_region(addr, num) \
	((hwif->mmio) ? release_mem_region((addr),(num)) : release_region((addr),(num)))

/*
 * Note that we only release the standard ports,
 * and do not even try to handle any extra ports
 * allocated for weird IDE interface chipsets.
 */
void hwif_unregister (ide_hwif_t *hwif)
{
	u32 i = 0;

	if (hwif->mmio == 2)
		return;
	if (hwif->io_ports[IDE_CONTROL_OFFSET])
		hwif_release_region(hwif->io_ports[IDE_CONTROL_OFFSET], 1);
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (hwif->io_ports[IDE_IRQ_OFFSET])
		hwif_release_region(hwif->io_ports[IDE_IRQ_OFFSET], 1);
#endif /* (CONFIG_AMIGA) || (CONFIG_MAC) */

	if (hwif->straight8) {
		hwif_release_region(hwif->io_ports[IDE_DATA_OFFSET], 8);
		return;
	}
	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		if (hwif->io_ports[i]) {
			hwif_release_region(hwif->io_ports[i], 1);
		}
	}
}

EXPORT_SYMBOL(hwif_unregister);

extern void init_hwif_data(unsigned int index);

void ide_unregister (unsigned int index)
{
	ide_drive_t *drive, *d;
	ide_hwif_t *hwif, *g;
	ide_hwgroup_t *hwgroup;
	int irq_count = 0, unit, i;
	unsigned long flags;
	unsigned int p, minor;
	ide_hwif_t old_hwif;

	if (index >= MAX_HWIFS)
		return;
	spin_lock_irqsave(&ide_lock, flags);
	hwif = &ide_hwifs[index];
	if (!hwif->present)
		goto abort;
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		drive = &hwif->drives[unit];
		if (!drive->present)
			continue;
		if (drive->usage)
			goto abort;
		if (drive->driver != NULL && DRIVER(drive)->cleanup(drive))
			goto abort;
	}
	hwif->present = 0;
	
	/*
	 * All clear?  Then blow away the buffer cache
	 */
	spin_unlock_irqrestore(&ide_lock, flags);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		drive = &hwif->drives[unit];
		if (!drive->present)
			continue;
		minor = drive->select.b.unit << PARTN_BITS;
		for (p = 0; p < (1<<PARTN_BITS); ++p) {
			if (get_capacity(drive->disk)) {
				kdev_t devp = mk_kdev(hwif->major, minor+p);
				invalidate_device(devp, 0);
			}
		}
#ifdef CONFIG_PROC_FS
		destroy_proc_ide_drives(hwif);
#endif
	}
	spin_lock_irqsave(&ide_lock, flags);
	hwgroup = hwif->hwgroup;

	/*
	 * free the irq if we were the only hwif using it
	 */
	g = hwgroup->hwif;
	do {
		if (g->irq == hwif->irq)
			++irq_count;
		g = g->next;
	} while (g != hwgroup->hwif);
	if (irq_count == 1)
		free_irq(hwif->irq, hwgroup);

	/*
	 * Note that we only release the standard ports,
	 * and do not even try to handle any extra ports
	 * allocated for weird IDE interface chipsets.
	 */
	hwif_unregister(hwif);

	/*
	 * Remove us from the hwgroup, and free
	 * the hwgroup if we were the only member
	 */
	d = hwgroup->drive;
	for (i = 0; i < MAX_DRIVES; ++i) {
		drive = &hwif->drives[i];
		if (drive->de) {
			devfs_unregister(drive->de);
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
	}
	if (d->present)
		hwgroup->drive = d;
	while (hwgroup->hwif->next != hwif)
		hwgroup->hwif = hwgroup->hwif->next;
	hwgroup->hwif->next = hwif->next;
	if (hwgroup->hwif == hwif)
		kfree(hwgroup);
	else
		hwgroup->hwif = HWIF(hwgroup->drive);

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
	if (hwif->dma_base) {
		(void) ide_release_dma(hwif);

		hwif->dma_base = 0;
		hwif->dma_master = 0;
		hwif->dma_command = 0;
		hwif->dma_vendor1 = 0;
		hwif->dma_status = 0;
		hwif->dma_vendor3 = 0;
		hwif->dma_prdtable = 0;
	}
#endif /* (CONFIG_BLK_DEV_IDEDMA) && !(CONFIG_DMA_NONPCI) */

	/*
	 * Remove us from the kernel's knowledge
	 */
	unregister_blkdev(hwif->major, hwif->name);
	blk_dev[hwif->major].data = NULL;
	blk_dev[hwif->major].queue = NULL;
	for (i = 0; i < MAX_DRIVES; i++) {
		struct gendisk *disk = hwif->drives[i].disk;
		hwif->drives[i].disk = NULL;
		put_disk(disk);
	}
	old_hwif			= *hwif;
	init_hwif_data(index);	/* restore hwif data to pristine status */
	hwif->hwgroup			= old_hwif.hwgroup;

	hwif->proc			= old_hwif.proc;

	hwif->major			= old_hwif.major;
//	hwif->index			= old_hwif.index;
//	hwif->channel			= old_hwif.channel;
	hwif->straight8			= old_hwif.straight8;
	hwif->bus_state			= old_hwif.bus_state;

	hwif->atapi_dma			= old_hwif.atapi_dma;
	hwif->ultra_mask		= old_hwif.ultra_mask;
	hwif->mwdma_mask		= old_hwif.mwdma_mask;
	hwif->swdma_mask		= old_hwif.swdma_mask;

	hwif->chipset			= old_hwif.chipset;

#ifdef CONFIG_BLK_DEV_IDEPCI
	hwif->pci_dev			= old_hwif.pci_dev;
	hwif->cds			= old_hwif.cds;
#endif /* CONFIG_BLK_DEV_IDEPCI */

#if 0
	hwif->hwifops			= old_hwif.hwifops;
#else
	hwif->identify			= old_hwif.identify;
	hwif->tuneproc			= old_hwif.tuneproc;
	hwif->speedproc			= old_hwif.speedproc;
	hwif->selectproc		= old_hwif.selectproc;
	hwif->reset_poll		= old_hwif.reset_poll;
	hwif->pre_reset			= old_hwif.pre_reset;
	hwif->resetproc			= old_hwif.resetproc;
	hwif->intrproc			= old_hwif.intrproc;
	hwif->maskproc			= old_hwif.maskproc;
	hwif->quirkproc			= old_hwif.quirkproc;
	hwif->busproc			= old_hwif.busproc;
#endif

#if 0
	hwif->pioops			= old_hwif.pioops;
#else
	hwif->ata_input_data		= old_hwif.ata_input_data;
	hwif->ata_output_data		= old_hwif.ata_output_data;
	hwif->atapi_input_bytes		= old_hwif.atapi_input_bytes;
	hwif->atapi_output_bytes	= old_hwif.atapi_output_bytes;
#endif

#if 0
	hwif->dmaops			= old_hwif.dmaops;
#else
	hwif->ide_dma_read		= old_hwif.ide_dma_read;
	hwif->ide_dma_write		= old_hwif.ide_dma_write;
	hwif->ide_dma_begin		= old_hwif.ide_dma_begin;
	hwif->ide_dma_end		= old_hwif.ide_dma_end;
	hwif->ide_dma_check		= old_hwif.ide_dma_check;
	hwif->ide_dma_on		= old_hwif.ide_dma_on;
	hwif->ide_dma_off		= old_hwif.ide_dma_off;
	hwif->ide_dma_off_quietly	= old_hwif.ide_dma_off_quietly;
	hwif->ide_dma_test_irq		= old_hwif.ide_dma_test_irq;
	hwif->ide_dma_host_on		= old_hwif.ide_dma_host_on;
	hwif->ide_dma_host_off		= old_hwif.ide_dma_host_off;
	hwif->ide_dma_bad_drive		= old_hwif.ide_dma_bad_drive;
	hwif->ide_dma_good_drive	= old_hwif.ide_dma_good_drive;
	hwif->ide_dma_count		= old_hwif.ide_dma_count;
	hwif->ide_dma_verbose		= old_hwif.ide_dma_verbose;
	hwif->ide_dma_retune		= old_hwif.ide_dma_retune;
	hwif->ide_dma_lostirq		= old_hwif.ide_dma_lostirq;
	hwif->ide_dma_timeout		= old_hwif.ide_dma_timeout;
#endif

#if 0
	hwif->iops			= old_hwif.iops;
#else
	hwif->OUTB		= old_hwif.OUTB;
	hwif->OUTW		= old_hwif.OUTW;
	hwif->OUTL		= old_hwif.OUTL;
	hwif->OUTSW		= old_hwif.OUTSW;
	hwif->OUTSL		= old_hwif.OUTSL;

	hwif->INB		= old_hwif.INB;
	hwif->INW		= old_hwif.INW;
	hwif->INL		= old_hwif.INL;
	hwif->INSW		= old_hwif.INSW;
	hwif->INSL		= old_hwif.INSL;
#endif

	hwif->mmio			= old_hwif.mmio;
	hwif->rqsize			= old_hwif.rqsize;
	hwif->addressing		= old_hwif.addressing;
#ifndef CONFIG_BLK_DEV_IDECS
	hwif->irq			= old_hwif.irq;
#endif /* CONFIG_BLK_DEV_IDECS */
	hwif->initializing		= old_hwif.initializing;

	hwif->dma_base			= old_hwif.dma_base;
	hwif->dma_master		= old_hwif.dma_master;
	hwif->dma_command		= old_hwif.dma_command;
	hwif->dma_vendor1		= old_hwif.dma_vendor1;
	hwif->dma_status		= old_hwif.dma_status;
	hwif->dma_vendor3		= old_hwif.dma_vendor3;
	hwif->dma_prdtable		= old_hwif.dma_prdtable;

	hwif->dma_extra			= old_hwif.dma_extra;
	hwif->config_data		= old_hwif.config_data;
	hwif->select_data		= old_hwif.select_data;
	hwif->autodma			= old_hwif.autodma;
	hwif->udma_four			= old_hwif.udma_four;
	hwif->no_dsc			= old_hwif.no_dsc;

	hwif->hwif_data			= old_hwif.hwif_data;
abort:
	spin_unlock_irqrestore(&ide_lock, flags);
}

EXPORT_SYMBOL(ide_unregister);

/*
 * Setup hw_regs_t structure described by parameters.  You
 * may set up the hw structure yourself OR use this routine to
 * do it for you.
 */
void ide_setup_ports (	hw_regs_t *hw,
			ide_ioreg_t base, int *offsets,
			ide_ioreg_t ctrl, ide_ioreg_t intr,
			ide_ack_intr_t *ack_intr,
/*
 *			ide_io_ops_t *iops,
 */
			int irq)
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
#endif /* (CONFIG_AMIGA) || (CONFIG_MAC) */
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
/*
 *	hw->iops = iops;
 */
}

EXPORT_SYMBOL(ide_setup_ports);

/*
 * Register an IDE interface, specifing exactly the registers etc
 * Set init=1 iff calling before probes have taken place.
 */
int ide_register_hw (hw_regs_t *hw, ide_hwif_t **hwifp)
{
	int index, retry = 1;
	ide_hwif_t *hwif;

	do {
		for (index = 0; index < MAX_HWIFS; ++index) {
			hwif = &ide_hwifs[index];
			if (hwif->hw.io_ports[IDE_DATA_OFFSET] == hw->io_ports[IDE_DATA_OFFSET])
				goto found;
		}
		for (index = 0; index < MAX_HWIFS; ++index) {
			hwif = &ide_hwifs[index];
			if ((!hwif->present && !hwif->mate && !initializing) ||
			    (!hwif->hw.io_ports[IDE_DATA_OFFSET] && initializing))
				goto found;
		}
		for (index = 0; index < MAX_HWIFS; index++)
			ide_unregister(index);
	} while (retry--);
	return -1;
found:
	if (hwif->present)
		ide_unregister(index);
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
	}

	if (hwifp)
		*hwifp = hwif;

	return (initializing || hwif->present) ? index : -1;
}

EXPORT_SYMBOL(ide_register_hw);

/*
 * Compatability function with existing drivers.  If you want
 * something different, use the function above.
 */
int ide_register (int arg1, int arg2, int irq)
{
	hw_regs_t hw;
	ide_init_hwif_ports(&hw, (ide_ioreg_t) arg1, (ide_ioreg_t) arg2, NULL);
	hw.irq = irq;
	return ide_register_hw(&hw, NULL);
}

EXPORT_SYMBOL(ide_register);

void ide_add_setting (ide_drive_t *drive, const char *name, int rw, int read_ioctl, int write_ioctl, int data_type, int min, int max, int mul_factor, int div_factor, void *data, ide_procset_t *set)
{
	ide_settings_t **p = (ide_settings_t **) &drive->settings, *setting = NULL;

	while ((*p) && strcmp((*p)->name, name) < 0)
		p = &((*p)->next);
	if ((setting = kmalloc(sizeof(*setting), GFP_KERNEL)) == NULL)
		goto abort;
	memset(setting, 0, sizeof(*setting));
	if ((setting->name = kmalloc(strlen(name) + 1, GFP_KERNEL)) == NULL)
		goto abort;
	strcpy(setting->name, name);
	setting->rw = rw;
	setting->read_ioctl = read_ioctl;
	setting->write_ioctl = write_ioctl;
	setting->data_type = data_type;
	setting->min = min;
	setting->max = max;
	setting->mul_factor = mul_factor;
	setting->div_factor = div_factor;
	setting->data = data;
	setting->set = set;
	setting->next = *p;
	if (drive->driver)
		setting->auto_remove = 1;
	*p = setting;
	return;
abort:
	if (setting)
		kfree(setting);
}

EXPORT_SYMBOL(ide_add_setting);

void ide_remove_setting (ide_drive_t *drive, char *name)
{
	ide_settings_t **p = (ide_settings_t **) &drive->settings, *setting;

	while ((*p) && strcmp((*p)->name, name))
		p = &((*p)->next);
	if ((setting = (*p)) == NULL)
		return;
	(*p) = setting->next;
	kfree(setting->name);
	kfree(setting);
}

EXPORT_SYMBOL(ide_remove_setting);

static ide_settings_t *ide_find_setting_by_ioctl (ide_drive_t *drive, int cmd)
{
	ide_settings_t *setting = drive->settings;

	while (setting) {
		if (setting->read_ioctl == cmd || setting->write_ioctl == cmd)
			break;
		setting = setting->next;
	}
	return setting;
}

ide_settings_t *ide_find_setting_by_name (ide_drive_t *drive, char *name)
{
	ide_settings_t *setting = drive->settings;

	while (setting) {
		if (strcmp(setting->name, name) == 0)
			break;
		setting = setting->next;
	}
	return setting;
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

	while (hwgroup->busy) {
		unsigned long lflags;
		spin_unlock_irq(&ide_lock);
		local_irq_set(lflags);
		if (time_after(jiffies, timeout)) {
			local_irq_restore(lflags);
			printk("%s: channel busy\n", drive->name);
			return -EBUSY;
		}
		local_irq_restore(lflags);
		spin_lock_irq(&ide_lock);
	}
	return 0;
}

EXPORT_SYMBOL(ide_spin_wait_hwgroup);

/*
 * FIXME:  This should be changed to enqueue a special request
 * to the driver to change settings, and then wait on a sema for completion.
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

EXPORT_SYMBOL(ide_write_setting);

static int set_io_32bit(ide_drive_t *drive, int arg)
{
	drive->io_32bit = arg;
#ifdef CONFIG_BLK_DEV_DTC2278
	if (HWIF(drive)->chipset == ide_dtc2278)
		HWIF(drive)->drives[!drive->select.b.unit].io_32bit = arg;
#endif /* CONFIG_BLK_DEV_DTC2278 */
	return 0;
}

static int set_using_dma (ide_drive_t *drive, int arg)
{
	if (!drive->driver || !DRIVER(drive)->supports_dma)
		return -EPERM;
	if (!drive->id || !(drive->id->capability & 1))
		return -EPERM;
	if (HWIF(drive)->ide_dma_check == NULL)
		return -EPERM;
	if (arg) {
		if (HWIF(drive)->ide_dma_on(drive)) return -EIO;
	} else {
		if (HWIF(drive)->ide_dma_off(drive)) return -EIO;
	}
	return 0;
}

static int set_pio_mode (ide_drive_t *drive, int arg)
{
	struct request rq;

	if (!HWIF(drive)->tuneproc)
		return -ENOSYS;
	if (drive->special.b.set_tune)
		return -EBUSY;
	ide_init_drive_cmd(&rq);
	drive->tune_req = (u8) arg;
	drive->special.b.set_tune = 1;
	(void) ide_do_drive_cmd(drive, &rq, ide_wait);
	return 0;
}

static int set_xfer_rate (ide_drive_t *drive, int arg)
{
	int err = ide_wait_cmd(drive,
			WIN_SETFEATURES, (u8) arg,
			SETFEATURES_XFER, 0, NULL);

	if (!err && arg) {
		if ((HWIF(drive)->speedproc) != NULL)
			HWIF(drive)->speedproc(drive, (u8) arg);
		ide_driveid_update(drive);
	}
	return err;
}

int ide_atapi_to_scsi (ide_drive_t *drive, int arg)
{
	if (drive->media == ide_disk) {
		drive->scsi = 0;
		return 0;
	}

	if (drive->driver != NULL) {
#if 0
		ide_unregister_subdriver(drive);
#else
		if (DRIVER(drive)->cleanup(drive)) {
			drive->scsi = 0;
			return 0;
		}
#endif
	}

	drive->scsi = (u8) arg;
	ata_attach(drive);
	return 0;
}

void ide_add_generic_settings (ide_drive_t *drive)
{
/*
 *			drive	setting name		read/write access				read ioctl		write ioctl		data type	min	max				mul_factor	div_factor	data pointer			set function
 */
	ide_add_setting(drive,	"io_32bit",		drive->no_io_32bit ? SETTING_READ : SETTING_RW,	HDIO_GET_32BIT,		HDIO_SET_32BIT,		TYPE_BYTE,	0,	1 + (SUPPORT_VLB_SYNC << 1),	1,		1,		&drive->io_32bit,		set_io_32bit);
	ide_add_setting(drive,	"keepsettings",		SETTING_RW,					HDIO_GET_KEEPSETTINGS,	HDIO_SET_KEEPSETTINGS,	TYPE_BYTE,	0,	1,				1,		1,		&drive->keep_settings,		NULL);
	ide_add_setting(drive,	"nice1",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	1,				1,		1,		&drive->nice1,			NULL);
	ide_add_setting(drive,	"pio_mode",		SETTING_WRITE,					-1,			HDIO_SET_PIO_MODE,	TYPE_BYTE,	0,	255,				1,		1,		NULL,				set_pio_mode);
	ide_add_setting(drive,	"slow",			SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	1,				1,		1,		&drive->slow,			NULL);
	ide_add_setting(drive,	"unmaskirq",		drive->no_unmask ? SETTING_READ : SETTING_RW,	HDIO_GET_UNMASKINTR,	HDIO_SET_UNMASKINTR,	TYPE_BYTE,	0,	1,				1,		1,		&drive->unmask,			NULL);
	ide_add_setting(drive,	"using_dma",		SETTING_RW,					HDIO_GET_DMA,		HDIO_SET_DMA,		TYPE_BYTE,	0,	1,				1,		1,		&drive->using_dma,		set_using_dma);
	ide_add_setting(drive,	"init_speed",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	70,				1,		1,		&drive->init_speed,		NULL);
	ide_add_setting(drive,	"current_speed",	SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	70,				1,		1,		&drive->current_speed,		set_xfer_rate);
	ide_add_setting(drive,	"number",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	3,				1,		1,		&drive->dn,			NULL);
	if (drive->media != ide_disk)
		ide_add_setting(drive,	"ide-scsi",		SETTING_RW,					-1,		HDIO_SET_IDE_SCSI,		TYPE_BYTE,	0,	1,				1,		1,		&drive->scsi,			ide_atapi_to_scsi);
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

EXPORT_SYMBOL(ide_delay_50ms);

int system_bus_clock (void)
{
	return((int) ((!system_bus_speed) ? ide_system_bus_speed() : system_bus_speed ));
}

EXPORT_SYMBOL(system_bus_clock);

/*
 *	Locking is badly broken here - since way back.  That sucker is
 * root-only, but that's not an excuse...  The real question is what
 * exclusion rules do we want here.
 */
int ide_replace_subdriver (ide_drive_t *drive, const char *driver)
{
	if (!drive->present || drive->usage)
		goto abort;
	if (drive->driver != NULL && DRIVER(drive)->cleanup(drive))
		goto abort;
	strncpy(drive->driver_req, driver, 9);
	if (ata_attach(drive)) {
		spin_lock(&drives_lock);
		list_del_init(&drive->list);
		spin_unlock(&drives_lock);
		drive->driver_req[0] = 0;
		ata_attach(drive);
	} else {
		drive->driver_req[0] = 0;
	}
	if (DRIVER(drive) && !strcmp(DRIVER(drive)->name, driver))
		return 0;
abort:
	return 1;
}

EXPORT_SYMBOL(ide_replace_subdriver);

int ata_attach(ide_drive_t *drive)
{
	struct list_head *p;
	spin_lock(&drivers_lock);
	list_for_each(p, &drivers) {
		ide_driver_t *driver = list_entry(p, ide_driver_t, drivers);
		if (!try_inc_mod_count(driver->owner))
			continue;
		spin_unlock(&drivers_lock);
		if (driver->attach(drive) == 0) {
			if (driver->owner)
				__MOD_DEC_USE_COUNT(driver->owner);
			drive->gendev.driver = &driver->gen_driver;
			return 0;
		}
		spin_lock(&drivers_lock);
		if (driver->owner)
			__MOD_DEC_USE_COUNT(driver->owner);
	}
	spin_unlock(&drivers_lock);
	spin_lock(&drives_lock);
	list_add_tail(&drive->list, &ata_unused);
	spin_unlock(&drives_lock);
	return 1;
}

EXPORT_SYMBOL(ata_attach);

static int ide_ioctl (struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int err = 0, major, minor;
	ide_drive_t *drive;
	struct request rq;
	kdev_t dev;
	ide_settings_t *setting;

	major = major(dev); minor = minor(dev);
	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENODEV;

	if ((setting = ide_find_setting_by_ioctl(drive, cmd)) != NULL) {
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
			u16 bios_cyl = drive->bios_cyl; /* truncate */
			if (!loc || (drive->media != ide_disk && drive->media != ide_floppy)) return -EINVAL;
			if (put_user(drive->bios_head, (u8 *) &loc->heads)) return -EFAULT;
			if (put_user(drive->bios_sect, (u8 *) &loc->sectors)) return -EFAULT;
			if (put_user(bios_cyl, (u16 *) &loc->cylinders)) return -EFAULT;
			if (put_user((unsigned)get_start_sect(inode->i_bdev),
				(unsigned long *) &loc->start)) return -EFAULT;
			return 0;
		}

		case HDIO_GETGEO_BIG_RAW:
		{
			struct hd_big_geometry *loc = (struct hd_big_geometry *) arg;
			if (!loc || (drive->media != ide_disk && drive->media != ide_floppy)) return -EINVAL;
			if (put_user(drive->head, (u8 *) &loc->heads)) return -EFAULT;
			if (put_user(drive->sect, (u8 *) &loc->sectors)) return -EFAULT;
			if (put_user(drive->cyl, (unsigned int *) &loc->cylinders)) return -EFAULT;
			if (put_user((unsigned)get_start_sect(inode->i_bdev),
				(unsigned long *) &loc->start)) return -EFAULT;
			return 0;
		}

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
					drive->atapi_overlap	<<	IDE_NICE_ATAPI_OVERLAP	|
					drive->nice0		<< 	IDE_NICE_0		|
					drive->nice1		<<	IDE_NICE_1		|
					drive->nice2		<<	IDE_NICE_2,
					(long *) arg);

#ifdef CONFIG_IDE_TASK_IOCTL
		case HDIO_DRIVE_TASKFILE:
		        if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
				return -EACCES;
			switch(drive->media) {
				case ide_disk:
					return ide_taskfile_ioctl(drive, inode, file, cmd, arg);
#ifdef CONFIG_PKT_TASK_IOCTL
				case ide_cdrom:
				case ide_tape:
				case ide_floppy:
					return pkt_taskfile_ioctl(drive, inode, file, cmd, arg);
#endif /* CONFIG_PKT_TASK_IOCTL */
				default:
					return -ENOMSG;
			}
#endif /* CONFIG_IDE_TASK_IOCTL */

		case HDIO_DRIVE_CMD:
			if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
				return -EACCES;
			return ide_cmd_ioctl(drive, inode, file, cmd, arg);

		case HDIO_DRIVE_TASK:
			if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
				return -EACCES;
			return ide_task_ioctl(drive, inode, file, cmd, arg);

		case HDIO_SCAN_HWIF:
		{
			int args[3];
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
			if (copy_from_user(args, (void *)arg, 3 * sizeof(int)))
				return -EFAULT;
			if (ide_register(args[0], args[1], args[2]) == -1)
				return -EIO;
			return 0;
		}
	        case HDIO_UNREGISTER_HWIF:
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
			/* (arg > MAX_HWIFS) checked in function */
			ide_unregister(arg);
			return 0;
		case HDIO_SET_NICE:
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
			if (drive->driver == NULL)
				return -EPERM;
			if (arg != (arg & ((1 << IDE_NICE_DSC_OVERLAP) | (1 << IDE_NICE_1))))
				return -EPERM;
			drive->dsc_overlap = (arg >> IDE_NICE_DSC_OVERLAP) & 1;
			if (drive->dsc_overlap && !DRIVER(drive)->supports_dsc_overlap) {
				drive->dsc_overlap = 0;
				return -EPERM;
			}
			drive->nice1 = (arg >> IDE_NICE_1) & 1;
			return 0;
		case HDIO_DRIVE_RESET:
		{
			unsigned long flags;
			if (!capable(CAP_SYS_ADMIN)) return -EACCES;
#if 1
			spin_lock_irqsave(&ide_lock, flags);
			if ( HWGROUP(drive)->handler != NULL) {
				printk("%s: ide_set_handler: handler not null; %p\n", drive->name, HWGROUP(drive)->handler);
				(void) HWGROUP(drive)->handler(drive);
//				HWGROUP(drive)->handler = NULL;
				HWGROUP(drive)->expiry	= NULL;
				del_timer(&HWGROUP(drive)->timer);
			}
			spin_unlock_irqrestore(&ide_lock, flags);

#endif
			(void) ide_do_reset(drive);
			if (drive->suspend_reset) {
/*
 *				APM WAKE UP todo !!
 *				int nogoodpower = 1;
 *				while(nogoodpower) {
 *					check_power1() or check_power2()
 *					nogoodpower = 0;
 *				} 
 *				HWIF(drive)->multiproc(drive);
 */
				return file->f_op->ioctl(inode, file,
								BLKRRPART, 0);
			}
			return 0;
		}

		case CDROMEJECT:
		case CDROMCLOSETRAY:
			return block_ioctl(inode->i_bdev, cmd, arg);

		case HDIO_GET_BUSSTATE:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (put_user(HWIF(drive)->bus_state, (long *)arg))
				return -EFAULT;
			return 0;

		case HDIO_SET_BUSSTATE:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (HWIF(drive)->busproc)
				HWIF(drive)->busproc(drive, (int)arg);
			return 0;

		default:
			if (drive->driver != NULL)
				return DRIVER(drive)->ioctl(drive, inode, file, cmd, arg);
			return -EPERM;
	}
}

static int ide_check_media_change (kdev_t i_rdev)
{
	ide_drive_t *drive;

	if ((drive = get_info_ptr(i_rdev)) == NULL)
		return -ENODEV;
	if (drive->driver != NULL)
		return DRIVER(drive)->media_change(drive);
	return 0;
}

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
 * match_parm() does parsing for ide_setup():
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
 * ide_setup() gets called VERY EARLY during initialization,
 * to handle kernel "command line" strings beginning with "hdx="
 * or "ide".  Here is the complete set currently supported:
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
 * "hdx=swapdata"	: when the drive is a disk, byte swap all data
 * "hdx=bswap"		: same as above..........
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
 * "idex=base"		: probe for an interface at the addr specified,
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
	ide_hwif_t *hwif;
	ide_drive_t *drive;
	unsigned int hw, unit;
	const char max_drive = 'a' + ((MAX_HWIFS * MAX_DRIVES) - 1);
	const char max_hwif  = '0' + (MAX_HWIFS - 1);

	
	if (strncmp(s,"hd",2) == 0 && s[2] == '=')	/* hd= is for hd.c   */
		return 0;				/* driver and not us */

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
#endif /* CONFIG_BLK_DEV_IDEDOUBLER */

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
#endif /* CONFIG_BLK_DEV_IDEPCI */

	/*
	 * Look for drive options:  "hdx="
	 */
	if (s[0] == 'h' && s[1] == 'd' && s[2] >= 'a' && s[2] <= max_drive) {
		const char *hd_words[] = {"none", "noprobe", "nowerr", "cdrom",
				"serialize", "autotune", "noautotune",
				"slow", "swapdata", "bswap", "flash",
				"remap", "noremap", "scsi", NULL};
		unit = s[2] - 'a';
		hw   = unit / MAX_DRIVES;
		unit = unit % MAX_DRIVES;
		hwif = &ide_hwifs[hw];
		drive = &hwif->drives[unit];
		if (strncmp(s + 4, "ide-", 4) == 0) {
			strncpy(drive->driver_req, s + 4, 9);
			goto done;
		}
		/*
		 * Look for last lun option:  "hdxlun="
		 */
		if (s[3] == 'l' && s[4] == 'u' && s[5] == 'n') {
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
				drive->media = ide_cdrom;
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
			case -9: /* "swapdata" or "bswap" */
			case -10:
				drive->bswap = 1;
				goto done;
			case -11: /* "flash" */
				drive->ata_flash = 1;
				goto done;
			case -12: /* "remap" */
				drive->remap_0_to_1 = 1;
				goto done;
			case -13: /* "noremap" */
				drive->remap_0_to_1 = 2;
				goto done;
			case -14: /* "scsi" */
#if defined(CONFIG_BLK_DEV_IDESCSI) && defined(CONFIG_SCSI)
				drive->scsi = 1;
				goto done;
#else
				drive->scsi = 0;
				goto bad_option;
#endif /* defined(CONFIG_BLK_DEV_IDESCSI) && defined(CONFIG_SCSI) */
			case 3: /* cyl,head,sect */
				drive->media	= ide_disk;
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

	if (s[0] != 'i' || s[1] != 'd' || s[2] != 'e')
		goto bad_option;
	/*
	 * Look for bus speed option:  "idebus="
	 */
	if (s[3] == 'b' && s[4] == 'u' && s[5] == 's') {
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
	if (s[3] >= '0' && s[3] <= max_hwif) {
		/*
		 * Be VERY CAREFUL changing this: note hardcoded indexes below
		 * -8,-9,-10 : are reserved for future idex calls to ease the hardcoding.
		 */
		const char *ide_words[] = {
			"noprobe", "serialize", "autotune", "noautotune", "reset", "dma", "ata66",
			"minus8", "minus9", "minus10",
			"four", "qd65xx", "ht6560b", "cmd640_vlb", "dtc2278", "umc8672", "ali14xx", "dc4030", NULL };
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
#endif /* CONFIG_BLK_DEV_ALI14XX */
#ifdef CONFIG_BLK_DEV_UMC8672
			case -16: /* "umc8672" */
			{
				extern void init_umc8672 (void);
				init_umc8672();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_UMC8672 */
#ifdef CONFIG_BLK_DEV_DTC2278
			case -15: /* "dtc2278" */
			{
				extern void init_dtc2278 (void);
				init_dtc2278();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_DTC2278 */
#ifdef CONFIG_BLK_DEV_CMD640
			case -14: /* "cmd640_vlb" */
			{
				extern int cmd640_vlb; /* flag for cmd640.c */
				cmd640_vlb = 1;
				goto done;
			}
#endif /* CONFIG_BLK_DEV_CMD640 */
#ifdef CONFIG_BLK_DEV_HT6560B
			case -13: /* "ht6560b" */
			{
				extern void init_ht6560b (void);
				init_ht6560b();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_HT6560B */
#if CONFIG_BLK_DEV_QD65XX
			case -12: /* "qd65xx" */
			{
				extern void init_qd65xx (void);
				init_qd65xx();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_QD65XX */
#ifdef CONFIG_BLK_DEV_4DRIVES
			case -11: /* "four" drives on one set of ports */
			{
				ide_hwif_t *mate = &ide_hwifs[hw^1];
				mate->drives[0].select.all ^= 0x20;
				mate->drives[1].select.all ^= 0x20;
				hwif->chipset = mate->chipset = ide_4drives;
				mate->irq = hwif->irq;
				memcpy(mate->io_ports, hwif->io_ports, sizeof(hwif->io_ports));
				goto do_serialize;
			}
#endif /* CONFIG_BLK_DEV_4DRIVES */
			case -10: /* minus10 */
			case -9: /* minus9 */
			case -8: /* minus8 */
				goto bad_option;
			case -7: /* ata66 */
#ifdef CONFIG_BLK_DEV_IDEPCI
				hwif->udma_four = 1;
				goto done;
#else /* !CONFIG_BLK_DEV_IDEPCI */
				hwif->udma_four = 0;
				goto bad_hwif;
#endif /* CONFIG_BLK_DEV_IDEPCI */
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
				hwif->mate = &ide_hwifs[hw^1];
				hwif->mate->mate = hwif;
				hwif->serialized = hwif->mate->serialized = 1;
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

/*
 * probe_for_hwifs() finds/initializes "known" IDE interfaces
 */
static void __init probe_for_hwifs (void)
{
#ifdef CONFIG_BLK_DEV_IDEPCI
	if (pci_present())
	{
		ide_scan_pcibus(ide_scan_direction);
	}
#endif /* CONFIG_BLK_DEV_IDEPCI */

#ifdef CONFIG_ETRAX_IDE
	{
		extern void init_e100_ide(void);
		init_e100_ide();
	}
#endif /* CONFIG_ETRAX_IDE */
#ifdef CONFIG_BLK_DEV_CMD640
	{
		extern void ide_probe_for_cmd640x(void);
		ide_probe_for_cmd640x();
	}
#endif /* CONFIG_BLK_DEV_CMD640 */
#ifdef CONFIG_BLK_DEV_PDC4030
	{
		extern int ide_probe_for_pdc4030(void);
		(void) ide_probe_for_pdc4030();
	}
#endif /* CONFIG_BLK_DEV_PDC4030 */
#ifdef CONFIG_BLK_DEV_IDE_PMAC
	{
		extern void pmac_ide_probe(void);
		pmac_ide_probe();
	}
#endif /* CONFIG_BLK_DEV_IDE_PMAC */
#ifdef CONFIG_BLK_DEV_IDE_SWARM
	{
		extern void swarm_ide_probe(void);
		swarm_ide_probe();
	}
#endif /* CONFIG_BLK_DEV_IDE_SWARM */
#ifdef CONFIG_BLK_DEV_IDE_ICSIDE
	{
		extern void icside_init(void);
		icside_init();
	}
#endif /* CONFIG_BLK_DEV_IDE_ICSIDE */
#ifdef CONFIG_BLK_DEV_IDE_RAPIDE
	{
		extern void rapide_init(void);
		rapide_init();
	}
#endif /* CONFIG_BLK_DEV_IDE_RAPIDE */
#ifdef CONFIG_BLK_DEV_GAYLE
	{
		extern void gayle_init(void);
		gayle_init();
	}
#endif /* CONFIG_BLK_DEV_GAYLE */
#ifdef CONFIG_BLK_DEV_FALCON_IDE
	{
		extern void falconide_init(void);
		falconide_init();
	}
#endif /* CONFIG_BLK_DEV_FALCON_IDE */
#ifdef CONFIG_BLK_DEV_MAC_IDE
	{
		extern void macide_init(void);
		macide_init();
	}
#endif /* CONFIG_BLK_DEV_MAC_IDE */
#ifdef CONFIG_BLK_DEV_Q40IDE
	{
		extern void q40ide_init(void);
		q40ide_init();
	}
#endif /* CONFIG_BLK_DEV_Q40IDE */
#ifdef CONFIG_BLK_DEV_BUDDHA
	{
		extern void buddha_init(void);
		buddha_init();
	}
#endif /* CONFIG_BLK_DEV_BUDDHA */
#if defined(CONFIG_BLK_DEV_ISAPNP) && defined(CONFIG_ISAPNP)
	{
		extern void pnpide_init(int enable);
		pnpide_init(1);
	}
#endif /* CONFIG_BLK_DEV_ISAPNP */
}

void __init ide_init_builtin_drivers (void)
{
	/*
	 * Probe for special PCI and other "known" interface chipsets
	 */
	probe_for_hwifs ();

#ifdef CONFIG_BLK_DEV_IDE
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET])
		ide_get_lock(&ide_intr_lock, NULL, NULL); /* for atari only */

	(void) ideprobe_init();

	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET])
		ide_release_lock(&ide_intr_lock);	/* for atari only */
#endif /* CONFIG_BLK_DEV_IDE */

#ifdef CONFIG_PROC_FS
	proc_ide_create();
#endif
}

static int default_cleanup (ide_drive_t *drive)
{
	return ide_unregister_subdriver(drive);
}

static int default_standby (ide_drive_t *drive)
{
	return 0;
}

static int default_suspend (ide_drive_t *drive)
{
	return 0;
}

static int default_resume (ide_drive_t *drive)
{
	return 0;
}

static int default_flushcache (ide_drive_t *drive)
{
	return 0;
}

static ide_startstop_t default_do_request (ide_drive_t *drive, struct request *rq, sector_t block)
{
	ide_end_request(drive, 0, 0);
	return ide_stopped;
}

static int default_end_request (ide_drive_t *drive, int uptodate, int nr_sects)
{
	return ide_end_request(drive, uptodate, nr_sects);
}

static u8 default_sense (ide_drive_t *drive, const char *msg, u8 stat)
{
	return ide_dump_status(drive, msg, stat);
}

static ide_startstop_t default_error (ide_drive_t *drive, const char *msg, u8 stat)
{
	return ide_error(drive, msg, stat);
}

static int default_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int default_open (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	drive->usage--;
	return -EIO;
}

static void default_release (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
}

static int default_check_media_change (ide_drive_t *drive)
{
	return 1;
}

static void default_pre_reset (ide_drive_t *drive)
{
}

static unsigned long default_capacity (ide_drive_t *drive)
{
	return 0x7fffffff;
}

static ide_startstop_t default_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

	s->all = 0;
	drive->mult_req = 0;
	return ide_stopped;
}

static int default_attach (ide_drive_t *drive)
{
	printk(KERN_ERR "%s: does not support hotswap of device class !\n",
		drive->name);

	return 0;
}

static void setup_driver_defaults (ide_drive_t *drive)
{
	ide_driver_t *d = drive->driver;

	if (d->cleanup == NULL)		d->cleanup = default_cleanup;
	if (d->standby == NULL)		d->standby = default_standby;
	if (d->suspend == NULL)		d->suspend = default_suspend;
	if (d->resume == NULL)		d->resume = default_resume;
	if (d->flushcache == NULL)	d->flushcache = default_flushcache;
	if (d->do_request == NULL)	d->do_request = default_do_request;
	if (d->end_request == NULL)	d->end_request = default_end_request;
	if (d->sense == NULL)		d->sense = default_sense;
	if (d->error == NULL)		d->error = default_error;
	if (d->ioctl == NULL)		d->ioctl = default_ioctl;
	if (d->open == NULL)		d->open = default_open;
	if (d->release == NULL)		d->release = default_release;
	if (d->media_change == NULL)	d->media_change = default_check_media_change;
	if (d->pre_reset == NULL)	d->pre_reset = default_pre_reset;
	if (d->capacity == NULL)	d->capacity = default_capacity;
	if (d->special == NULL)		d->special = default_special;
	if (d->attach == NULL)		d->attach = default_attach;
}

int ide_register_subdriver (ide_drive_t *drive, ide_driver_t *driver, int version)
{
	unsigned long flags;
	
	spin_lock_irqsave(&ide_lock, flags);
	if (version != IDE_SUBDRIVER_VERSION || !drive->present ||
	    drive->driver != NULL || drive->usage) {
		spin_unlock_irqrestore(&ide_lock, flags);
		return 1;
	}
	drive->driver = driver;
	setup_driver_defaults(drive);
	spin_unlock_irqrestore(&ide_lock, flags);
	spin_lock(&drives_lock);
	list_add(&drive->list, &driver->drives);
	spin_unlock(&drives_lock);
	if (drive->autotune != 2) {
		/* DMA timings and setup moved to ide-probe.c */
		if (!driver->supports_dma && HWIF(drive)->ide_dma_off_quietly)
//			HWIF(drive)->ide_dma_off_quietly(drive);
			HWIF(drive)->ide_dma_off(drive);
		drive->dsc_overlap = (drive->next != drive && driver->supports_dsc_overlap);
		drive->nice1 = 1;
	}
	drive->suspend_reset = 0;
#ifdef CONFIG_PROC_FS
	ide_add_proc_entries(drive->proc, generic_subdriver_entries, drive);
	ide_add_proc_entries(drive->proc, driver->proc, drive);
#endif
	return 0;
}

EXPORT_SYMBOL(ide_register_subdriver);

int ide_unregister_subdriver (ide_drive_t *drive)
{
	unsigned long flags;
	
	spin_lock_irqsave(&ide_lock, flags);
	if (drive->usage || drive->driver == NULL || DRIVER(drive)->busy) {
		spin_unlock_irqrestore(&ide_lock, flags);
		return 1;
	}
#if defined(CONFIG_BLK_DEV_ISAPNP) && defined(CONFIG_ISAPNP) && defined(MODULE)
	pnpide_init(0);
#endif /* CONFIG_BLK_DEV_ISAPNP */
#ifdef CONFIG_PROC_FS
	ide_remove_proc_entries(drive->proc, DRIVER(drive)->proc);
	ide_remove_proc_entries(drive->proc, generic_subdriver_entries);
#endif
	auto_remove_settings(drive);
	drive->driver = NULL;
	spin_unlock_irqrestore(&ide_lock, flags);
	spin_lock(&drives_lock);
	list_del_init(&drive->list);
	spin_unlock(&drives_lock);
	return 0;
}

EXPORT_SYMBOL(ide_unregister_subdriver);

static int ide_drive_remove(struct device * dev)
{
	ide_drive_t * drive = container_of(dev,ide_drive_t,gendev);
	ide_driver_t * driver = drive->driver;

	if (driver && driver->standby)
		driver->standby(drive);
	return 0;
}

int ide_register_driver(ide_driver_t *driver)
{
	struct list_head list;

	spin_lock(&drivers_lock);
	list_add(&driver->drivers, &drivers);
	spin_unlock(&drivers_lock);

	spin_lock(&drives_lock);
	INIT_LIST_HEAD(&list);
	list_splice_init(&ata_unused, &list);
	spin_unlock(&drives_lock);

	while (!list_empty(&list)) {
		ide_drive_t *drive = list_entry(list.next, ide_drive_t, list);
		list_del_init(&drive->list);
		ata_attach(drive);
	}
	driver->gen_driver.name = driver->name;
	driver->gen_driver.bus = &ide_bus_type;
	driver->gen_driver.remove = ide_drive_remove;
	return driver_register(&driver->gen_driver);
}

EXPORT_SYMBOL(ide_register_driver);

void ide_unregister_driver(ide_driver_t *driver)
{
	ide_drive_t *drive;

	spin_lock(&drivers_lock);
	list_del(&driver->drivers);
	spin_unlock(&drivers_lock);

	while(!list_empty(&driver->drives)) {
		drive = list_entry(driver->drives.next, ide_drive_t, list);
		if (driver->cleanup(drive)) {
			printk("%s: cleanup_module() called while still busy\n", drive->name);
			BUG();
		}
		/* We must remove proc entries defined in this module.
		   Otherwise we oops while accessing these entries */
#ifdef CONFIG_PROC_FS
		if (drive->proc)
			ide_remove_proc_entries(drive->proc, driver->proc);
#endif
		ata_attach(drive);
	}
}

EXPORT_SYMBOL(ide_unregister_driver);

struct block_device_operations ide_fops[] = {{
	.owner			= THIS_MODULE,
	.open			= ide_open,
	.release		= ide_release,
	.ioctl			= ide_ioctl,
	.check_media_change	= ide_check_media_change,
	.revalidate		= ide_revalidate_disk
}};

EXPORT_SYMBOL(ide_fops);

/*
 * Probe module
 */
devfs_handle_t ide_devfs_handle;

EXPORT_SYMBOL(ide_lock);
EXPORT_SYMBOL(ide_probe);
EXPORT_SYMBOL(ide_devfs_handle);

struct bus_type ide_bus_type = {
	.name		= "ide",
};

/*
 * This is gets invoked once during initialization, to set *everything* up
 */
int __init ide_init (void)
{
	static char banner_printed;
	if (!banner_printed) {
		printk(KERN_INFO "Uniform Multi-Platform E-IDE driver " REVISION "\n");
		ide_devfs_handle = devfs_mk_dir(NULL, "ide", NULL);
		system_bus_speed = ide_system_bus_speed();
		banner_printed = 1;
	}

	bus_register(&ide_bus_type);

	init_ide_data();

	initializing = 1;
	ide_init_builtin_drivers();
	initializing = 0;

	return 0;
}

module_init(ide_init);

#ifdef MODULE
char *options = NULL;
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

int init_module (void)
{
	parse_options(options);
	return ide_init();
}

void cleanup_module (void)
{
	int index;

	for (index = 0; index < MAX_HWIFS; ++index) {
		ide_unregister(index);
#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
		if (ide_hwifs[index].dma_base)
			(void) ide_release_dma(&ide_hwifs[index]);
#endif /* (CONFIG_BLK_DEV_IDEDMA) && !(CONFIG_DMA_NONPCI) */
	}

#ifdef CONFIG_PROC_FS
	proc_ide_destroy();
#endif
	devfs_unregister (ide_devfs_handle);

	bus_unregister(&ide_bus_type);
}

#else /* !MODULE */

__setup("", ide_setup);

#endif /* MODULE */
