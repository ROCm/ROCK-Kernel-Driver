/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 2002		Marcin Dalecki <martin@dalecki.de>
 *  Copyright (C) 2000		Michael Cornwell <cornwell@acm.org>
 *  Copyright (C) 2000		Andre Hedrick <andre@linux-ide.org>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 */

#include <linux/config.h>
#define __NO_VERSION__
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
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#define DEBUG_TASKFILE	0	/* unset when fixed */

#if DEBUG_TASKFILE
#define DTF(x...) printk(##x)
#else
#define DTF(x...)
#endif

/*
 * for now, taskfile requests are special :/
 */
static inline char *ide_map_rq(struct request *rq, unsigned long *flags)
{
	if (rq->bio)
		return bio_kmap_irq(rq->bio, flags) + ide_rq_offset(rq);
	else
		return rq->buffer + ((rq)->nr_sectors - (rq)->current_nr_sectors) * SECTOR_SIZE;
}

static inline void ide_unmap_rq(struct request *rq, char *to,
				unsigned long *flags)
{
	if (rq->bio)
		bio_kunmap_irq(to, flags);
}

/*
 * Data transfer functions for polled IO.
 */

static void ata_read_32(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	insl(IDE_DATA_REG, buffer, wcount);
}

static void ata_write_32(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	outsl(IDE_DATA_REG, buffer, wcount);
}

#if SUPPORT_SLOW_DATA_PORTS
static void ata_read_slow(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	unsigned short *ptr = (unsigned short *) buffer;

	while (wcount--) {
		*ptr++ = inw_p(IDE_DATA_REG);
		*ptr++ = inw_p(IDE_DATA_REG);
	}
}

static void ata_write_slow(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	unsigned short *ptr = (unsigned short *) buffer;

	while (wcount--) {
		outw_p(*ptr++, IDE_DATA_REG);
		outw_p(*ptr++, IDE_DATA_REG);
	}
}
#endif

static void ata_read_16(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	insw(IDE_DATA_REG, buffer, wcount<<1);
}

static void ata_write_16(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	outsw(IDE_DATA_REG, buffer, wcount<<1);
}

/*
 * This is used for most PIO data transfers *from* the device.
 */
void ata_read(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	int io_32bit;

	/*
	 * First check if this controller has defined a special function
	 * for handling polled ide transfers.
	 */
	if (drive->channel->ata_read) {
		drive->channel->ata_read(drive, buffer, wcount);
		return;
	}

	io_32bit = drive->channel->io_32bit;

	if (io_32bit) {
		ata_read_32(drive, buffer, wcount);
	} else {
#if SUPPORT_SLOW_DATA_PORTS
		if (drive->channel->slow)
			ata_read_slow(drive, buffer, wcount);
		else
#endif
			ata_read_16(drive, buffer, wcount);
	}
}

/*
 * This is used for most PIO data transfers *to* the device interface.
 */
void ata_write(struct ata_device *drive, void *buffer, unsigned int wcount)
{
	int io_32bit;

	if (drive->channel->ata_write) {
		drive->channel->ata_write(drive, buffer, wcount);
		return;
	}

	io_32bit = drive->channel->io_32bit;

	if (io_32bit) {
		ata_write_32(drive, buffer, wcount);
	} else {
#if SUPPORT_SLOW_DATA_PORTS
		if (drive->channel->slow)
			ata_write_slow(drive, buffer, wcount);
		else
#endif
			ata_write_16(drive, buffer, wcount);
	}
}

/*
 * Needed for PCI irq sharing
 */
