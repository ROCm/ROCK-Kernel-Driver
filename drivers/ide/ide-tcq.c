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
#include <linux/ide.h>

#include <asm/io.h>
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
 * bad drive blacklist, for drives that raport tcq capability but don't
 * work reliably with the default config. initially from freebsd table.
 */
struct ide_tcq_blacklist {
	char *model;
	char works;
	unsigned int max_sectors;
};

static struct ide_tcq_blacklist ide_tcq_blacklist[] = {
	{
		.model =	"IBM-DTTA",
		.works =	1,
		.max_sectors =	128,
	},
	{
		.model =	"IBM-DJNA",
		.works =	0,
	},
	{
		.model =	"WDC AC",
		.works = 	0,
	},
	{
		.model =	NULL,
	},
};

ide_startstop_t ide_dmaq_intr(ide_drive_t *drive);
ide_startstop_t ide_service(ide_drive_t *drive);

static struct ide_tcq_blacklist *ide_find_drive_blacklist(ide_drive_t *drive)
{
	struct ide_tcq_blacklist *itb;
	int i = 0;

	do {
		itb = &ide_tcq_blacklist[i];

		if (!itb->model)
			break;

		if (!strncmp(drive->id->model, itb->model, strlen(itb->model)))
			return itb;

		i++;
	} while (1);

	return NULL;
}

static inline void drive_ctl_nien(ide_drive_t *drive, int set)
{
#ifdef IDE_TCQ_NIEN
	if (IDE_CONTROL_REG) {
		int mask = set ? 0x02 : 0x00;

		hwif->OUTB(drive->ctl | mask, IDE_CONTROL_REG);
	}
#endif
}

static ide_startstop_t ide_tcq_nop_handler(ide_drive_t *drive)
{
	ide_task_t *args = HWGROUP(drive)->rq->special;
	ide_hwif_t *hwif = HWIF(drive);
	int auto_poll_check = 0;
	u8 stat, err;

	if (args->tfRegister[IDE_FEATURE_OFFSET] & 0x01)
		auto_poll_check = 1;

	local_irq_enable();

	stat = hwif->INB(IDE_STATUS_REG);
	err = hwif->INB(IDE_ERROR_REG);
	ide_end_drive_cmd(drive, stat, err);

	/*
	 * do taskfile and check ABRT bit -- intelligent adapters will not
	 * pass NOP with sub-code 0x01 to device, so the command will not
	 * fail there
	 */
	if (auto_poll_check) {
		if (!(args->tfRegister[IDE_FEATURE_OFFSET] & ABRT_ERR)) {
			HWIF(drive)->auto_poll = 1;
			printk("%s: NOP Auto-poll enabled\n",HWIF(drive)->name);
		}
	}

	kfree(args);
	return ide_stopped;
}

/*
 * if we encounter _any_ error doing I/O to one of the tags, we must
 * invalidate the pending queue. clear the software busy queue and requeue
 * on the request queue for restart. issue a WIN_NOP to clear hardware queue
 */
static void ide_tcq_invalidate_queue(ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	request_queue_t *q = drive->queue;
	struct request *rq;
	unsigned long flags;

	printk("%s: invalidating tag queue (%d commands)\n", drive->name, ata_pending_commands(drive));

	/*
	 * first kill timer and block queue
	 */
	spin_lock_irqsave(&ide_lock, flags);

	del_timer(&hwgroup->timer);

	if (HWIF(drive)->dma)
		HWIF(drive)->ide_dma_end(drive);

	blk_queue_invalidate_tags(q);

	drive->using_tcq = 0;
	drive->queue_depth = 1;
	hwgroup->busy = 0;
	hwgroup->handler = NULL;

	spin_unlock_irqrestore(&ide_lock, flags);

	/*
	 * now kill hardware queue with a NOP
	 */
	rq = &hwgroup->wrq;
	ide_init_drive_cmd(rq);
	rq->buffer = hwgroup->cmd_buf;
	memset(rq->buffer, 0, sizeof(hwgroup->cmd_buf));
	rq->buffer[0] = WIN_NOP;
	ide_do_drive_cmd(drive, rq, ide_preempt);
}

