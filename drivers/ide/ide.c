/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 1994-1998,2002  Linus Torvalds and authors:
 *
 *	Mark Lord	<mlord@pobox.com>
 *      Gadi Oxman	<gadio@netvision.net.il>
 *      Andre Hedrick	<andre@linux-ide.org>
 *	Jens Axboe	<axboe@suse.de>
 *      Marcin Dalecki	<martin@dalecki.de>
 *
 *  See linux/MAINTAINERS for address of current maintainer.
 *
 * This is the basic common code of the ATA interface drivers.
 *
 * It supports up to MAX_HWIFS IDE interfaces, on one or more IRQs (usually 14
 * & 15).  There can be up to two drives per interface, as per the ATA-7 spec.
 *
 * Primary:    ide0, port 0x1f0; major=3;  hda is minor=0; hdb is minor=64
 * Secondary:  ide1, port 0x170; major=22; hdc is minor=0; hdd is minor=64
 * Tertiary:   ide2, port 0x???; major=33; hde is minor=0; hdf is minor=64
 * Quaternary: ide3, port 0x???; major=34; hdg is minor=0; hdh is minor=64
 * ...
 *
 *  Contributors:
 *
 *	Drew Eckhardt
 *	Branko Lankester	<lankeste@fwi.uva.nl>
 *	Mika Liljeberg
 *	Delman Lee		<delman@ieee.org>
 *	Scott Snyder		<snyder@fnald0.fnal.gov>
 *
 *  Some additional driver compile-time options are in <linux/ide.h>
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
#include "pcihost.h"

/*
 * Those will be moved into separate header files eventually.
 */
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

#ifdef CONFIG_PCI
static int ide_scan_direction;	/* THIS was formerly 2.2.x pci=reverse */
#endif

#if defined(__mc68000__) || defined(CONFIG_APUS)
/*
 * This is used by the Atari code to obtain access to the IDE interrupt,
 * which is shared between several drivers.
 */
static int irq_lock;
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

static void init_hwif_data(struct ata_channel *ch, unsigned int index)
{
	static const byte ide_major[] = {
		IDE0_MAJOR, IDE1_MAJOR, IDE2_MAJOR, IDE3_MAJOR, IDE4_MAJOR,
		IDE5_MAJOR, IDE6_MAJOR, IDE7_MAJOR, IDE8_MAJOR, IDE9_MAJOR
	};

	unsigned int unit;
	hw_regs_t hw;

	/* bulk initialize channel & drive info with zeros */
	memset(ch, 0, sizeof(struct ata_channel));
	memset(&hw, 0, sizeof(hw_regs_t));

	/* fill in any non-zero initial values */
	ch->index     = index;
	ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, &ch->irq);
	memcpy(&ch->hw, &hw, sizeof(hw));
	memcpy(ch->io_ports, hw.io_ports, sizeof(hw.io_ports));
	ch->noprobe	= !ch->io_ports[IDE_DATA_OFFSET];
#ifdef CONFIG_BLK_DEV_HD
	if (ch->io_ports[IDE_DATA_OFFSET] == HD_DATA)
		ch->noprobe = 1; /* may be overridden by ide_setup() */
#endif
	ch->major = ide_major[index];
	sprintf(ch->name, "ide%d", index);
	ch->bus_state = BUSSTATE_ON;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *drive = &ch->drives[unit];

		drive->type		= ATA_DISK;
		drive->select.all	= (unit<<4)|0xa0;
		drive->channel		= ch;
		drive->ctl		= 0x08;
		drive->ready_stat	= READY_STAT;
		drive->bad_wstat	= BAD_W_STAT;
		sprintf(drive->name, "hd%c", 'a' + (index * MAX_DRIVES) + unit);
		drive->max_failures	= IDE_DEFAULT_MAX_FAILURES;

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
 * ata_device structs as needed, rather than always consuming memory
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
int drive_is_flashcard(struct ata_device *drive)
{
	struct hd_driveid *id = drive->id;
	int i;

	char *flashcards[] = {
		"KODAK ATA_FLASH",
		"Hitachi CV",
		"SunDisk SDCFB",
		"HAGIWARA HPC",
		"LEXAR ATA_FLASH",
		"ATA_FLASH"		/* Simple Tech */
	};

	if (drive->removable && id != NULL) {
		if (id->config == 0x848a)
			return 1;	/* CompactFlash */
		for (i = 0; i < ARRAY_SIZE(flashcards); i++)
			if (!strncmp(id->model, flashcards[i],
				     strlen(flashcards[i])))
				return 1;
	}
	return 0;
}

int __ide_end_request(struct ata_device *drive, struct request *rq, int uptodate, unsigned int nr_secs)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(drive->channel->lock, flags);

	BUG_ON(!(rq->flags & REQ_STARTED));

	/*
	 * small hack to eliminate locking from ide_end_request to grab
	 * the first segment number of sectors
	 */
	if (!nr_secs)
		nr_secs = rq->hard_cur_sectors;

	/*
	 * Decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO.
	 */

	if (drive->state == DMA_PIO_RETRY && drive->retry_pio <= 3) {
		drive->state = 0;
		udma_enable(drive, 1, 1);
	}

	if (!end_that_request_first(rq, uptodate, nr_secs)) {
		add_blkdev_randomness(major(rq->rq_dev));
		if (!blk_rq_tagged(rq))
			blkdev_dequeue_request(rq);
		else
			blk_queue_end_tag(&drive->queue, rq);
		drive->rq = NULL;
		end_that_request_last(rq);
		ret = 0;
	}

	spin_unlock_irqrestore(drive->channel->lock, flags);

	return ret;
}

/*
 * This should get invoked any time we exit the driver to
 * wait for an interrupt response from a drive.  handler() points
 * at the appropriate code to handle the next interrupt, and a
 * timer is started to prevent us from waiting forever in case
 * something goes wrong (see the ide_timer_expiry() handler later on).
 */
void ide_set_handler(struct ata_device *drive, ata_handler_t handler,
		      unsigned long timeout, ata_expiry_t expiry)
{
	unsigned long flags;
	struct ata_channel *ch = drive->channel;

	spin_lock_irqsave(ch->lock, flags);

	if (ch->handler != NULL) {
		printk("%s: ide_set_handler: handler not null; old=%p, new=%p, from %p\n",
			drive->name, ch->handler, handler, __builtin_return_address(0));
	}
	ch->handler = handler;
	ch->expiry = expiry;
	ch->timer.expires = jiffies + timeout;
	add_timer(&ch->timer);

	spin_unlock_irqrestore(ch->lock, flags);
}

static u8 auto_reduce_xfer(struct ata_device *drive)
{
	if (!drive->crc_count)
		return drive->current_speed;
	drive->crc_count = 0;

	switch(drive->current_speed) {
		case XFER_UDMA_7:	return XFER_UDMA_6;
		case XFER_UDMA_6:	return XFER_UDMA_5;
		case XFER_UDMA_5:	return XFER_UDMA_4;
		case XFER_UDMA_4:	return XFER_UDMA_3;
		case XFER_UDMA_3:	return XFER_UDMA_2;
		case XFER_UDMA_2:	return XFER_UDMA_1;
		case XFER_UDMA_1:	return XFER_UDMA_0;
			/*
			 * OOPS we do not goto non Ultra DMA modes
			 * without iCRC's available we force
			 * the system to PIO and make the user
			 * invoke the ATA-1 ATA-2 DMA modes.
			 */
		case XFER_UDMA_0:
		default:		return XFER_PIO_4;
	}
}