int drive_is_ready(struct ata_device *drive)
{
	if (drive->waiting_for_dma)
		return udma_irq_status(drive);

	/*
	 * Need to guarantee 400ns since last command was issued?
	 */

	/* FIXME: promote this to the general status read method perhaps.
	 */
#ifdef CONFIG_IDEPCI_SHARE_IRQ
	/*
	 * We do a passive status test under shared PCI interrupts on
	 * cards that truly share the ATA side interrupt, but may also share
	 * an interrupt with another pci card/device.  We make no assumptions
	 * about possible isa-pnp and pci-pnp issues yet.
	 */
	if (drive->channel->io_ports[IDE_CONTROL_OFFSET])
		drive->status = GET_ALTSTAT();
	else
#endif
		ata_status(drive, 0, 0);	/* Note: this may clear a pending IRQ! */

	if (drive->status & BUSY_STAT)
		return 0;	/* drive busy:  definitely not interrupting */

	return 1;	/* drive ready: *might* be interrupting */
}

/*
 * Polling wait until the drive is ready.
 *
 * Stuff the first sector(s) by implicitly calling the handler driectly
 * therafter.
 */
void ata_poll_drive_ready(struct ata_device *drive)
{
	int i;

	if (drive_is_ready(drive))
		return;

	/* FIXME: Replace hard-coded 100, what about error handling?
	 */
	for (i = 0; i < 100; ++i) {
		if (drive_is_ready(drive))
			break;
	}
}

static ide_startstop_t pre_task_mulout_intr(struct ata_device *drive, struct request *rq)
{
	struct ata_taskfile *args = rq->special;
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, rq, DATA_READY, drive->bad_wstat, WAIT_DRQ))
		return startstop;

	ata_poll_drive_ready(drive);

	return args->handler(drive, rq);
}

static ide_startstop_t task_mulout_intr(struct ata_device *drive, struct request *rq)
{
	int ok;
	int mcount = drive->mult_count;
	ide_startstop_t startstop;


	/*
	 * FIXME: the drive->status checks here seem to be messy.
	 *
	 * (ks/hs): Handle last IRQ on multi-sector transfer,
	 * occurs after all data was sent in this chunk
	 */

	ok = ata_status(drive, DATA_READY, BAD_R_STAT);

	if (!ok || !rq->nr_sectors) {
		if (drive->status & (ERR_STAT | DRQ_STAT)) {
			startstop = ata_error(drive, rq, __FUNCTION__);

			return startstop;
		}
	}

	if (!rq->nr_sectors) {
		__ide_end_request(drive, rq, 1, rq->hard_nr_sectors);
		rq->bio = NULL;

		return ide_stopped;
	}

	if (!ok) {
		/* no data yet, so wait for another interrupt */
		if (!drive->channel->handler)
			ide_set_handler(drive, task_mulout_intr, WAIT_CMD, NULL);

		return ide_started;
	}

	do {
		char *buffer;
		int nsect = rq->current_nr_sectors;
		unsigned long flags;

		if (nsect > mcount)
			nsect = mcount;
		mcount -= nsect;

		buffer = bio_kmap_irq(rq->bio, &flags) + ide_rq_offset(rq);
		rq->sector += nsect;
		rq->nr_sectors -= nsect;
		rq->current_nr_sectors -= nsect;

		/* Do we move to the next bio after this? */
		if (!rq->current_nr_sectors) {
			/* remember to fix this up /jens */
			struct bio *bio = rq->bio->bi_next;

			/* end early if we ran out of requests */
			if (!bio) {
				mcount = 0;
			} else {
				rq->bio = bio;
				rq->current_nr_sectors = bio_iovec(bio)->bv_len >> 9;
			}
		}

		/*
		 * Ok, we're all setup for the interrupt re-entering us on the
		 * last transfer.
		 */
		ata_write(drive, buffer, nsect * SECTOR_WORDS);
		bio_kunmap_irq(buffer, &flags);
	} while (mcount);

	rq->errors = 0;
	if (!drive->channel->handler)
		ide_set_handler(drive, task_mulout_intr, WAIT_CMD, NULL);

	return ide_started;
}

