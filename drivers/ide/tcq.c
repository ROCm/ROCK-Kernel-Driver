/*
 * Copyright (C) 2001, 2002 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Support for the DMA queued protocol, which enables ATA disk drives to
 * use tagged command queueing.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ide.h>

#include <asm/delay.h>

/*
 * warning: it will be _very_ verbose if defined
 */
#undef IDE_TCQ_DEBUG

#ifdef IDE_TCQ_DEBUG
#define TCQ_PRINTK printk
#else
#define TCQ_PRINTK(x...)
#endif

/*
 * use nIEN or not
 */
#undef IDE_TCQ_NIEN

/*
 * We are leaving the SERVICE interrupt alone, IBM drives have it
 * on per default and it can't be turned off. Doesn't matter, this
 * is the sane config.
 */
#undef IDE_TCQ_FIDDLE_SI

static ide_startstop_t ide_dmaq_intr(struct ata_device *drive, struct request *rq);
static ide_startstop_t service(struct ata_device *drive, struct request *rq);

static ide_startstop_t tcq_nop_handler(struct ata_device *drive, struct request *rq)
{
	unsigned long flags;
	struct ata_taskfile *args = rq->special;
	struct ata_channel *ch = drive->channel;

	local_irq_enable();

	spin_lock_irqsave(ch->lock, flags);

	blkdev_dequeue_request(rq);
	drive->rq = NULL;
	end_that_request_last(rq);

	spin_unlock_irqrestore(ch->lock, flags);

	kfree(args);

	return ATA_OP_FINISHED;
}

/*
 * If we encounter _any_ error doing I/O to one of the tags, we must
 * invalidate the pending queue. Clear the software busy queue and requeue
 * on the request queue for restart. Issue a WIN_NOP to clear hardware queue.
 */
static void tcq_invalidate_queue(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	request_queue_t *q = &drive->queue;
	struct ata_taskfile *ar;
	struct request *rq;
	unsigned long flags;

	printk(KERN_INFO "ATA: %s: invalidating pending queue (%d)\n", drive->name, ata_pending_commands(drive));

	spin_lock_irqsave(ch->lock, flags);

	del_timer(&ch->timer);

	if (test_bit(IDE_DMA, ch->active))
		udma_stop(drive);

	blk_queue_invalidate_tags(q);

	drive->using_tcq = 0;
	drive->queue_depth = 1;
	clear_bit(IDE_BUSY, ch->active);
	clear_bit(IDE_DMA, ch->active);
	ch->handler = NULL;

	/*
	 * Do some internal stuff -- we really need this command to be
	 * executed before any new commands are started. issue a NOP
	 * to clear internal queue on drive.
	 */
	ar = kmalloc(sizeof(*ar), GFP_ATOMIC);
	if (!ar) {
		printk(KERN_ERR "ATA: %s: failed to issue NOP\n", drive->name);
		goto out;
	}

	rq = __blk_get_request(&drive->queue, READ);
	if (!rq)
		rq = __blk_get_request(&drive->queue, WRITE);

	/*
	 * blk_queue_invalidate_tags() just added back at least one command
	 * to the free list, so there _must_ be at least one free.
	 */
	BUG_ON(!rq);

	/* WIN_NOP is a special request so set it's flags ?? */
	rq->flags = REQ_SPECIAL;
	rq->special = ar;
	ar->cmd = WIN_NOP;
	ar->XXX_handler = tcq_nop_handler;
	ar->command_type = IDE_DRIVE_TASK_NO_DATA;

	_elv_add_request(q, rq, 0, 0);

out:
#ifdef IDE_TCQ_NIEN
	ata_irq_enable(drive, 1);
#endif

	/*
	 * start doing stuff again
	 */
	q->request_fn(q);
	spin_unlock_irqrestore(ch->lock, flags);
	printk(KERN_DEBUG "ATA: tcq_invalidate_queue: done\n");
}

static void ata_tcq_irq_timeout(unsigned long data)
{
	struct ata_device *drive = (struct ata_device *) data;
	struct ata_channel *ch = drive->channel;
	unsigned long flags;

	printk(KERN_ERR "ATA: %s: timeout waiting for interrupt...\n", __FUNCTION__);

	spin_lock_irqsave(ch->lock, flags);

	if (test_and_set_bit(IDE_BUSY, ch->active))
		printk(KERN_ERR "ATA: %s: IRQ handler not busy\n", __FUNCTION__);
	if (!ch->handler)
		printk(KERN_ERR "ATA: %s: missing ISR!\n", __FUNCTION__);

	spin_unlock_irqrestore(ch->lock, flags);

	/*
	 * if pending commands, try service before giving up
	 */
	if (ata_pending_commands(drive) && !ata_status(drive, 0, SERVICE_STAT))
		if (service(drive, drive->rq) == ATA_OP_CONTINUES)
			return;

	if (drive)
		tcq_invalidate_queue(drive);
}