static void check_crc_errors(struct ata_device *drive)
{
	if (!drive->using_dma)
	    return;

	/* check the DMA crc count */
	if (drive->crc_count) {
		udma_enable(drive, 0, 0);
		if (drive->channel->speedproc) {
			u8 pio = auto_reduce_xfer(drive);
		        drive->channel->speedproc(drive, pio);
		}
		if (drive->current_speed >= XFER_SW_DMA_0)
			udma_enable(drive, 1, 1);
	} else
		udma_enable(drive, 0, 1);
}

/*
 * The capacity of a drive according to its current geometry/LBA settings in
 * sectors.
 */
sector_t ata_capacity(struct ata_device *drive)
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

extern struct block_device_operations ide_fops[];

/*
 * This is called exactly *once* for each channel.
 */
void ide_geninit(struct ata_channel *ch)
{
	unsigned int unit;
	struct gendisk *gd = ch->gd;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *drive = &ch->drives[unit];

		if (!drive->present)
			continue;

		if (drive->type != ATA_DISK && drive->type != ATA_FLOPPY)
			continue;

		register_disk(gd,mk_kdev(ch->major,unit<<PARTN_BITS),
#ifdef CONFIG_BLK_DEV_ISAPNP
			(drive->forced_geom && drive->noprobe) ? 1 :
#endif
			1 << PARTN_BITS, ide_fops, ata_capacity(drive));
	}
}

static ide_startstop_t do_reset1(struct ata_device *, int); /* needed below */

/*
 * Poll the interface for completion every 50ms during an ATAPI drive reset
 * operation. If the drive has not yet responded, and we have not yet hit our
 * maximum waiting time, then the timer is restarted for another 50ms.
 */
static ide_startstop_t atapi_reset_pollfunc(struct ata_device *drive, struct request *__rq)
{
	struct ata_channel *ch = drive->channel;
	u8 stat;

	SELECT_DRIVE(ch,drive);
	udelay (10);

	if (OK_STAT(stat=GET_STAT(), 0, BUSY_STAT)) {
		printk("%s: ATAPI reset complete\n", drive->name);
	} else {
		if (time_before(jiffies, ch->poll_timeout)) {
			ide_set_handler (drive, atapi_reset_pollfunc, HZ/20, NULL);
			return ide_started;	/* continue polling */
		}
		ch->poll_timeout = 0;	/* end of polling */
		printk("%s: ATAPI reset timed out, status=0x%02x\n", drive->name, stat);
		return do_reset1 (drive, 1);	/* do it the old fashioned way */
	}
	ch->poll_timeout = 0;	/* done polling */

	return ide_stopped;
}

/*
 * Poll the interface for completion every 50ms during an ata reset operation.
 * If the drives have not yet responded, and we have not yet hit our maximum
 * waiting time, then the timer is restarted for another 50ms.
 */
static ide_startstop_t reset_pollfunc(struct ata_device *drive, struct request *__rq)
{
	struct ata_channel *ch = drive->channel;
	u8 stat;

	if (!OK_STAT(stat=GET_STAT(), 0, BUSY_STAT)) {
		if (time_before(jiffies, ch->poll_timeout)) {
			ide_set_handler(drive, reset_pollfunc, HZ/20, NULL);
			return ide_started;	/* continue polling */
		}
		printk("%s: reset timed out, status=0x%02x\n", ch->name, stat);
		drive->failures++;
	} else  {
		printk("%s: reset: ", ch->name);
		if ((stat = GET_ERR()) == 1) {
			printk("success\n");
			drive->failures = 0;
		} else {
			char *msg;

#if FANCY_STATUS_DUMPS
			u8 val;
			static char *messages[5] = {
				" passed",
				" formatter device",
				" sector buffer",
				" ECC circuitry",
				" controlling MPU error"
			};

			printk("master:");
			val = stat & 0x7f;
			if (val >= 1 && val <= 5)
				msg = messages[val -1];
			else
				msg = "";
			if (stat & 0x80)
				printk("; slave:");
#endif
			printk("%s error [%02x]\n", msg, stat);
			drive->failures++;
		}
	}
	ch->poll_timeout = 0;	/* done polling */

	return ide_stopped;
}

/*
 * Attempt to recover a confused drive by resetting it.  Unfortunately,
 * resetting a disk drive actually resets all devices on the same interface, so
 * it can really be thought of as resetting the interface rather than resetting
 * the drive.
 *
 * ATAPI devices have their own reset mechanism which allows them to be
 * individually reset without clobbering other devices on the same interface.
 *
 * Unfortunately, the IDE interface does not generate an interrupt to let us
 * know when the reset operation has finished, so we must poll for this.
 * Equally poor, though, is the fact that this may a very long time to
 * complete, (up to 30 seconds worst case).  So, instead of busy-waiting here
 * for it, we set a timer to poll at 50ms intervals.
 */

static ide_startstop_t do_reset1(struct ata_device *drive, int do_not_try_atapi)
{
	unsigned int unit;
	unsigned long flags;
	struct ata_channel *ch = drive->channel;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */

	/* For an ATAPI device, first try an ATAPI SRST. */
	if (drive->type != ATA_DISK && !do_not_try_atapi) {
		check_crc_errors(drive);
		SELECT_DRIVE(ch, drive);
		udelay (20);
		OUT_BYTE(WIN_SRST, IDE_COMMAND_REG);
		ch->poll_timeout = jiffies + WAIT_WORSTCASE;
		ide_set_handler(drive, atapi_reset_pollfunc, HZ/20, NULL);
		__restore_flags(flags);	/* local CPU only */

		return ide_started;
	}

	/*
	 * First, reset any device state data we were maintaining
	 * for any of the drives on this interface.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit)
		check_crc_errors(&ch->drives[unit]);

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
	ch->poll_timeout = jiffies + WAIT_WORSTCASE;
	ide_set_handler(drive, reset_pollfunc, HZ/20, NULL);

	/*
	 * Some weird controller like resetting themselves to a strange
	 * state when the disks are reset this way. At least, the Winbond
	 * 553 documentation says that
	 */
	if (ch->resetproc != NULL)
		ch->resetproc(drive);

	/* FIXME: we should handle mulit mode setting here as well ! */
#endif

	__restore_flags (flags);	/* local CPU only */

	return ide_started;
}

static inline u32 read_24(struct ata_device *drive)
{
	return  (IN_BYTE(IDE_HCYL_REG)<<16) |
		(IN_BYTE(IDE_LCYL_REG)<<8) |
		 IN_BYTE(IDE_SECTOR_REG);
}

/*
 * Clean up after success/failure of an explicit drive cmd
 *
 * Should be called under lock held.
 */
void ide_end_drive_cmd(struct ata_device *drive, struct request *rq, u8 stat, u8 err)
{
	if (rq->flags & REQ_DRIVE_CMD) {
		u8 *args = rq->buffer;
		rq->errors = !OK_STAT(stat, READY_STAT, BAD_STAT);
		if (args) {
			args[0] = stat;
			args[1] = err;
			args[2] = IN_BYTE(IDE_NSECTOR_REG);
		}
	} else if (rq->flags & REQ_DRIVE_ACB) {
		struct ata_taskfile *args = rq->special;

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
	}

	blkdev_dequeue_request(rq);
	drive->rq = NULL;
	end_that_request_last(rq);
}

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
u8 ide_dump_status(struct ata_device *drive, struct request * rq, const char *msg, u8 stat)
{
	unsigned long flags;
	byte err = 0;

	__save_flags (flags);	/* local CPU only */
	ide__sti();		/* local CPU only */
#if !(FANCY_STATUS_DUMPS)
	printk("%s: %s: status=0x%02x\n", drive->name, msg, stat);
#else
	printk(" { ");
	{
		char *msg = "";
		if (stat & BUSY_STAT)
			msg = "Busy";
		else {
			if (stat & READY_STAT)
				msg = "DriveReady";
			if (stat & WRERR_STAT)
				msg = "DeviceFault";
			if (stat & SEEK_STAT)
				msg = "SeekComplete";
			if (stat & DRQ_STAT)
				msg = "DataRequest";
			if (stat & ECC_STAT)
				msg = "CorrectedError";
			if (stat & INDEX_STAT)
				msg = "Index";
			if (stat & ERR_STAT)
				msg = "Error";
		}
	}
	printk("%s }\n", msg);
#endif
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
				if (rq)
					printk(", sector=%ld", rq->sector);
			}
		}