ide_startstop_t ata_taskfile(struct ata_device *drive,
		struct ata_taskfile *args, struct request *rq)
{
	struct hd_driveid *id = drive->id;
	u8 HIHI = (drive->addressing) ? 0xE0 : 0xEF;

	/* (ks/hs): Moved to start, do not use for multiple out commands */
	if (args->handler != task_mulout_intr) {
		ata_irq_enable(drive, 1);
		ata_mask(drive);
	}

	if ((id->command_set_2 & 0x0400) &&
	    (id->cfs_enable_2 & 0x0400) &&
	    (drive->addressing == 1)) {
		OUT_BYTE(args->hobfile.feature, IDE_FEATURE_REG);
		OUT_BYTE(args->hobfile.sector_count, IDE_NSECTOR_REG);
		OUT_BYTE(args->hobfile.sector_number, IDE_SECTOR_REG);
		OUT_BYTE(args->hobfile.low_cylinder, IDE_LCYL_REG);
		OUT_BYTE(args->hobfile.high_cylinder, IDE_HCYL_REG);
	}

	OUT_BYTE(args->taskfile.feature, IDE_FEATURE_REG);
	OUT_BYTE(args->taskfile.sector_count, IDE_NSECTOR_REG);
	/* refers to number of sectors to transfer */
	OUT_BYTE(args->taskfile.sector_number, IDE_SECTOR_REG);
	/* refers to sector offset or start sector */
	OUT_BYTE(args->taskfile.low_cylinder, IDE_LCYL_REG);
	OUT_BYTE(args->taskfile.high_cylinder, IDE_HCYL_REG);

	OUT_BYTE((args->taskfile.device_head & HIHI) | drive->select.all, IDE_SELECT_REG);
	if (args->handler != NULL) {

		/* This is apparently supposed to reset the wait timeout for
		 * the interrupt to accur.
		 */

		ide_set_handler(drive, args->handler, WAIT_CMD, NULL);
		OUT_BYTE(args->taskfile.command, IDE_COMMAND_REG);

		/*
		 * Warning check for race between handler and prehandler for
		 * writing first block of data.  however since we are well
		 * inside the boundaries of the seek, we should be okay.
		 */

		if (args->prehandler != NULL)
			return args->prehandler(drive, rq);
	} else {
		/*
		 * FIXME: this is a gross hack, need to unify tcq dma proc and
		 * regular dma proc -- basically split stuff that needs to act
		 * on a request from things like ide_dma_check etc.
		 */

		if (!drive->using_dma)
			return ide_started;

		/* for dma commands we don't set the handler */
		if (args->taskfile.command == WIN_WRITEDMA
		 || args->taskfile.command == WIN_WRITEDMA_EXT)
			return !udma_write(drive, rq);
		else if (args->taskfile.command == WIN_READDMA
		      || args->taskfile.command == WIN_READDMA_EXT)
			return !udma_read(drive, rq);
#ifdef CONFIG_BLK_DEV_IDE_TCQ
		else if (args->taskfile.command == WIN_WRITEDMA_QUEUED
		      || args->taskfile.command == WIN_WRITEDMA_QUEUED_EXT
		      || args->taskfile.command == WIN_READDMA_QUEUED
		      || args->taskfile.command == WIN_READDMA_QUEUED_EXT)
			return udma_tcq_taskfile(drive, rq);
#endif
		else {
			printk("ata_taskfile: unknown command %x\n", args->taskfile.command);
			return ide_stopped;
		}
	}

	return ide_started;
}

/*
 * This is invoked on completion of a WIN_RESTORE (recalibrate) cmd.
 */
ide_startstop_t recal_intr(struct ata_device *drive, struct request *rq)
{
	if (!ata_status(drive, READY_STAT, BAD_STAT))
		return ata_error(drive, rq, __FUNCTION__);

	return ide_stopped;
}

/*
 * Handler for commands without a data phase
 */
