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
 * Invoked on completion of a special REQ_SPECIAL command.
 */
static ide_startstop_t special_intr(struct ata_device *drive, struct request *rq) {
	unsigned long flags;
	struct ata_channel *ch =drive->channel;
	struct ata_taskfile *ar = rq->special;
	ide_startstop_t ret = ATA_OP_FINISHED;

	local_irq_enable();

	if (rq->buffer && ar->taskfile.sector_number) {
		if (!ata_status(drive, 0, DRQ_STAT) && ar->taskfile.sector_number) {
			int retries = 10;

			ata_read(drive, rq->buffer, ar->taskfile.sector_number * SECTOR_WORDS);

			while (!ata_status(drive, 0, BUSY_STAT) && retries--)
				udelay(100);
		}
	}

	if (!ata_status(drive, READY_STAT, BAD_STAT)) {
		/* Keep quiet for NOP because it is expected to fail. */
		if (ar->cmd != WIN_NOP)
			ret = ata_error(drive, rq, __FUNCTION__);
		rq->errors = 1;
	}

	ar->taskfile.feature = IN_BYTE(IDE_ERROR_REG);
	ata_in_regfile(drive, &ar->taskfile);
	ar->taskfile.device_head = IN_BYTE(IDE_SELECT_REG);
	if ((drive->id->command_set_2 & 0x0400) &&
			(drive->id->cfs_enable_2 & 0x0400) &&
			(drive->addressing == 1)) {
		/* The following command goes to the hob file! */
		OUT_BYTE(0x80, drive->channel->io_ports[IDE_CONTROL_OFFSET]);
		ar->hobfile.feature = IN_BYTE(IDE_FEATURE_REG);
		ata_in_regfile(drive, &ar->hobfile);
	}

	spin_lock_irqsave(ch->lock, flags);

	blkdev_dequeue_request(rq);
	drive->rq = NULL;
	end_that_request_last(rq);

	spin_unlock_irqrestore(ch->lock, flags);

	return ret;
}

int ide_raw_taskfile(struct ata_device *drive, struct ata_taskfile *ar, char *buf)
{
	struct request *rq;
	unsigned long flags;
	struct ata_channel *ch = drive->channel;
	request_queue_t *q = &drive->queue;
	struct list_head *queue_head = &q->queue_head;
	DECLARE_COMPLETION(wait);

#ifdef CONFIG_BLK_DEV_PDC4030
	if (ch->chipset == ide_pdc4030 && buf)
		return -ENOSYS;  /* special drive cmds not supported */
#endif

	rq = __blk_get_request(&drive->queue, READ);
	if (!rq)
		rq = __blk_get_request(&drive->queue, WRITE);

	/*
	 * FIXME: Make sure there is a free slot on the list!
	 */

	BUG_ON(!rq);

	rq->flags = REQ_SPECIAL;
	rq->buffer = buf;
	rq->special = ar;
	rq->errors = 0;
	rq->rq_status = RQ_ACTIVE;
	rq->waiting = &wait;

	ar->XXX_handler = special_intr;
	ar->command_type = IDE_DRIVE_TASK_NO_DATA;

	spin_lock_irqsave(ch->lock, flags);

	if (!blk_queue_empty(&drive->queue))
		queue_head = queue_head->prev;
	__elv_add_request(q, rq, queue_head);

	q->request_fn(q);
	spin_unlock_irqrestore(ch->lock, flags);

	wait_for_completion(&wait);	/* wait for it to be serviced */

	return rq->errors ? -EIO : 0;	/* return -EIO if errors */
}

EXPORT_SYMBOL(ata_read);
EXPORT_SYMBOL(ata_write);
EXPORT_SYMBOL(ide_raw_taskfile);