#endif
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
static void try_to_flush_leftover_data(struct ata_device *drive)
{
	int i;

	if (drive->type != ATA_DISK)
		return;

	for (i = (drive->mult_count ? drive->mult_count : 1); i > 0; --i) {
		u32 buffer[SECTOR_WORDS];

		ata_read(drive, buffer, SECTOR_WORDS);
	}
}

#ifdef CONFIG_BLK_DEV_PDC4030
# define IS_PDC4030_DRIVE (drive->channel->chipset == ide_pdc4030)
#else
# define IS_PDC4030_DRIVE (0)	/* auto-NULLs out pdc4030 code */
#endif

/*
 * We are still on the old request path here so issuing the recalibrate command
 * directly should just work.
 */
static int do_recalibrate(struct ata_device *drive)
{
	printk(KERN_INFO "%s: recalibrating!\n", drive->name);

	if (drive->type != ATA_DISK)
		return ide_stopped;

	if (!IS_PDC4030_DRIVE) {
		struct ata_taskfile args;

		memset(&args, 0, sizeof(args));
		args.taskfile.sector_count = drive->sect;
		args.taskfile.command = WIN_RESTORE;
		args.handler = recal_intr;
		ata_taskfile(drive, &args, NULL);
	}

	return IS_PDC4030_DRIVE ? ide_stopped : ide_started;
}

/*
 * Take action based on the error returned by the drive.
 */
ide_startstop_t ide_error(struct ata_device *drive, struct request *rq, const char *msg, byte stat)
{
	byte err;

	err = ide_dump_status(drive, rq, msg, stat);
	if (!drive || !rq)
		return ide_stopped;
	/* retry only "normal" I/O: */
	if (!(rq->flags & REQ_CMD)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, rq, stat, err);
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
		if (ata_ops(drive) && ata_ops(drive)->end_request)
			ata_ops(drive)->end_request(drive, rq, 0);
		else
			ide_end_request(drive, rq, 0);
	} else {
		++rq->errors;
		if ((rq->errors & ERROR_RESET) == ERROR_RESET)
			return do_reset1(drive, 0);
		if ((rq->errors & ERROR_RECAL) == ERROR_RECAL)
			return do_recalibrate(drive);
	}
	return ide_stopped;
}

/*
 * Invoked on completion of a special DRIVE_CMD.
 */
static ide_startstop_t drive_cmd_intr(struct ata_device *drive, struct request *rq)
{
	u8 *args = rq->buffer;
	u8 stat = GET_STAT();
	int retries = 10;

	ide__sti();	/* local CPU only */
	if ((stat & DRQ_STAT) && args && args[3]) {
		ata_read(drive, &args[4], args[3] * SECTOR_WORDS);

		while (((stat = GET_STAT()) & BUSY_STAT) && retries--)
			udelay(100);
	}

	if (!OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_error(drive, rq, "drive_cmd", stat); /* already calls ide_end_drive_cmd */
	ide_end_drive_cmd(drive, rq, stat, GET_ERR());

	return ide_stopped;
}

/*
 * Issue a simple drive command.  The drive must be selected beforehand.
 */
static void drive_cmd(struct ata_device *drive, u8 cmd, u8 nsect)
{
	ide_set_handler(drive, drive_cmd_intr, WAIT_CMD, NULL);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl, IDE_CONTROL_REG);	/* clear nIEN */
	SELECT_MASK(drive->channel, drive, 0);
	OUT_BYTE(nsect, IDE_NSECTOR_REG);
	OUT_BYTE(cmd, IDE_COMMAND_REG);
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
int ide_wait_stat(ide_startstop_t *startstop,
		struct ata_device *drive, struct request *rq,
		byte good, byte bad, unsigned long timeout)
{
	u8 stat;
	int i;

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		*startstop = ide_stopped;
		return 1;
	}

	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */
	if ((stat = GET_STAT()) & BUSY_STAT) {
		timeout += jiffies;
		while ((stat = GET_STAT()) & BUSY_STAT) {
			if (time_after(jiffies, timeout)) {
				*startstop = ide_error(drive, rq, "status timeout", stat);
				return 1;
			}
		}
	}

	/*
	 * Allow status to settle, then read it again.  A few rare drives
	 * vastly violate the 400ns spec here, so we'll wait up to 10usec for a
	 * "good" status rather than expensively fail things immediately.  This
	 * fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		if (OK_STAT((stat = GET_STAT()), good, bad))
			return 0;
	}
	*startstop = ide_error(drive, rq, "status error", stat);

	return 1;
}

/*
 * This initiates handling of a new I/O request.
 */
static ide_startstop_t start_request(struct ata_device *drive, struct request *rq)
{
	sector_t block;
	unsigned int minor = minor(rq->rq_dev);
	unsigned int unit = minor >> PARTN_BITS;
	struct ata_channel *ch = drive->channel;

	BUG_ON(!(rq->flags & REQ_STARTED));

#ifdef DEBUG
	printk("%s: start_request: current=0x%08lx\n", ch->name, (unsigned long) rq);
#endif

	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures))
		goto kill_rq;

	if (unit >= MAX_DRIVES) {
		printk(KERN_ERR "%s: bad device number: %s\n", ch->name, kdevname(rq->rq_dev));
		goto kill_rq;
	}

	block = rq->sector;

	/* Strange disk manager remap.
	 */
	if ((rq->flags & REQ_CMD) &&
	    (drive->type == ATA_DISK || drive->type == ATA_FLOPPY)) {
		block += drive->sect0;
	}

	/* Yecch - this will shift the entire interval, possibly killing some
	 * innocent following sector.
	 */
	if (block == 0 && drive->remap_0_to_1 == 1)
		block = 1;  /* redirect MBR access to EZ-Drive partn table */

#if (DISK_RECOVERY_TIME > 0)
	while ((read_timer() - ch->last_time) < DISK_RECOVERY_TIME);