static void __set_irq(struct ata_channel *ch, ata_handler_t *handler)
{
	/*
	 * always just bump the timer for now, the timeout handling will
	 * have to be changed to be per-command
	 *
	 * FIXME: Jens - this is broken it will interfere with
	 * the normal timer function on serialized drives!
	 */

	ch->timer.function = ata_tcq_irq_timeout;
	ch->timer.data = (unsigned long) ch->drive;
	mod_timer(&ch->timer, jiffies + 5 * HZ);
	ch->handler = handler;
}

static void set_irq(struct ata_device *drive, ata_handler_t *handler)
{
	struct ata_channel *ch = drive->channel;
	unsigned long flags;

	spin_lock_irqsave(ch->lock, flags);
	__set_irq(ch, handler);
	spin_unlock_irqrestore(ch->lock, flags);
}

/*
 * wait 400ns, then poll for busy_mask to clear from alt status
 */
#define IDE_TCQ_WAIT	(10000)
static int wait_altstat(struct ata_device *drive, u8 *stat, u8 busy_mask)
{
	int i = 0;

	udelay(1);

	while ((*stat = GET_ALTSTAT()) & busy_mask) {
		if (unlikely(i++ > IDE_TCQ_WAIT))
			return 1;

		udelay(10);
	}

	return 0;
}

static ide_startstop_t udma_tcq_start(struct ata_device *drive, struct request *rq);

/*
 * issue SERVICE command to drive -- drive must have been selected first,
 * and it must have reported a need for service (status has SERVICE_STAT set)
 *
 * Also, nIEN must be set as not to need protection against ide_dmaq_intr
 */
static ide_startstop_t service(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;
	unsigned long flags;
	u8 feat, stat;
	int tag;

	TCQ_PRINTK("%s: started service\n", drive->name);

	/*
	 * Could be called with IDE_DMA in-progress from invalidate
	 * handler, refuse to do anything.
	 */
	if (test_bit(IDE_DMA, drive->channel->active))
		return ATA_OP_FINISHED;

	/*
	 * need to select the right drive first...
	 */
	if (drive != drive->channel->drive)
		ata_select(drive, 10);

#ifdef IDE_TCQ_NIEN
	ata_irq_enable(drive, 0);
#endif
	/*
	 * send SERVICE, wait 400ns, wait for BUSY_STAT to clear
	 */
	OUT_BYTE(WIN_QUEUED_SERVICE, IDE_COMMAND_REG);

	if (wait_altstat(drive, &stat, BUSY_STAT)) {
		ata_dump(drive, rq, "BUSY clear took too long");
		tcq_invalidate_queue(drive);

		return ATA_OP_FINISHED;
	}

#ifdef IDE_TCQ_NIEN
	ata_irq_enable(drive, 1);
#endif

	/*
	 * FIXME, invalidate queue
	 */
	if (stat & ERR_STAT) {
		ata_dump(drive, rq, "ERR condition");
		tcq_invalidate_queue(drive);

		return ATA_OP_FINISHED;
	}

	/*
	 * should not happen, a buggy device could introduce loop
	 */
	if ((feat = GET_FEAT()) & NSEC_REL) {
		drive->rq = NULL;
		printk("%s: release in service\n", drive->name);
		return ATA_OP_FINISHED;
	}

	tag = feat >> 3;

	TCQ_PRINTK("%s: stat %x, feat %x\n", __FUNCTION__, stat, feat);

	spin_lock_irqsave(ch->lock, flags);

	rq = blk_queue_find_tag(&drive->queue, tag);
	if (!rq) {
		printk(KERN_ERR"%s: missing request for tag %d\n", __FUNCTION__, tag);
		spin_unlock_irqrestore(ch->lock, flags);
		return ATA_OP_FINISHED;
	}

	drive->rq = rq;

	spin_unlock_irqrestore(ch->lock, flags);
	/*
	 * we'll start a dma read or write, device will trigger
	 * interrupt to indicate end of transfer, release is not allowed
	 */
	TCQ_PRINTK("%s: starting command %x\n", __FUNCTION__, stat);

	return udma_tcq_start(drive, rq);
}