ide_startstop_t task_no_data_intr(struct ata_device *drive, struct request *rq)
{
	struct ata_taskfile *args = rq->special;

	ide__sti();	/* local CPU only */

	if (!ata_status(drive, READY_STAT, BAD_STAT)) {
		/* Keep quiet for NOP because it is expected to fail. */
		if (args && args->taskfile.command != WIN_NOP)
			return ata_error(drive, rq, __FUNCTION__);
	}

	if (args)
		ide_end_drive_cmd(drive, rq, GET_ERR());

	return ide_stopped;
}

/*
 * Handler for command with PIO data-in phase
 */
static ide_startstop_t task_in_intr(struct ata_device *drive, struct request *rq)
{
	char *buf = NULL;
	unsigned long flags;

	if (!ata_status(drive, DATA_READY, BAD_R_STAT)) {
		if (drive->status & (ERR_STAT|DRQ_STAT))
			return ata_error(drive, rq, __FUNCTION__);

		if (!(drive->status & BUSY_STAT)) {
			DTF("task_in_intr to Soon wait for next interrupt\n");
			ide_set_handler(drive, task_in_intr, WAIT_CMD, NULL);

			return ide_started;
		}
	}
	DTF("stat: %02x\n", drive->status);
	buf = ide_map_rq(rq, &flags);
	DTF("Read: %p, rq->current_nr_sectors: %d\n", buf, (int) rq->current_nr_sectors);

	ata_read(drive, buf, SECTOR_WORDS);
	ide_unmap_rq(rq, buf, &flags);

	/* First segment of the request is complete. note that this does not
	 * necessarily mean that the entire request is done!! this is only true
	 * if ide_end_request() returns 0.
	 */

	if (--rq->current_nr_sectors <= 0) {
		DTF("Request Ended stat: %02x\n", drive->status);
		if (!ide_end_request(drive, rq, 1))
			return ide_stopped;
	}

	/* still data left to transfer */
	ide_set_handler(drive, task_in_intr,  WAIT_CMD, NULL);

	return ide_started;
}

static ide_startstop_t pre_task_out_intr(struct ata_device *drive, struct request *rq)
{
	struct ata_taskfile *args = rq->special;
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, rq, DATA_READY, drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing %s\n", drive->name, drive->mult_count ? "MULTWRITE" : "WRITE");
		return startstop;
	}

	/* (ks/hs): Fixed Multi Write */
	if ((args->taskfile.command != WIN_MULTWRITE) &&
	    (args->taskfile.command != WIN_MULTWRITE_EXT)) {
		unsigned long flags;
		char *buf = ide_map_rq(rq, &flags);
		/* For Write_sectors we need to stuff the first sector */
		ata_write(drive, buf, SECTOR_WORDS);
		rq->current_nr_sectors--;
		ide_unmap_rq(rq, buf, &flags);
	} else {
		ata_poll_drive_ready(drive);
		return args->handler(drive, rq);
	}
	return ide_started;
}

/*
 * Handler for command with PIO data-out phase
 */
static ide_startstop_t task_out_intr(struct ata_device *drive, struct request *rq)
{
	char *buf = NULL;
	unsigned long flags;

	if (!ata_status(drive, DRIVE_READY, drive->bad_wstat))
		return ata_error(drive, rq, __FUNCTION__);

	if (!rq->current_nr_sectors)
		if (!ide_end_request(drive, rq, 1))
			return ide_stopped;

	if ((rq->nr_sectors == 1) != (drive->status & DRQ_STAT)) {
		buf = ide_map_rq(rq, &flags);
		DTF("write: %p, rq->current_nr_sectors: %d\n", buf, (int) rq->current_nr_sectors);

		ata_write(drive, buf, SECTOR_WORDS);
		ide_unmap_rq(rq, buf, &flags);
		rq->errors = 0;
		rq->current_nr_sectors--;
	}

	ide_set_handler(drive, task_out_intr, WAIT_CMD, NULL);

	return ide_started;
}

