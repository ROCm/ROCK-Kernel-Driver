/*
 *  linux/drivers/ide/ide-disk.c	Version 1.18	Mar 05, 2003
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
 *  Copyright (C) 1998-2002  Linux ATA Development
 *				Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2003	     Red Hat <alan@redhat.com>
 */

/*
 *  Mostly written by Mark Lord <mlord@pobox.com>
 *                and Gadi Oxman <gadio@netvision.net.il>
 *                and Andre Hedrick <andre@linux-ide.org>
 *
 * This is the IDE/ATA disk driver, as evolved from hd.c and ide.c.
 *
 * Version 1.00		move disk only code from ide.c to ide-disk.c
 *			support optional byte-swapping of all data
 * Version 1.01		fix previous byte-swapping code
 * Version 1.02		remove ", LBA" from drive identification msgs
 * Version 1.03		fix display of id->buf_size for big-endian
 * Version 1.04		add /proc configurable settings and S.M.A.R.T support
 * Version 1.05		add capacity support for ATA3 >= 8GB
 * Version 1.06		get boot-up messages to show full cyl count
 * Version 1.07		disable door-locking if it fails
 * Version 1.08		fixed CHS/LBA translations for ATA4 > 8GB,
 *			process of adding new ATA4 compliance.
 *			fixed problems in allowing fdisk to see
 *			the entire disk.
 * Version 1.09		added increment of rq->sector in ide_multwrite
 *			added UDMA 3/4 reporting
 * Version 1.10		request queue changes, Ultra DMA 100
 * Version 1.11		added 48-bit lba
 * Version 1.12		adding taskfile io access method
 * Version 1.13		added standby and flush-cache for notifier
 * Version 1.14		added acoustic-wcache
 * Version 1.15		convert all calls to ide_raw_taskfile
 *				since args will return register content.
 * Version 1.16		added suspend-resume-checkpower
 * Version 1.17		do flush on standy, do flush on ATA < ATA6
 *			fix wcache setup.
 */

#define IDEDISK_VERSION	"1.18"

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

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
#include <linux/slab.h>
#include <linux/delay.h>

#define _IDE_DISK

#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/div64.h>

/* FIXME: some day we shouldn't need to look in here! */

#include "legacy/pdc4030.h"

/*
 * lba_capacity_is_ok() performs a sanity check on the claimed "lba_capacity"
 * value for this drive (from its reported identification information).
 *
 * Returns:	1 if lba_capacity looks sensible
 *		0 otherwise
 *
 * It is called only once for each drive.
 */
static int lba_capacity_is_ok (struct hd_driveid *id)
{
	unsigned long lba_sects, chs_sects, head, tail;

	/*
	 * The ATA spec tells large drives to return
	 * C/H/S = 16383/16/63 independent of their size.
	 * Some drives can be jumpered to use 15 heads instead of 16.
	 * Some drives can be jumpered to use 4092 cyls instead of 16383.
	 */
	if ((id->cyls == 16383
	     || (id->cyls == 4092 && id->cur_cyls == 16383)) &&
	    id->sectors == 63 &&
	    (id->heads == 15 || id->heads == 16) &&
	    (id->lba_capacity >= 16383*63*id->heads))
		return 1;

	lba_sects   = id->lba_capacity;
	chs_sects   = id->cyls * id->heads * id->sectors;

	/* perform a rough sanity check on lba_sects:  within 10% is OK */
	if ((lba_sects - chs_sects) < chs_sects/10)
		return 1;

	/* some drives have the word order reversed */
	head = ((lba_sects >> 16) & 0xffff);
	tail = (lba_sects & 0xffff);
	lba_sects = (head | (tail << 16));
	if ((lba_sects - chs_sects) < chs_sects/10) {
		id->lba_capacity = lba_sects;
		return 1;	/* lba_capacity is (now) good */
	}

	return 0;	/* lba_capacity value may be bad */
}

static int idedisk_start_tag(ide_drive_t *drive, struct request *rq)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&ide_lock, flags);

	if (ata_pending_commands(drive) < drive->queue_depth)
		ret = blk_queue_start_tag(drive->queue, rq);

	spin_unlock_irqrestore(&ide_lock, flags);
	return ret;
}

#ifndef CONFIG_IDE_TASKFILE_IO

/*
 * read_intr() is the handler for disk read/multread interrupts
 */
static ide_startstop_t read_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u32 i = 0, nsect	= 0, msect = drive->mult_count;
	struct request *rq;
	unsigned long flags;
	u8 stat;
	char *to;

	/* new way for dealing with premature shared PCI interrupts */
	if (!OK_STAT(stat=hwif->INB(IDE_STATUS_REG),DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			return DRIVER(drive)->error(drive, "read_intr", stat);
		}
		/* no data yet, so wait for another interrupt */
		ide_set_handler(drive, &read_intr, WAIT_CMD, NULL);
		return ide_started;
	}
	
read_next:
	rq = HWGROUP(drive)->rq;
	if (msect) {
		if ((nsect = rq->current_nr_sectors) > msect)
			nsect = msect;
		msect -= nsect;
	} else
		nsect = 1;
	to = ide_map_buffer(rq, &flags);
	taskfile_input_data(drive, to, nsect * SECTOR_WORDS);
#ifdef DEBUG
	printk("%s:  read: sectors(%ld-%ld), buffer=0x%08lx, remaining=%ld\n",
		drive->name, rq->sector, rq->sector+nsect-1,
		(unsigned long) rq->buffer+(nsect<<9), rq->nr_sectors-nsect);
#endif
	ide_unmap_buffer(rq, to, &flags);
	rq->sector += nsect;
	rq->errors = 0;
	i = (rq->nr_sectors -= nsect);
	if (((long)(rq->current_nr_sectors -= nsect)) <= 0)
		ide_end_request(drive, 1, rq->hard_cur_sectors);
	/*
	 * Another BH Page walker and DATA INTEGRITY Questioned on ERROR.
	 * If passed back up on multimode read, BAD DATA could be ACKED
	 * to FILE SYSTEMS above ...
	 */
	if (i > 0) {
		if (msect)
			goto read_next;
		ide_set_handler(drive, &read_intr, WAIT_CMD, NULL);
                return ide_started;
	}
        return ide_stopped;
}

/*
 * write_intr() is the handler for disk write interrupts
 */
static ide_startstop_t write_intr (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
	ide_hwif_t *hwif	= HWIF(drive);
	struct request *rq	= hwgroup->rq;
	u32 i = 0;
	u8 stat;

	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG),
			DRIVE_READY, drive->bad_wstat)) {
		printk("%s: write_intr error1: nr_sectors=%ld, stat=0x%02x\n",
			drive->name, rq->nr_sectors, stat);
        } else {
#ifdef DEBUG
		printk("%s: write: sector %ld, buffer=0x%08lx, remaining=%ld\n",
			drive->name, rq->sector, (unsigned long) rq->buffer,
			rq->nr_sectors-1);
#endif
		if ((rq->nr_sectors == 1) ^ ((stat & DRQ_STAT) != 0)) {
			rq->sector++;
			rq->errors = 0;
			i = --rq->nr_sectors;
			--rq->current_nr_sectors;
			if (((long)rq->current_nr_sectors) <= 0)
				ide_end_request(drive, 1, rq->hard_cur_sectors);
			if (i > 0) {
				unsigned long flags;
				char *to = ide_map_buffer(rq, &flags);
				taskfile_output_data(drive, to, SECTOR_WORDS);
				ide_unmap_buffer(rq, to, &flags);
				ide_set_handler(drive, &write_intr, WAIT_CMD, NULL);
                                return ide_started;
			}
                        return ide_stopped;
		}
		/* the original code did this here (?) */
		return ide_stopped;
	}
	return DRIVER(drive)->error(drive, "write_intr", stat);
}

/*
 * ide_multwrite() transfers a block of up to mcount sectors of data
 * to a drive as part of a disk multiple-sector write operation.
 *
 * Note that we may be called from two contexts - __ide_do_rw_disk() context
 * and IRQ context. The IRQ can happen any time after we've output the
 * full "mcount" number of sectors, so we must make sure we update the
 * state _before_ we output the final part of the data!
 *
 * The update and return to BH is a BLOCK Layer Fakey to get more data
 * to satisfy the hardware atomic segment.  If the hardware atomic segment
 * is shorter or smaller than the BH segment then we should be OKAY.
 * This is only valid if we can rewind the rq->current_nr_sectors counter.
 */
