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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
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
#include "ioctl.h"

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

int ata_end_request(struct ata_device *drive, struct request *rq, int uptodate, unsigned int nr_secs)
{
	unsigned long flags;
	struct ata_channel *ch = drive->channel;
	int ret = 1;

	spin_lock_irqsave(ch->lock, flags);

	BUG_ON(!(rq->flags & REQ_STARTED));

	/* FIXME: Make this "small" hack to eliminate locking from
	 * ata_end_request to grab the first segment number of sectors go away.
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
		add_blkdev_randomness(ch->major);
		if (!blk_rq_tagged(rq))
			blkdev_dequeue_request(rq);
		else
			blk_queue_end_tag(&drive->queue, rq);
		drive->rq = NULL;
		end_that_request_last(rq);
		ret = 0;
	}

	spin_unlock_irqrestore(ch->lock, flags);

	return ret;
}

/*
 * This should get invoked any time we exit the driver to
 * wait for an interrupt response from a drive.  handler() points
 * at the appropriate code to handle the next interrupt, and a
 * timer is started to prevent us from waiting forever in case
 * something goes wrong (see the ide_timer_expiry() handler later on).
 */
void ata_set_handler(struct ata_device *drive, ata_handler_t handler,
		      unsigned long timeout, ata_expiry_t expiry)
{
	unsigned long flags;
	struct ata_channel *ch = drive->channel;

	spin_lock_irqsave(ch->lock, flags);

	/* FIXME: change it later to BUG_ON(ch->handler)  --bzolnier */
	if (ch->handler)
		printk("%s: %s: handler not null; old=%p, new=%p, from %p\n",
			drive->name, __FUNCTION__, ch->handler, handler, __builtin_return_address(0));

	ch->handler = handler;

	ch->expiry = expiry;
	ch->timer.expires = jiffies + timeout;

	add_timer(&ch->timer);

	spin_unlock_irqrestore(ch->lock, flags);
}

static void check_crc_errors(struct ata_device *drive)
{
	if (!drive->using_dma)
		return;

	/* check the DMA crc count */
	if (drive->crc_count) {
		udma_enable(drive, 0, 0);
		if (drive->channel->speedproc) {
			u8 mode = drive->current_speed;
			drive->crc_count = 0;

			if (mode > XFER_UDMA_0)
				mode--;
			else

			/*
			 * We do not do non Ultra DMA modes.  Without iCRC's
			 * available, we force the system to PIO and make the
			 * user select the ATA-1 ATA-2 DMA modes himself.
			 */

				mode = XFER_PIO_4;

			drive->channel->speedproc(drive, mode);
		}
		if (drive->current_speed >= XFER_UDMA_0)
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

	return ~0UL;
}

static inline u32 read_24(struct ata_device *drive)
{
	return  (IN_BYTE(IDE_HCYL_REG) << 16) |
		(IN_BYTE(IDE_LCYL_REG) << 8) |
		 IN_BYTE(IDE_SECTOR_REG);
}

#if FANCY_STATUS_DUMPS
struct ata_bit_messages {
	u8 mask;
	u8 match;
	const char *msg;
};

static struct ata_bit_messages ata_status_msgs[] = {
	{ BUSY_STAT,  BUSY_STAT,  "busy"            },
	{ READY_STAT, READY_STAT, "drive ready"     },
	{ WRERR_STAT, WRERR_STAT, "device fault"    },
	{ SEEK_STAT,  SEEK_STAT,  "seek complete"   },
	{ DRQ_STAT,   DRQ_STAT,   "data request"    },
	{ ECC_STAT,   ECC_STAT,   "corrected error" },
	{ INDEX_STAT, INDEX_STAT, "index"           },
	{ ERR_STAT,   ERR_STAT,   "error"           }
};

static struct ata_bit_messages ata_error_msgs[] = {
	{ ICRC_ERR|ABRT_ERR,	ABRT_ERR,		"drive status error"	},
	{ ICRC_ERR|ABRT_ERR,	ICRC_ERR,		"bad sectorr"		},
	{ ICRC_ERR|ABRT_ERR,	ICRC_ERR|ABRT_ERR,	"invalid checksum"	},
	{ ECC_ERR,		ECC_ERR,		"uncorrectable error"	},
	{ ID_ERR,		ID_ERR,			"sector id not found"   },
	{ TRK0_ERR,		TRK0_ERR,		"track zero not found"	},
	{ MARK_ERR,		MARK_ERR,		"addr mark not found"   }
};

static void dump_bits(struct ata_bit_messages *msgs, int nr, byte bits)
{
	int i;
	int first = 1;

	printk(" [ ");

	for (i = 0; i < nr; i++, msgs++)
		if ((bits & msgs->mask) == msgs->match) {
			if (!first)
				printk(",");
			printk("%s", msgs->msg);
			first = 0;
		}

	printk("] ");
}
#else
# define dump_bits(msgs,nr,bits) do { } while (0)
#endif

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
u8 ata_dump(struct ata_device *drive, struct request * rq, const char *msg)
{
	unsigned long flags;
	u8 err = 0;

	/* FIXME:  --bzolnier */
	__save_flags(flags);
	local_irq_enable();

	printk("%s: %s: status=0x%02x", drive->name, msg, drive->status);
	dump_bits(ata_status_msgs, ARRAY_SIZE(ata_status_msgs), drive->status);
	printk("\n");

	if ((drive->status & (BUSY_STAT|ERR_STAT)) == ERR_STAT) {
		err = GET_ERR();
		printk("%s: %s: error=0x%02x", drive->name, msg, err);
#if FANCY_STATUS_DUMPS
		if (drive->type == ATA_DISK) {
			dump_bits(ata_error_msgs, ARRAY_SIZE(ata_error_msgs), err);

			if ((err & (BBD_ERR | ABRT_ERR)) == BBD_ERR || (err & (ECC_ERR|ID_ERR|MARK_ERR))) {
				if ((drive->id->command_set_2 & 0x0400) &&
				    (drive->id->cfs_enable_2 & 0x0400) &&
				    (drive->addressing == 1)) {
					__u64 sectors = 0;
					u32 low = 0, high = 0;
					low = read_24(drive);
					OUT_BYTE(0x80, drive->channel->io_ports[IDE_CONTROL_OFFSET]);
					high = read_24(drive);

					sectors = ((__u64)high << 24) | low;
					printk(", LBAsect=%lld, high=%d, low=%d", (long long) sectors, high, low);
				} else {
					u8 cur = IN_BYTE(IDE_SELECT_REG);
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
	__restore_flags (flags);

	return err;
}

/*
 * Take action based on the error returned by the drive.
 *
 * FIXME: Separate the error handling code out and call it only in cases where
 * we really wan't to try to recover from the error and not just reporting.
 */
ide_startstop_t ata_error(struct ata_device *drive, struct request *rq,	const char *msg)
{
	u8 err;
	u8 stat = drive->status;

	err = ata_dump(drive, rq, msg);

	/* Only try to recover from block I/O operations.
	 */
	if (!rq || !(rq->flags & REQ_CMD)) {
		rq->errors = 1;

		return ATA_OP_FINISHED;
	}

	/* other bits are useless when BUSY */
	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr))
		rq->errors |= ERROR_RESET;
	else if (drive->type == ATA_DISK) {
		/* The error bit has different meaning on cdrom and tape.
		 */
		if (stat & ERR_STAT) {
			if (err == ABRT_ERR) {
				if (drive->select.b.lba && IN_BYTE(IDE_COMMAND_REG) == WIN_SPECIFY)
					return ATA_OP_FINISHED; /* some newer drives don't support WIN_SPECIFY */
			} else if ((err & (ABRT_ERR | ICRC_ERR)) == (ABRT_ERR | ICRC_ERR))
				drive->crc_count++; /* UDMA crc error -- just retry the operation */
			else if (err & (BBD_ERR | ECC_ERR))	/* retries won't help these */
				rq->errors = ERROR_MAX;
		}

		/* As an alternative to resetting the drive, we try to clear
		 * the condition by reading a sector's worth of data from the
		 * drive.  Of course, this can not help if the drive is
		 * *waiting* for data from *us*.
		 */

		if ((stat & DRQ_STAT) && rq_data_dir(rq) == READ) {
			int i;

			for (i = (drive->mult_count ? drive->mult_count : 1); i > 0; --i) {
				u32 buffer[SECTOR_WORDS];

				ata_read(drive, buffer, SECTOR_WORDS);
			}
		}
	}

	/* Force an abort if not even the status data is available.  This will
	 * clear all pending IRQs on the drive as well.
	 */
	if (!ata_status(drive, 0, BUSY_STAT | DRQ_STAT))
		OUT_BYTE(WIN_IDLEIMMEDIATE, IDE_COMMAND_REG);

	/* Bail out immediately. */
	if (rq->errors >= ERROR_MAX) {
		printk(KERN_ERR "%s: max number of retries exceeded!\n", drive->name);
		if (ata_ops(drive) && ata_ops(drive)->end_request)
			ata_ops(drive)->end_request(drive, rq, 0);
		else
			ata_end_request(drive, rq, 0, 0);

		return ATA_OP_FINISHED;
	}

	++rq->errors;
	printk(KERN_INFO "%s: request error, nr. %d\n", drive->name, rq->errors);

	/*
	 * Attempt to recover a confused drive by resetting it.  Unfortunately,
	 * resetting a disk drive actually resets all devices on the same
	 * interface, so it can really be thought of as resetting the interface
	 * rather than resetting the drive.
	 *
	 * ATAPI devices have their own reset mechanism which allows them to be
	 * individually reset without clobbering other devices on the same
	 * interface.
	 *
	 * The IDE interface does not generate an interrupt to let us know when
	 * the reset operation has finished, so we must poll for this.  This
	 * may take a very long time to complete.
	 *
	 * Maybe we can check if we are in IRQ context and schedule the CPU
	 * during this time. But for certain we should block all data transfers
	 * on the channel in question during those operations.
	 */

	if ((rq->errors & ERROR_RESET) == ERROR_RESET) {
		unsigned int unit;
		struct ata_channel *ch = drive->channel;
		int ret;

		/* For an ATAPI device, first try an ATAPI SRST.
		 */

		if (drive->type != ATA_DISK) {
			check_crc_errors(drive);
			ata_select(drive, 20);
			udelay(1);
			ata_irq_enable(drive, 0);
			OUT_BYTE(WIN_SRST, IDE_COMMAND_REG);
			if (drive->quirk_list == 2)
				ata_irq_enable(drive, 1);
			udelay(1);
			ret = ata_status_poll(drive, 0, BUSY_STAT, WAIT_WORSTCASE, NULL);
			ata_mask(drive);

			if (ret == ATA_OP_READY) {
				printk("%s: ATAPI reset complete\n", drive->name);

				return ATA_OP_CONTINUES;
			} else
				printk(KERN_ERR "%s: ATAPI reset timed out, status=0x%02x\n",
						drive->name, drive->status);
		}

		/* Reset all devices on channel.
		 */

		/* First, reset any device state data we were maintaining for
		 * any of the drives on this interface.
		 */
		for (unit = 0; unit < MAX_DRIVES; ++unit)
			check_crc_errors(&ch->drives[unit]);

		/* And now actually perform the reset operation.
		 */
		printk("%s: ATA reset...\n", ch->name);
		ata_select(drive, 20);
		udelay(1);
		ata_irq_enable(drive, 0);

		/* This command actually looks suspicious, since I couldn't
		 * find it in any standard document.
		 */
		OUT_BYTE(0x04, ch->io_ports[IDE_CONTROL_OFFSET]);
		udelay(10);
		OUT_BYTE(WIN_NOP, ch->io_ports[IDE_CONTROL_OFFSET]);
		ret = ata_status_poll(drive, 0, BUSY_STAT, WAIT_WORSTCASE, NULL);
		ata_mask(drive);

		if (ret == ATA_OP_READY)
			printk("%s: ATA reset complete\n", drive->name);
		else
			printk(KERN_ERR "%s: ATA reset timed out, status=0x%02x\n",
					drive->name, drive->status);
		mdelay(100);
	}

	/* signal that we should retry this request */
	return ATA_OP_CONTINUES;
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
 * Issue a new request.
 * Caller must have already done spin_lock_irqsave(channel->lock, ...)
 */
static void do_request(struct ata_channel *channel)
{
	struct ata_channel *ch;
	struct ata_device *drive = NULL;
	unsigned int unit;
	ide_startstop_t ret;

	local_irq_disable();	/* necessary paranoia */

	/*
	 * Select the next device which will be serviced.  This selects
	 * only between devices on the same channel, since everything
	 * else will be scheduled on the queue level.
	 */

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		struct ata_device *tmp = &channel->drives[unit];

		if (!tmp->present)
			continue;

		/* There are no requests pending for this device.
		 */
		if (blk_queue_empty(&tmp->queue))
			continue;


		/* This device still wants to remain idle.
		 */
		if (tmp->sleep && time_after(tmp->sleep, jiffies))
			continue;

		/* Take this device, if there is no device choosen thus
		 * far or which is more urgent.
		 */
		if (!drive || (tmp->sleep && (!drive->sleep || time_after(drive->sleep, tmp->sleep)))) {
			if (!blk_queue_plugged(&tmp->queue))
				drive = tmp;
		}
	}

	if (!drive) {
		unsigned long sleep = 0;

		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			struct ata_device *tmp = &channel->drives[unit];

			if (!tmp->present)
				continue;

			/* This device is sleeping and waiting to be serviced
			 * earlier than any other device we checked thus far.
			 */
			if (tmp->sleep && (!sleep || time_after(sleep, tmp->sleep)))
				sleep = tmp->sleep;
		}

		if (sleep) {
			/*
			 * Take a short snooze, and then wake up again.  Just
			 * in case there are big differences in relative
			 * throughputs.. don't want to hog the cpu too much.
			 */

			if (time_after(jiffies, sleep - WAIT_MIN_SLEEP))
				sleep = jiffies + WAIT_MIN_SLEEP;
#if 1
			if (timer_pending(&channel->timer))
				printk(KERN_ERR "%s: timer already active\n", __FUNCTION__);
#endif
			set_bit(IDE_SLEEP, channel->active);
			mod_timer(&channel->timer, sleep);

			/*
			 * We purposely leave us busy while sleeping becouse we
			 * are prepared to handle the IRQ from it.
			 *
			 * FIXME: Make sure sleeping can't interferre with
			 * operations of other devices on the same channel.
			 */
		} else {
			/* FIXME: use queue plugging instead of active to block
			 * upper layers from stomping on us */
			/* Ugly, but how can we sleep for the lock otherwise?
			 * */

			ide_release_lock(&ide_irq_lock);/* for atari only */
			clear_bit(IDE_BUSY, channel->active);

			/* All requests are done.
			 *
			 * Disable IRQs from the last drive on this channel, to
			 * make sure that it wan't throw stones at us when we
			 * are not prepared to take them.
			 */

			if (channel->drive && !channel->drive->using_tcq)
				ata_irq_enable(channel->drive, 0);
		}

		return;
	}

	/* Remember the last drive we where acting on.
	 */
	ch = drive->channel;
	ch->drive = drive;

	/* Feed commands to a drive until it barfs.
	 */
	do {
		struct request *rq = NULL;
		sector_t block;

		/* Abort early if we can't queue another command. for non tcq,
		 * ata_can_queue is always 1 since we never get here unless the
		 * drive is idle.
		 */

		if (!ata_can_queue(drive)) {
			if (!ata_pending_commands(drive)) {
				clear_bit(IDE_BUSY, ch->active);
				if (drive->using_tcq)
					ata_irq_enable(drive, 0);
			}
			break;
		}

		drive->sleep = 0;

		if (test_bit(IDE_DMA, ch->active)) {
			printk(KERN_ERR "%s: error: DMA in progress...\n", drive->name);
			break;
		}

		/* There's a small window between where the queue could be
		 * replugged while we are in here when using tcq (in which case
		 * the queue is probably empty anyways...), so check and leave
		 * if appropriate. When not using tcq, this is still a severe
		 * BUG!
		 */

		if (blk_queue_plugged(&drive->queue)) {
			BUG_ON(!drive->using_tcq);
			break;
		}

		if (!(rq = elv_next_request(&drive->queue))) {
			if (!ata_pending_commands(drive)) {
				clear_bit(IDE_BUSY, ch->active);
				if (drive->using_tcq)
					ata_irq_enable(drive, 0);
			}
			drive->rq = NULL;

			break;
		}

		/* If there are queued commands, we can't start a
		 * non-fs request (really, a non-queuable command)
		 * until the queue is empty.
		 */
		if (!(rq->flags & REQ_CMD) && ata_pending_commands(drive))
			break;

		drive->rq = rq;

		spin_unlock(ch->lock);
		/* allow other IRQs while we start this request */
		local_irq_enable();

		/*
		 * This initiates handling of a new I/O request.
		 */

		BUG_ON(!(rq->flags & REQ_STARTED));

#ifdef DEBUG
		printk("%s: %s: current=0x%08lx\n", ch->name, __FUNCTION__, (unsigned long) rq);
#endif

		/* bail early if we've exceeded max_failures */
		if (drive->max_failures && (drive->failures > drive->max_failures))
			goto kill_rq;

		block = rq->sector;

		/* Strange disk manager remap.
		 */
		if (rq->flags & REQ_CMD)
			if (drive->type == ATA_DISK || drive->type == ATA_FLOPPY)
				block += drive->sect0;

		/* Yecch - this will shift the entire interval, possibly killing some
		 * innocent following sector.
		 */
		if (block == 0 && drive->remap_0_to_1 == 1)
			block = 1;  /* redirect MBR access to EZ-Drive partn table */

		ata_select(drive, 0);
		ret = ata_status_poll(drive, drive->ready_stat, BUSY_STAT | DRQ_STAT,
				WAIT_READY, rq);

		if (ret != ATA_OP_READY) {
			printk(KERN_ERR "%s: drive not ready for command\n", drive->name);

			goto kill_rq;
		}

		if (!ata_ops(drive)) {
			printk(KERN_WARNING "%s: device type %d not supported\n",
					drive->name, drive->type);
			goto kill_rq;
		}

		/* The normal way of execution is to pass and execute the request
		 * handler down to the device type driver.
		 */

		if (ata_ops(drive)->do_request) {
			ret = ata_ops(drive)->do_request(drive, rq, block);
		} else {
kill_rq:
			if (ata_ops(drive) && ata_ops(drive)->end_request)
				ata_ops(drive)->end_request(drive, rq, 0);
			else
				ata_end_request(drive, rq, 0, 0);
			ret = ATA_OP_FINISHED;

		}
		spin_lock_irq(ch->lock);

		/* continue if command started, so we are busy */
	} while (ret != ATA_OP_CONTINUES);
	/* make sure the BUSY bit is set */
	/* FIXME: perhaps there is some place where we miss to set it? */
//		set_bit(IDE_BUSY, ch->active);
}

void do_ide_request(request_queue_t *q)
{
	struct ata_channel *ch = q->queuedata;

	while (!test_and_set_bit(IDE_BUSY, ch->active)) {
		do_request(ch);
	}
}

/*
 * This is our timeout function for all drive operations.  But note that it can
 * also be invoked as a result of a "sleep" operation triggered by the
 * mod_timer() call in do_request.
 *
 * FIXME: This should take a drive context instead of a channel.
 * FIXME: This should not explicitly reenter the request handling engine.
 */
void ide_timer_expiry(unsigned long data)
{
	unsigned long flags;
	struct ata_channel *ch = (struct ata_channel *) data;

	spin_lock_irqsave(ch->lock, flags);
	del_timer(&ch->timer);

	if (!ch->drive) {
		printk(KERN_ERR "%s: channel->drive was NULL\n", __FUNCTION__);
		ch->handler = NULL;
	} else if (!ch->handler) {

		/*
		 * Either a marginal timeout occurred (got the interrupt just
		 * as timer expired), or we were "sleeping" to give other
		 * devices a chance.  Either way, we don't really want to
		 * complain about anything.
		 *
		 * FIXME: Do we really still have to clear IDE_BUSY here?
		 */

		if (test_and_clear_bit(IDE_SLEEP, ch->active))
			clear_bit(IDE_BUSY, ch->active);
	} else {
		struct ata_device *drive = ch->drive;
		ide_startstop_t ret;
		ata_handler_t *handler;

		/* paranoia */
		if (!test_and_set_bit(IDE_BUSY, ch->active))
			printk(KERN_ERR "%s: %s: channel was not busy?!\n",
					drive->name, __FUNCTION__);

		if (ch->expiry) {
			unsigned long wait;

			/* continue */
			ret = ch->expiry(drive, drive->rq, &wait);
			if (ret == ATA_OP_CONTINUES) {
				/* reengage timer */
				if (wait) {
					ch->timer.expires  = jiffies + wait;
					add_timer(&ch->timer);
				}

				spin_unlock_irqrestore(ch->lock, flags);

				return;
			}
		}

		/*
		 * We need to simulate a real interrupt when invoking the
		 * handler() function, which means we need to globally mask the
		 * specific IRQ:
		 */

		handler = ch->handler;
		ch->handler = NULL;

		spin_unlock(ch->lock);
#if DISABLE_IRQ_NOSYNC
		disable_irq_nosync(ch->irq);
#else
		disable_irq(ch->irq);	/* disable_irq_nosync ?? */
#endif

		local_irq_disable();
		if (ch->poll_timeout) {
			ret = handler(drive, drive->rq);
		} else if (ata_status_irq(drive)) {
			if (test_bit(IDE_DMA, ch->active))
				udma_irq_lost(drive);
			(void) ide_ack_intr(ch);
			printk("%s: lost interrupt\n", drive->name);
			ret = handler(drive, drive->rq);
		} else if (test_bit(IDE_DMA, ch->active)) {
			struct request *rq = drive->rq;

			/*
			 * Un-busy the hwgroup etc, and clear any pending DMA
			 * status. we want to retry the current request in PIO
			 * mode instead of risking tossing it all away.
			 */

			udma_stop(drive);
			udma_timeout(drive);

			/* Disable dma for now, but remember that we did so
			 * because of a timeout -- we'll reenable after we
			 * finish this next request (or rather the first chunk
			 * of it) in pio.
			 */

			drive->retry_pio++;
			drive->state = DMA_PIO_RETRY;
			udma_enable(drive, 0, 0);

			/* Un-busy drive etc (hwgroup->busy is cleared on
			 * return) and make sure request is sane.
			 */

			drive->rq = NULL;

			rq->errors = 0;
			if (rq->bio) {
				rq->sector = rq->bio->bi_sector;
				rq->current_nr_sectors = bio_iovec(rq->bio)->bv_len >> 9;
				rq->buffer = NULL;
			}
			ret = ATA_OP_FINISHED;
		} else
			ret = ata_error(drive, drive->rq, "irq timeout");

		enable_irq(ch->irq);
		spin_lock_irq(ch->lock);

		if (ret == ATA_OP_FINISHED) {
			/* Reenter the request handling engine. */
			do_request(ch);
		}
	}
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
 * This routine assumes IRQ are disabled on entry.
 *
 * If an unexpected interrupt happens on irq15 while we are handling irq14
 * and if the two interfaces are "serialized" (CMD640), then it looks like
 * we could screw up by interfering with a new request being set up for irq15.
 *
 * In reality, this is a non-issue.  The new command is not sent unless the
 * drive is ready to accept one, in which case we know the drive is not
 * trying to interrupt us.  And ata_set_handler() is always invoked before
 * completing the issuance of any new drive command, so we will not be
 * accidentally invoked as a result of any valid command completion interrupt.
 *
 */
static void unexpected_irq(int irq)
{
	/* Try to not flood the console with msgs */
	static unsigned long last_msgtime; /* = 0 */
	static int count;		   /* = 0 */
	int i;

	for (i = 0; i < MAX_HWIFS; ++i) {
		struct ata_channel *ch = &ide_hwifs[i];
		int j;
		struct ata_device *drive;

		if (!ch->present || ch->irq != irq)
			continue;

		for (j = 0; j < MAX_DRIVES; ++j) {
			drive = &ch->drives[j];

			/* this drive is idle */
			if (ata_status(drive, READY_STAT, BAD_STAT))
				continue;

			++count;

			/* don't report too frequently */
			if (!time_after(jiffies, last_msgtime + HZ))
				continue;

			last_msgtime = jiffies;
			printk("%s: unexpected interrupt, status=0x%02x, count=%d\n",
					ch->name, drive->status, count);
		}
	}
}

/*
 * Entry point for all interrupts. Aussumes disabled IRQs.
 */
void ata_irq_request(int irq, void *data, struct pt_regs *regs)
{
	struct ata_channel *ch = data;
	unsigned long flags;
	struct ata_device *drive;
	ata_handler_t *handler;
	ide_startstop_t ret;

	spin_lock_irqsave(ch->lock, flags);

	if (!ide_ack_intr(ch))
		goto out_lock;

	handler = ch->handler;
	drive = ch->drive;
	if (!handler || ch->poll_timeout) {
#if 0
		printk(KERN_INFO "ide: unexpected interrupt %d %d\n", ch->unit, irq);
#endif

		/*
		 * Not expecting an interrupt from this drive.  That means this
		 * could be:
		 *
		 * - an interrupt from another PCI device sharing the same PCI
		 * INT# as us.
		 *
		 * - a drive just entered sleep or standby mode, and is
		 * interrupting to let us know.
		 *
		 * - a spurious interrupt of unknown origin.
		 *
		 * For PCI, we cannot tell the difference, so in that case we
		 * just clear it and hope it goes away.
		 */

#ifdef CONFIG_PCI
		if (ch->pci_dev && !ch->pci_dev->vendor)
#endif
			unexpected_irq(irq);
#ifdef CONFIG_PCI
		else
			ata_status(drive, READY_STAT, BAD_STAT);
#endif

		goto out_lock;
	}
	if (!ata_status_irq(drive)) {
		/* This happens regularly when we share a PCI IRQ with another device.
		 * Unfortunately, it can also happen with some buggy drives that trigger
		 * the IRQ before their status register is up to date.  Hopefully we have
		 * enough advance overhead that the latter isn't a problem.
		 */

		goto out_lock;
	}

	/* paranoia */
	if (!test_and_set_bit(IDE_BUSY, ch->active))
		printk(KERN_ERR "%s: %s: channel was not busy!?\n", drive->name, __FUNCTION__);

	ch->handler = NULL;
	del_timer(&ch->timer);

	spin_unlock(ch->lock);

	if (ch->unmask)
		local_irq_enable();

	/*
	 * Service this interrupt, this may setup handler for next interrupt.
	 */
	ret = handler(drive, drive->rq);

	spin_lock_irq(ch->lock);

	/*
	 * Note that handler() may have set things up for another interrupt to
	 * occur soon, but it cannot happen until we exit from this routine,
	 * because it will be the same irq as is currently being serviced here,
	 * and Linux won't allow another of the same (on any CPU) until we
	 * return.
	 */

	if (ret == ATA_OP_FINISHED) {

		/* Reenter the request handling engine if we are not expecting
		 * another interrupt.
		 */

		if (!ch->handler)
			do_request(ch);
		else
			printk("%s: %s: huh? expected NULL handler on exit\n",
					drive->name, __FUNCTION__);
	}

out_lock:
	spin_unlock_irqrestore(ch->lock, flags);
}

static int ide_open(struct inode * inode, struct file * filp)
{
	struct ata_device *drive;

	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENXIO;

	/* Request a particular device type module.
	 *
	 * FIXME: The function which should rather requests the drivers is
	 * ide_driver_module(), since it seems illogical and even a bit
	 * dangerous to postpone this until open time!
	 */

#ifdef CONFIG_KMOD
	if (!drive->driver) {
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

	if (drive->driver == NULL)
		ide_driver_module();

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
	--drive->usage;

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

struct block_device_operations ide_fops[] = {{
	.owner =		THIS_MODULE,
	.open =			ide_open,
	.release =		ide_release,
	.ioctl =		ata_ioctl,
	.check_media_change =	ide_check_media_change,
	.revalidate =		ata_revalidate
}};

EXPORT_SYMBOL(ide_fops);
EXPORT_SYMBOL(ide_spin_wait_hwgroup);

EXPORT_SYMBOL(drive_is_flashcard);
EXPORT_SYMBOL(ide_timer_expiry);
EXPORT_SYMBOL(do_ide_request);

EXPORT_SYMBOL(ata_set_handler);
EXPORT_SYMBOL(ata_dump);
EXPORT_SYMBOL(ata_error);

EXPORT_SYMBOL(ata_end_request);
EXPORT_SYMBOL(ide_stall_queue);

EXPORT_SYMBOL(ide_setup_ports);