/*
 * Handler for command with Read Multiple
 */
static ide_startstop_t task_mulin_intr(struct ata_device *drive, struct request *rq)
{
	char *buf = NULL;
	unsigned int msect, nsect;
	unsigned long flags;

	if (!ata_status(drive, DATA_READY, BAD_R_STAT)) {
		if (drive->status & (ERR_STAT|DRQ_STAT))
			return ata_error(drive, rq, __FUNCTION__);

		/* no data yet, so wait for another interrupt */
		ide_set_handler(drive, task_mulin_intr, WAIT_CMD, NULL);
		return ide_started;
	}

	/* (ks/hs): Fixed Multi-Sector transfer */
	msect = drive->mult_count;

	do {
		nsect = rq->current_nr_sectors;
		if (nsect > msect)
			nsect = msect;

		buf = ide_map_rq(rq, &flags);

		DTF("Multiread: %p, nsect: %d , rq->current_nr_sectors: %d\n",
			buf, nsect, rq->current_nr_sectors);
		ata_read(drive, buf, nsect * SECTOR_WORDS);
		ide_unmap_rq(rq, buf, &flags);
		rq->errors = 0;
		rq->current_nr_sectors -= nsect;
		msect -= nsect;
		if (!rq->current_nr_sectors) {
			if (!ide_end_request(drive, rq, 1))
				return ide_stopped;
		}
	} while (msect);


	/*
	 * more data left
	 */
	ide_set_handler(drive, task_mulin_intr, WAIT_CMD, NULL);
	return ide_started;
}

/* Called to figure out the type of command being called.
 */