static ide_startstop_t check_service(struct ata_device *drive, struct request *rq)
{
	TCQ_PRINTK("%s: %s\n", drive->name, __FUNCTION__);

	if (!ata_pending_commands(drive))
		return ATA_OP_FINISHED;

	if (!ata_status(drive, 0, SERVICE_STAT))
		return service(drive, rq);

	/*
	 * we have pending commands, wait for interrupt
	 */
	set_irq(drive, ide_dmaq_intr);

	return ATA_OP_CONTINUES;
}

static ide_startstop_t dmaq_complete(struct ata_device *drive, struct request *rq)
{
	u8 dma_stat;

	/*
	 * transfer was in progress, stop DMA engine
	 */
	dma_stat = udma_stop(drive);

	/*
	 * must be end of I/O, check status and complete as necessary
	 */
	if (!ata_status(drive, READY_STAT, drive->bad_wstat | DRQ_STAT)) {
		ata_dump(drive, rq, __FUNCTION__);
		tcq_invalidate_queue(drive);

		return ATA_OP_FINISHED;
	}

	if (dma_stat)
		printk("%s: bad DMA status (dma_stat=%x)\n", drive->name, dma_stat);

	TCQ_PRINTK("%s: ending %p, tag %d\n", __FUNCTION__, rq, rq->tag);

	ata_end_request(drive, rq, !dma_stat, rq->nr_sectors);

	/*
	 * we completed this command, check if we can service a new command
	 */
	return check_service(drive, rq);
}

/*
 * Interrupt handler for queued dma operations. this can be entered for two
 * reasons:
 *
 * 1) device has completed dma transfer
 * 2) service request to start a command
 *
 * if the drive has an active tag, we first complete that request before
 * processing any pending SERVICE.
 */
static ide_startstop_t ide_dmaq_intr(struct ata_device *drive, struct request *rq)
{
	int ok;

	ok = !ata_status(drive, 0, SERVICE_STAT);
	TCQ_PRINTK("%s: stat=%x\n", __FUNCTION__, drive->status);

	/*
	 * If a command completion interrupt is pending, do that first and
	 * check service afterwards.
	 */
	if (rq)
		return dmaq_complete(drive, rq);

	/*
	 * service interrupt
	 */
	if (ok) {
		TCQ_PRINTK("%s: SERV (stat=%x)\n", __FUNCTION__, drive->status);
		return service(drive, rq);
	}

	printk("%s: stat=%x, not expected\n", __FUNCTION__, drive->status);

	return check_service(drive, rq);
}

/*
 * Check if the ata adapter this drive is attached to supports the
 * NOP auto-poll for multiple tcq enabled drives on one channel.
 */
static int check_autopoll(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	struct ata_taskfile args;
	int drives = 0, i;

	/*
	 * only need to probe if both drives on a channel support tcq
	 */
	for (i = 0; i < MAX_DRIVES; i++)
		if (drive->channel->drives[i].present &&drive->type == ATA_DISK)
			drives++;

	if (drives <= 1)
		return 0;

	/*
	 * do taskfile and check ABRT bit -- intelligent adapters will not
	 * pass NOP with sub-code 0x01 to device, so the command will not
	 * fail there
	 */
	memset(&args, 0, sizeof(args));
	args.taskfile.feature = 0x01;
	args.cmd = WIN_NOP;
	ide_raw_taskfile(drive, &args, NULL);
	if (args.taskfile.feature & ABRT_ERR)
		return 1;

	ch->auto_poll = 1;
	printk("%s: NOP Auto-poll enabled\n", ch->name);
	return 0;
}

/*
 * configure the drive for tcq
 */
static int configure_tcq(struct ata_device *drive)
{
	int tcq_mask = 1 << 1 | 1 << 14;
	int tcq_bits = tcq_mask | 1 << 15;
	struct ata_taskfile args;

	/*
	 * bit 14 and 1 must be set in word 83 of the device id to indicate
	 * support for dma queued protocol, and bit 15 must be cleared
	 */
	if ((drive->id->command_set_2 & tcq_bits) ^ tcq_mask)
		return -EIO;

	memset(&args, 0, sizeof(args));
	args.taskfile.feature = SETFEATURES_EN_WCACHE;
	args.cmd = WIN_SETFEATURES;
	if (ide_raw_taskfile(drive, &args, NULL)) {
		printk("%s: failed to enable write cache\n", drive->name);
		return 1;
	}

	/*
	 * disable RELease interrupt, it's quicker to poll this after
	 * having sent the command opcode
	 */
	memset(&args, 0, sizeof(args));
	args.taskfile.feature = SETFEATURES_DIS_RI;
	args.cmd = WIN_SETFEATURES;
	if (ide_raw_taskfile(drive, &args, NULL)) {
		printk("%s: disabling release interrupt fail\n", drive->name);
		return 1;
	}

#ifdef IDE_TCQ_FIDDLE_SI
	/*
	 * enable SERVICE interrupt
	 */
	memset(&args, 0, sizeof(args));
	args.taskfile.feature = SETFEATURES_EN_SI;
	args.cmd = WIN_SETFEATURES;
	if (ide_raw_taskfile(drive, &args, NULL)) {
		printk("%s: enabling service interrupt fail\n", drive->name);
		return 1;
	}
#endif

	return 0;
}