static void ide_multwrite(ide_drive_t *drive, unsigned int mcount)
{
 	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
 	struct request *rq	= &hwgroup->wrq;
 
  	do {
  		char *buffer;
  		int nsect = rq->current_nr_sectors;
		unsigned long flags;
 
		if (nsect > mcount)
			nsect = mcount;
		mcount -= nsect;
		buffer = ide_map_buffer(rq, &flags);

		rq->sector += nsect;
		rq->nr_sectors -= nsect;
		rq->current_nr_sectors -= nsect;

		/* Do we move to the next bh after this? */
		if (!rq->current_nr_sectors) {
			struct bio *bio = rq->bio;

			/*
			 * only move to next bio, when we have processed
			 * all bvecs in this one.
			 */
			if (++bio->bi_idx >= bio->bi_vcnt) {
				bio->bi_idx = bio->bi_vcnt - rq->nr_cbio_segments;
				bio = bio->bi_next;
			}

			/* end early early we ran out of requests */
			if (!bio) {
				mcount = 0;
			} else {
				rq->bio = bio;
				rq->nr_cbio_segments = bio_segments(bio);
				rq->current_nr_sectors = bio_cur_sectors(bio);
				rq->hard_cur_sectors = rq->current_nr_sectors;
			}
		}

		/*
		 * Ok, we're all setup for the interrupt
		 * re-entering us on the last transfer.
		 */
		taskfile_output_data(drive, buffer, nsect<<7);
		ide_unmap_buffer(rq, buffer, &flags);
	} while (mcount);
}

/*
 * multwrite_intr() is the handler for disk multwrite interrupts
 */
static ide_startstop_t multwrite_intr (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup	= HWGROUP(drive);
	ide_hwif_t *hwif	= HWIF(drive);
	struct request *rq	= &hwgroup->wrq;
	struct bio *bio		= rq->bio;
	u8 stat;

	stat = hwif->INB(IDE_STATUS_REG);
	if (OK_STAT(stat, DRIVE_READY, drive->bad_wstat)) {
		if (stat & DRQ_STAT) {
			/*
			 *	The drive wants data. Remember rq is the copy
			 *	of the request
			 */
			if (rq->nr_sectors) {
				ide_multwrite(drive, drive->mult_count);
				ide_set_handler(drive, &multwrite_intr, WAIT_CMD, NULL);
				return ide_started;
			}
		} else {
			/*
			 *	If the copy has all the blocks completed then
			 *	we can end the original request.
			 */
			if (!rq->nr_sectors) {	/* all done? */
				bio->bi_idx = bio->bi_vcnt - rq->nr_cbio_segments;
				rq = hwgroup->rq;
				ide_end_request(drive, 1, rq->nr_sectors);
				return ide_stopped;
			}
		}
		bio->bi_idx = bio->bi_vcnt - rq->nr_cbio_segments;
		/* the original code did this here (?) */
		return ide_stopped;
	}
	bio->bi_idx = bio->bi_vcnt - rq->nr_cbio_segments;
	return DRIVER(drive)->error(drive, "multwrite_intr", stat);
}

/*
 * __ide_do_rw_disk() issues READ and WRITE commands to a disk,
 * using LBA if supported, or CHS otherwise, to address sectors.
 * It also takes care of issuing special DRIVE_CMDs.
 */
ide_startstop_t __ide_do_rw_disk (ide_drive_t *drive, struct request *rq, sector_t block)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 lba48		= (drive->addressing == 1) ? 1 : 0;
	task_ioreg_t command	= WIN_NOP;
	ata_nsector_t		nsectors;

	nsectors.all		= (u16) rq->nr_sectors;

	if (IDE_CONTROL_REG)
		hwif->OUTB(drive->ctl, IDE_CONTROL_REG);

	if (drive->select.b.lba) {
		if (drive->addressing == 1) {
			task_ioreg_t tasklets[10];

			if (blk_rq_tagged(rq)) {
				tasklets[0] = nsectors.b.low;
				tasklets[1] = nsectors.b.high;
				tasklets[2] = rq->tag << 3;
				tasklets[3] = 0;
			} else {
				tasklets[0] = 0;
				tasklets[1] = 0;
				tasklets[2] = nsectors.b.low;
				tasklets[3] = nsectors.b.high;
			}

			tasklets[4] = (task_ioreg_t) block;
			tasklets[5] = (task_ioreg_t) (block>>8);
			tasklets[6] = (task_ioreg_t) (block>>16);
			tasklets[7] = (task_ioreg_t) (block>>24);
			if (sizeof(block) == 4) {
				tasklets[8] = (task_ioreg_t) 0;
				tasklets[9] = (task_ioreg_t) 0;
			} else {
				tasklets[8] = (task_ioreg_t)((u64)block >> 32);
				tasklets[9] = (task_ioreg_t)((u64)block >> 40);
			}
#ifdef DEBUG
			printk("%s: %sing: LBAsect=%lu, sectors=%ld, "
				"buffer=0x%08lx, LBAsect=0x%012lx\n",
				drive->name,
				rq_data_dir(rq)==READ?"read":"writ",
				block,
				rq->nr_sectors,
				(unsigned long) rq->buffer,
				block);
			printk("%s: 0x%02x%02x 0x%02x%02x%02x%02x%02x%02x\n",
				drive->name, tasklets[3], tasklets[2],
				tasklets[9], tasklets[8], tasklets[7],
				tasklets[6], tasklets[5], tasklets[4]);
#endif
			hwif->OUTB(tasklets[1], IDE_FEATURE_REG);
			hwif->OUTB(tasklets[3], IDE_NSECTOR_REG);
			hwif->OUTB(tasklets[7], IDE_SECTOR_REG);
			hwif->OUTB(tasklets[8], IDE_LCYL_REG);
			hwif->OUTB(tasklets[9], IDE_HCYL_REG);

			hwif->OUTB(tasklets[0], IDE_FEATURE_REG);
			hwif->OUTB(tasklets[2], IDE_NSECTOR_REG);
			hwif->OUTB(tasklets[4], IDE_SECTOR_REG);
			hwif->OUTB(tasklets[5], IDE_LCYL_REG);
			hwif->OUTB(tasklets[6], IDE_HCYL_REG);
			hwif->OUTB(0x00|drive->select.all,IDE_SELECT_REG);
		} else {
#ifdef DEBUG
			printk("%s: %sing: LBAsect=%llu, sectors=%ld, "
				"buffer=0x%08lx\n",
				drive->name,
				rq_data_dir(rq)==READ?"read":"writ",
				(unsigned long long)block, rq->nr_sectors,
				(unsigned long) rq->buffer);
#endif
			if (blk_rq_tagged(rq)) {
				hwif->OUTB(nsectors.b.low, IDE_FEATURE_REG);
				hwif->OUTB(rq->tag << 3, IDE_NSECTOR_REG);
			} else {
				hwif->OUTB(0x00, IDE_FEATURE_REG);
				hwif->OUTB(nsectors.b.low, IDE_NSECTOR_REG);
			}

			hwif->OUTB(block, IDE_SECTOR_REG);
			hwif->OUTB(block>>=8, IDE_LCYL_REG);
			hwif->OUTB(block>>=8, IDE_HCYL_REG);
			hwif->OUTB(((block>>8)&0x0f)|drive->select.all,IDE_SELECT_REG);
		}
	} else {
		unsigned int sect,head,cyl,track;
		track = (int)block / drive->sect;
		sect  = (int)block % drive->sect + 1;
		hwif->OUTB(sect, IDE_SECTOR_REG);
		head  = track % drive->head;
		cyl   = track / drive->head;

		if (blk_rq_tagged(rq)) {
			hwif->OUTB(nsectors.b.low, IDE_FEATURE_REG);
			hwif->OUTB(rq->tag << 3, IDE_NSECTOR_REG);
		} else {
			hwif->OUTB(0x00, IDE_FEATURE_REG);
			hwif->OUTB(nsectors.b.low, IDE_NSECTOR_REG);
		}

		hwif->OUTB(cyl, IDE_LCYL_REG);
		hwif->OUTB(cyl>>8, IDE_HCYL_REG);
		hwif->OUTB(head|drive->select.all,IDE_SELECT_REG);
#ifdef DEBUG
		printk("%s: %sing: CHS=%d/%d/%d, sectors=%ld, buffer=0x%08lx\n",
			drive->name, rq_data_dir(rq)==READ?"read":"writ", cyl,
			head, sect, rq->nr_sectors, (unsigned long) rq->buffer);
#endif
	}

	if (rq_data_dir(rq) == READ) {
#ifdef CONFIG_BLK_DEV_IDE_TCQ
		if (blk_rq_tagged(rq))
			return __ide_dma_queued_read(drive);
#endif
		if (drive->using_dma && !hwif->ide_dma_read(drive))
			return ide_started;

		command = ((drive->mult_count) ?
			   ((lba48) ? WIN_MULTREAD_EXT : WIN_MULTREAD) :
			   ((lba48) ? WIN_READ_EXT : WIN_READ));
		ide_execute_command(drive, command, &read_intr, WAIT_CMD, NULL);
		return ide_started;
	} else {
		ide_startstop_t startstop;
#ifdef CONFIG_BLK_DEV_IDE_TCQ
		if (blk_rq_tagged(rq))
			return __ide_dma_queued_write(drive);
#endif
		if (drive->using_dma && !(HWIF(drive)->ide_dma_write(drive)))
			return ide_started;

		command = ((drive->mult_count) ?
			   ((lba48) ? WIN_MULTWRITE_EXT : WIN_MULTWRITE) :
			   ((lba48) ? WIN_WRITE_EXT : WIN_WRITE));
		hwif->OUTB(command, IDE_COMMAND_REG);

		if (ide_wait_stat(&startstop, drive, DATA_READY,
				drive->bad_wstat, WAIT_DRQ)) {
			printk(KERN_ERR "%s: no DRQ after issuing %s\n",
				drive->name,
				drive->mult_count ? "MULTWRITE" : "WRITE");
			return startstop;
		}
		if (!drive->unmask)
			local_irq_disable();
		if (drive->mult_count) {
			ide_hwgroup_t *hwgroup = HWGROUP(drive);

			hwgroup->wrq = *rq; /* scratchpad */
			ide_set_handler(drive, &multwrite_intr, WAIT_CMD, NULL);
			ide_multwrite(drive, drive->mult_count);
		} else {
			unsigned long flags;
			char *to = ide_map_buffer(rq, &flags);
			ide_set_handler(drive, &write_intr, WAIT_CMD, NULL);
			taskfile_output_data(drive, to, SECTOR_WORDS);
			ide_unmap_buffer(rq, to, &flags);
		}
		return ide_started;
	}
}
EXPORT_SYMBOL_GPL(__ide_do_rw_disk);

