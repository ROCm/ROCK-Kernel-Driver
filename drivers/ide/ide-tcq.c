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
 * we are leaving the SERVICE interrupt alone, IBM drives have it
 * on per default and it can't be turned off. Doesn't matter, this
 * is the sane config.
 */
#undef IDE_TCQ_FIDDLE_SI

/*
 * wait for data phase before starting DMA or not
 */
#undef IDE_TCQ_WAIT_DATAPHASE

ide_startstop_t ide_dmaq_intr(ide_drive_t *drive);
ide_startstop_t ide_service(ide_drive_t *drive);

static inline void drive_ctl_nien(ide_drive_t *drive, int clear)
{
#ifdef IDE_TCQ_NIEN
	int mask = clear ? 0x00 : 0x02;

	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl | mask, IDE_CONTROL_REG);
#endif
}

/*
 * if we encounter _any_ error doing I/O to one of the tags, we must
 * invalidate the pending queue. clear the software busy queue and requeue
 * on the request queue for restart. issue a WIN_NOP to clear hardware queue
 */
static void ide_tcq_invalidate_queue(ide_drive_t *drive)
{
	request_queue_t *q = &drive->queue;
	unsigned long flags;
	struct ata_request *ar;
	int i;

	printk("%s: invalidating pending queue\n", drive->name);

	spin_lock_irqsave(&ide_lock, flags);

	del_timer(&HWGROUP(drive)->timer);

	/*
	 * assume oldest commands have the higher tags... doesn't matter
	 * much. shove requests back into request queue.
	 */
	for (i = drive->queue_depth - 1; i; i--) {
		ar = drive->tcq->ar[i];
		if (!ar)
			continue;

		ar->ar_rq->special = NULL;
		ar->ar_rq->flags &= ~REQ_STARTED;
		_elv_add_request(q, ar->ar_rq, 0, 0);
		ata_ar_put(drive, ar);
	}

	drive->tcq->queued = 0;
	drive->using_tcq = 0;
	drive->queue_depth = 1;
	clear_bit(IDE_BUSY, &HWGROUP(drive)->flags);
	clear_bit(IDE_DMA, &HWGROUP(drive)->flags);
	HWGROUP(drive)->handler = NULL;

	/*
	 * do some internal stuff -- we really need this command to be
	 * executed before any new commands are started. issue a NOP
	 * to clear internal queue on drive
	 */
	ar = ata_ar_get(drive);

	memset(&ar->ar_task, 0, sizeof(ar->ar_task));
	AR_TASK_CMD(ar) = WIN_NOP;
	ide_cmd_type_parser(&ar->ar_task);
	ar->ar_rq = &HWGROUP(drive)->wrq;
	init_taskfile_request(ar->ar_rq);
	ar->ar_rq->rq_dev = mk_kdev(drive->channel->major, (drive->select.b.unit)<<PARTN_BITS);
	ar->ar_rq->special = ar;
	ar->ar_flags |= ATA_AR_RETURN;
	_elv_add_request(q, ar->ar_rq, 0, 0);

	/*
	 * make sure that nIEN is cleared
	 */
	drive_ctl_nien(drive, 0);

	/*
	 * start doing stuff again
	 */
	q->request_fn(q);
	spin_unlock_irqrestore(&ide_lock, flags);
	printk("ide_tcq_invalidate_queue: done\n");
}

void ide_tcq_intr_timeout(unsigned long data)
{
	ide_hwgroup_t *hwgroup = (ide_hwgroup_t *) data;
	unsigned long flags;
	ide_drive_t *drive;

	printk("ide_tcq_intr_timeout: timeout waiting for interrupt...\n");

	spin_lock_irqsave(&ide_lock, flags);

	if (test_and_set_bit(IDE_BUSY, &hwgroup->flags))
		printk("ide_tcq_intr_timeout: hwgroup not busy\n");
	if (hwgroup->handler == NULL)
		printk("ide_tcq_intr_timeout: missing isr!\n");
	if ((drive = hwgroup->drive) == NULL)
		printk("ide_tcq_intr_timeout: missing drive!\n");

	spin_unlock_irqrestore(&ide_lock, flags);

	/*
	 * if pending commands, try service before giving up
	 */
	if (ide_pending_commands(drive) && (GET_STAT() & SERVICE_STAT))
		if (ide_service(drive) == ide_started)
			return;

	if (drive)
		ide_tcq_invalidate_queue(drive);
}