#endif

	{
		ide_startstop_t res;

		SELECT_DRIVE(ch, drive);
		if (ide_wait_stat(&res, drive, rq, drive->ready_stat,
					BUSY_STAT|DRQ_STAT, WAIT_READY)) {
			printk(KERN_WARNING "%s: drive not ready for command\n", drive->name);

			return res;
		}
	}

	/* This issues a special drive command, usually initiated by ioctl()
	 * from the external hdparm program.
	 */
	if (rq->flags & REQ_DRIVE_ACB) {
		struct ata_taskfile *args = rq->special;

		if (!(args))
			goto args_error;

		ata_taskfile(drive, args, NULL);

		if (((args->command_type == IDE_DRIVE_TASK_RAW_WRITE) ||
		     (args->command_type == IDE_DRIVE_TASK_OUT)) &&
				args->prehandler && args->handler)
			return args->prehandler(drive, rq);

		return ide_started;
	}

	if (rq->flags & REQ_DRIVE_CMD) {
		u8 *args = rq->buffer;

		if (!(args))
			goto args_error;
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
			drive_cmd(drive, args[0], args[3]);

			return ide_started;
		}
		OUT_BYTE(args[2],IDE_FEATURE_REG);
		drive_cmd(drive, args[0], args[1]);

		return ide_started;
	}

	/* The normal way of execution is to pass and execute the request
	 * handler down to the device type driver.
	 */
	if (ata_ops(drive)) {
		if (ata_ops(drive)->do_request)
			return ata_ops(drive)->do_request(drive, rq, block);
		else {
			ide_end_request(drive, rq, 0);

			return ide_stopped;
		}
	}

	/*
	 * Error handling:
	 */

	printk(KERN_WARNING "%s: device type %d not supported\n",
			drive->name, drive->type);

kill_rq:
	if (ata_ops(drive) && ata_ops(drive)->end_request)
		ata_ops(drive)->end_request(drive, rq, 0);
	else
		ide_end_request(drive, rq, 0);

	return ide_stopped;

args_error:

	/* NULL as arguemnt is used by ioctls as a way of waiting for all
	 * current requests to be flushed from the queue.
	 */

#ifdef DEBUG
	printk("%s: DRIVE_CMD (null)\n", drive->name);
#endif
	ide_end_drive_cmd(drive, rq, GET_STAT(), GET_ERR());

	return ide_stopped;
}

ide_startstop_t restart_request(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	unsigned long flags;

	spin_lock_irqsave(ch->lock, flags);

	ch->handler = NULL;
	del_timer(&ch->timer);

	spin_unlock_irqrestore(ch->lock, flags);

	return start_request(drive, drive->rq);
}

/*
 * This is used by a drive to give excess bandwidth back by sleeping for
 * timeout jiffies.
 */
void ide_stall_queue(struct ata_device *drive, unsigned long timeout)
{
	if (timeout > WAIT_WORSTCASE)
		timeout = WAIT_WORSTCASE;
	drive->sleep = timeout + jiffies;
}


/*
 * Determine the longest sleep time for the devices at this channel.
 */
static unsigned long longest_sleep(struct ata_channel *channel)
{
	unsigned long sleep = 0;
	int unit;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *drive = &channel->drives[unit];

		if (!drive->present)
			continue;

		/* This device is sleeping and waiting to be serviced
		 * later than any other device we checked thus far.
		 */
		if (drive->sleep && (!sleep || time_after(sleep, drive->sleep)))
			sleep = drive->sleep;
	}

	return sleep;
}

/*
 * Select the next device which will be serviced.  This selects only between
 * devices on the same channel, since everything else will be scheduled on the
 * queue level.
 */
static struct ata_device *choose_urgent_device(struct ata_channel *channel)
{
	struct ata_device *choice = NULL;
	unsigned long sleep = 0;
	int unit;

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *drive = &channel->drives[unit];

		if (!drive->present)
			continue;

		/* There are no request pending for this device.
		 */
		if (list_empty(&drive->queue.queue_head))
			continue;

		/* This device still wants to remain idle.
		 */
		if (drive->sleep && time_after(drive->sleep, jiffies))
			continue;

		/* Take this device, if there is no device choosen thus far or
		 * it's more urgent.
		 */
		if (!choice || (drive->sleep && (!choice->sleep || time_after(choice->sleep, drive->sleep)))) {
			if (!blk_queue_plugged(&drive->queue))
				choice = drive;
		}
	}

	if (choice)
		return choice;

	sleep = longest_sleep(channel);

	if (sleep) {

		/*
		 * Take a short snooze, and then wake up again.  Just in case
		 * there are big differences in relative throughputs.. don't
		 * want to hog the cpu too much.
		 */

		if (time_after(jiffies, sleep - WAIT_MIN_SLEEP))
			sleep = jiffies + WAIT_MIN_SLEEP;
#if 1
		if (timer_pending(&channel->timer))
			printk(KERN_ERR "ide_set_handler: timer already active\n");
#endif
		set_bit(IDE_SLEEP, channel->active);
		mod_timer(&channel->timer, sleep);
		/* we purposely leave hwgroup busy while sleeping */
	} else {
		/* Ugly, but how can we sleep for the lock otherwise? perhaps
		 * from tq_disk? */
		ide_release_lock(&irq_lock);/* for atari only */
		clear_bit(IDE_BUSY, channel->active);
	}

	return NULL;
}


/*
 * Feed commands to a drive until it barfs.  Called with queue lock held and
 * busy channel.
 */
static void queue_commands(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	ide_startstop_t startstop = -1;

	for (;;) {
		struct request *rq = NULL;

		if (!test_bit(IDE_BUSY, ch->active))
			printk(KERN_ERR "%s: error: not busy while queueing!\n", drive->name);

		/* Abort early if we can't queue another command. for non
		 * tcq, ata_can_queue is always 1 since we never get here
		 * unless the drive is idle.
		 */
		if (!ata_can_queue(drive)) {
			if (!ata_pending_commands(drive))
				clear_bit(IDE_BUSY, ch->active);
			break;
		}

		drive->sleep = 0;

		if (test_bit(IDE_DMA, ch->active)) {
			printk(KERN_ERR "%s: error: DMA in progress...\n", drive->name);
			break;
		}

		/* There's a small window between where the queue could be
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
			if (!ata_pending_commands(drive))
				clear_bit(IDE_BUSY, ch->active);
			drive->rq = NULL;
			break;
		}

		/* If there are queued commands, we can't start a non-fs
		 * request (really, a non-queuable command) until the
		 * queue is empty.
		 */
		if (!(rq->flags & REQ_CMD) && ata_pending_commands(drive))
			break;

		drive->rq = rq;

		spin_unlock(drive->channel->lock);

		ide__sti();	/* allow other IRQs while we start this request */
		startstop = start_request(drive, rq);

		spin_lock_irq(drive->channel->lock);

		/* command started, we are busy */
		if (startstop == ide_started)
			break;

		/* start_request() can return either ide_stopped (no command
		 * was started), ide_started (command started, don't queue
		 * more), or ide_released (command started, try and queue
		 * more).
		 */
#if 0
		if (startstop == ide_stopped)
			set_bit(IDE_BUSY, &hwgroup->flags);
#endif

	}
}

/*
 * Issue a new request.
 * Caller must have already done spin_lock_irqsave(channel->lock, ...)
 */
static void do_request(struct ata_channel *channel)
{
	ide_get_lock(&irq_lock, ata_irq_request, hwgroup);/* for atari only: POSSIBLY BROKEN HERE(?) */
//	__cli();	/* necessary paranoia: ensure IRQs are masked on local CPU */

	while (!test_and_set_bit(IDE_BUSY, channel->active)) {
		struct ata_channel *ch;
		struct ata_device *drive;

		/* this will clear IDE_BUSY, if appropriate */
		drive = choose_urgent_device(channel);

		if (!drive)
			break;

		ch = drive->channel;

		if (channel->sharing_irq && ch != channel && ch->io_ports[IDE_CONTROL_OFFSET]) {
			/* set nIEN for previous channel */
			/* FIXME: check this! It appears to act on the current channel! */

			if (ch->intrproc)
				ch->intrproc(drive);
			else
				OUT_BYTE((drive)->ctl|2, ch->io_ports[IDE_CONTROL_OFFSET]);
		}

		/* Remember the last drive we where acting on.
		 */
		ch->drive = drive;

		queue_commands(drive);
	}

}