#else /* CONFIG_IDE_TASKFILE_IO */

static ide_startstop_t chs_rw_disk(ide_drive_t *, struct request *, unsigned long);
static ide_startstop_t lba_28_rw_disk(ide_drive_t *, struct request *, unsigned long);
static ide_startstop_t lba_48_rw_disk(ide_drive_t *, struct request *, unsigned long long);

/*
 * __ide_do_rw_disk() issues READ and WRITE commands to a disk,
 * using LBA if supported, or CHS otherwise, to address sectors.
 * It also takes care of issuing special DRIVE_CMDs.
 */
ide_startstop_t __ide_do_rw_disk (ide_drive_t *drive, struct request *rq, sector_t block)
{
	/*
	 * 268435455  == 137439 MB or 28bit limit
	 *
	 * need to add split taskfile operations based on 28bit threshold.
	 */
	if (drive->addressing == 1)		/* 48-bit LBA */
		return lba_48_rw_disk(drive, rq, (unsigned long long) block);
	if (drive->select.b.lba)		/* 28-bit LBA */
		return lba_28_rw_disk(drive, rq, (unsigned long) block);

	/* 28-bit CHS : DIE DIE DIE piece of legacy crap!!! */
	return chs_rw_disk(drive, rq, (unsigned long) block);
}
EXPORT_SYMBOL_GPL(__ide_do_rw_disk);

static u8 get_command(ide_drive_t *drive, int cmd, ide_task_t *task)
{
	unsigned int lba48 = (drive->addressing == 1) ? 1 : 0;

	if (cmd == READ) {
		task->command_type = IDE_DRIVE_TASK_IN;
		if (drive->using_tcq)
			return lba48 ? WIN_READDMA_QUEUED_EXT : WIN_READDMA_QUEUED;
		if (drive->using_dma)
			return lba48 ? WIN_READDMA_EXT : WIN_READDMA;
		if (drive->mult_count) {
			task->handler = &task_mulin_intr;
			return lba48 ? WIN_MULTREAD_EXT : WIN_MULTREAD;
		}
		task->handler = &task_in_intr;
		return lba48 ? WIN_READ_EXT : WIN_READ;
	} else {
		task->command_type = IDE_DRIVE_TASK_RAW_WRITE;
		if (drive->using_tcq)
			return lba48 ? WIN_WRITEDMA_QUEUED_EXT : WIN_WRITEDMA_QUEUED;
		if (drive->using_dma)
			return lba48 ? WIN_WRITEDMA_EXT : WIN_WRITEDMA;
		if (drive->mult_count) {
			task->prehandler = &pre_task_mulout_intr;
			task->handler = &task_mulout_intr;
			return lba48 ? WIN_MULTWRITE_EXT : WIN_MULTWRITE;
		}
		task->prehandler = &pre_task_out_intr;
		task->handler = &task_out_intr;
		return lba48 ? WIN_WRITE_EXT : WIN_WRITE;
	}
}

static ide_startstop_t chs_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	ide_task_t		args;
	int			sectors;
	ata_nsector_t		nsectors;
	unsigned int track	= (block / drive->sect);
	unsigned int sect	= (block % drive->sect) + 1;
	unsigned int head	= (track % drive->head);
	unsigned int cyl	= (track / drive->head);

	nsectors.all = (u16) rq->nr_sectors;

#ifdef DEBUG
	printk("%s: %sing: ", drive->name, (rq_data_dir(rq)==READ) ? "read" : "writ");
	printk("CHS=%d/%d/%d, ", cyl, head, sect);
	printk("sectors=%ld, ", rq->nr_sectors);
	printk("buffer=0x%08lx\n", (unsigned long) rq->buffer);
#endif

	memset(&args, 0, sizeof(ide_task_t));

	sectors	= (rq->nr_sectors == 256) ? 0x00 : rq->nr_sectors;

	if (blk_rq_tagged(rq)) {
		args.tfRegister[IDE_FEATURE_OFFSET] = sectors;
		args.tfRegister[IDE_NSECTOR_OFFSET] = rq->tag << 3;
	} else
		args.tfRegister[IDE_NSECTOR_OFFSET] = sectors;

	args.tfRegister[IDE_SECTOR_OFFSET]	= sect;
	args.tfRegister[IDE_LCYL_OFFSET]	= cyl;
	args.tfRegister[IDE_HCYL_OFFSET]	= (cyl>>8);
	args.tfRegister[IDE_SELECT_OFFSET]	= head;
	args.tfRegister[IDE_SELECT_OFFSET]	|= drive->select.all;
	args.tfRegister[IDE_COMMAND_OFFSET]	= get_command(drive, rq_data_dir(rq), &args);
	args.rq					= (struct request *) rq;
	rq->special				= (ide_task_t *)&args;
	return do_rw_taskfile(drive, &args);
}

static ide_startstop_t lba_28_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	ide_task_t		args;
	int			sectors;
	ata_nsector_t		nsectors;

	nsectors.all = (u16) rq->nr_sectors;

#ifdef DEBUG
	printk("%s: %sing: ", drive->name, (rq_data_dir(rq)==READ) ? "read" : "writ");
	printk("LBAsect=%lld, ", block);
	printk("sectors=%ld, ", rq->nr_sectors);
	printk("buffer=0x%08lx\n", (unsigned long) rq->buffer);
#endif

	memset(&args, 0, sizeof(ide_task_t));

	sectors = (rq->nr_sectors == 256) ? 0x00 : rq->nr_sectors;

	if (blk_rq_tagged(rq)) {
		args.tfRegister[IDE_FEATURE_OFFSET] = sectors;
		args.tfRegister[IDE_NSECTOR_OFFSET] = rq->tag << 3;
	} else
		args.tfRegister[IDE_NSECTOR_OFFSET] = sectors;

	args.tfRegister[IDE_SECTOR_OFFSET]	= block;
	args.tfRegister[IDE_LCYL_OFFSET]	= (block>>=8);
	args.tfRegister[IDE_HCYL_OFFSET]	= (block>>=8);
	args.tfRegister[IDE_SELECT_OFFSET]	= ((block>>8)&0x0f);
	args.tfRegister[IDE_SELECT_OFFSET]	|= drive->select.all;
	args.tfRegister[IDE_COMMAND_OFFSET]	= get_command(drive, rq_data_dir(rq), &args);
	args.rq					= (struct request *) rq;
	rq->special				= (ide_task_t *)&args;
	return do_rw_taskfile(drive, &args);
}

/*
 * 268435455  == 137439 MB or 28bit limit
 * 320173056  == 163929 MB or 48bit addressing
 * 1073741822 == 549756 MB or 48bit addressing fake drive
 */