void ide_cmd_type_parser(struct ata_taskfile *args)
{
	struct hd_drive_task_hdr *taskfile = &args->taskfile;

	args->prehandler = NULL;
	args->handler = NULL;

	switch(args->taskfile.command) {
		case WIN_IDENTIFY:
		case WIN_PIDENTIFY:
			args->handler = task_in_intr;
			args->command_type = IDE_DRIVE_TASK_IN;
			return;

		case CFA_TRANSLATE_SECTOR:
		case WIN_READ:
		case WIN_READ_EXT:
		case WIN_READ_BUFFER:
			args->handler = task_in_intr;
			args->command_type = IDE_DRIVE_TASK_IN;
			return;

		case CFA_WRITE_SECT_WO_ERASE:
		case WIN_WRITE:
		case WIN_WRITE_EXT:
		case WIN_WRITE_VERIFY:
		case WIN_WRITE_BUFFER:
		case WIN_DOWNLOAD_MICROCODE:
			args->prehandler = pre_task_out_intr;
			args->handler = task_out_intr;
			args->command_type = IDE_DRIVE_TASK_RAW_WRITE;
			return;

		case WIN_MULTREAD:
		case WIN_MULTREAD_EXT:
			args->handler = task_mulin_intr;
			args->command_type = IDE_DRIVE_TASK_IN;
			return;

		case CFA_WRITE_MULTI_WO_ERASE:
		case WIN_MULTWRITE:
		case WIN_MULTWRITE_EXT:
			args->prehandler = pre_task_mulout_intr;
			args->handler = task_mulout_intr;
			args->command_type = IDE_DRIVE_TASK_RAW_WRITE;
			return;

		case WIN_SECURITY_DISABLE:
		case WIN_SECURITY_ERASE_UNIT:
		case WIN_SECURITY_SET_PASS:
		case WIN_SECURITY_UNLOCK:
			args->handler = task_out_intr;
			args->command_type = IDE_DRIVE_TASK_OUT;
			return;

		case WIN_SMART:
			if (taskfile->feature == SMART_WRITE_LOG_SECTOR)
				args->prehandler = pre_task_out_intr;

			args->taskfile.low_cylinder = SMART_LCYL_PASS;
			args->taskfile.high_cylinder = SMART_HCYL_PASS;

			switch(args->taskfile.feature) {
				case SMART_READ_VALUES:
				case SMART_READ_THRESHOLDS:
				case SMART_READ_LOG_SECTOR:
					args->handler = task_in_intr;
					args->command_type = IDE_DRIVE_TASK_IN;
					return;

				case SMART_WRITE_LOG_SECTOR:
					args->handler = task_out_intr;
					args->command_type = IDE_DRIVE_TASK_OUT;
					return;

				default:
					args->handler = task_no_data_intr;
					args->command_type = IDE_DRIVE_TASK_NO_DATA;
					return;

			}
#ifdef CONFIG_BLK_DEV_IDEDMA
		case WIN_READDMA:
		case WIN_IDENTIFY_DMA:
		case WIN_READDMA_QUEUED:
		case WIN_READDMA_EXT:
		case WIN_READDMA_QUEUED_EXT:
			args->command_type = IDE_DRIVE_TASK_IN;
			return;

		case WIN_WRITEDMA:
		case WIN_WRITEDMA_QUEUED:
		case WIN_WRITEDMA_EXT:
		case WIN_WRITEDMA_QUEUED_EXT:
			args->command_type = IDE_DRIVE_TASK_RAW_WRITE;
			return;

#endif
		case WIN_SETFEATURES:
			args->handler = task_no_data_intr;
			switch(args->taskfile.feature) {
				case SETFEATURES_XFER:
					args->command_type = IDE_DRIVE_TASK_SET_XFER;
					return;
				case SETFEATURES_DIS_DEFECT:
				case SETFEATURES_EN_APM:
				case SETFEATURES_DIS_MSN:
				case SETFEATURES_EN_RI:
				case SETFEATURES_EN_SI:
				case SETFEATURES_DIS_RPOD:
				case SETFEATURES_DIS_WCACHE:
				case SETFEATURES_EN_DEFECT:
				case SETFEATURES_DIS_APM:
				case SETFEATURES_EN_MSN:
				case SETFEATURES_EN_RLA:
				case SETFEATURES_PREFETCH:
				case SETFEATURES_EN_RPOD:
				case SETFEATURES_DIS_RI:
				case SETFEATURES_DIS_SI:
				default:
					args->command_type = IDE_DRIVE_TASK_NO_DATA;
					return;
			}

		case WIN_SPECIFY:
			args->handler = task_no_data_intr;
			args->command_type = IDE_DRIVE_TASK_NO_DATA;
			return;

		case WIN_RESTORE:
			args->handler = recal_intr;
			args->command_type = IDE_DRIVE_TASK_NO_DATA;
			return;

		case WIN_DIAGNOSE:
		case WIN_FLUSH_CACHE:
		case WIN_FLUSH_CACHE_EXT:
		case WIN_STANDBYNOW1:
		case WIN_STANDBYNOW2:
		case WIN_SLEEPNOW1:
		case WIN_SLEEPNOW2:
		case WIN_SETIDLE1:
		case WIN_CHECKPOWERMODE1:
		case WIN_CHECKPOWERMODE2:
		case WIN_GETMEDIASTATUS:
		case WIN_MEDIAEJECT:
		case CFA_REQ_EXT_ERROR_CODE:
		case CFA_ERASE_SECTORS:
		case WIN_VERIFY:
		case WIN_VERIFY_EXT:
		case WIN_SEEK:
		case WIN_READ_NATIVE_MAX:
		case WIN_SET_MAX:
		case WIN_READ_NATIVE_MAX_EXT:
		case WIN_SET_MAX_EXT:
		case WIN_SECURITY_ERASE_PREPARE:
		case WIN_SECURITY_FREEZE_LOCK:
		case WIN_DOORLOCK:
		case WIN_DOORUNLOCK:
		case DISABLE_SEAGATE:
		case EXABYTE_ENABLE_NEST:

			args->handler = task_no_data_intr;
			args->command_type = IDE_DRIVE_TASK_NO_DATA;
			return;

		case WIN_SETMULT:
			args->handler = task_no_data_intr;
			args->command_type = IDE_DRIVE_TASK_NO_DATA;
			return;

		case WIN_NOP:
			args->handler = task_no_data_intr;
			args->command_type = IDE_DRIVE_TASK_NO_DATA;
			return;

		case WIN_FORMAT:
		case WIN_INIT:
		case WIN_DEVICE_RESET:
		case WIN_QUEUED_SERVICE:
		case WIN_PACKETCMD:
		default:
			args->command_type = IDE_DRIVE_TASK_INVALID;
			return;
	}
}