void do_ide_request(request_queue_t *q)
{
	do_request(q->queuedata);
}

/*
 * Un-busy the hwgroup etc, and clear any pending DMA status. we want to
 * retry the current request in PIO mode instead of risking tossing it
 * all away
 */
static void dma_timeout_retry(struct ata_device *drive, struct request *rq)
{
	/*
	 * end current dma transaction
	 */
	udma_stop(drive);
	udma_timeout(drive);

	/*
	 * Disable dma for now, but remember that we did so because of
	 * a timeout -- we'll reenable after we finish this next request
	 * (or rather the first chunk of it) in pio.
	 */
	drive->retry_pio++;
	drive->state = DMA_PIO_RETRY;
	udma_enable(drive, 0, 0);

	/*
	 * un-busy drive etc (hwgroup->busy is cleared on return) and
	 * make sure request is sane
	 */
	drive->rq = NULL;

	rq->errors = 0;
	if (rq->bio) {
		rq->sector = rq->bio->bi_sector;
		rq->current_nr_sectors = bio_iovec(rq->bio)->bv_len >> 9;
		rq->buffer = NULL;
	}
}

/*
 * This is our timeout function for all drive operations.  But note that it can
 * also be invoked as a result of a "sleep" operation triggered by the
 * mod_timer() call in do_request.
 */
void ide_timer_expiry(unsigned long data)
{
	struct ata_channel *ch = (struct ata_channel *) data;
	ata_handler_t *handler;
	ata_expiry_t *expiry;
	unsigned long flags;
	unsigned long wait;

	/*
	 * A global lock protects timers etc -- shouldn't get contention
	 * worth mentioning.
	 */

	spin_lock_irqsave(ch->lock, flags);
	del_timer(&ch->timer);

	handler = ch->handler;
	if (!handler) {

		/*
		 * Either a marginal timeout occurred (got the interrupt just
		 * as timer expired), or we were "sleeping" to give other
		 * devices a chance.  Either way, we don't really want to
		 * complain about anything.
		 */

		if (test_and_clear_bit(IDE_SLEEP, ch->active))
			clear_bit(IDE_BUSY, ch->active);
	} else {
		struct ata_device *drive = ch->drive;
		if (!drive) {
			printk(KERN_ERR "ide_timer_expiry: IRQ handler was NULL\n");
			ch->handler = NULL;
		} else {
			ide_startstop_t startstop;

			/* paranoia */
			if (!test_and_set_bit(IDE_BUSY, ch->active))
				printk(KERN_ERR "%s: ide_timer_expiry: IRQ handler was not busy??\n", drive->name);
			if ((expiry = ch->expiry) != NULL) {
				/* continue */
				if ((wait = expiry(drive, drive->rq)) != 0) {
					/* reengage timer */
					ch->timer.expires  = jiffies + wait;
					add_timer(&ch->timer);

					spin_unlock_irqrestore(ch->lock, flags);

					return;
				}
			}
			ch->handler = NULL;
			/*
			 * We need to simulate a real interrupt when invoking
			 * the handler() function, which means we need to globally
			 * mask the specific IRQ:
			 */

			spin_unlock(ch->lock);

			ch = drive->channel;
#if DISABLE_IRQ_NOSYNC
			disable_irq_nosync(ch->irq);
#else
			disable_irq(ch->irq);	/* disable_irq_nosync ?? */
#endif
			__cli();	/* local CPU only, as if we were handling an interrupt */
			if (ch->poll_timeout != 0) {
				startstop = handler(drive, drive->rq);
			} else if (drive_is_ready(drive)) {
				if (drive->waiting_for_dma)
					udma_irq_lost(drive);
				(void) ide_ack_intr(ch);
				printk("%s: lost interrupt\n", drive->name);
				startstop = handler(drive, drive->rq);
			} else {
				if (drive->waiting_for_dma) {
					startstop = ide_stopped;
					dma_timeout_retry(drive, drive->rq);
				} else
					startstop = ide_error(drive, drive->rq, "irq timeout", GET_STAT());
			}
			set_recovery_timer(ch);
			enable_irq(ch->irq);

			spin_lock_irq(ch->lock);

			if (startstop == ide_stopped)
				clear_bit(IDE_BUSY, ch->active);
		}
	}

	do_request(ch->drive->channel);

	spin_unlock_irqrestore(ch->lock, flags);
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
static void unexpected_irq(int irq)
{
	int i;

	for (i = 0; i < MAX_HWIFS; ++i) {
		u8 stat;
		struct ata_channel *ch = &ide_hwifs[i];

		if (!ch->present)
			continue;

		if (ch->irq != irq)
			continue;

		stat = IN_BYTE(ch->io_ports[IDE_STATUS_OFFSET]);
		if (!OK_STAT(stat, READY_STAT, BAD_STAT)) {
			/* Try to not flood the console with msgs */
			static unsigned long last_msgtime;
			static int count;

			++count;
			if (time_after(jiffies, last_msgtime + HZ)) {
				last_msgtime = jiffies;
				printk("%s: unexpected interrupt, status=0x%02x, count=%d\n",
						ch->name, stat, count);
			}
		}
	}
}

/*
 * Entry point for all interrupts, caller does __cli() for us.
 */
void ata_irq_request(int irq, void *data, struct pt_regs *regs)
{
	struct ata_channel *ch = data;
	unsigned long flags;
	struct ata_device *drive;
	ata_handler_t *handler = ch->handler;
	ide_startstop_t startstop;

	spin_lock_irqsave(ch->lock, flags);

	if (!ide_ack_intr(ch))
		goto out_lock;

	if (handler == NULL || ch->poll_timeout != 0) {
#if 0
		printk(KERN_INFO "ide: unexpected interrupt %d %d\n", ch->unit, irq);
#endif
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
#ifdef CONFIG_PCI
		if (ch->pci_dev && !ch->pci_dev->vendor)
#endif
		{
			/* Probably not a shared PCI interrupt, so we can
			 * safely try to do something about it:
			 */
			unexpected_irq(irq);
#ifdef CONFIG_PCI
		} else {
			/*
			 * Whack the status register, just in case we have a leftover pending IRQ.
			 */
			IN_BYTE(ch->io_ports[IDE_STATUS_OFFSET]);
#endif
		}
		goto out_lock;
	}
	drive = ch->drive;
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
	if (!test_and_set_bit(IDE_BUSY, ch->active))
		printk(KERN_ERR "%s: %s: hwgroup was not busy!?\n", drive->name, __FUNCTION__);
	ch->handler = NULL;
	del_timer(&ch->timer);

	spin_unlock(ch->lock);

	if (ch->unmask)
		ide__sti();	/* local CPU only */

	/* service this interrupt, may set handler for next interrupt */
	startstop = handler(drive, drive->rq);
	spin_lock_irq(ch->lock);

	/*
	 * Note that handler() may have set things up for another
	 * interrupt to occur soon, but it cannot happen until
	 * we exit from this routine, because it will be the
	 * same irq as is currently being serviced here, and Linux
	 * won't allow another of the same (on any CPU) until we return.
	 */
	set_recovery_timer(drive->channel);
	if (startstop == ide_stopped) {
		if (!ch->handler) {	/* paranoia */
			clear_bit(IDE_BUSY, ch->active);
			do_request(ch);
		} else {
			printk("%s: %s: huh? expected NULL handler on exit\n", drive->name, __FUNCTION__);
		}
	} else if (startstop == ide_released)
		queue_commands(drive);

out_lock:
	spin_unlock_irqrestore(ch->lock, flags);
}

/*
 * get_info_ptr() returns the (struct ata_device *) for a given device number.
 * It returns NULL if the given device number does not match any present drives.
 */
struct ata_device *get_info_ptr(kdev_t i_rdev)
{
	unsigned int major = major(i_rdev);
	int h;

	for (h = 0; h < MAX_HWIFS; ++h) {
		struct ata_channel *ch = &ide_hwifs[h];
		if (ch->present && major == ch->major) {
			int unit = DEVICE_NR(i_rdev);
			if (unit < MAX_DRIVES) {
				struct ata_device *drive = &ch->drives[unit];
				if (drive->present)
					return drive;
			}
			break;
		}
	}
	return NULL;
}

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
int ide_revalidate_disk(kdev_t i_rdev)
{
	struct ata_device *drive;
	unsigned long flags;
	int res;

	if ((drive = get_info_ptr(i_rdev)) == NULL)
		return -ENODEV;

	/* FIXME: The locking here doesn't make the slightest sense! */
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
	int i;

	for (i = 0; i < MAX_HWIFS; ++i) {
		int unit;
		struct ata_channel *ch = &ide_hwifs[i];

		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			struct ata_device *drive = &ch->drives[unit];

			if (drive->revalidate) {
				drive->revalidate = 0;
				if (!initializing)
					ide_revalidate_disk(mk_kdev(ch->major, unit<<PARTN_BITS));
			}
		}
	}
}