static ide_startstop_t lba_48_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long long block)
{
	ide_task_t		args;
	int			sectors;
	ata_nsector_t		nsectors;

	nsectors.all = (u16) rq->nr_sectors;

#ifdef DEBUG
	printk("%s: %sing: ", drive->name, (rq_data_dir(rq)==READ) ? "read" : "writ");
	printk("LBAsect=%lld, ", block);
	printk("sectors=%ld, ", rq->nr_sectors);
	printk("buffer=0x%08lx\n", (unsigned long) rq->buffer);
#endif

	memset(&args, 0, sizeof(ide_task_t));

	sectors = (rq->nr_sectors == 65536) ? 0 : rq->nr_sectors;

	if (blk_rq_tagged(rq)) {
		args.tfRegister[IDE_FEATURE_OFFSET] = sectors;
		args.tfRegister[IDE_NSECTOR_OFFSET] = rq->tag << 3;
		args.hobRegister[IDE_FEATURE_OFFSET] = sectors >> 8;
		args.hobRegister[IDE_NSECTOR_OFFSET] = 0;
	} else {
		args.tfRegister[IDE_NSECTOR_OFFSET] = sectors;
		args.hobRegister[IDE_NSECTOR_OFFSET] = sectors >> 8;
	}

	args.tfRegister[IDE_SECTOR_OFFSET]	= block;	/* low lba */
	args.tfRegister[IDE_LCYL_OFFSET]	= (block>>=8);	/* mid lba */
	args.tfRegister[IDE_HCYL_OFFSET]	= (block>>=8);	/* hi  lba */
	args.tfRegister[IDE_SELECT_OFFSET]	= drive->select.all;
	args.tfRegister[IDE_COMMAND_OFFSET]	= get_command(drive, rq_data_dir(rq), &args);
	args.hobRegister[IDE_SECTOR_OFFSET]	= (block>>=8);	/* low lba */
	args.hobRegister[IDE_LCYL_OFFSET]	= (block>>=8);	/* mid lba */
	args.hobRegister[IDE_HCYL_OFFSET]	= (block>>=8);	/* hi  lba */
	args.hobRegister[IDE_SELECT_OFFSET]	= drive->select.all;
	args.hobRegister[IDE_CONTROL_OFFSET_HOB]= (drive->ctl|0x80);
	args.rq					= (struct request *) rq;
	rq->special				= (ide_task_t *)&args;
	return do_rw_taskfile(drive, &args);
}

#endif /* CONFIG_IDE_TASKFILE_IO */

static ide_startstop_t ide_do_rw_disk (ide_drive_t *drive, struct request *rq, sector_t block)
{
	ide_hwif_t *hwif = HWIF(drive);

	BUG_ON(drive->blocked);

	if (!blk_fs_request(rq)) {
		blk_dump_rq_flags(rq, "ide_do_rw_disk - bad command");
		ide_end_request(drive, 0, 0);
		return ide_stopped;
	}

	if (drive->using_tcq && idedisk_start_tag(drive, rq)) {
		if (!ata_pending_commands(drive))
			BUG();

		return ide_started;
	}

	if (hwif->rw_disk)
		return hwif->rw_disk(drive, rq, block);
	else
		return __ide_do_rw_disk(drive, rq, block);
}

static u8 idedisk_dump_status (ide_drive_t *drive, const char *msg, u8 stat)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	u8 err = 0;

	local_irq_set(flags);
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
		err = hwif->INB(IDE_ERROR_REG);
		printk("%s: %s: error=0x%02x", drive->name, msg, err);
#if FANCY_STATUS_DUMPS
		printk(" { ");
		if (err & ABRT_ERR)	printk("DriveStatusError ");
		if (err & ICRC_ERR)
			printk("Bad%s ", (err & ABRT_ERR) ? "CRC" : "Sector");
		if (err & ECC_ERR)	printk("UncorrectableError ");
		if (err & ID_ERR)	printk("SectorIdNotFound ");
		if (err & TRK0_ERR)	printk("TrackZeroNotFound ");
		if (err & MARK_ERR)	printk("AddrMarkNotFound ");
		printk("}");
		if ((err & (BBD_ERR | ABRT_ERR)) == BBD_ERR ||
		    (err & (ECC_ERR|ID_ERR|MARK_ERR))) {
			if (drive->addressing == 1) {
				__u64 sectors = 0;
				u32 low = 0, high = 0;
				low = ide_read_24(drive);
				hwif->OUTB(drive->ctl|0x80, IDE_CONTROL_REG);
				high = ide_read_24(drive);
				sectors = ((__u64)high << 24) | low;
				printk(", LBAsect=%llu, high=%d, low=%d",
				       (unsigned long long) sectors,
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
				printk(", sector=%llu",
					(unsigned long long)HWGROUP(drive)->rq->sector);
		}
	}
#endif	/* FANCY_STATUS_DUMPS */
	printk("\n");
	local_irq_restore(flags);
	return err;
}

ide_startstop_t idedisk_error (ide_drive_t *drive, const char *msg, u8 stat)
{
	ide_hwif_t *hwif;
	struct request *rq;
	u8 err;
	int i = (drive->mult_count ? drive->mult_count : 1) * SECTOR_WORDS;

	err = idedisk_dump_status(drive, msg, stat);

	if (drive == NULL || (rq = HWGROUP(drive)->rq) == NULL)
		return ide_stopped;

	hwif = HWIF(drive);
	/* retry only "normal" I/O: */
	if (rq->flags & (REQ_DRIVE_CMD | REQ_DRIVE_TASK | REQ_DRIVE_TASKFILE)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, stat, err);
		return ide_stopped;
	}
#ifdef CONFIG_IDE_TASKFILE_IO
	/* make rq completion pointers new submission pointers */
	blk_rq_prep_restart(rq);
#endif

	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr)) {
		/* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else if (stat & ERR_STAT) {
		/* err has different meaning on cdrom and tape */
		if (err == ABRT_ERR) {
			if (drive->select.b.lba &&
			    /* some newer drives don't support WIN_SPECIFY */
			    hwif->INB(IDE_COMMAND_REG) == WIN_SPECIFY)
				return ide_stopped;
		} else if ((err & BAD_CRC) == BAD_CRC) {
			/* UDMA crc error, just retry the operation */
			drive->crc_count++;
		} else if (err & (BBD_ERR | ECC_ERR)) {
			/* retries won't help these */
			rq->errors = ERROR_MAX;
		} else if (err & TRK0_ERR) {
			/* help it find track zero */
			rq->errors |= ERROR_RECAL;
		}
	}
	if ((stat & DRQ_STAT) && rq_data_dir(rq) == READ) {
		/*
		 * try_to_flush_leftover_data() is invoked in response to
		 * a drive unexpectedly having its DRQ_STAT bit set.  As
		 * an alternative to resetting the drive, this routine
		 * tries to clear the condition by read a sector's worth
		 * of data from the drive.  Of course, this may not help
		 * if the drive is *waiting* for data from *us*.
		 */
		while (i > 0) {
			u32 buffer[16];
			unsigned int wcount = (i > 16) ? 16 : i;
			i -= wcount;
			taskfile_input_data(drive, buffer, wcount);
		}
	}
	if (hwif->INB(IDE_STATUS_REG) & (BUSY_STAT|DRQ_STAT)) {
		/* force an abort */
		hwif->OUTB(WIN_IDLEIMMEDIATE,IDE_COMMAND_REG);
	}
	if (rq->errors >= ERROR_MAX || blk_noretry_request(rq))
		DRIVER(drive)->end_request(drive, 0, 0);
	else {
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

ide_startstop_t idedisk_abort(ide_drive_t *drive, const char *msg)
{
	ide_hwif_t *hwif;
	struct request *rq;

	if (drive == NULL || (rq = HWGROUP(drive)->rq) == NULL)
		return ide_stopped;

	hwif = HWIF(drive);

	if (rq->flags & (REQ_DRIVE_CMD | REQ_DRIVE_TASK | REQ_DRIVE_TASKFILE)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, BUSY_STAT, 0);
		return ide_stopped;
	}

	DRIVER(drive)->end_request(drive, 0, 0);
	return ide_stopped;
}

/*
 * Queries for true maximum capacity of the drive.
 * Returns maximum LBA address (> 0) of the drive, 0 if failed.
 */
static unsigned long idedisk_read_native_max_address(ide_drive_t *drive)
{
	ide_task_t args;
	unsigned long addr = 0;

	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_SELECT_OFFSET]	= 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_READ_NATIVE_MAX;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		addr = ((args.tfRegister[IDE_SELECT_OFFSET] & 0x0f) << 24)
		     | ((args.tfRegister[  IDE_HCYL_OFFSET]       ) << 16)
		     | ((args.tfRegister[  IDE_LCYL_OFFSET]       ) <<  8)
		     | ((args.tfRegister[IDE_SECTOR_OFFSET]       ));
		addr++;	/* since the return value is (maxlba - 1), we add 1 */
	}
	return addr;
}