/*
 * This function is intended to be used prior to invoking ide_do_drive_cmd().
 */
void ide_init_drive_cmd(struct request *rq)
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
 * If action is ide_end, then the rq is queued at the end of the request queue,
 * and the function returns immediately without waiting for the new rq to be
 * completed. This is again intended for careful use by the ATAPI tape/cdrom
 * driver code.
 */
int ide_do_drive_cmd(struct ata_device *drive, struct request *rq, ide_action_t action)
{
	unsigned long flags;
	unsigned int major = drive->channel->major;
	request_queue_t *q = &drive->queue;
	struct list_head *queue_head = &q->queue_head;
	DECLARE_COMPLETION(wait);

#ifdef CONFIG_BLK_DEV_PDC4030
	if (drive->channel->chipset == ide_pdc4030 && rq->buffer != NULL)
		return -ENOSYS;  /* special drive cmds not supported */
#endif
	rq->errors = 0;
	rq->rq_status = RQ_ACTIVE;
	rq->rq_dev = mk_kdev(major,(drive->select.b.unit)<<PARTN_BITS);
	if (action == ide_wait)
		rq->waiting = &wait;

	spin_lock_irqsave(drive->channel->lock, flags);

	if (blk_queue_empty(&drive->queue) || action == ide_preempt) {
		if (action == ide_preempt)
			drive->rq = NULL;
	} else {
		if (action == ide_wait)
			queue_head = queue_head->prev;
		else
			queue_head = queue_head->next;
	}
	q->elevator.elevator_add_req_fn(q, rq, queue_head);

	do_ide_request(q);

	spin_unlock_irqrestore(drive->channel->lock, flags);

	if (action == ide_wait) {
		wait_for_completion(&wait);	/* wait for it to be serviced */
		return rq->errors ? -EIO : 0;	/* return -EIO if errors */
	}

	return 0;

}