static int tcq_wait_dataphase(struct ata_device *drive)
{
	int i;

	while (!ata_status(drive, 0, BUSY_STAT))
		udelay(10);

	if (ata_status(drive, READY_STAT | DRQ_STAT, drive->bad_wstat))
		return 0;

	i = 0;
	udelay(1);
	while (!ata_status(drive, READY_STAT | DRQ_STAT, drive->bad_wstat)) {
		++i;
		if (i > IDE_TCQ_WAIT)
			return 1;

		udelay(10);
	}

	return 0;
}

/****************************************************************************
 * UDMA transfer handling functions.
 */

/*
 * Invoked from a SERVICE interrupt, command etc already known.  Just need to
 * start the dma engine for this tag.
 */
static ide_startstop_t udma_tcq_start(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;

	TCQ_PRINTK("%s: setting up queued %d\n", __FUNCTION__, rq->tag);
	if (!test_bit(IDE_BUSY, ch->active))
		printk("queued_rw: IDE_BUSY not set\n");

	if (tcq_wait_dataphase(drive))
		return ATA_OP_FINISHED;

	if (ata_start_dma(drive, rq))
		return ATA_OP_FINISHED;

	__set_irq(ch, ide_dmaq_intr);
	udma_start(drive, rq);

	return ATA_OP_CONTINUES;
}

/*
 * Start a queued command from scratch.
 */
ide_startstop_t udma_tcq_init(struct ata_device *drive, struct request *rq)
{
	u8 stat;
	u8 feat;

	struct ata_taskfile *args = rq->special;

	TCQ_PRINTK("%s: start tag %d\n", drive->name, rq->tag);

	/*
	 * set nIEN, tag start operation will enable again when
	 * it is safe
	 */
#ifdef IDE_TCQ_NIEN
	ata_irq_enable(drive, 0);
#endif

	OUT_BYTE(args->cmd, IDE_COMMAND_REG);

	if (wait_altstat(drive, &stat, BUSY_STAT)) {
		ata_dump(drive, rq, "queued start");
		tcq_invalidate_queue(drive);
		return ATA_OP_FINISHED;
	}

#ifdef IDE_TCQ_NIEN
	ata_irq_enable(drive, 1);
#endif

	if (stat & ERR_STAT) {
		ata_dump(drive, rq, "tcq_start");
		return ATA_OP_FINISHED;
	}

	/*
	 * drive released the bus, clear active tag and
	 * check for service
	 */
	if ((feat = GET_FEAT()) & NSEC_REL) {
		drive->immed_rel++;
		drive->rq = NULL;
		set_irq(drive, ide_dmaq_intr);

		TCQ_PRINTK("REL in queued_start\n");

		if (!ata_status(drive, 0, SERVICE_STAT))
			return service(drive, rq);

		return ATA_OP_RELEASED;
	}

	TCQ_PRINTK("IMMED in queued_start\n");
	drive->immed_comp++;

	return udma_tcq_start(drive, rq);
}

/*
 * For now assume that command list is always as big as we need and don't
 * attempt to shrink it on tcq disable.
 */
int udma_tcq_enable(struct ata_device *drive, int on)
{
	int depth = drive->using_tcq ? drive->queue_depth : 0;

	/*
	 * disable or adjust queue depth
	 */
	if (!on) {
		if (drive->using_tcq)
			printk("%s: TCQ disabled\n", drive->name);
		drive->using_tcq = 0;
		return 0;
	}

	if (configure_tcq(drive)) {
		drive->using_tcq = 0;
		return 1;
	}

	/*
	 * enable block tagging
	 */
	if (!blk_queue_tagged(&drive->queue))
		blk_queue_init_tags(&drive->queue, IDE_MAX_TAG);

	/*
	 * check auto-poll support
	 */
	check_autopoll(drive);

	if (depth != drive->queue_depth)
		printk("%s: tagged command queueing enabled, command queue depth %d\n", drive->name, drive->queue_depth);

	drive->using_tcq = 1;
	return 0;
}

/* FIXME: This should go away! */
EXPORT_SYMBOL(udma_tcq_enable);