static unsigned long long idedisk_read_native_max_address_ext(ide_drive_t *drive)
{
	ide_task_t args;
	unsigned long long addr = 0;

	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));

	args.tfRegister[IDE_SELECT_OFFSET]	= 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_READ_NATIVE_MAX_EXT;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
        /* submit command request */
        ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		u32 high = (args.hobRegister[IDE_HCYL_OFFSET] << 16) |
			   (args.hobRegister[IDE_LCYL_OFFSET] <<  8) |
			    args.hobRegister[IDE_SECTOR_OFFSET];
		u32 low  = ((args.tfRegister[IDE_HCYL_OFFSET])<<16) |
			   ((args.tfRegister[IDE_LCYL_OFFSET])<<8) |
			    (args.tfRegister[IDE_SECTOR_OFFSET]);
		addr = ((__u64)high << 24) | low;
		addr++;	/* since the return value is (maxlba - 1), we add 1 */
	}
	return addr;
}

#ifdef CONFIG_IDEDISK_STROKE
/*
 * Sets maximum virtual LBA address of the drive.
 * Returns new maximum virtual LBA address (> 0) or 0 on failure.
 */
static unsigned long idedisk_set_max_address(ide_drive_t *drive, unsigned long addr_req)
{
	ide_task_t args;
	unsigned long addr_set = 0;
	
	addr_req--;
	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_SECTOR_OFFSET]	= ((addr_req >>  0) & 0xff);
	args.tfRegister[IDE_LCYL_OFFSET]	= ((addr_req >>  8) & 0xff);
	args.tfRegister[IDE_HCYL_OFFSET]	= ((addr_req >> 16) & 0xff);
	args.tfRegister[IDE_SELECT_OFFSET]	= ((addr_req >> 24) & 0x0f) | 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SET_MAX;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);
	/* if OK, read new maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		addr_set = ((args.tfRegister[IDE_SELECT_OFFSET] & 0x0f) << 24)
			 | ((args.tfRegister[  IDE_HCYL_OFFSET]       ) << 16)
			 | ((args.tfRegister[  IDE_LCYL_OFFSET]       ) <<  8)
			 | ((args.tfRegister[IDE_SECTOR_OFFSET]       ));
		addr_set++;
	}
	return addr_set;
}

static unsigned long long idedisk_set_max_address_ext(ide_drive_t *drive, unsigned long long addr_req)
{
	ide_task_t args;
	unsigned long long addr_set = 0;

	addr_req--;
	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_SECTOR_OFFSET]	= ((addr_req >>  0) & 0xff);
	args.tfRegister[IDE_LCYL_OFFSET]	= ((addr_req >>= 8) & 0xff);
	args.tfRegister[IDE_HCYL_OFFSET]	= ((addr_req >>= 8) & 0xff);
	args.tfRegister[IDE_SELECT_OFFSET]      = 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SET_MAX_EXT;
	args.hobRegister[IDE_SECTOR_OFFSET]	= (addr_req >>= 8) & 0xff;
	args.hobRegister[IDE_LCYL_OFFSET]	= (addr_req >>= 8) & 0xff;
	args.hobRegister[IDE_HCYL_OFFSET]	= (addr_req >>= 8) & 0xff;
	args.hobRegister[IDE_SELECT_OFFSET]	= 0x40;
	args.hobRegister[IDE_CONTROL_OFFSET_HOB]= (drive->ctl|0x80);
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);
	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		u32 high = (args.hobRegister[IDE_HCYL_OFFSET] << 16) |
			   (args.hobRegister[IDE_LCYL_OFFSET] <<  8) |
			    args.hobRegister[IDE_SECTOR_OFFSET];
		u32 low  = ((args.tfRegister[IDE_HCYL_OFFSET])<<16) |
			   ((args.tfRegister[IDE_LCYL_OFFSET])<<8) |
			    (args.tfRegister[IDE_SECTOR_OFFSET]);
		addr_set = ((__u64)high << 24) | low;
		addr_set++;
	}
	return addr_set;
}

#endif /* CONFIG_IDEDISK_STROKE */

static unsigned long long sectors_to_MB(unsigned long long n)
{
	n <<= 9;		/* make it bytes */
	do_div(n, 1000000);	/* make it MB */
	return n;
}

/*
 * Bits 10 of command_set_1 and cfs_enable_1 must be equal,
 * so on non-buggy drives we need test only one.
 * However, we should also check whether these fields are valid.
 */
static inline int idedisk_supports_hpa(const struct hd_driveid *id)
{
	return (id->command_set_1 & 0x0400) && (id->cfs_enable_1 & 0x0400);
}

/*
 * The same here.
 */
static inline int idedisk_supports_lba48(const struct hd_driveid *id)
{
	return (id->command_set_2 & 0x0400) && (id->cfs_enable_2 & 0x0400)
	       && id->lba_capacity_2;
}

static inline void idedisk_check_hpa(ide_drive_t *drive)
{
	unsigned long long capacity, set_max;
	int lba48 = idedisk_supports_lba48(drive->id);

	capacity = drive->capacity64;
	if (lba48)
		set_max = idedisk_read_native_max_address_ext(drive);
	else
		set_max = idedisk_read_native_max_address(drive);

	if (set_max <= capacity)
		return;

	printk(KERN_INFO "%s: Host Protected Area detected.\n"
			 "\tcurrent capacity is %llu sectors (%llu MB)\n"
			 "\tnative  capacity is %llu sectors (%llu MB)\n",
			 drive->name,
			 capacity, sectors_to_MB(capacity),
			 set_max, sectors_to_MB(set_max));
#ifdef CONFIG_IDEDISK_STROKE
	if (lba48)
		set_max = idedisk_set_max_address_ext(drive, set_max);
	else
		set_max = idedisk_set_max_address(drive, set_max);
	if (set_max) {
		drive->capacity64 = set_max;
		printk(KERN_INFO "%s: Host Protected Area disabled.\n",
				 drive->name);
	}
#endif
}

/*
 * Compute drive->capacity, the full capacity of the drive
 * Called with drive->id != NULL.
 *
 * To compute capacity, this uses either of
 *
 *    1. CHS value set by user       (whatever user sets will be trusted)
 *    2. LBA value from target drive (require new ATA feature)
 *    3. LBA value from system BIOS  (new one is OK, old one may break)
 *    4. CHS value from system BIOS  (traditional style)
 *
 * in above order (i.e., if value of higher priority is available,
 * reset will be ignored).
 */
static void init_idedisk_capacity (ide_drive_t  *drive)
{
	struct hd_driveid *id = drive->id;
	/*
	 * If this drive supports the Host Protected Area feature set,
	 * then we may need to change our opinion about the drive's capacity.
	 */
	int hpa = idedisk_supports_hpa(id);

	if (idedisk_supports_lba48(id)) {
		/* drive speaks 48-bit LBA */
		drive->select.b.lba = 1;
		drive->capacity64 = id->lba_capacity_2;
		if (hpa)
			idedisk_check_hpa(drive);
	} else if ((id->capability & 2) && lba_capacity_is_ok(id)) {
		/* drive speaks 28-bit LBA */
		drive->select.b.lba = 1;
		drive->capacity64 = id->lba_capacity;
		if (hpa)
			idedisk_check_hpa(drive);
	} else {
		/* drive speaks boring old 28-bit CHS */
		drive->capacity64 = drive->cyl * drive->head * drive->sect;
	}
}

static sector_t idedisk_capacity (ide_drive_t *drive)
{
	return drive->capacity64 - drive->sect0;
}