void ide_tcq_intr_timeout(unsigned long data)
{
	ide_drive_t *drive = (ide_drive_t *) data;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;

	printk(KERN_ERR "ide_tcq_intr_timeout: timeout waiting for %s interrupt\n", hwgroup->rq ? "completion" : "service");

	spin_lock_irqsave(&ide_lock, flags);

	if (!hwgroup->busy)
		printk(KERN_ERR "ide_tcq_intr_timeout: hwgroup not busy\n");
	if (hwgroup->handler == NULL)
		printk(KERN_ERR "ide_tcq_intr_timeout: missing isr!\n");

	hwgroup->busy = 1;
	spin_unlock_irqrestore(&ide_lock, flags);

	/*
	 * if pending commands, try service before giving up
	 */
	if (ata_pending_commands(drive)) {
		u8 stat = hwif->INB(IDE_STATUS_REG);

		if ((stat & SRV_STAT) && (ide_service(drive) == ide_started))
			return;
	}

	if (drive)
		ide_tcq_invalidate_queue(drive);
}

void __ide_tcq_set_intr(ide_hwgroup_t *hwgroup, ide_handler_t *handler)
{
	/*
	 * always just bump the timer for now, the timeout handling will
	 * have to be changed to be per-command
	 */
	hwgroup->timer.function = ide_tcq_intr_timeout;
	hwgroup->timer.data = (unsigned long) hwgroup->drive;
	mod_timer(&hwgroup->timer, jiffies + 5 * HZ);

	hwgroup->handler = handler;
}