static void ide_driver_module (void)
{
	int i;

	/* Don't reinit the probe if there is already one channel detected. */

	for (i = 0; i < MAX_HWIFS; ++i) {
		if (ide_hwifs[i].present)
			goto revalidate;
	}

	ideprobe_init();

revalidate:
	revalidate_drives();
}

static int ide_open(struct inode * inode, struct file * filp)
{
	struct ata_device *drive;

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
	struct ata_device *drive;

	if (!(drive = get_info_ptr(inode->i_rdev)))
		return 0;

	drive->usage--;
	if (ata_ops(drive) && ata_ops(drive)->release)
		ata_ops(drive)->release(inode, file, drive);
	return 0;
}

void ide_unregister(struct ata_channel *ch)
{
	struct gendisk *gd;
	struct ata_device *d;
	spinlock_t *lock;
	int unit;
	int i;
	unsigned long flags;
	unsigned int p, minor;
	struct ata_channel old;
	int n_irq;
	int n_ch;

	spin_lock_irqsave(&ide_lock, flags);

	if (!ch->present)
		goto abort;

	put_device(&ch->dev);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device * drive = &ch->drives[unit];

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
	ch->present = 0;

	/*
	 * All clear?  Then blow away the buffer cache
	 */
	spin_unlock_irqrestore(&ide_lock, flags);

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device * drive = &ch->drives[unit];

		if (!drive->present)
			continue;

		minor = drive->select.b.unit << PARTN_BITS;
		for (p = 0; p < (1<<PARTN_BITS); ++p) {
			if (drive->part[p].nr_sects > 0) {
				kdev_t devp = mk_kdev(ch->major, minor+p);
				invalidate_device(devp, 0);
			}
		}
	}

	spin_lock_irqsave(&ide_lock, flags);

	/*
	 * Note that we only release the standard ports, and do not even try to
	 * handle any extra ports allocated for weird IDE interface chipsets.
	 */

	if (ch->straight8) {
		release_region(ch->io_ports[IDE_DATA_OFFSET], 8);
	} else {
		for (i = 0; i < 8; i++)
			if (ch->io_ports[i])
				release_region(ch->io_ports[i], 1);
	}
	if (ch->io_ports[IDE_CONTROL_OFFSET])
		release_region(ch->io_ports[IDE_CONTROL_OFFSET], 1);
/* FIXME: check if we can remove this ifdef */
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (ch->io_ports[IDE_IRQ_OFFSET])
		release_region(ch->io_ports[IDE_IRQ_OFFSET], 1);
#endif

	/*
	 * Remove us from the lock group.
	 */

	lock = ch->lock;
	d = ch->drive;
	for (i = 0; i < MAX_DRIVES; ++i) {
		struct ata_device *drive = &ch->drives[i];

		if (drive->de) {
			devfs_unregister (drive->de);
			drive->de = NULL;
		}
		if (!drive->present)
			continue;

		/* FIXME: possibly unneccessary */
		if (ch->drive == drive)
			ch->drive = NULL;

		if (drive->id != NULL) {
			kfree(drive->id);
			drive->id = NULL;
		}
		drive->present = 0;
		blk_cleanup_queue(&drive->queue);
	}
	if (d->present)
		ch->drive = d;


	/*
	 * Free the irq if we were the only channel using it.
	 *
	 * Free the lock group if we were the only member.
	 */
	n_irq = n_ch = 0;
	for (i = 0; i < MAX_HWIFS; ++i) {
		struct ata_channel *tmp = &ide_hwifs[i];

		if (!tmp->present)
			continue;

		if (tmp->irq == ch->irq)
			++n_irq;
		if (tmp->lock == ch->lock)
			++n_ch;
	}
	if (n_irq == 1)
		free_irq(ch->irq, ch);
	if (n_ch == 1) {
		kfree(ch->lock);
		kfree(ch->active);
		ch->lock = NULL;
		ch->active = NULL;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	ide_release_dma(ch);
#endif

	/*
	 * Remove us from the kernel's knowledge.
	 */
	unregister_blkdev(ch->major, ch->name);
	blk_dev[ch->major].data = NULL;
	blk_dev[ch->major].queue = NULL;
	blk_clear(ch->major);
	gd = ch->gd;
	if (gd) {
		del_gendisk(gd);
		kfree(gd->sizes);
		kfree(gd->part);
		if (gd->de_arr)
			kfree (gd->de_arr);
		if (gd->flags)
			kfree (gd->flags);
		kfree(gd);
		ch->gd = NULL;
	}

	/*
	 * Reinitialize the channel handler, but preserve any special methods for
	 * it.
	 */

	old = *ch;
	init_hwif_data(ch, ch->index);
	ch->lock = old.lock;
	ch->tuneproc = old.tuneproc;
	ch->speedproc = old.speedproc;
	ch->selectproc = old.selectproc;
	ch->resetproc = old.resetproc;
	ch->intrproc = old.intrproc;
	ch->maskproc = old.maskproc;
	ch->quirkproc = old.quirkproc;
	ch->ata_read = old.ata_read;
	ch->ata_write = old.ata_write;
	ch->atapi_read = old.atapi_read;
	ch->atapi_write = old.atapi_write;
	ch->XXX_udma = old.XXX_udma;
	ch->udma_enable = old.udma_enable;
	ch->udma_start = old.udma_start;
	ch->udma_stop = old.udma_stop;
	ch->udma_read = old.udma_read;
	ch->udma_write = old.udma_write;
	ch->udma_irq_status = old.udma_irq_status;
	ch->udma_timeout = old.udma_timeout;
	ch->udma_irq_lost = old.udma_irq_lost;
	ch->busproc = old.busproc;
	ch->bus_state = old.bus_state;
	ch->dma_base = old.dma_base;
	ch->dma_extra = old.dma_extra;
	ch->config_data = old.config_data;
	ch->select_data = old.select_data;
	ch->proc = old.proc;
	/* FIXME: most propably this is always right:! */
#ifndef CONFIG_BLK_DEV_IDECS
	ch->irq = old.irq;
#endif
	ch->major = old.major;
	ch->chipset = old.chipset;
	ch->autodma = old.autodma;
	ch->udma_four = old.udma_four;
#ifdef CONFIG_PCI
	ch->pci_dev = old.pci_dev;
#endif
	ch->straight8 = old.straight8;

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
		if (offsets[i] != -1)
			hw->io_ports[i] = base + offsets[i];
		else
			hw->io_ports[i] = 0;
	}
	if (offsets[IDE_CONTROL_OFFSET] == -1)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl;