static ide_startstop_t idedisk_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

	if (s->b.set_geometry) {
		s->b.set_geometry	= 0;
		if (!IS_PDC4030_DRIVE) {
			ide_task_t args;
			memset(&args, 0, sizeof(ide_task_t));
			args.tfRegister[IDE_NSECTOR_OFFSET] = drive->sect;
			args.tfRegister[IDE_SECTOR_OFFSET]  = drive->sect;
			args.tfRegister[IDE_LCYL_OFFSET]    = drive->cyl;
			args.tfRegister[IDE_HCYL_OFFSET]    = drive->cyl>>8;
			args.tfRegister[IDE_SELECT_OFFSET]  = ((drive->head-1)|drive->select.all)&0xBF;
			args.tfRegister[IDE_COMMAND_OFFSET] = WIN_SPECIFY;
			args.command_type = IDE_DRIVE_TASK_NO_DATA;
			args.handler	  = &set_geometry_intr;
			do_rw_taskfile(drive, &args);
		}
	} else if (s->b.recalibrate) {
		s->b.recalibrate = 0;
		if (!IS_PDC4030_DRIVE) {
			ide_task_t args;
			memset(&args, 0, sizeof(ide_task_t));
			args.tfRegister[IDE_NSECTOR_OFFSET] = drive->sect;
			args.tfRegister[IDE_COMMAND_OFFSET] = WIN_RESTORE;
			args.command_type = IDE_DRIVE_TASK_NO_DATA;
			args.handler	  = &recal_intr;
			do_rw_taskfile(drive, &args);
		}
	} else if (s->b.set_multmode) {
		s->b.set_multmode = 0;
		if (drive->mult_req > drive->id->max_multsect)
			drive->mult_req = drive->id->max_multsect;
		if (!IS_PDC4030_DRIVE) {
			ide_task_t args;
			memset(&args, 0, sizeof(ide_task_t));
			args.tfRegister[IDE_NSECTOR_OFFSET] = drive->mult_req;
			args.tfRegister[IDE_COMMAND_OFFSET] = WIN_SETMULT;
			args.command_type = IDE_DRIVE_TASK_NO_DATA;
			args.handler	  = &set_multmode_intr;
			do_rw_taskfile(drive, &args);
		}
	} else if (s->all) {
		int special = s->all;
		s->all = 0;
		printk(KERN_ERR "%s: bad special flag: 0x%02x\n", drive->name, special);
		return ide_stopped;
	}
	return IS_PDC4030_DRIVE ? ide_stopped : ide_started;
}

static void idedisk_pre_reset (ide_drive_t *drive)
{
	int legacy = (drive->id->cfs_enable_2 & 0x0400) ? 0 : 1;

	drive->special.all = 0;
	drive->special.b.set_geometry = legacy;
	drive->special.b.recalibrate  = legacy;
	if (OK_TO_RESET_CONTROLLER)
		drive->mult_count = 0;
	if (!drive->keep_settings && !drive->using_dma)
		drive->mult_req = 0;
	if (drive->mult_req != drive->mult_count)
		drive->special.b.set_multmode = 1;
}

#ifdef CONFIG_PROC_FS

static int smart_enable(ide_drive_t *drive)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= SMART_ENABLE;
	args.tfRegister[IDE_LCYL_OFFSET]	= SMART_LCYL_PASS;
	args.tfRegister[IDE_HCYL_OFFSET]	= SMART_HCYL_PASS;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SMART;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	return ide_raw_taskfile(drive, &args, NULL);
}

static int get_smart_values(ide_drive_t *drive, u8 *buf)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= SMART_READ_VALUES;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	args.tfRegister[IDE_LCYL_OFFSET]	= SMART_LCYL_PASS;
	args.tfRegister[IDE_HCYL_OFFSET]	= SMART_HCYL_PASS;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SMART;
	args.command_type			= IDE_DRIVE_TASK_IN;
	args.handler				= &task_in_intr;
	(void) smart_enable(drive);
	return ide_raw_taskfile(drive, &args, buf);
}

static int get_smart_thresholds(ide_drive_t *drive, u8 *buf)
{
	ide_task_t args;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= SMART_READ_THRESHOLDS;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	args.tfRegister[IDE_LCYL_OFFSET]	= SMART_LCYL_PASS;
	args.tfRegister[IDE_HCYL_OFFSET]	= SMART_HCYL_PASS;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SMART;
	args.command_type			= IDE_DRIVE_TASK_IN;
	args.handler				= &task_in_intr;
	(void) smart_enable(drive);
	return ide_raw_taskfile(drive, &args, buf);
}

static int proc_idedisk_read_cache
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	if (drive->id_read)
		len = sprintf(out,"%i\n", drive->id->buf_size / 2);
	else
		len = sprintf(out,"(none)\n");
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_idedisk_read_smart_thresholds
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;

	if (!get_smart_thresholds(drive, page)) {
		unsigned short *val = (unsigned short *) page;
		char *out = ((char *)val) + (SECTOR_WORDS * 4);
		page = out;
		do {
			out += sprintf(out, "%04x%c", le16_to_cpu(*val), (++i & 7) ? ' ' : '\n');
			val += 1;
		} while (i < (SECTOR_WORDS * 2));
		len = out - page;
	}
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static int proc_idedisk_read_smart_values
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *)data;
	int		len = 0, i = 0;

	if (!get_smart_values(drive, page)) {
		unsigned short *val = (unsigned short *) page;
		char *out = ((char *)val) + (SECTOR_WORDS * 4);
		page = out;
		do {
			out += sprintf(out, "%04x%c", le16_to_cpu(*val), (++i & 7) ? ' ' : '\n');
			val += 1;
		} while (i < (SECTOR_WORDS * 2));
		len = out - page;
	}
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static ide_proc_entry_t idedisk_proc[] = {
	{ "cache",		S_IFREG|S_IRUGO,	proc_idedisk_read_cache,		NULL },
	{ "geometry",		S_IFREG|S_IRUGO,	proc_ide_read_geometry,			NULL },
	{ "smart_values",	S_IFREG|S_IRUSR,	proc_idedisk_read_smart_values,		NULL },
	{ "smart_thresholds",	S_IFREG|S_IRUSR,	proc_idedisk_read_smart_thresholds,	NULL },
	{ NULL, 0, NULL, NULL }
};

#else

#define	idedisk_proc	NULL

#endif	/* CONFIG_PROC_FS */

/*
 * This is tightly woven into the driver->do_special can not touch.
 * DON'T do it again until a total personality rewrite is committed.
 */
static int set_multcount(ide_drive_t *drive, int arg)
{
	struct request rq;

	if (drive->special.b.set_multmode)
		return -EBUSY;
	ide_init_drive_cmd (&rq);
	rq.flags = REQ_DRIVE_CMD;
	drive->mult_req = arg;
	drive->special.b.set_multmode = 1;
	(void) ide_do_drive_cmd (drive, &rq, ide_wait);
	return (drive->mult_count == arg) ? 0 : -EIO;
}

static int set_nowerr(ide_drive_t *drive, int arg)
{
	if (ide_spin_wait_hwgroup(drive))
		return -EBUSY;
	drive->nowerr = arg;
	drive->bad_wstat = arg ? BAD_R_STAT : BAD_W_STAT;
	spin_unlock_irq(&ide_lock);
	return 0;
}

/* check if CACHE FLUSH (EXT) command is supported (bits defined in ATA-6) */
#define ide_id_has_flush_cache(id)	((id)->cfs_enable_2 & 0x3000)

/* some Maxtor disks have bit 13 defined incorrectly so check bit 10 too */
#define ide_id_has_flush_cache_ext(id)	\
	(((id)->cfs_enable_2 & 0x2400) == 0x2400)

static int write_cache (ide_drive_t *drive, int arg)
{
	ide_task_t args;

	if (!ide_id_has_flush_cache(drive->id))
		return 1;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= (arg) ?
			SETFEATURES_EN_WCACHE : SETFEATURES_DIS_WCACHE;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SETFEATURES;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	(void) ide_raw_taskfile(drive, &args, NULL);

	drive->wcache = arg;
	return 0;
}

static int do_idedisk_flushcache (ide_drive_t *drive)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	if (ide_id_has_flush_cache_ext(drive->id))
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_FLUSH_CACHE_EXT;
	else
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_FLUSH_CACHE;
	args.command_type			= IDE_DRIVE_TASK_NO_DATA;
	args.handler				= &task_no_data_intr;
	return ide_raw_taskfile(drive, &args, NULL);
}

static int set_acoustic (ide_drive_t *drive, int arg)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= (arg) ? SETFEATURES_EN_AAM :
							  SETFEATURES_DIS_AAM;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= arg;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SETFEATURES;
	args.command_type = IDE_DRIVE_TASK_NO_DATA;
	args.handler	  = &task_no_data_intr;
	ide_raw_taskfile(drive, &args, NULL);
	drive->acoustic = arg;
	return 0;
}

#ifdef CONFIG_BLK_DEV_IDE_TCQ
static int set_using_tcq(ide_drive_t *drive, int arg)
{
	int ret;

	if (!drive->driver)
		return -EPERM;
	if (arg == drive->queue_depth && drive->using_tcq)
		return 0;

	/*
	 * set depth, but check also id for max supported depth
	 */
	drive->queue_depth = arg ? arg : 1;
	if (drive->id) {
		if (drive->queue_depth > drive->id->queue_depth + 1)
			drive->queue_depth = drive->id->queue_depth + 1;
	}

	if (arg)
		ret = __ide_dma_queued_on(drive);
	else
		ret = __ide_dma_queued_off(drive);

	return ret ? -EIO : 0;
}
#endif

/*
 * drive->addressing:
 *	0: 28-bit
 *	1: 48-bit
 *	2: 48-bit capable doing 28-bit
 */