int ide_raw_taskfile(struct ata_device *drive, struct ata_taskfile *args)
{
	struct request rq;

	memset(&rq, 0, sizeof(rq));
	rq.flags = REQ_DRIVE_ACB;

#if 0
	if (args->command_type != IDE_DRIVE_TASK_NO_DATA)
		rq.current_nr_sectors = rq.nr_sectors
			= (args->hobfile.sector_count << 8)
			| args->taskfile.sector_count;
#endif
	rq.special = args;

	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

/*
 * Implement generic ioctls invoked from userspace to imlpement specific
 * functionality.
 *
 * Unfortunately every single low level programm out there is using this
 * interface.
 */

/*
 * Backside of HDIO_DRIVE_CMD call of SETFEATURES_XFER.
 * 1 : Safe to update drive->id DMA registers.
 * 0 : OOPs not allowed.
 */
static int set_transfer(struct ata_device *drive, struct ata_taskfile *args)
{
	if ((args->taskfile.command == WIN_SETFEATURES) &&
	    (args->taskfile.sector_number >= XFER_SW_DMA_0) &&
	    (args->taskfile.feature == SETFEATURES_XFER) &&
	    (drive->id->dma_ultra ||
	     drive->id->dma_mword ||
	     drive->id->dma_1word))
		return 1;

	return 0;
}

/*
 * Verify that we are doing an approved SETFEATURES_XFER with respect
 * to the hardware being able to support request.  Since some hardware
 * can improperly report capabilties, we check to see if the host adapter
 * in combination with the device (usually a disk) properly detect
 * and acknowledge each end of the ribbon.
 */
static int ata66_check(struct ata_device *drive, struct ata_taskfile *args)
{
	if ((args->taskfile.command == WIN_SETFEATURES) &&
	    (args->taskfile.sector_number > XFER_UDMA_2) &&
	    (args->taskfile.feature == SETFEATURES_XFER)) {
		if (!drive->channel->udma_four) {
			printk("%s: Speed warnings UDMA 3/4/5 is not functional.\n", drive->channel->name);
			return 1;
		}
#ifndef CONFIG_IDEDMA_IVB
		if ((drive->id->hw_config & 0x6000) == 0) {
#else
		if (((drive->id->hw_config & 0x2000) == 0) ||
		    ((drive->id->hw_config & 0x4000) == 0)) {
#endif
			printk("%s: Speed warnings UDMA 3/4/5 is not functional.\n", drive->name);
			return 1;
		}
	}
	return 0;
}

int ide_cmd_ioctl(struct ata_device *drive, unsigned long arg)
{
	int err = 0;
	u8 vals[4];
	u8 *argbuf = vals;
	u8 pio = 0;
	int argsize = 4;
	struct ata_taskfile args;
	struct request rq;

	ide_init_drive_cmd(&rq);

	/* Wait for drive ready.
	 */
	if (!arg)
		return ide_do_drive_cmd(drive, &rq, ide_wait);

	/* Second phase.
	 */
	if (copy_from_user(vals, (void *)arg, 4))
		return -EFAULT;

	args.taskfile.feature = vals[2];
	args.taskfile.sector_count = vals[3];
	args.taskfile.sector_number = vals[1];
	args.taskfile.low_cylinder = 0x00;
	args.taskfile.high_cylinder = 0x00;
	args.taskfile.device_head = 0x00;
	args.taskfile.command = vals[0];

	if (vals[3]) {
		argsize = 4 + (SECTOR_WORDS * 4 * vals[3]);
		argbuf = kmalloc(argsize, GFP_KERNEL);
		if (argbuf == NULL)
			return -ENOMEM;
		memcpy(argbuf, vals, 4);
		memset(argbuf + 4, 0, argsize - 4);
	}

	/* Always make sure the transfer reate has been setup.
	 * FIXME: what about setting up the drive with ->tuneproc?
	 */
	if (set_transfer(drive, &args)) {
		pio = vals[1];
		if (ata66_check(drive, &args))
			goto abort;
	}

	/* Issue ATA command and wait for completion.
	 */
	rq.buffer = argbuf;
	err = ide_do_drive_cmd(drive, &rq, ide_wait);

	if (!err && pio) {
		/* active-retuning-calls future */
		/* FIXME: what about the setup for the drive?! */
		if (drive->channel->speedproc)
			drive->channel->speedproc(drive, pio);
	}

abort:
	if (copy_to_user((void *)arg, argbuf, argsize))
		err = -EFAULT;

	if (argsize > 4)
		kfree(argbuf);

	return err;
}

EXPORT_SYMBOL(drive_is_ready);
EXPORT_SYMBOL(ata_read);
EXPORT_SYMBOL(ata_write);
EXPORT_SYMBOL(ata_taskfile);
EXPORT_SYMBOL(recal_intr);
EXPORT_SYMBOL(task_no_data_intr);
EXPORT_SYMBOL(ide_init_drive_cmd);
EXPORT_SYMBOL(ide_do_drive_cmd);
EXPORT_SYMBOL(ide_raw_taskfile);
EXPORT_SYMBOL(ide_cmd_type_parser);
EXPORT_SYMBOL(ide_cmd_ioctl);