/* FIMXE: check if we can remove this ifdef */
#if defined(CONFIG_AMIGA) || defined(CONFIG_MAC)
	if (offsets[IDE_IRQ_OFFSET] == -1)
		hw->io_ports[IDE_IRQ_OFFSET] = intr;
#endif
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
		ideprobe_init();
		revalidate_drives();
		/* FIXME: Do we really have to call it second time here?! */
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

int ide_spin_wait_hwgroup(struct ata_device *drive)
{
	/* FIXME: Wait on a proper timer. Instead of playing games on the
	 * spin_lock().
	 */

	unsigned long timeout = jiffies + (10 * HZ);

	spin_lock_irq(drive->channel->lock);

	while (test_bit(IDE_BUSY, drive->channel->active)) {

		spin_unlock_irq(drive->channel->lock);

		if (time_after(jiffies, timeout)) {
			printk("%s: channel busy\n", drive->name);
			return -EBUSY;
		}

		spin_lock_irq(drive->channel->lock);
	}

	return 0;
}

static int set_io_32bit(struct ata_device *drive, int arg)
{
	if (drive->channel->no_io_32bit)
		return -EIO;

	drive->channel->io_32bit = arg;

	return 0;
}

static int set_using_dma(struct ata_device *drive, int arg)
{
	if (!drive->driver)
		return -EPERM;

	if (!drive->id || !(drive->id->capability & 1) || !drive->channel->XXX_udma)
		return -EPERM;

	udma_enable(drive, arg, 1);

	return 0;
}

static int set_pio_mode(struct ata_device *drive, int arg)
{
	if (!drive->channel->tuneproc)
		return -ENOSYS;

	if (drive->channel->tuneproc != NULL)
		drive->channel->tuneproc(drive, (u8) arg);

	return 0;
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

/*
 * Handle ioctls.
 *
 * NOTE: Due to ridiculous coding habbits in the hdparm utility we have to
 * always return unsigned long in case we are returning simple values.
 */
static int ide_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int major, minor;
	struct ata_device *drive;
	struct request rq;
	kdev_t dev;

	dev = inode->i_rdev;
	major = major(dev);
	minor = minor(dev);

	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENODEV;


	/* Contrary to popular beleve we disallow even the reading of the ioctl
	 * values for users which don't have permission too. We do this becouse
	 * such information could be used by an attacker to deply a simple-user
	 * attack, which triggers bugs present only on a particular
	 * configuration.
	 */

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	ide_init_drive_cmd(&rq);
	switch (cmd) {
		case HDIO_GET_32BIT: {
			unsigned long val = drive->channel->io_32bit;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;
			return 0;
		}

		case HDIO_SET_32BIT: {
			int val;

			if (arg < 0 || arg > 1)
				return -EINVAL;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			val = set_io_32bit(drive, arg);
			spin_unlock_irq(drive->channel->lock);

			return val;
		}

		case HDIO_SET_PIO_MODE: {
			int val;

			if (arg < 0 || arg > 255)
				return -EINVAL;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			val = set_pio_mode(drive, arg);
			spin_unlock_irq(drive->channel->lock);

			return val;
		}

		case HDIO_GET_UNMASKINTR: {
			unsigned long val = drive->channel->unmask;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;

			return 0;
		}


		case HDIO_SET_UNMASKINTR: {
			if (arg < 0 || arg > 1)
				return -EINVAL;

			if (drive->channel->no_unmask)
				return -EIO;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			drive->channel->unmask = arg;
			spin_unlock_irq(drive->channel->lock);

			return 0;
		}

		case HDIO_GET_DMA: {
			unsigned long val = drive->using_dma;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;

			return 0;
		}

		case HDIO_SET_DMA: {
			int val;

			if (arg < 0 || arg > 1)
				return -EINVAL;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			val = set_using_dma(drive, arg);
			spin_unlock_irq(drive->channel->lock);

			return val;
		}

		case HDIO_GETGEO: {
			struct hd_geometry *loc = (struct hd_geometry *) arg;
			unsigned short bios_cyl = drive->bios_cyl; /* truncate */

			if (!loc || (drive->type != ATA_DISK && drive->type != ATA_FLOPPY))
				return -EINVAL;

			if (put_user(drive->bios_head, (byte *) &loc->heads))
				return -EFAULT;

			if (put_user(drive->bios_sect, (byte *) &loc->sectors))
				return -EFAULT;

			if (put_user(bios_cyl, (unsigned short *) &loc->cylinders))
				return -EFAULT;

			if (put_user((unsigned)drive->part[minor(inode->i_rdev)&PARTN_MASK].start_sect,
				(unsigned long *) &loc->start))
				return -EFAULT;

			return 0;
		}

		case HDIO_GETGEO_BIG_RAW: {
			struct hd_big_geometry *loc = (struct hd_big_geometry *) arg;

			if (!loc || (drive->type != ATA_DISK && drive->type != ATA_FLOPPY))
				return -EINVAL;

			if (put_user(drive->head, (u8 *) &loc->heads))
				return -EFAULT;

			if (put_user(drive->sect, (u8 *) &loc->sectors))
				return -EFAULT;

			if (put_user(drive->cyl, (unsigned int *) &loc->cylinders))
				return -EFAULT;

			if (put_user((unsigned)drive->part[minor(inode->i_rdev)&PARTN_MASK].start_sect,
				(unsigned long *) &loc->start))
				return -EFAULT;

			return 0;
		}

		case BLKRRPART: /* Re-read partition tables */
			return ide_revalidate_disk(inode->i_rdev);

		case HDIO_GET_IDENTITY:
			if (minor(inode->i_rdev) & PARTN_MASK)
				return -EINVAL;

			if (drive->id == NULL)
				return -ENOMSG;

			if (copy_to_user((char *)arg, (char *)drive->id, sizeof(*drive->id)))
				return -EFAULT;

			return 0;

		case HDIO_GET_NICE:
			return put_user(drive->dsc_overlap << IDE_NICE_DSC_OVERLAP |
					drive->atapi_overlap << IDE_NICE_ATAPI_OVERLAP,
					(long *) arg);

		case HDIO_DRIVE_CMD:
			if (!capable(CAP_SYS_RAWIO))
				return -EACCES;

			return ide_cmd_ioctl(drive, arg);

		case HDIO_SET_NICE:
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
			return block_ioctl(inode->i_bdev, cmd, arg);

		case HDIO_GET_BUSSTATE:
			if (put_user(drive->channel->bus_state, (long *)arg))
				return -EFAULT;

			return 0;

		case HDIO_SET_BUSSTATE:
			if (drive->channel->busproc)
				drive->channel->busproc(drive, (int)arg);

			return 0;

		/* Now check whatever this particular ioctl has a device type
		 * specific implementation.
		 */
		default:
			if (ata_ops(drive) && ata_ops(drive)->ioctl)
				return ata_ops(drive)->ioctl(drive, inode, file, cmd, arg);

			return -EINVAL;
	}
}