static int set_lba_addressing(ide_drive_t *drive, int arg)
{
	drive->addressing =  0;

	if (HWIF(drive)->no_lba48)
		return 0;

	if (!idedisk_supports_lba48(drive->id))
                return -EIO;
	drive->addressing = arg;
	return 0;
}

static void idedisk_add_settings(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;

	ide_add_setting(drive,	"bios_cyl",		SETTING_RW,					-1,			-1,			TYPE_INT,	0,	65535,				1,	1,	&drive->bios_cyl,		NULL);
	ide_add_setting(drive,	"bios_head",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	255,				1,	1,	&drive->bios_head,		NULL);
	ide_add_setting(drive,	"bios_sect",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	63,				1,	1,	&drive->bios_sect,		NULL);
	ide_add_setting(drive,	"address",		SETTING_RW,					HDIO_GET_ADDRESS,	HDIO_SET_ADDRESS,	TYPE_INTA,	0,	2,				1,	1,	&drive->addressing,	set_lba_addressing);
	ide_add_setting(drive,	"bswap",		SETTING_READ,					-1,			-1,			TYPE_BYTE,	0,	1,				1,	1,	&drive->bswap,			NULL);
	ide_add_setting(drive,	"multcount",		id ? SETTING_RW : SETTING_READ,			HDIO_GET_MULTCOUNT,	HDIO_SET_MULTCOUNT,	TYPE_BYTE,	0,	id ? id->max_multsect : 0,	1,	1,	&drive->mult_count,		set_multcount);
	ide_add_setting(drive,	"nowerr",		SETTING_RW,					HDIO_GET_NOWERR,	HDIO_SET_NOWERR,	TYPE_BYTE,	0,	1,				1,	1,	&drive->nowerr,			set_nowerr);
	ide_add_setting(drive,	"lun",			SETTING_RW,					-1,			-1,			TYPE_INT,	0,	7,				1,	1,	&drive->lun,			NULL);
	ide_add_setting(drive,	"wcache",		SETTING_RW,					HDIO_GET_WCACHE,	HDIO_SET_WCACHE,	TYPE_BYTE,	0,	1,				1,	1,	&drive->wcache,			write_cache);
	ide_add_setting(drive,	"acoustic",		SETTING_RW,					HDIO_GET_ACOUSTIC,	HDIO_SET_ACOUSTIC,	TYPE_BYTE,	0,	254,				1,	1,	&drive->acoustic,		set_acoustic);
 	ide_add_setting(drive,	"failures",		SETTING_RW,					-1,			-1,			TYPE_INT,	0,	65535,				1,	1,	&drive->failures,		NULL);
 	ide_add_setting(drive,	"max_failures",		SETTING_RW,					-1,			-1,			TYPE_INT,	0,	65535,				1,	1,	&drive->max_failures,		NULL);
#ifdef CONFIG_BLK_DEV_IDE_TCQ
	ide_add_setting(drive,	"using_tcq",		SETTING_RW,					HDIO_GET_QDMA,		HDIO_SET_QDMA,		TYPE_BYTE,	0,	IDE_MAX_TAG,			1,		1,		&drive->using_tcq,		set_using_tcq);
#endif
}

/*
 * Power Management state machine. This one is rather trivial for now,
 * we should probably add more, like switching back to PIO on suspend
 * to help some BIOSes, re-do the door locking on resume, etc...
 */

enum {
	idedisk_pm_flush_cache	= ide_pm_state_start_suspend,
	idedisk_pm_standby,

	idedisk_pm_restore_dma	= ide_pm_state_start_resume,
};

static void idedisk_complete_power_step (ide_drive_t *drive, struct request *rq, u8 stat, u8 error)
{
	switch (rq->pm->pm_step) {
	case idedisk_pm_flush_cache:	/* Suspend step 1 (flush cache) complete */
		if (rq->pm->pm_state == 4)
			rq->pm->pm_step = ide_pm_state_completed;
		else
			rq->pm->pm_step = idedisk_pm_standby;
		break;
	case idedisk_pm_standby:	/* Suspend step 2 (standby) complete */
		rq->pm->pm_step = ide_pm_state_completed;
		break;
	}
}

static ide_startstop_t idedisk_start_power_step (ide_drive_t *drive, struct request *rq)
{
	ide_task_t *args = rq->special;

	memset(args, 0, sizeof(*args));

	switch (rq->pm->pm_step) {
	case idedisk_pm_flush_cache:	/* Suspend step 1 (flush cache) */
		/* Not supported? Switch to next step now. */
		if (!drive->wcache || !ide_id_has_flush_cache(drive->id)) {
			idedisk_complete_power_step(drive, rq, 0, 0);
			return ide_stopped;
		}
		if (ide_id_has_flush_cache_ext(drive->id))
			args->tfRegister[IDE_COMMAND_OFFSET] = WIN_FLUSH_CACHE_EXT;
		else
			args->tfRegister[IDE_COMMAND_OFFSET] = WIN_FLUSH_CACHE;
		args->command_type = IDE_DRIVE_TASK_NO_DATA;
		args->handler	   = &task_no_data_intr;
		return do_rw_taskfile(drive, args);
	case idedisk_pm_standby:	/* Suspend step 2 (standby) */
		args->tfRegister[IDE_COMMAND_OFFSET] = WIN_STANDBYNOW1;
		args->command_type = IDE_DRIVE_TASK_NO_DATA;
		args->handler	   = &task_no_data_intr;
		return do_rw_taskfile(drive, args);

	case idedisk_pm_restore_dma:	/* Resume step 1 (restore DMA) */
		/*
		 * Right now, all we do is call hwif->ide_dma_check(drive),
		 * we could be smarter and check for current xfer_speed
		 * in struct drive etc...
		 * Also, this step could be implemented as a generic helper
		 * as most subdrivers will use it
		 */
		if ((drive->id->capability & 1) == 0)
			break;
		if (HWIF(drive)->ide_dma_check == NULL)
			break;
		HWIF(drive)->ide_dma_check(drive);
		break;
	}
	rq->pm->pm_step = ide_pm_state_completed;
	return ide_stopped;
}

static void idedisk_setup (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	unsigned long long capacity;

	idedisk_add_settings(drive);

	if (drive->id_read == 0)
		return;

	/*
	 * CompactFlash cards and their brethern look just like hard drives
	 * to us, but they are removable and don't have a doorlock mechanism.
	 */
	if (drive->removable && !(drive->is_flash)) {
		/*
		 * Removable disks (eg. SYQUEST); ignore 'WD' drives 
		 */
		if (id->model[0] != 'W' || id->model[1] != 'D') {
			drive->doorlocking = 1;
		}
	}

	(void)set_lba_addressing(drive, 1);

	if (drive->addressing == 1) {
		ide_hwif_t *hwif = HWIF(drive);
		int max_s = 2048;

		if (max_s > hwif->rqsize)
			max_s = hwif->rqsize;

		blk_queue_max_sectors(drive->queue, max_s);
	}

	printk("%s: max request size: %dKiB\n", drive->name, drive->queue->max_sectors / 2);

	/* Extract geometry if we did not already have one for the drive */
	if (!drive->cyl || !drive->head || !drive->sect) {
		drive->cyl     = drive->bios_cyl  = id->cyls;
		drive->head    = drive->bios_head = id->heads;
		drive->sect    = drive->bios_sect = id->sectors;
	}

	/* Handle logical geometry translation by the drive */
	if ((id->field_valid & 1) && id->cur_cyls &&
	    id->cur_heads && (id->cur_heads <= 16) && id->cur_sectors) {
		drive->cyl  = id->cur_cyls;
		drive->head = id->cur_heads;
		drive->sect = id->cur_sectors;
	}

	/* Use physical geometry if what we have still makes no sense */
	if (drive->head > 16 && id->heads && id->heads <= 16) {
		drive->cyl  = id->cyls;
		drive->head = id->heads;
		drive->sect = id->sectors;
	}

	/* calculate drive capacity, and select LBA if possible */
	init_idedisk_capacity (drive);

	/* limit drive capacity to 137GB if LBA48 cannot be used */
	if (drive->addressing == 0 && drive->capacity64 > 1ULL << 28) {
		printk("%s: cannot use LBA48 - full capacity "
		       "%llu sectors (%llu MB)\n",
		       drive->name, (unsigned long long)drive->capacity64,
		       sectors_to_MB(drive->capacity64));
		drive->capacity64 = 1ULL << 28;
	}

	/*
	 * if possible, give fdisk access to more of the drive,
	 * by correcting bios_cyls:
	 */
	capacity = idedisk_capacity (drive);
	if (!drive->forced_geom) {

		if (idedisk_supports_lba48(drive->id)) {
			/* compatibility */
			drive->bios_sect = 63;
			drive->bios_head = 255;
		}

		if (drive->bios_sect && drive->bios_head) {
			unsigned int cap0 = capacity; /* truncate to 32 bits */
			unsigned int cylsz, cyl;

			if (cap0 != capacity)
				drive->bios_cyl = 65535;
			else {
				cylsz = drive->bios_sect * drive->bios_head;
				cyl = cap0 / cylsz;
				if (cyl > 65535)
					cyl = 65535;
				if (cyl > drive->bios_cyl)
					drive->bios_cyl = cyl;
			}
		}
	}
	printk(KERN_INFO "%s: %llu sectors (%llu MB)",
			 drive->name, capacity, sectors_to_MB(capacity));

	/* Only print cache size when it was specified */
	if (id->buf_size)
		printk (" w/%dKiB Cache", id->buf_size/2);

	printk(", CHS=%d/%d/%d", 
	       drive->bios_cyl, drive->bios_head, drive->bios_sect);
	if (drive->using_dma)
		(void) HWIF(drive)->ide_dma_verbose(drive);
	printk("\n");

	drive->mult_count = 0;
	if (id->max_multsect) {
#ifdef CONFIG_IDEDISK_MULTI_MODE
		id->multsect = ((id->max_multsect/2) > 1) ? id->max_multsect : 0;
		id->multsect_valid = id->multsect ? 1 : 0;
		drive->mult_req = id->multsect_valid ? id->max_multsect : INITIAL_MULT_COUNT;
		drive->special.b.set_multmode = drive->mult_req ? 1 : 0;
#else	/* original, pre IDE-NFG, per request of AC */
		drive->mult_req = INITIAL_MULT_COUNT;
		if (drive->mult_req > id->max_multsect)
			drive->mult_req = id->max_multsect;
		if (drive->mult_req || ((id->multsect_valid & 1) && id->multsect))
			drive->special.b.set_multmode = 1;
#endif	/* CONFIG_IDEDISK_MULTI_MODE */
	}
	drive->no_io_32bit = id->dword_io ? 1 : 0;

	/* write cache enabled? */
	if ((id->csfo & 1) || (id->cfs_enable_1 & (1 << 5)))
		drive->wcache = 1;

	write_cache(drive, 1);

#ifdef CONFIG_BLK_DEV_IDE_TCQ_DEFAULT
	if (drive->using_dma)
		__ide_dma_queued_on(drive);
#endif
}