void ide_tcq_set_intr(ide_hwgroup_t *hwgroup, ide_handler_t *handler)
{
	unsigned long flags;

	spin_lock_irqsave(&ide_lock, flags);
	__ide_tcq_set_intr(hwgroup, handler);
	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * wait 400ns, then poll for busy_mask to clear from alt status
 */
#define IDE_TCQ_WAIT	(10000)
int ide_tcq_wait_altstat(ide_drive_t *drive, byte *stat, byte busy_mask)
{
	ide_hwif_t *hwif = HWIF(drive);
	int i = 0;

	udelay(1);

	do {
		*stat = hwif->INB(IDE_ALTSTATUS_REG);

		if (!(*stat & busy_mask))
			break;

		if (unlikely(i++ > IDE_TCQ_WAIT))
			return 1;

		udelay(10);
	} while (1);

	return 0;
}

/*
 * issue SERVICE command to drive -- drive must have been selected first,
 * and it must have reported a need for service (status has SRV_STAT set)
 *
 * Also, nIEN must be set as not to need protection against ide_dmaq_intr
 */
ide_startstop_t ide_service(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	struct request *rq;
	byte feat, stat;
	int tag;

	TCQ_PRINTK("%s: started service\n", drive->name);

	/*
	 * could be called with IDE_DMA in-progress from invalidate
	 * handler, refuse to do anything
	 */
	if (hwif->dma)
		return ide_stopped;

	/*
	 * need to select the right drive first...
	 */
	if (drive != HWGROUP(drive)->drive) {
		SELECT_DRIVE(drive);
		udelay(10);
	}

	drive_ctl_nien(drive, 1);

	/*
	 * send SERVICE, wait 400ns, wait for BUSY_STAT to clear
	 */
	hwif->OUTB(WIN_QUEUED_SERVICE, IDE_COMMAND_REG);

	if (ide_tcq_wait_altstat(drive, &stat, BUSY_STAT)) {
		printk(KERN_ERR "ide_service: BUSY clear took too long\n");
		ide_dump_status(drive, "ide_service", stat);
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
	feat = hwif->INB(IDE_NSECTOR_REG);
	if (feat & REL) {
		HWGROUP(drive)->rq = NULL;
		printk(KERN_ERR "%s: release in service\n", drive->name);
		return ide_stopped;
	}

	tag = feat >> 3;

	TCQ_PRINTK("ide_service: stat %x, feat %x\n", stat, feat);

	spin_lock_irqsave(&ide_lock, flags);

	if ((rq = blk_queue_find_tag(drive->queue, tag))) {
		HWGROUP(drive)->rq = rq;

		/*
		 * we'll start a dma read or write, device will trigger
		 * interrupt to indicate end of transfer, release is not
		 * allowed
		 */
		TCQ_PRINTK("ide_service: starting command, stat=%x\n", stat);
		spin_unlock_irqrestore(&ide_lock, flags);
		return HWIF(drive)->ide_dma_queued_start(drive);
	}

	printk(KERN_ERR "ide_service: missing request for tag %d\n", tag);
	spin_unlock_irqrestore(&ide_lock, flags);
	return ide_stopped;
}

ide_startstop_t ide_check_service(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	byte stat;

	TCQ_PRINTK("%s: ide_check_service\n", drive->name);

	if (!ata_pending_commands(drive))
		return ide_stopped;

	stat = hwif->INB(IDE_STATUS_REG);
	if (stat & SRV_STAT)
		return ide_service(drive);

	/*
	 * we have pending commands, wait for interrupt
	 */
	TCQ_PRINTK("%s: wait for service interrupt\n", drive->name);
	ide_tcq_set_intr(HWGROUP(drive), ide_dmaq_intr);
	return ide_started;
}

ide_startstop_t ide_dmaq_complete(ide_drive_t *drive, struct request *rq, byte stat)
{
	byte dma_stat;

	/*
	 * transfer was in progress, stop DMA engine
	 */
	dma_stat = HWIF(drive)->ide_dma_end(drive);

	/*
	 * must be end of I/O, check status and complete as necessary
	 */
	if (unlikely(!OK_STAT(stat, READY_STAT, drive->bad_wstat | DRQ_STAT))) {
		printk(KERN_ERR "ide_dmaq_intr: %s: error status %x\n",drive->name,stat);
		ide_dump_status(drive, "ide_dmaq_complete", stat);
		ide_tcq_invalidate_queue(drive);
		return ide_stopped;
	}

	if (dma_stat)
		printk(KERN_WARNING "%s: bad DMA status (dma_stat=%x)\n", drive->name, dma_stat);

	TCQ_PRINTK("ide_dmaq_complete: ending %p, tag %d\n", rq, rq->tag);
	ide_end_request(drive, 1, rq->nr_sectors);

	/*
	 * we completed this command, check if we can service a new command
	 */
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
	struct request *rq = HWGROUP(drive)->rq;
	ide_hwif_t *hwif = HWIF(drive);
	byte stat = hwif->INB(IDE_STATUS_REG);

	TCQ_PRINTK("ide_dmaq_intr: stat=%x\n", stat);

	/*
	 * if a command completion interrupt is pending, do that first and
	 * check service afterwards
	 */
	if (rq) {
		TCQ_PRINTK("ide_dmaq_intr: completion\n");
		return ide_dmaq_complete(drive, rq, stat);
	}

	/*
	 * service interrupt
	 */
	if (stat & SRV_STAT) {
		TCQ_PRINTK("ide_dmaq_intr: SERV (stat=%x)\n", stat);
		return ide_service(drive);
	}

	printk("ide_dmaq_intr: stat=%x, not expected\n", stat);
	return ide_check_service(drive);
}

/*
 * check if the ata adapter this drive is attached to supports the
 * NOP auto-poll for multiple tcq enabled drives on one channel
 */
static int ide_tcq_check_autopoll(ide_drive_t *drive)
{
	ide_task_t *args;
	int i, drives;

	/*
	 * only need to probe if both drives on a channel support tcq
	 */
	for (i = 0, drives = 0; i < MAX_DRIVES; i++)
		if (HWIF(drive)->drives[i].present && drive->media == ide_disk)
			drives++;

	if (drives <= 1)
		return 0;

	/*
	 * what a mess...
	 */
	args = kmalloc(sizeof(*args), GFP_ATOMIC);
	if (!args)
		return 1;

	memset(args, 0, sizeof(*args));

	args->tfRegister[IDE_FEATURE_OFFSET] = 0x01;
	args->tfRegister[IDE_COMMAND_OFFSET] = WIN_NOP;
	args->command_type = ide_cmd_type_parser(args);
	args->handler = ide_tcq_nop_handler;
	return ide_raw_taskfile(drive, args, NULL);
}

/*
 * configure the drive for tcq
 */
static int ide_tcq_configure(ide_drive_t *drive)
{
	int tcq_mask = 1 << 1 | 1 << 14;
	int tcq_bits = tcq_mask | 1 << 15;
	ide_task_t *args;

	/*
	 * bit 14 and 1 must be set in word 83 of the device id to indicate
	 * support for dma queued protocol, and bit 15 must be cleared
	 */
	if ((drive->id->command_set_2 & tcq_bits) ^ tcq_mask) {
		printk(KERN_INFO "%s: TCQ not supported\n", drive->name);
		return -EIO;
	}

	args = kmalloc(sizeof(*args), GFP_ATOMIC);
	if (!args)
		return -ENOMEM;

	memset(args, 0, sizeof(ide_task_t));
	args->tfRegister[IDE_COMMAND_OFFSET] = WIN_SETFEATURES;
	args->tfRegister[IDE_FEATURE_OFFSET] = SETFEATURES_EN_WCACHE;
	args->command_type = ide_cmd_type_parser(args);

	if (ide_raw_taskfile(drive, args, NULL)) {
		printk(KERN_WARNING "%s: failed to enable write cache\n", drive->name);
		goto err;
	}

	/*
	 * disable RELease interrupt, it's quicker to poll this after
	 * having sent the command opcode
	 */
	memset(args, 0, sizeof(ide_task_t));
	args->tfRegister[IDE_COMMAND_OFFSET] = WIN_SETFEATURES;
	args->tfRegister[IDE_FEATURE_OFFSET] = SETFEATURES_DIS_RI;
	args->command_type = ide_cmd_type_parser(args);

	if (ide_raw_taskfile(drive, args, NULL)) {
		printk(KERN_ERR "%s: disabling release interrupt fail\n", drive->name);
		goto err;
	}

#ifdef IDE_TCQ_FIDDLE_SI
	/*
	 * enable SERVICE interrupt
	 */
	memset(args, 0, sizeof(ide_task_t));
	args->tfRegister[IDE_COMMAND_OFFSET] = WIN_SETFEATURES;
	args->tfRegister[IDE_FEATURE_OFFSET] = SETFEATURES_EN_SI;
	args->command_type = ide_cmd_type_parser(args);

	if (ide_raw_taskfile(drive, args, NULL)) {
		printk(KERN_ERR "%s: enabling service interrupt fail\n", drive->name);
		goto err;
	}
#endif

	kfree(args);
	return 0;
err:
	kfree(args);
	return -EIO;
}

/*
 * for now assume that command list is always as big as we need and don't
 * attempt to shrink it on tcq disable
 */
static int ide_enable_queued(ide_drive_t *drive, int on)
{
	struct ide_tcq_blacklist *itb;
	int depth = drive->using_tcq ? drive->queue_depth : 0;

	/*
	 * disable or adjust queue depth
	 */
	if (!on) {
		if (drive->using_tcq)
			printk(KERN_INFO "%s: TCQ disabled\n", drive->name);

		drive->using_tcq = 0;
		return 0;
	}

	if (ide_tcq_configure(drive)) {
		drive->using_tcq = 0;
		return 1;
	}

	/*
	 * some drives need limited transfer size in tcq
	 */
	itb = ide_find_drive_blacklist(drive);
	if (itb && itb->max_sectors) {
		if (itb->max_sectors > HWIF(drive)->rqsize)
			itb->max_sectors = HWIF(drive)->rqsize;

		blk_queue_max_sectors(drive->queue, itb->max_sectors);
	}

	/*
	 * enable block tagging
	 */
	if (!blk_queue_tagged(drive->queue))
		blk_queue_init_tags(drive->queue, IDE_MAX_TAG, NULL);

	/*
	 * check auto-poll support
	 */
	ide_tcq_check_autopoll(drive);

	if (depth != drive->queue_depth)
		printk(KERN_INFO "%s: tagged command queueing enabled, command queue depth %d\n", drive->name, drive->queue_depth);

	drive->using_tcq = 1;
	return 0;
}

int ide_tcq_wait_dataphase(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	byte stat;
	int i;

	do {
		stat = hwif->INB(IDE_STATUS_REG);
		if (!(stat & BUSY_STAT))
			break;

		udelay(10);
	} while (1);

	if (OK_STAT(stat, READY_STAT | DRQ_STAT, drive->bad_wstat))
		return 0;

	i = 0;
	udelay(1);
	do {
		stat = hwif->INB(IDE_STATUS_REG);

		if (OK_STAT(stat, READY_STAT | DRQ_STAT, drive->bad_wstat))
			break;

		++i;
		if (unlikely(i >= IDE_TCQ_WAIT))
			return 1;

		udelay(10);
	} while (1);

	return 0;
}

static int ide_tcq_check_blacklist(ide_drive_t *drive)
{
	struct ide_tcq_blacklist *itb = ide_find_drive_blacklist(drive);

	if (!itb)
		return 0;

	return !itb->works;
}

int __ide_dma_queued_on(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	if (drive->media != ide_disk)
		return 1;
	if (!drive->using_dma)
		return 1;
	if (hwif->chipset == ide_pdc4030)
		return 1;
	if (ide_tcq_check_blacklist(drive)) {
		printk(KERN_WARNING "%s: tcq forbidden by blacklist\n",
					drive->name);
		return 1;
	}
	if (hwif->drives[0].present && hwif->drives[1].present) {
		printk(KERN_WARNING "%s: only one drive on a channel supported"
					" for tcq\n", drive->name);
		return 1;
	}
	if (ata_pending_commands(drive)) {
		printk(KERN_WARNING "ide-tcq; can't toggle tcq feature on "
					"busy drive\n");
		return 1;
	}

	return ide_enable_queued(drive, 1);
}

int __ide_dma_queued_off(ide_drive_t *drive)
{
	if (drive->media != ide_disk)
		return 1;
	if (ata_pending_commands(drive)) {
		printk("ide-tcq; can't toggle tcq feature on busy drive\n");
		return 1;
	}

	return ide_enable_queued(drive, 0);
}

static ide_startstop_t ide_dma_queued_rw(ide_drive_t *drive, u8 command)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	byte stat, feat;

	TCQ_PRINTK("%s: starting tag\n", drive->name);

	/*
	 * set nIEN, tag start operation will enable again when
	 * it is safe
	 */
	drive_ctl_nien(drive, 1);

	TCQ_PRINTK("%s: sending cmd=%x\n", drive->name, command);
	hwif->OUTB(command, IDE_COMMAND_REG);

	if (ide_tcq_wait_altstat(drive, &stat, BUSY_STAT)) {
		printk("%s: alt stat timeout\n", drive->name);
		goto err;
	}

	drive_ctl_nien(drive, 0);

	if (stat & ERR_STAT)
		goto err;

	/*
	 * bus not released, start dma
	 */
	feat = hwif->INB(IDE_NSECTOR_REG);
	if (!(feat & REL)) {
		TCQ_PRINTK("IMMED in queued_start, feat=%x\n", feat);
		return hwif->ide_dma_queued_start(drive);
	}

	/*
	 * drive released the bus, clear active request and check for service
	 */
	spin_lock_irqsave(&ide_lock, flags);
	HWGROUP(drive)->rq = NULL;
	__ide_tcq_set_intr(HWGROUP(drive), ide_dmaq_intr);
	spin_unlock_irqrestore(&ide_lock, flags);

	TCQ_PRINTK("REL in queued_start\n");

	stat = hwif->INB(IDE_STATUS_REG);
	if (stat & SRV_STAT)
		return ide_service(drive);

	return ide_released;
err:
	ide_dump_status(drive, "rw_queued", stat);
	ide_tcq_invalidate_queue(drive);
	return ide_stopped;
}

ide_startstop_t __ide_dma_queued_read(ide_drive_t *drive)
{
	u8 command = WIN_READDMA_QUEUED;

	if (drive->addressing == 1)
		 command = WIN_READDMA_QUEUED_EXT;

	return ide_dma_queued_rw(drive, command);
}

ide_startstop_t __ide_dma_queued_write(ide_drive_t *drive)
{
	u8 command = WIN_WRITEDMA_QUEUED;

	if (drive->addressing == 1)
		 command = WIN_WRITEDMA_QUEUED_EXT;

	return ide_dma_queued_rw(drive, command);
}

ide_startstop_t __ide_dma_queued_start(ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	struct request *rq = hwgroup->rq;
	ide_hwif_t *hwif = HWIF(drive);
	unsigned int reading = 0;

	TCQ_PRINTK("ide_dma: setting up queued tag=%d\n", rq->tag);

	if (!hwgroup->busy)
		printk(KERN_ERR "queued_rw: hwgroup not busy\n");

	if (ide_tcq_wait_dataphase(drive)) {
		printk(KERN_WARNING "timeout waiting for data phase\n");
		return ide_stopped;
	}

	if (rq_data_dir(rq) == READ)
		reading = 1 << 3;

	if (ide_start_dma(hwif, drive, reading))
		return ide_stopped;

	ide_tcq_set_intr(hwgroup, ide_dmaq_intr);

	if (!hwif->ide_dma_begin(drive))
		return ide_started;

	return ide_stopped;
}