static int ide_check_media_change(kdev_t i_rdev)
{
	struct ata_device *drive;
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
 * "hdx=ide-scsi"	: the return of the ide-scsi flag, this is useful for
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
	struct ata_device *drive;
	unsigned int hw, unit;
	const char max_drive = 'a' + ((MAX_HWIFS * MAX_DRIVES) - 1);
	const char max_hwif  = '0' + (MAX_HWIFS - 1);

	if (!strncmp(s, "hd=", 3))	/* hd= is for hd.c driver and not us */
		return 0;

	if (strncmp(s,"ide",3) &&
	    strncmp(s,"idebus",6) &&
	    strncmp(s,"hd",2))		/* hdx= & hdxlun= */
		return 0;

	printk(KERN_INFO  "ide_setup: %s", s);
	init_ide_data ();

#ifdef CONFIG_BLK_DEV_IDEDOUBLER
	if (!strcmp(s, "ide=doubler")) {
		extern int ide_doubler;

		printk(KERN_INFO" : Enabled support for IDE doublers\n");
		ide_doubler = 1;

		return 1;
	}
#endif

	if (!strcmp(s, "ide=nodma")) {
		printk(KERN_INFO "ATA: Prevented DMA\n");
		noautodma = 1;

		return 1;
	}

#ifdef CONFIG_PCI
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
				hwif->slow = 1;
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
#endif
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
#ifdef CONFIG_PCI
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
int ide_end_request(struct ata_device *drive, struct request *rq, int uptodate)
{
	return __ide_end_request(drive, rq, uptodate, 0);
}

/*
 * Lookup ATA devices, which requested a particular driver.
 */
struct ata_device *ide_scan_devices(byte type, const char *name, struct ata_operations *driver, int n)
{
	unsigned int unit, index, i;

	for (index = 0, i = 0; index < MAX_HWIFS; ++index) {
		struct ata_channel *ch = &ide_hwifs[index];

		if (!ch->present)
			continue;

		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			struct ata_device *drive = &ch->drives[unit];
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
int ide_register_subdriver(struct ata_device *drive, struct ata_operations *driver)
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
		if (drive->channel->XXX_udma) {

			/*
			 * Force DMAing for the beginning of the check.  Some
			 * chipsets appear to do interesting things, if not
			 * checked and cleared.
			 *
			 *   PARANOIA!!!
			 */

			udma_enable(drive, 0, 0);
			drive->channel->XXX_udma(drive);
#ifdef CONFIG_BLK_DEV_IDE_TCQ_DEFAULT
			udma_tcq_enable(drive, 1);
#endif
		}

		/* Only CD-ROMs and tape drives support DSC overlap.  But only
		 * if they are alone on a channel. */
		if (drive->type == ATA_ROM || drive->type == ATA_TAPE) {
			int single = 0;
			int unit;

			for (unit = 0; unit < MAX_DRIVES; ++unit)
				if (drive->channel->drives[unit].present)
					++single;

			drive->dsc_overlap = (single == 1);
		} else
			drive->dsc_overlap = 0;

	}
	drive->revalidate = 1;
	drive->suspend_reset = 0;

	return 0;
}

/*
 * This is in fact the default cleanup routine.
 *
 * FIXME: Check whatever we maybe don't call it twice!.
 */
int ide_unregister_subdriver(struct ata_device *drive)
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
EXPORT_SYMBOL(ide_end_drive_cmd);
EXPORT_SYMBOL(__ide_end_request);
EXPORT_SYMBOL(ide_end_request);
EXPORT_SYMBOL(ide_revalidate_disk);
EXPORT_SYMBOL(ide_delay_50ms);
EXPORT_SYMBOL(ide_stall_queue);

EXPORT_SYMBOL(ide_register_hw);
EXPORT_SYMBOL(ide_register);
EXPORT_SYMBOL(ide_unregister);
EXPORT_SYMBOL(ide_setup_ports);
EXPORT_SYMBOL(get_info_ptr);

static int ide_notify_reboot (struct notifier_block *this, unsigned long event, void *x)
{
	int i;

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
		int unit;
		struct ata_channel *ch = &ide_hwifs[i];

		if (!ch->present)
			continue;

		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			struct ata_device *drive = &ch->drives[unit];

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

	printk(KERN_INFO "ATA/ATAPI driver v" VERSION "\n");

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

	printk(KERN_INFO "ATA: system bus speed %dMHz\n", system_bus_speed);

	init_ide_data ();

	initializing = 1;

#ifdef CONFIG_PCI
	/*
	 * Register the host chip drivers.
	 */
# ifdef CONFIG_BLK_DEV_PIIX
	init_piix();
# endif
# ifdef CONFIG_BLK_DEV_VIA82CXXX
	init_via82cxxx();
# endif
# ifdef CONFIG_BLK_DEV_PDC202XX
	init_pdc202xx();
# endif
# ifdef CONFIG_BLK_DEV_RZ1000
	init_rz1000();
# endif
# ifdef CONFIG_BLK_DEV_SIS5513
	init_sis5513();
# endif
# ifdef CONFIG_BLK_DEV_CMD64X
	init_cmd64x();
# endif
# ifdef CONFIG_BLK_DEV_OPTI621
	init_opti621();
# endif
# ifdef CONFIG_BLK_DEV_TRM290
	init_trm290();
# endif
# ifdef CONFIG_BLK_DEV_NS87415
	init_ns87415();
# endif
# ifdef CONFIG_BLK_DEV_AEC62XX
	init_aec62xx();
# endif
# ifdef CONFIG_BLK_DEV_SL82C105
	init_sl82c105();
# endif
# ifdef CONFIG_BLK_DEV_HPT34X
	init_hpt34x();
# endif
# ifdef CONFIG_BLK_DEV_HPT366
	init_hpt366();
# endif
# ifdef CONFIG_BLK_DEV_ALI15X3
	init_ali15x3();
# endif
# ifdef CONFIG_BLK_DEV_CY82C693
	init_cy82c693();
# endif
# ifdef CONFIG_BLK_DEV_CS5530
	init_cs5530();
# endif
# ifdef CONFIG_BLK_DEV_AMD74XX
	init_amd74xx();
# endif
# ifdef CONFIG_BLK_DEV_SVWKS
	init_svwks();
# endif
# ifdef CONFIG_BLK_DEV_IT8172
	init_it8172();
# endif

	init_ata_pci_misc();

	/*
	 * Detect and initialize "known" IDE host chip types.
	 */
	if (pci_present()) {
# ifdef CONFIG_PCI
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
		// ide_get_lock(&irq_lock, NULL, NULL);/* for atari only */
		disable_irq(ide_hwifs[0].irq);	/* disable_irq_nosync ?? */
//		disable_irq_nosync(ide_hwifs[0].irq);
	}
# endif

	ideprobe_init();

# if defined(__mc68000__) || defined(CONFIG_APUS)
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET]) {
		enable_irq(ide_hwifs[0].irq);
		ide_release_lock(&irq_lock);/* for atari only */
	}
# endif
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

static int __init init_ata(void)
{

	if (options != NULL && *options) {
		char *next = options;

		while ((options = next) != NULL) {
			if ((next = strchr(options,' ')) != NULL)
				*next++ = 0;
			if (!ide_setup(options))
				printk(KERN_ERR "Unknown option '%s'\n", options);
		}
	}
	return ata_module_init();
}

static void __exit cleanup_ata(void)
{
	int h;

	unregister_reboot_notifier(&ide_notifier);
	for (h = 0; h < MAX_HWIFS; ++h) {
		ide_unregister(&ide_hwifs[h]);
	}

	devfs_unregister(ide_devfs_handle);
}

module_init(init_ata);
module_exit(cleanup_ata);

#ifndef MODULE

/* command line option parser */
__setup("", ide_setup);

#endif