static void ide_cacheflush_p(ide_drive_t *drive)
{
	if (!drive->wcache || !ide_id_has_flush_cache(drive->id))
		return;

	if (do_idedisk_flushcache(drive))
		printk(KERN_INFO "%s: wcache flush failed!\n", drive->name);
}

static int idedisk_cleanup (ide_drive_t *drive)
{
	struct gendisk *g = drive->disk;
	ide_cacheflush_p(drive);
	if (ide_unregister_subdriver(drive))
		return 1;
	del_gendisk(g);
	drive->devfs_name[0] = '\0';
	g->fops = ide_fops;
	return 0;
}

static int idedisk_attach(ide_drive_t *drive);

static void ide_device_shutdown(struct device *dev)
{
	ide_drive_t *drive = container_of(dev, ide_drive_t, gendev);

	if (system_state == SYSTEM_RESTART) {
		ide_cacheflush_p(drive);
		return;
	}

	printk("Shutdown: %s\n", drive->name);
	dev->bus->suspend(dev, PM_SUSPEND_STANDBY);
}

/*
 *      IDE subdriver functions, registered with ide.c
 */
static ide_driver_t idedisk_driver = {
	.owner			= THIS_MODULE,
	.gen_driver = {
		.shutdown	= ide_device_shutdown,
	},
	.name			= "ide-disk",
	.version		= IDEDISK_VERSION,
	.media			= ide_disk,
	.busy			= 0,
	.supports_dsc_overlap	= 0,
	.cleanup		= idedisk_cleanup,
	.do_request		= ide_do_rw_disk,
	.sense			= idedisk_dump_status,
	.error			= idedisk_error,
	.abort			= idedisk_abort,
	.pre_reset		= idedisk_pre_reset,
	.capacity		= idedisk_capacity,
	.special		= idedisk_special,
	.proc			= idedisk_proc,
	.attach			= idedisk_attach,
	.drives			= LIST_HEAD_INIT(idedisk_driver.drives),
	.start_power_step	= idedisk_start_power_step,
	.complete_power_step	= idedisk_complete_power_step,
};

static int idedisk_open(struct inode *inode, struct file *filp)
{
	ide_drive_t *drive = inode->i_bdev->bd_disk->private_data;
	drive->usage++;
	if (drive->removable && drive->usage == 1) {
		ide_task_t args;
		memset(&args, 0, sizeof(ide_task_t));
		args.tfRegister[IDE_COMMAND_OFFSET] = WIN_DOORLOCK;
		args.command_type = IDE_DRIVE_TASK_NO_DATA;
		args.handler	  = &task_no_data_intr;
		check_disk_change(inode->i_bdev);
		/*
		 * Ignore the return code from door_lock,
		 * since the open() has already succeeded,
		 * and the door_lock is irrelevant at this point.
		 */
		if (drive->doorlocking && ide_raw_taskfile(drive, &args, NULL))
			drive->doorlocking = 0;
	}
	return 0;
}

static int idedisk_release(struct inode *inode, struct file *filp)
{
	ide_drive_t *drive = inode->i_bdev->bd_disk->private_data;
	if (drive->usage == 1)
		ide_cacheflush_p(drive);
	if (drive->removable && drive->usage == 1) {
		ide_task_t args;
		memset(&args, 0, sizeof(ide_task_t));
		args.tfRegister[IDE_COMMAND_OFFSET] = WIN_DOORUNLOCK;
		args.command_type = IDE_DRIVE_TASK_NO_DATA;
		args.handler	  = &task_no_data_intr;
		if (drive->doorlocking && ide_raw_taskfile(drive, &args, NULL))
			drive->doorlocking = 0;
	}
	drive->usage--;
	return 0;
}

static int idedisk_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	return generic_ide_ioctl(bdev, cmd, arg);
}

static int idedisk_media_changed(struct gendisk *disk)
{
	ide_drive_t *drive = disk->private_data;

	/* do not scan partitions twice if this is a removable device */
	if (drive->attach) {
		drive->attach = 0;
		return 0;
	}
	/* if removable, always assume it was changed */
	return drive->removable;
}

static int idedisk_revalidate_disk(struct gendisk *disk)
{
	ide_drive_t *drive = disk->private_data;
	set_capacity(disk, current_capacity(drive));
	return 0;
}

static struct block_device_operations idedisk_ops = {
	.owner		= THIS_MODULE,
	.open		= idedisk_open,
	.release	= idedisk_release,
	.ioctl		= idedisk_ioctl,
	.media_changed	= idedisk_media_changed,
	.revalidate_disk= idedisk_revalidate_disk
};

MODULE_DESCRIPTION("ATA DISK Driver");

static int idedisk_attach(ide_drive_t *drive)
{
	struct gendisk *g = drive->disk;

	/* strstr("foo", "") is non-NULL */
	if (!strstr("ide-disk", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_disk)
		goto failed;

	if (ide_register_subdriver(drive, &idedisk_driver)) {
		printk (KERN_ERR "ide-disk: %s: Failed to register the driver with ide.c\n", drive->name);
		goto failed;
	}
	DRIVER(drive)->busy++;
	idedisk_setup(drive);
	if ((!drive->head || drive->head > 16) && !drive->select.b.lba) {
		printk(KERN_ERR "%s: INVALID GEOMETRY: %d PHYSICAL HEADS?\n",
			drive->name, drive->head);
		ide_cacheflush_p(drive);
		ide_unregister_subdriver(drive);
		DRIVER(drive)->busy--;
		goto failed;
	}
	DRIVER(drive)->busy--;
	g->minors = 1 << PARTN_BITS;
	strcpy(g->devfs_name, drive->devfs_name);
	g->driverfs_dev = &drive->gendev;
	g->flags = drive->removable ? GENHD_FL_REMOVABLE : 0;
	set_capacity(g, current_capacity(drive));
	g->fops = &idedisk_ops;
	drive->attach = 1;
	add_disk(g);
	return 0;
failed:
	return 1;
}

static void __exit idedisk_exit (void)
{
	ide_unregister_driver(&idedisk_driver);
}

static int idedisk_init (void)
{
	return ide_register_driver(&idedisk_driver);
}

module_init(idedisk_init);
module_exit(idedisk_exit);
MODULE_LICENSE("GPL");