void ide_tcq_set_intr(ide_hwgroup_t *hwgroup, ide_handler_t *handler)
{
	unsigned long flags;

	spin_lock_irqsave(&ide_lock, flags);

	/*
	 * always just bump the timer for now, the timeout handling will
	 * have to be changed to be per-command
	 */
	hwgroup->timer.function = ide_tcq_intr_timeout;
	hwgroup->timer.data = (unsigned long) hwgroup;
	mod_timer(&hwgroup->timer, jiffies + 5 * HZ);

	hwgroup->handler = handler;
	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * wait 400ns, then poll for busy_mask to clear from alt status
 */
#define IDE_TCQ_WAIT	(10000)
int ide_tcq_wait_altstat(ide_drive_t *drive, byte *stat, byte busy_mask)
{
	int i;

	/*
	 * one initial udelay(1) should be enough, reading alt stat should
	 * provide the required delay...
	 */
	*stat = 0;
	i = 0;
	do {
		udelay(1);

		if (unlikely(i++ > IDE_TCQ_WAIT))
			return 1;

	} while ((*stat = GET_ALTSTAT()) & busy_mask);

	return 0;
}

/*
 * issue SERVICE command to drive -- drive must have been selected first,
 * and it must have reported a need for service (status has SERVICE_STAT set)
 *
 * Also, nIEN must be set as not to need protection against ide_dmaq_intr
 */
ide_startstop_t ide_service(ide_drive_t *drive)
{
	struct ata_request *ar;
	byte feat, stat;
	int tag;

	TCQ_PRINTK("%s: started service\n", drive->name);

	drive->service_pending = 0;

	if (test_bit(IDE_DMA, &HWGROUP(drive)->flags))
		printk("ide_service: DMA in progress\n");

	/*
	 * need to select the right drive first...
	 */
	if (drive != HWGROUP(drive)->drive) {
		SELECT_DRIVE(drive->channel, drive);
		udelay(10);
	}

	drive_ctl_nien(drive, 1);

	/*
	 * send SERVICE, wait 400ns, wait for BUSY_STAT to clear
	 */
	OUT_BYTE(WIN_QUEUED_SERVICE, IDE_COMMAND_REG);

	if (ide_tcq_wait_altstat(drive, &stat, BUSY_STAT)) {
		printk("ide_service: BUSY clear took too long\n");
		ide_tcq_invalidate_queue(drive);
		return ide_stopped;
	}

	drive_ctl_nien(drive, 0);

	/*
	 * FIXME, invalidate queue
	 */
	if (stat & ERR_STAT) {
		ide_dump_status(drive, "ide_service", stat);
		ide_tcq_invalidate_queue(drive);
		return ide_stopped;
	}

	/*
	 * should not happen, a buggy device could introduce loop
	 */
	if ((feat = GET_FEAT()) & NSEC_REL) {
		printk("%s: release in service\n", drive->name);
		IDE_SET_CUR_TAG(drive, IDE_INACTIVE_TAG);
		return ide_stopped;
	}

	/*
	 * start dma
	 */
	tag = feat >> 3;
	IDE_SET_CUR_TAG(drive, tag);

	TCQ_PRINTK("ide_service: stat %x, feat %x\n", stat, feat);

	if ((ar = IDE_CUR_TAG(drive)) == NULL) {
		printk("ide_service: missing request for tag %d\n", tag);
		return ide_stopped;
	}

	HWGROUP(drive)->rq = ar->ar_rq;

	/*
	 * we'll start a dma read or write, device will trigger
	 * interrupt to indicate end of transfer, release is not allowed
	 */
	if (rq_data_dir(ar->ar_rq) == READ) {
		TCQ_PRINTK("ide_service: starting READ %x\n", stat);
		drive->channel->dmaproc(ide_dma_read_queued, drive);
	} else {
		TCQ_PRINTK("ide_service: starting WRITE %x\n", stat);
		drive->channel->dmaproc(ide_dma_write_queued, drive);
	}

	/*
	 * dmaproc set intr handler
	 */
	return ide_started;
}

ide_startstop_t ide_check_service(ide_drive_t *drive)
{
	byte stat;

	TCQ_PRINTK("%s: ide_check_service\n", drive->name);

	if (!ide_pending_commands(drive))
		return ide_stopped;

	if ((stat = GET_STAT()) & SERVICE_STAT)
		return ide_service(drive);

	/*
	 * we have pending commands, wait for interrupt
	 */
	ide_tcq_set_intr(HWGROUP(drive), ide_dmaq_intr);
	return ide_started;
}

ide_startstop_t ide_dmaq_complete(ide_drive_t *drive, byte stat)
{
	struct ata_request *ar = IDE_CUR_TAG(drive);
	byte dma_stat;

	/*
	 * transfer was in progress, stop DMA engine
	 */
	dma_stat = drive->channel->dmaproc(ide_dma_end, drive);

	/*
	 * must be end of I/O, check status and complete as necessary
	 */
	if (unlikely(!OK_STAT(stat, READY_STAT, drive->bad_wstat | DRQ_STAT))) {
		printk("ide_dmaq_intr: %s: error status %x\n", drive->name, stat);
		ide_dump_status(drive, "ide_dmaq_intr", stat);
		ide_tcq_invalidate_queue(drive);
		return ide_stopped;
	}

	if (dma_stat)
		printk("%s: bad DMA status (dma_stat=%x)\n", drive->name, dma_stat);

	TCQ_PRINTK("ide_dmaq_intr: ending %p, tag %d\n", ar, ar->ar_tag);
	ide_end_queued_request(drive, !dma_stat, ar->ar_rq);

	IDE_SET_CUR_TAG(drive, IDE_INACTIVE_TAG);

	/*
	 * keep the queue full, or honor SERVICE? note that this may race
	 * and no new command will be started, in which case idedisk_do_request
	 * will notice and do the service check
	 */
#if CONFIG_BLK_DEV_IDE_TCQ_FULL
	if (!drive->service_pending && (ide_pending_commands(drive) > 1)) {
		if (!blk_queue_empty(&drive->queue)) {
			drive->service_pending = 1;
			ide_tcq_set_intr(HWGROUP(drive), ide_dmaq_intr);
			return ide_released;
		}
	}
#endif

	return ide_check_service(drive);
}

/*
 * intr handler for queued dma operations. this can be entered for two
 * reasons:
 *
 * 1) device has completed dma transfer
 * 2) service request to start a command
 *
 * if the drive has an active tag, we first complete that request before
 * processing any pending SERVICE.
 */
ide_startstop_t ide_dmaq_intr(ide_drive_t *drive)
{
	byte stat = GET_STAT();

	TCQ_PRINTK("ide_dmaq_intr: stat=%x, tag %d\n", stat, drive->tcq->active_tag);

	/*
	 * if a command completion interrupt is pending, do that first and
	 * check service afterwards
	 */
	if (drive->tcq->active_tag != IDE_INACTIVE_TAG)
		return ide_dmaq_complete(drive, stat);

	/*
	 * service interrupt
	 */
	if (stat & SERVICE_STAT) {
		TCQ_PRINTK("ide_dmaq_intr: SERV (stat=%x)\n", stat);
		return ide_service(drive);
	}

	printk("ide_dmaq_intr: stat=%x, not expected\n", stat);
	return ide_check_service(drive);
}

/*
 * configure the drive for tcq
 */
static int ide_tcq_configure(ide_drive_t *drive)
{
	struct ata_taskfile args;
	int tcq_supp = 1 << 1 | 1 << 14;

	/*
	 * bit 14 and 1 must be set in word 83 of the device id to indicate
	 * support for dma queued protocol
	 */
	if ((drive->id->command_set_2 & tcq_supp) != tcq_supp)
		return -EIO;

	memset(&args, 0, sizeof(args));
	args.taskfile.feature = SETFEATURES_EN_WCACHE;
	args.taskfile.command = WIN_SETFEATURES;
	ide_cmd_type_parser(&args);

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
	args.taskfile.command = WIN_SETFEATURES;
	ide_cmd_type_parser(&args);

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
	args.taskfile.command = WIN_SETFEATURES;
	ide_cmd_type_parser(&args);

	if (ide_raw_taskfile(drive, &args, NULL)) {
		printk("%s: enabling service interrupt fail\n", drive->name);
		return 1;
	}
#endif

	if (!drive->tcq) {
		drive->tcq = kmalloc(sizeof(ide_tag_info_t), GFP_ATOMIC);
		if (!drive->tcq)
			return -ENOMEM;

		memset(drive->tcq, 0, sizeof(ide_tag_info_t));
		drive->tcq->active_tag = IDE_INACTIVE_TAG;
	}

	return 0;
}

/*
 * for now assume that command list is always as big as we need and don't
 * attempt to shrink it on tcq disable
 */
static int ide_enable_queued(ide_drive_t *drive, int on)
{
	/*
	 * disable or adjust queue depth
	 */
	if (!on) {
		printk("%s: TCQ disabled\n", drive->name);
		drive->using_tcq = 0;
		return 0;
	}

	if (ide_tcq_configure(drive)) {
		drive->using_tcq = 0;
		return 1;
	}

	if (ide_build_commandlist(drive))
		return 1;

	printk("%s: tagged command queueing enabled, command queue depth %d\n", drive->name, drive->queue_depth);
	drive->using_tcq = 1;
	drive->tcq->max_depth = 0;
	return 0;
}

int ide_tcq_wait_dataphase(ide_drive_t *drive)
{
#ifdef IDE_TCQ_WAIT_DATAPHASE
	ide_startstop_t foo;

	if (ide_wait_stat(&startstop, drive, READY_STAT | DRQ_STAT, BUSY_STAT, WAIT_READY)) {
		printk("%s: timeout waiting for data phase\n", drive->name);
		return 1;
	}
#endif
	return 0;
}

int ide_tcq_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	struct ata_channel *hwif = drive->channel;
	unsigned int reading = 0, enable_tcq = 1;
	struct ata_request *ar;
	byte stat, feat;

	switch (func) {
		/*
		 * invoked from a SERVICE interrupt, command etc already known.
		 * just need to start the dma engine for this tag
		 */
		case ide_dma_read_queued:
			reading = 1 << 3;
		case ide_dma_write_queued:
			TCQ_PRINTK("ide_dma: setting up queued %d\n", drive->tcq->active_tag);
			BUG_ON(drive->tcq->active_tag == IDE_INACTIVE_TAG);

			if (!test_bit(IDE_BUSY, &HWGROUP(drive)->flags))
				printk("queued_rw: IDE_BUSY not set\n");

			if (ide_tcq_wait_dataphase(drive))
				return ide_stopped;

			if (ide_start_dma(hwif, drive, func))
				return 1;

			ide_tcq_set_intr(HWGROUP(drive), ide_dmaq_intr);
			return hwif->dmaproc(ide_dma_begin, drive);

			/*
			 * start a queued command from scratch
			 */
		case ide_dma_queued_start:
			BUG_ON(drive->tcq->active_tag == IDE_INACTIVE_TAG);
			ar = IDE_CUR_TAG(drive);

			/*
			 * set nIEN, tag start operation will enable again when
			 * it is safe
			 */
			drive_ctl_nien(drive, 1);

			OUT_BYTE(AR_TASK_CMD(ar), IDE_COMMAND_REG);

			if (ide_tcq_wait_altstat(drive, &stat, BUSY_STAT)) {
				printk("ide_dma_queued_start: abort (stat=%x)\n", stat);
				return ide_stopped;
			}

			drive_ctl_nien(drive, 0);

			if (stat & ERR_STAT) {
				ide_dump_status(drive, "tcq_start", stat);
				return ide_stopped;
			}

			/*
			 * drive released the bus, clear active tag and
			 * check for service
			 */
			if ((feat = GET_FEAT()) & NSEC_REL) {
				IDE_SET_CUR_TAG(drive, IDE_INACTIVE_TAG);
				drive->tcq->immed_rel++;

				TCQ_PRINTK("REL in queued_start\n");

				if ((stat = GET_STAT()) & SERVICE_STAT)
					return ide_service(drive);

				ide_tcq_set_intr(HWGROUP(drive), ide_dmaq_intr);
				return ide_released;
			}

			drive->tcq->immed_comp++;

			if (ide_tcq_wait_dataphase(drive))
				return ide_stopped;

			if (ide_start_dma(hwif, drive, func))
				return ide_stopped;

			/*
			 * need to arm handler before starting dma engine,
			 * transfer could complete right away
			 */
			ide_tcq_set_intr(HWGROUP(drive), ide_dmaq_intr);

			if (hwif->dmaproc(ide_dma_begin, drive))
				return ide_stopped;

			/*
			 * wait for SERVICE or completion interrupt
			 */
			return ide_started;

		case ide_dma_queued_off:
			enable_tcq = 0;
		case ide_dma_queued_on:
			return ide_enable_queued(drive, enable_tcq);
		default:
			break;
	}

	return 1;
}

int ide_build_sglist (struct ata_channel *hwif, struct request *rq);
ide_startstop_t ide_start_tag(ide_dma_action_t func, ide_drive_t *drive,
			      struct ata_request *ar)
{
	ide_startstop_t startstop;

	TCQ_PRINTK("%s: ide_start_tag: begin tag %p/%d, rq %p\n", drive->name,ar,ar->ar_tag, ar->ar_rq);

	/*
	 * do this now, no need to run that with interrupts disabled
	 */
	if (!ide_build_sglist(drive->channel, ar->ar_rq))
		return ide_stopped;

	IDE_SET_CUR_TAG(drive, ar->ar_tag);
	HWGROUP(drive)->rq = ar->ar_rq;

	startstop = ide_tcq_dmaproc(func, drive);

	if (unlikely(startstop == ide_stopped)) {
		IDE_SET_CUR_TAG(drive, IDE_INACTIVE_TAG);
		HWGROUP(drive)->rq = NULL;
	}

	return startstop;
}
