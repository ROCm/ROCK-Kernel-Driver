/*
 *  linux/drivers/ide/ide-disk.c	Version 1.16	April 7, 2002
 *
 *  Copyright (C) 1998-2002  Linux ATA Developemt
 *				Andre Hedrick <andre@linux-ide.org>
 *
 *
 *  Copyright (C) 1994-1998  Linus Torvalds & authors (see below)
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
 */

#define IDEDISK_VERSION	"1.16"

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
#include <linux/ide.h>
#include <linux/suspend.h>
#include <linux/buffer_head.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#ifdef CONFIG_BLK_DEV_PDC4030
#define IS_PDC4030_DRIVE (HWIF(drive)->chipset == ide_pdc4030)
#else
#define IS_PDC4030_DRIVE (0)	/* auto-NULLs out pdc4030 code */
#endif

static int driver_blocked;

static inline u32 idedisk_read_24 (ide_drive_t *drive)
{
	return	(IN_BYTE(IDE_HCYL_REG)<<16) |
		(IN_BYTE(IDE_LCYL_REG)<<8) |
		 IN_BYTE(IDE_SECTOR_REG);
}

static int idedisk_end_request(ide_drive_t *drive, int uptodate);

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

	if ((id->command_set_2 & 0x0400) && (id->cfs_enable_2 & 0x0400)) {
		printk("48-bit Drive: %llu \n", id->lba_capacity_2);
		return 1;
	}

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
	    id->lba_capacity >= 16383*63*id->heads)
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

#ifndef CONFIG_IDE_TASKFILE_IO

/*
 * read_intr() is the handler for disk read/multread interrupts
 */
static ide_startstop_t read_intr (ide_drive_t *drive)
{
	byte stat;
	int i;
	unsigned int msect, nsect;
	struct request *rq;
	unsigned long flags;
	char *to;

	/* new way for dealing with premature shared PCI interrupts */
	if (!OK_STAT(stat=GET_STAT(),DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			return DRIVER(drive)->error(drive, "read_intr", stat);
		}
		/* no data yet, so wait for another interrupt */
		if (HWGROUP(drive)->handler != NULL)
			BUG();
		ide_set_handler(drive, &read_intr, WAIT_CMD, NULL);
		return ide_started;
	}
	msect = drive->mult_count;
	
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
	ide_unmap_buffer(to, &flags);
	rq->sector += nsect;
	rq->errors = 0;
	i = (rq->nr_sectors -= nsect);
	if (((long)(rq->current_nr_sectors -= nsect)) <= 0)
		idedisk_end_request(drive, 1);
	/*
	 * Another BH Page walker and DATA INTERGRITY Questioned on ERROR.
	 * If passed back up on multimode read, BAD DATA could be ACKED
	 * to FILE SYSTEMS above ...
	 */
	if (i > 0) {
		if (msect)
			goto read_next;
		if (HWGROUP(drive)->handler != NULL)	/* paranoia check */
			BUG();
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
	byte stat;
	int i;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	struct request *rq = hwgroup->rq;

	if (!OK_STAT(stat=GET_STAT(),DRIVE_READY,drive->bad_wstat)) {
		printk("%s: write_intr error1: nr_sectors=%ld, stat=0x%02x\n", drive->name, rq->nr_sectors, stat);
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
				idedisk_end_request(drive, 1);
			if (i > 0) {
				unsigned long flags;
				char *to = ide_map_buffer(rq, &flags);
				taskfile_output_data(drive, to, SECTOR_WORDS);
				ide_unmap_buffer(to, &flags);
				if (HWGROUP(drive)->handler != NULL)
					BUG();
				ide_set_handler(drive, &write_intr, WAIT_CMD, NULL);
                                return ide_started;
			}
                        return ide_stopped;
		}
		return ide_stopped;	/* the original code did this here (?) */
	}
	return DRIVER(drive)->error(drive, "write_intr", stat);
}

/*
 * ide_multwrite() transfers a block of up to mcount sectors of data
 * to a drive as part of a disk multiple-sector write operation.
 *
 * Returns 0 on success.
 *
 * Note that we may be called from two contexts - the do_rw_disk context
 * and IRQ context. The IRQ can happen any time after we've output the
 * full "mcount" number of sectors, so we must make sure we update the
 * state _before_ we output the final part of the data!
 *
 * The update and return to BH is a BLOCK Layer Fakey to get more data
 * to satisfy the hardware atomic segment.  If the hardware atomic segment
 * is shorter or smaller than the BH segment then we should be OKAY.
 * This is only valid if we can rewind the rq->current_nr_sectors counter.
 */
int ide_multwrite (ide_drive_t *drive, unsigned int mcount)
{
 	ide_hwgroup_t	*hwgroup= HWGROUP(drive);
 	struct request	*rq = &hwgroup->wrq;
 
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
				bio->bi_idx = 0;
				bio = bio->bi_next;
			}

			/* end early early we ran out of requests */
			if (!bio) {
				mcount = 0;
			} else {
				rq->bio = bio;
				rq->current_nr_sectors = bio_iovec(bio)->bv_len >> 9;
				rq->hard_cur_sectors = rq->current_nr_sectors;
			}
		}

		/*
		 * Ok, we're all setup for the interrupt
		 * re-entering us on the last transfer.
		 */
		taskfile_output_data(drive, buffer, nsect<<7);
		ide_unmap_buffer(buffer, &flags);
	} while (mcount);

        return 0;
}

/*
 * multwrite_intr() is the handler for disk multwrite interrupts
 */
static ide_startstop_t multwrite_intr (ide_drive_t *drive)
{
	byte stat;
	int i;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	struct request *rq = &hwgroup->wrq;

	if (OK_STAT(stat=GET_STAT(),DRIVE_READY,drive->bad_wstat)) {
		if (stat & DRQ_STAT) {
			/*
			 *	The drive wants data. Remember rq is the copy
			 *	of the request
			 */
			if (rq->nr_sectors) {
				if (ide_multwrite(drive, drive->mult_count))
					return ide_stopped;
				if (HWGROUP(drive)->handler != NULL)
					BUG();
				ide_set_handler(drive, &multwrite_intr, WAIT_CMD, NULL);
				return ide_started;
			}
		} else {
			/*
			 *	If the copy has all the blocks completed then
			 *	we can end the original request.
			 */
			if (!rq->nr_sectors) {	/* all done? */
				rq = hwgroup->rq;
				for (i = rq->nr_sectors; i > 0;){
					i -= rq->current_nr_sectors;
					idedisk_end_request(drive, 1);
				}
				return ide_stopped;
			}
		}
		return ide_stopped;	/* the original code did this here (?) */
	}
	return DRIVER(drive)->error(drive, "multwrite_intr", stat);
}
#endif /* CONFIG_IDE_TASKFILE_IO */

#ifdef CONFIG_IDE_TASKFILE_IO

static ide_startstop_t chs_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block);
static ide_startstop_t lba_28_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block);
static ide_startstop_t lba_48_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long long block);

/*
 * do_rw_disk() issues READ and WRITE commands to a disk,
 * using LBA if supported, or CHS otherwise, to address sectors.
 * It also takes care of issuing special DRIVE_CMDs.
 */
static ide_startstop_t do_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	if (!(rq->flags & REQ_CMD)) {
		blk_dump_rq_flags(rq, "do_rw_disk - bad command");
		idedisk_end_request(drive, 0);
		return ide_stopped;
	}

#ifdef CONFIG_BLK_DEV_PDC4030
	if (IS_PDC4030_DRIVE) {
		extern ide_startstop_t promise_rw_disk(ide_drive_t *, struct request *, unsigned long);
		return promise_rw_disk(drive, rq, block);
	}
#endif /* CONFIG_BLK_DEV_PDC4030 */

	if ((drive->id->cfs_enable_2 & 0x0400) &&
	    (drive->addressing == 1))		/* 48-bit LBA */
		return lba_48_rw_disk(drive, rq, (unsigned long long) block);
	if (drive->select.b.lba)		/* 28-bit LBA */
		return lba_28_rw_disk(drive, rq, (unsigned long) block);

	/* 28-bit CHS : DIE DIE DIE piece of legacy crap!!! */
	return chs_rw_disk(drive, rq, (unsigned long) block);
}

static task_ioreg_t get_command (ide_drive_t *drive, int cmd)
{
	int lba48bit = (drive->id->cfs_enable_2 & 0x0400) ? 1 : 0;

#if 1
	lba48bit = (drive->addressing == 1) ? 1 : 0;
#endif

	if ((cmd == READ) && (drive->using_dma))
		return (lba48bit) ? WIN_READDMA_EXT : WIN_READDMA;
	else if ((cmd == READ) && (drive->mult_count))
		return (lba48bit) ? WIN_MULTREAD_EXT : WIN_MULTREAD;
	else if (cmd == READ)
		return (lba48bit) ? WIN_READ_EXT : WIN_READ;
	else if ((cmd == WRITE) && (drive->using_dma))
		return (lba48bit) ? WIN_WRITEDMA_EXT : WIN_WRITEDMA;
	else if ((cmd == WRITE) && (drive->mult_count))
		return (lba48bit) ? WIN_MULTWRITE_EXT : WIN_MULTWRITE;
	else if (cmd == WRITE)
		return (lba48bit) ? WIN_WRITE_EXT : WIN_WRITE;
	else
		return WIN_NOP;
}

static ide_startstop_t chs_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	ide_task_t		args;
	int			sectors;
	task_ioreg_t command	= get_command(drive, rq_data_dir(rq));
	unsigned int track	= (block / drive->sect);
	unsigned int sect	= (block % drive->sect) + 1;
	unsigned int head	= (track % drive->head);
	unsigned int cyl	= (track / drive->head);

#ifdef DEBUG
	printk("%s: %sing: ", drive->name, (rq_data_dir(rq)==READ) ? "read" : "writ");
	printk("CHS=%d/%d/%d, ", cyl, head, sect);
	printk("sectors=%ld, ", rq->nr_sectors);
	printk("buffer=0x%08lx\n", (unsigned long) rq->buffer);
#endif

	memset(&args, 0, sizeof(ide_task_t));

	sectors	= (rq->nr_sectors == 256) ? 0x00 : rq->nr_sectors;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= sectors;
	args.tfRegister[IDE_SECTOR_OFFSET]	= sect;
	args.tfRegister[IDE_LCYL_OFFSET]	= cyl;
	args.tfRegister[IDE_HCYL_OFFSET]	= (cyl>>8);
	args.tfRegister[IDE_SELECT_OFFSET]	= head;
	args.tfRegister[IDE_SELECT_OFFSET]	|= drive->select.all;
	args.tfRegister[IDE_COMMAND_OFFSET]	= command;
	args.command_type			= ide_cmd_type_parser(&args);
	args.rq					= (struct request *) rq;
	rq->special				= (ide_task_t *)&args;
	return do_rw_taskfile(drive, &args);
}

static ide_startstop_t lba_28_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	ide_task_t		args;
	int			sectors;
	task_ioreg_t command	= get_command(drive, rq_data_dir(rq));

#ifdef DEBUG
	printk("%s: %sing: ", drive->name, (rq_data_dir(rq)==READ) ? "read" : "writ");
	printk("LBAsect=%lld, ", block);
	printk("sectors=%ld, ", rq->nr_sectors);
	printk("buffer=0x%08lx\n", (unsigned long) rq->buffer);
#endif

	memset(&args, 0, sizeof(ide_task_t));

	sectors = (rq->nr_sectors == 256) ? 0x00 : rq->nr_sectors;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= sectors;
	args.tfRegister[IDE_SECTOR_OFFSET]	= block;
	args.tfRegister[IDE_LCYL_OFFSET]	= (block>>=8);
	args.tfRegister[IDE_HCYL_OFFSET]	= (block>>=8);
	args.tfRegister[IDE_SELECT_OFFSET]	= ((block>>8)&0x0f);
	args.tfRegister[IDE_SELECT_OFFSET]	|= drive->select.all;
	args.tfRegister[IDE_COMMAND_OFFSET]	= command;
	args.command_type			= ide_cmd_type_parser(&args);
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
	task_ioreg_t command	= get_command(drive, rq_data_dir(rq));

#ifdef DEBUG
	printk("%s: %sing: ", drive->name, (rq_data_dir(rq)==READ) ? "read" : "writ");
	printk("LBAsect=%lld, ", block);
	printk("sectors=%ld, ", rq->nr_sectors);
	printk("buffer=0x%08lx\n", (unsigned long) rq->buffer);
#endif

	memset(&args, 0, sizeof(ide_task_t));

	sectors = (rq->nr_sectors == 65536) ? 0 : rq->nr_sectors;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= sectors;
	args.tfRegister[IDE_SECTOR_OFFSET]	= block;	/* low lba */
	args.tfRegister[IDE_LCYL_OFFSET]	= (block>>=8);	/* mid lba */
	args.tfRegister[IDE_HCYL_OFFSET]	= (block>>=8);	/* hi  lba */
	args.tfRegister[IDE_SELECT_OFFSET]	= drive->select.all;
	args.tfRegister[IDE_COMMAND_OFFSET]	= command;
	args.hobRegister[IDE_NSECTOR_OFFSET_HOB]= sectors >> 8;
	args.hobRegister[IDE_SECTOR_OFFSET_HOB]	= (block>>=8);	/* low lba */
	args.hobRegister[IDE_LCYL_OFFSET_HOB]	= (block>>=8);	/* mid lba */
	args.hobRegister[IDE_HCYL_OFFSET_HOB]	= (block>>=8);	/* hi  lba */
	args.hobRegister[IDE_SELECT_OFFSET_HOB]	= drive->select.all;
	args.hobRegister[IDE_CONTROL_OFFSET_HOB]= (drive->ctl|0x80);
	args.command_type			= ide_cmd_type_parser(&args);
	args.rq					= (struct request *) rq;
	rq->special				= (ide_task_t *)&args;
	return do_rw_taskfile(drive, &args);
}

#else /* !CONFIG_IDE_TASKFILE_IO */

/*
 * do_rw_disk() issues READ and WRITE commands to a disk,
 * using LBA if supported, or CHS otherwise, to address sectors.
 * It also takes care of issuing special DRIVE_CMDs.
 */
static ide_startstop_t do_rw_disk (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	if (!(rq->flags & REQ_CMD)) {
		blk_dump_rq_flags(rq, "do_rw_disk - bad command");
		return ide_stopped;
	}

	if (driver_blocked)
		panic("Request while ide driver is blocked?");
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl,IDE_CONTROL_REG);

#ifdef CONFIG_BLK_DEV_PDC4030
	if (drive->select.b.lba || IS_PDC4030_DRIVE) {
#else /* !CONFIG_BLK_DEV_PDC4030 */
	if (drive->select.b.lba) {
#endif /* CONFIG_BLK_DEV_PDC4030 */

		if ((drive->id->cfs_enable_2 & 0x0400) &&
		    (drive->addressing == 1)) {
			task_ioreg_t tasklets[10];

			tasklets[0] = 0;
			tasklets[1] = 0;
			tasklets[2] = rq->nr_sectors;
			tasklets[3] = (rq->nr_sectors>>8);
			if (rq->nr_sectors == 65536) {
				tasklets[2] = 0x00;
				tasklets[3] = 0x00;
			}
			tasklets[4] = (task_ioreg_t) block;
			tasklets[5] = (task_ioreg_t) (block>>8);
			tasklets[6] = (task_ioreg_t) (block>>16);
			tasklets[7] = (task_ioreg_t) (block>>24);
			tasklets[8] = (task_ioreg_t) 0;
			tasklets[9] = (task_ioreg_t) 0;
//			tasklets[8] = (task_ioreg_t) (block>>32);
//			tasklets[9] = (task_ioreg_t) (block>>40);
#ifdef DEBUG
			printk("%s: %sing: LBAsect=%lu, sectors=%ld, buffer=0x%08lx, LBAsect=0x%012lx\n",
				drive->name,
				(rq_data_dir(rq)==READ)?"read":"writ",
				block,
				rq->nr_sectors,
				(unsigned long) rq->buffer,
				block);
			printk("%s: 0x%02x%02x 0x%02x%02x%02x%02x%02x%02x\n",
				drive->name, tasklets[3], tasklets[2],
				tasklets[9], tasklets[8], tasklets[7],
				tasklets[6], tasklets[5], tasklets[4]);
#endif
			OUT_BYTE(tasklets[1], IDE_FEATURE_REG);
			OUT_BYTE(tasklets[3], IDE_NSECTOR_REG);
			OUT_BYTE(tasklets[7], IDE_SECTOR_REG);
			OUT_BYTE(tasklets[8], IDE_LCYL_REG);
			OUT_BYTE(tasklets[9], IDE_HCYL_REG);

			OUT_BYTE(tasklets[0], IDE_FEATURE_REG);
			OUT_BYTE(tasklets[2], IDE_NSECTOR_REG);
			OUT_BYTE(tasklets[4], IDE_SECTOR_REG);
			OUT_BYTE(tasklets[5], IDE_LCYL_REG);
			OUT_BYTE(tasklets[6], IDE_HCYL_REG);
			OUT_BYTE(0x00|drive->select.all,IDE_SELECT_REG);
		} else {
#ifdef DEBUG
			printk("%s: %sing: LBAsect=%ld, sectors=%ld, buffer=0x%08lx\n",
				drive->name, (rq_data_dir(rq)==READ)?"read":"writ",
				block, rq->nr_sectors, (unsigned long) rq->buffer);
#endif
			OUT_BYTE(0x00, IDE_FEATURE_REG);
			OUT_BYTE((rq->nr_sectors==256)?0x00:rq->nr_sectors,IDE_NSECTOR_REG);
			OUT_BYTE(block,IDE_SECTOR_REG);
			OUT_BYTE(block>>=8,IDE_LCYL_REG);
			OUT_BYTE(block>>=8,IDE_HCYL_REG);
			OUT_BYTE(((block>>8)&0x0f)|drive->select.all,IDE_SELECT_REG);
		}
	} else {
		unsigned int sect,head,cyl,track;
		track = block / drive->sect;
		sect  = block % drive->sect + 1;
		OUT_BYTE(sect,IDE_SECTOR_REG);
		head  = track % drive->head;
		cyl   = track / drive->head;

		OUT_BYTE(0x00, IDE_FEATURE_REG);
		OUT_BYTE((rq->nr_sectors==256)?0x00:rq->nr_sectors,IDE_NSECTOR_REG);
		OUT_BYTE(cyl,IDE_LCYL_REG);
		OUT_BYTE(cyl>>8,IDE_HCYL_REG);
		OUT_BYTE(head|drive->select.all,IDE_SELECT_REG);
#ifdef DEBUG
		printk("%s: %sing: CHS=%d/%d/%d, sectors=%ld, buffer=0x%08lx\n",
			drive->name, (rq_data_dir(rq)==READ)?"read":"writ", cyl,
			head, sect, rq->nr_sectors, (unsigned long) rq->buffer);
#endif
	}
#ifdef CONFIG_BLK_DEV_PDC4030
	if (IS_PDC4030_DRIVE) {
		extern ide_startstop_t do_pdc4030_io(ide_drive_t *, struct request *);
		return do_pdc4030_io (drive, rq);
	}
#endif /* CONFIG_BLK_DEV_PDC4030 */
	if (rq_data_dir(rq) == READ) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		if (drive->using_dma && !(HWIF(drive)->dmaproc(ide_dma_read, drive)))
			return ide_started;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		if (HWGROUP(drive)->handler != NULL)
			BUG();
		ide_set_handler(drive, &read_intr, WAIT_CMD, NULL);
		if ((drive->id->cfs_enable_2 & 0x0400) &&
		    (drive->addressing == 1)) {
			OUT_BYTE(drive->mult_count ? WIN_MULTREAD_EXT : WIN_READ_EXT, IDE_COMMAND_REG);
		} else {
			OUT_BYTE(drive->mult_count ? WIN_MULTREAD : WIN_READ, IDE_COMMAND_REG);
		}
		return ide_started;
	} else if (rq_data_dir(rq) == WRITE) {
		ide_startstop_t startstop;
#ifdef CONFIG_BLK_DEV_IDEDMA
		if (drive->using_dma && !(HWIF(drive)->dmaproc(ide_dma_write, drive)))
			return ide_started;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		if ((drive->id->cfs_enable_2 & 0x0400) &&
		    (drive->addressing == 1)) {
			OUT_BYTE(drive->mult_count ? WIN_MULTWRITE_EXT : WIN_WRITE_EXT, IDE_COMMAND_REG);
		} else {
			OUT_BYTE(drive->mult_count ? WIN_MULTWRITE : WIN_WRITE, IDE_COMMAND_REG);
		}
		if (ide_wait_stat(&startstop, drive, DATA_READY, drive->bad_wstat, WAIT_DRQ)) {
			printk(KERN_ERR "%s: no DRQ after issuing %s\n", drive->name,
				drive->mult_count ? "MULTWRITE" : "WRITE");
			return startstop;
		}
		if (!drive->unmask)
			local_irq_disable();
		if (drive->mult_count) {
			ide_hwgroup_t *hwgroup = HWGROUP(drive);
	/*
	 * Ugh.. this part looks ugly because we MUST set up
	 * the interrupt handler before outputting the first block
	 * of data to be written.  If we hit an error (corrupted buffer list)
	 * in ide_multwrite(), then we need to remove the handler/timer
	 * before returning.  Fortunately, this NEVER happens (right?).
	 *
	 * Except when you get an error it seems...
	 *
	 * MAJOR DATA INTEGRITY BUG !!! only if we error 
	 */
			hwgroup->wrq = *rq; /* scratchpad */
			if (HWGROUP(drive)->handler != NULL)
				BUG();
			ide_set_handler(drive, &multwrite_intr, WAIT_CMD, NULL);
			if (ide_multwrite(drive, drive->mult_count)) {
				unsigned long flags;
				spin_lock_irqsave(&ide_lock, flags);
				hwgroup->handler = NULL;
				del_timer(&hwgroup->timer);
				spin_unlock_irqrestore(&ide_lock, flags);
				return ide_stopped;
			}
		} else {
			unsigned long flags;
			char *buffer = ide_map_buffer(rq, &flags);
			if (HWGROUP(drive)->handler != NULL)
				BUG();
			ide_set_handler(drive, &write_intr, WAIT_CMD, NULL);
			taskfile_output_data(drive, buffer, SECTOR_WORDS);
			ide_unmap_buffer(buffer, &flags);
		}
		return ide_started;
	}
	printk(KERN_ERR "%s: bad command: %lx\n", drive->name, rq->flags);
	idedisk_end_request(drive, 0);
	return ide_stopped;
}

#endif /* CONFIG_IDE_TASKFILE_IO */

static int idedisk_open (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	MOD_INC_USE_COUNT;
	if (drive->removable && drive->usage == 1) {
		ide_task_t args;
		memset(&args, 0, sizeof(ide_task_t));
		args.tfRegister[IDE_COMMAND_OFFSET] = WIN_DOORLOCK;
		args.command_type = ide_cmd_type_parser(&args);
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

static int do_idedisk_flushcache(ide_drive_t *drive);

static void idedisk_release (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	if (drive->removable && !drive->usage) {
		ide_task_t args;
		memset(&args, 0, sizeof(ide_task_t));
		args.tfRegister[IDE_COMMAND_OFFSET] = WIN_DOORUNLOCK;
		args.command_type = ide_cmd_type_parser(&args);
		invalidate_bdev(inode->i_bdev, 0);
		if (drive->doorlocking && ide_raw_taskfile(drive, &args, NULL))
			drive->doorlocking = 0;
	}
	if ((drive->id->cfs_enable_2 & 0x3000) && drive->wcache)
		if (do_idedisk_flushcache(drive))
			printk (KERN_INFO "%s: Write Cache FAILED Flushing!\n",
				drive->name);
	MOD_DEC_USE_COUNT;
}

static int idedisk_media_change (ide_drive_t *drive)
{
	return drive->removable;	/* if removable, always assume it was changed */
}

static void idedisk_revalidate (ide_drive_t *drive)
{
	ide_revalidate_drive(drive);
}

static int idedisk_end_request (ide_drive_t *drive, int uptodate)
{
	struct request *rq;
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&ide_lock, flags);
	rq = HWGROUP(drive)->rq;

	/*
	 * decide whether to reenable DMA -- 3 is a random magic for now,
	 * if we DMA timeout more than 3 times, just stay in PIO
	 */
	if (drive->state == DMA_PIO_RETRY && drive->retry_pio <= 3) {
		drive->state = 0;
		HWGROUP(drive)->hwif->dmaproc(ide_dma_on, drive);
	}

	if (!end_that_request_first(rq, uptodate, rq->hard_cur_sectors)) {
		add_blkdev_randomness(major(rq->rq_dev));
		blkdev_dequeue_request(rq);
		HWGROUP(drive)->rq = NULL;
		end_that_request_last(rq);
		ret = 0;
	}
	spin_unlock_irqrestore(&ide_lock, flags);
	return ret;
}

static byte idedisk_dump_status (ide_drive_t *drive, const char *msg, byte stat)
{
	unsigned long flags;
	byte err = 0;

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
		err = GET_ERR();
		printk("%s: %s: error=0x%02x", drive->name, msg, err);
#if FANCY_STATUS_DUMPS
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
				low = idedisk_read_24(drive);
				OUT_BYTE(drive->ctl|0x80, IDE_CONTROL_REG);
				high = idedisk_read_24(drive);
				sectors = ((__u64)high << 24) | low;
				printk(", LBAsect=%llu, high=%d, low=%d",
				       (unsigned long long) sectors,
				       high, low);
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
	local_irq_restore(flags);
	return err;
}

ide_startstop_t idedisk_error (ide_drive_t *drive, const char *msg, byte stat)
{
	struct request *rq;
	byte err;
	int i = (drive->mult_count ? drive->mult_count : 1) * SECTOR_WORDS;

	err = idedisk_dump_status(drive, msg, stat);

	if (drive == NULL || (rq = HWGROUP(drive)->rq) == NULL)
		return ide_stopped;
	/* retry only "normal" I/O: */
	if (rq->flags & (REQ_DRIVE_CMD | REQ_DRIVE_TASK | REQ_DRIVE_TASKFILE)) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, stat, err);
		return ide_stopped;
	}
#if 0
	else if (rq->flags & REQ_DRIVE_TASKFILE) {
		rq->errors = 1;
		ide_end_taskfile(drive, stat, err);
		return ide_stopped;
	}
#endif
	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr)) {
		/* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else if (stat & ERR_STAT) {
		/* err has different meaning on cdrom and tape */
		if (err == ABRT_ERR) {
			if (drive->select.b.lba &&
			    /* some newer drives don't support WIN_SPECIFY */
			    IN_BYTE(IDE_COMMAND_REG) == WIN_SPECIFY)
				return ide_stopped;
		} else if ((err & (ABRT_ERR | ICRC_ERR)) == (ABRT_ERR | ICRC_ERR)) {
			/* UDMA crc error, just retry the operation */
			drive->crc_count++;
		} else if (err & (BBD_ERR | ECC_ERR))
			/* retries won't help these */
			rq->errors = ERROR_MAX;
		else if (err & TRK0_ERR)
			/* help it find track zero */
			rq->errors |= ERROR_RECAL;
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
			ata_input_data(drive, buffer, wcount);
		}
	}
	if (GET_STAT() & (BUSY_STAT|DRQ_STAT))
		/* force an abort */
		OUT_BYTE(WIN_IDLEIMMEDIATE,IDE_COMMAND_REG);
	if (rq->errors >= ERROR_MAX)
		DRIVER(drive)->end_request(drive, 0);
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

/*
 * Queries for true maximum capacity of the drive.
 * Returns maximum LBA address (> 0) of the drive, 0 if failed.
 */
static unsigned long idedisk_read_native_max_address(ide_drive_t *drive)
{
	ide_task_t args;
	unsigned long addr = 0;

#if 0
	if (!(drive->id->command_set_1 & 0x0400) &&
	    !(drive->id->cfs_enable_2 & 0x0100))
		return addr;
#endif

	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_SELECT_OFFSET]	= 0x40;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_READ_NATIVE_MAX;
	args.command_type			= ide_cmd_type_parser(&args);
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		addr = ((args.tfRegister[IDE_SELECT_OFFSET] & 0x0f) << 24)
		     | ((args.tfRegister[  IDE_HCYL_OFFSET]       ) << 16)
		     | ((args.tfRegister[  IDE_LCYL_OFFSET]       ) <<  8)
		     | ((args.tfRegister[IDE_SECTOR_OFFSET]       ));
	}
	addr++;	/* since the return value is (maxlba - 1), we add 1 */
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
	args.command_type			= ide_cmd_type_parser(&args);
        /* submit command request */
        ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		u32 high = ((args.hobRegister[IDE_HCYL_OFFSET_HOB])<<16) |
			   ((args.hobRegister[IDE_LCYL_OFFSET_HOB])<<8) |
  			    (args.hobRegister[IDE_SECTOR_OFFSET_HOB]); 
		u32 low  = ((args.tfRegister[IDE_HCYL_OFFSET])<<16) |
			   ((args.tfRegister[IDE_LCYL_OFFSET])<<8) |
			    (args.tfRegister[IDE_SECTOR_OFFSET]);
		addr = ((__u64)high << 24) | low;
	}
	addr++;	/* since the return value is (maxlba - 1), we add 1 */
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
	args.command_type			= ide_cmd_type_parser(&args);
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);
	/* if OK, read new maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		addr_set = ((args.tfRegister[IDE_SELECT_OFFSET] & 0x0f) << 24)
			 | ((args.tfRegister[  IDE_HCYL_OFFSET]       ) << 16)
			 | ((args.tfRegister[  IDE_LCYL_OFFSET]       ) <<  8)
			 | ((args.tfRegister[IDE_SECTOR_OFFSET]       ));
	}
	addr_set++;
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
	args.hobRegister[IDE_SECTOR_OFFSET_HOB]	= ((addr_req >>= 8) & 0xff);
	args.hobRegister[IDE_LCYL_OFFSET_HOB]	= ((addr_req >>= 8) & 0xff);
	args.hobRegister[IDE_HCYL_OFFSET_HOB]	= ((addr_req >>= 8) & 0xff);
	args.hobRegister[IDE_SELECT_OFFSET_HOB]	= 0x40;
	args.hobRegister[IDE_CONTROL_OFFSET_HOB]= (drive->ctl|0x80);
	args.command_type			= ide_cmd_type_parser(&args);
	/* submit command request */
	ide_raw_taskfile(drive, &args, NULL);
	/* if OK, compute maximum address value */
	if ((args.tfRegister[IDE_STATUS_OFFSET] & 0x01) == 0) {
		u32 high = ((args.hobRegister[IDE_HCYL_OFFSET_HOB])<<16) |
			   ((args.hobRegister[IDE_LCYL_OFFSET_HOB])<<8) |
			    (args.hobRegister[IDE_SECTOR_OFFSET_HOB]);
		u32 low  = ((args.tfRegister[IDE_HCYL_OFFSET])<<16) |
			   ((args.tfRegister[IDE_LCYL_OFFSET])<<8) |
			    (args.tfRegister[IDE_SECTOR_OFFSET]);
		addr_set = ((__u64)high << 24) | low;
	}
	return addr_set;
}

#endif /* CONFIG_IDEDISK_STROKE */

/*
 * Tests if the drive supports Host Protected Area feature.
 * Returns true if supported, false otherwise.
 */
static inline int idedisk_supports_host_protected_area(ide_drive_t *drive)
{
	int flag = (drive->id->cfs_enable_1 & 0x0400) ? 1 : 0;
	if (flag)
		printk("%s: host protected area => %d\n", drive->name, flag);
	return flag;
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
	unsigned long capacity = drive->cyl * drive->head * drive->sect;
	unsigned long set_max = idedisk_read_native_max_address(drive);
	unsigned long long capacity_2 = capacity;
	unsigned long long set_max_ext;

	drive->capacity48 = 0;
	drive->select.b.lba = 0;

	(void) idedisk_supports_host_protected_area(drive);

	if (id->cfs_enable_2 & 0x0400) {
		capacity_2 = id->lba_capacity_2;
		drive->cyl = (unsigned int) capacity_2 / (drive->head * drive->sect);
		drive->head		= drive->bios_head = 255;
		drive->sect		= drive->bios_sect = 63;
		drive->select.b.lba	= 1;
		set_max_ext = idedisk_read_native_max_address_ext(drive);
		if (set_max_ext > capacity_2) {
#ifdef CONFIG_IDEDISK_STROKE
			set_max_ext = idedisk_read_native_max_address_ext(drive);
			set_max_ext = idedisk_set_max_address_ext(drive, set_max_ext);
			if (set_max_ext) {
				drive->capacity48 = capacity_2 = set_max_ext;
				drive->cyl = (unsigned int) set_max_ext / (drive->head * drive->sect);
				drive->select.b.lba = 1;
				drive->id->lba_capacity_2 = capacity_2;
                        }
#else /* !CONFIG_IDEDISK_STROKE */
			printk("%s: setmax_ext LBA %llu, native  %llu\n",
				drive->name, set_max_ext, capacity_2);
#endif /* CONFIG_IDEDISK_STROKE */
		}
		drive->cyl = (unsigned int) capacity_2 / (drive->head * drive->sect);
		drive->bios_cyl		= drive->cyl;
		drive->capacity48	= capacity_2;
		drive->capacity		= (unsigned long) capacity_2;
		return;
	/* Determine capacity, and use LBA if the drive properly supports it */
	} else if ((id->capability & 2) && lba_capacity_is_ok(id)) {
		capacity = id->lba_capacity;
		drive->cyl = capacity / (drive->head * drive->sect);
		drive->select.b.lba = 1;
	}

	if (set_max > capacity) {
#ifdef CONFIG_IDEDISK_STROKE
		set_max = idedisk_read_native_max_address(drive);
		set_max = idedisk_set_max_address(drive, set_max);
		if (set_max) {
			drive->capacity = capacity = set_max;
			drive->cyl = set_max / (drive->head * drive->sect);
			drive->select.b.lba = 1;
			drive->id->lba_capacity = capacity;
		}
#else /* !CONFIG_IDEDISK_STROKE */
		printk("%s: setmax LBA %lu, native  %lu\n",
			drive->name, set_max, capacity);
#endif /* CONFIG_IDEDISK_STROKE */
	}

	drive->capacity = capacity;

	if ((id->command_set_2 & 0x0400) && (id->cfs_enable_2 & 0x0400)) {
		drive->capacity48 = id->lba_capacity_2;
		drive->head = 255;
		drive->sect = 63;
		drive->cyl = (unsigned long)(drive->capacity48) / (drive->head * drive->sect);
	}
}

static unsigned long idedisk_capacity (ide_drive_t *drive)
{
	if (drive->id->cfs_enable_2 & 0x0400)
		return (drive->capacity48 - drive->sect0);
	return (drive->capacity - drive->sect0);
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
			args.command_type = ide_cmd_type_parser(&args);
			do_rw_taskfile(drive, &args);
		}
	} else if (s->b.recalibrate) {
		s->b.recalibrate = 0;
		if (!IS_PDC4030_DRIVE) {
			ide_task_t args;
			memset(&args, 0, sizeof(ide_task_t));
			args.tfRegister[IDE_NSECTOR_OFFSET] = drive->sect;
			args.tfRegister[IDE_COMMAND_OFFSET] = WIN_RESTORE;
			args.command_type = ide_cmd_type_parser(&args);
			do_rw_taskfile(drive, &args);
		}
	} else if (s->b.set_multmode) {
		s->b.set_multmode = 0;
		if (drive->id && drive->mult_req > drive->id->max_multsect)
			drive->mult_req = drive->id->max_multsect;
		if (!IS_PDC4030_DRIVE) {
			ide_task_t args;
			memset(&args, 0, sizeof(ide_task_t));
			args.tfRegister[IDE_NSECTOR_OFFSET] = drive->mult_req;
			args.tfRegister[IDE_COMMAND_OFFSET] = WIN_SETMULT;
			args.command_type = ide_cmd_type_parser(&args);
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
	args.command_type			= ide_cmd_type_parser(&args);
	return ide_raw_taskfile(drive, &args, NULL);
}

static int get_smart_values(ide_drive_t *drive, byte *buf)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= SMART_READ_VALUES;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	args.tfRegister[IDE_LCYL_OFFSET]	= SMART_LCYL_PASS;
	args.tfRegister[IDE_HCYL_OFFSET]	= SMART_HCYL_PASS;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SMART;
	args.command_type			= ide_cmd_type_parser(&args);
	(void) smart_enable(drive);
	return ide_raw_taskfile(drive, &args, buf);
}

static int get_smart_thresholds(ide_drive_t *drive, byte *buf)
{
	ide_task_t args;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= SMART_READ_THRESHOLDS;
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	args.tfRegister[IDE_LCYL_OFFSET]	= SMART_LCYL_PASS;
	args.tfRegister[IDE_HCYL_OFFSET]	= SMART_HCYL_PASS;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SMART;
	args.command_type			= ide_cmd_type_parser(&args);
	(void) smart_enable(drive);
	return ide_raw_taskfile(drive, &args, buf);
}

static int proc_idedisk_read_cache
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	char		*out = page;
	int		len;

	if (drive->id)
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

static int write_cache (ide_drive_t *drive, int arg)
{
	ide_task_t args;

	if (!(drive->id->cfs_enable_2 & 0x3000))
		return 1;

	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_FEATURE_OFFSET]	= (arg) ?
			SETFEATURES_EN_WCACHE : SETFEATURES_DIS_WCACHE;
	args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_SETFEATURES;
	args.command_type			= ide_cmd_type_parser(&args);
	(void) ide_raw_taskfile(drive, &args, NULL);

	drive->wcache = arg;
	return 0;
}

static int call_idedisk_standby (ide_drive_t *drive, int arg)
{
	ide_task_t args;
	byte standby = (arg) ? WIN_STANDBYNOW2 : WIN_STANDBYNOW1;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_COMMAND_OFFSET]	= standby;
	args.command_type			= ide_cmd_type_parser(&args);
	return ide_raw_taskfile(drive, &args, NULL);
}

static int do_idedisk_standby (ide_drive_t *drive)
{
	return call_idedisk_standby(drive, 0);
}

static int call_idedisk_suspend (ide_drive_t *drive, int arg)
{
	ide_task_t args;
	byte suspend = (arg) ? WIN_SLEEPNOW2 : WIN_SLEEPNOW1;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_COMMAND_OFFSET]	= suspend;
	args.command_type			= ide_cmd_type_parser(&args);
	return ide_raw_taskfile(drive, &args, NULL);
}

static int do_idedisk_suspend (ide_drive_t *drive)
{
	if (drive->suspend_reset)
		return 1;

	return call_idedisk_suspend(drive, 0);
}

#if 0
static int call_idedisk_checkpower (ide_drive_t *drive, int arg)
{
	ide_task_t args;
	byte ckpw = (arg) ? WIN_CHECKPOWERMODE2 : WIN_CHECKPOWERMODE1;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_COMMAND_OFFSET]	= ckpw;
	args.command_type			= ide_cmd_type_parser(&args);
	ide_raw_taskfile(drive, &args, NULL);
#if 0
if (errno != EIO || args[0] != 0 || args[1] != 0)
   state = "unknown";
else
   state = "sleeping";
} else {
   state = (args[2] == 255) ? "active/idle" : "standby";
#endif
	return 0;
}

static int do_idedisk_checkpower (ide_drive_t *drive)
{
	return call_idedisk_checkpower(drive, 0);
}
#endif

static int do_idedisk_resume (ide_drive_t *drive)
{
	if (!drive->suspend_reset)
		return 1;
	return 0;
}

static int do_idedisk_flushcache (ide_drive_t *drive)
{
	ide_task_t args;

	memset(&args, 0, sizeof(ide_task_t));
	if (drive->id->cfs_enable_2 & 0x2400)
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_FLUSH_CACHE_EXT;
	else
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_FLUSH_CACHE;
	args.command_type	 		= ide_cmd_type_parser(&args);
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
	args.command_type = ide_cmd_type_parser(&args);
	ide_raw_taskfile(drive, &args, NULL);
	drive->acoustic = arg;
	return 0;
}

static int probe_lba_addressing (ide_drive_t *drive, int arg)
{
	drive->addressing =  0;

	if (HWIF(drive)->addressing)
		return 0;

	if (!(drive->id->cfs_enable_2 & 0x0400))
                return -EIO;
	drive->addressing = arg;
	return 0;
}

static int set_lba_addressing (ide_drive_t *drive, int arg)
{
	return (probe_lba_addressing(drive, arg));
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
}

static void idedisk_setup (ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	unsigned long capacity;
	
	idedisk_add_settings(drive);

	if (id == NULL)
		return;

	/*
	 * CompactFlash cards and their brethern look just like hard drives
	 * to us, but they are removable and don't have a doorlock mechanism.
	 */
	if (drive->removable && !drive_is_flashcard(drive)) {
		/*
		 * Removable disks (eg. SYQUEST); ignore 'WD' drives 
		 */
		if (id->model[0] != 'W' || id->model[1] != 'D') {
			drive->doorlocking = 1;
		}
	}

#if 1
	(void) probe_lba_addressing(drive, 1);
#else
	/* if using 48-bit addressing bump the request size up */
	if (probe_lba_addressing(drive, 1))
		blk_queue_max_sectors(&drive->queue, 2048);
#endif

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

	/*
	 * if possible, give fdisk access to more of the drive,
	 * by correcting bios_cyls:
	 */
	capacity = idedisk_capacity (drive);
	if ((capacity >= (drive->bios_cyl * drive->bios_sect * drive->bios_head)) &&
	    (!drive->forced_geom) && drive->bios_sect && drive->bios_head)
		drive->bios_cyl = (capacity / drive->bios_sect) / drive->bios_head;
	printk (KERN_INFO "%s: %ld sectors", drive->name, capacity);

	/* Give size in megabytes (MB), not mebibytes (MiB). */
	/* We compute the exact rounded value, avoiding overflow. */
	printk (" (%ld MB)", (capacity - capacity/625 + 974)/1950);

	/* Only print cache size when it was specified */
	if (id->buf_size)
		printk (" w/%dKiB Cache", id->buf_size/2);

	printk(", CHS=%d/%d/%d", 
	       drive->bios_cyl, drive->bios_head, drive->bios_sect);
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (drive->using_dma)
		(void) HWIF(drive)->dmaproc(ide_dma_verbose, drive);
#endif /* CONFIG_BLK_DEV_IDEDMA */
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
	if (drive->id->cfs_enable_2 & 0x3000)
		write_cache(drive, (id->cfs_enable_2 & 0x3000));
}

static int idedisk_cleanup (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	int unit = drive - hwif->drives;
	struct gendisk *g = hwif->gd[unit];
	if ((drive->id->cfs_enable_2 & 0x3000) && drive->wcache)
		if (do_idedisk_flushcache(drive))
			printk (KERN_INFO "%s: Write Cache FAILED Flushing!\n",
				drive->name);
	if (ide_unregister_subdriver(drive))
		return 1;
	del_gendisk(g);
	return 0;
}

static int idedisk_reinit(ide_drive_t *drive);

/*
 *      IDE subdriver functions, registered with ide.c
 */
static ide_driver_t idedisk_driver = {
	owner:			THIS_MODULE,
	name:			"ide-disk",
	version:		IDEDISK_VERSION,
	media:			ide_disk,
	busy:			0,
	supports_dma:		1,
	supports_dsc_overlap:	0,
	cleanup:		idedisk_cleanup,
	standby:		do_idedisk_standby,
	suspend:		do_idedisk_suspend,
	resume:			do_idedisk_resume,
	flushcache:		do_idedisk_flushcache,
	do_request:		do_rw_disk,
	end_request:		idedisk_end_request,
	sense:			idedisk_dump_status,
	error:			idedisk_error,
	ioctl:			NULL,
	open:			idedisk_open,
	release:		idedisk_release,
	media_change:		idedisk_media_change,
	revalidate:		idedisk_revalidate,
	pre_reset:		idedisk_pre_reset,
	capacity:		idedisk_capacity,
	special:		idedisk_special,
	proc:			idedisk_proc,
	reinit:			idedisk_reinit,
	ata_prebuilder:		NULL,
	atapi_prebuilder:	NULL,
	drives:			LIST_HEAD_INIT(idedisk_driver.drives),
};

MODULE_DESCRIPTION("ATA DISK Driver");

static int idedisk_reinit(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	int unit = drive - hwif->drives;
	struct gendisk *g = hwif->gd[unit];

	/* strstr("foo", "") is non-NULL */
	if (!strstr("ide-disk", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_disk)
		goto failed;

	if (ide_register_subdriver (drive, &idedisk_driver, IDE_SUBDRIVER_VERSION)) {
		printk (KERN_ERR "ide-disk: %s: Failed to register the driver with ide.c\n", drive->name);
		goto failed;
	}
	DRIVER(drive)->busy++;
	idedisk_setup(drive);
	if ((!drive->head || drive->head > 16) && !drive->select.b.lba) {
		printk(KERN_ERR "%s: INVALID GEOMETRY: %d PHYSICAL HEADS?\n",
			drive->name, drive->head);
		if ((drive->id->cfs_enable_2 & 0x3000) && drive->wcache)
			if (do_idedisk_flushcache(drive))
				printk (KERN_INFO "%s: Write Cache FAILED Flushing!\n",
					drive->name);
		ide_unregister_subdriver(drive);
		DRIVER(drive)->busy--;
		goto failed;
	}
	DRIVER(drive)->busy--;
	g->minor_shift = PARTN_BITS;
	g->de_arr[0] = drive->de;
	g->flags = drive->removable ? GENHD_FL_REMOVABLE : 0;
	add_gendisk(g);
	register_disk(g, mk_kdev(g->major,g->first_minor),
		      1<<g->minor_shift, ide_fops,
		      current_capacity(drive));
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
	ide_register_driver(&idedisk_driver);
	return 0;
}

ide_startstop_t panic_box(ide_drive_t *drive)
{
#if 0
	panic("%s: Attempted to corrupt something: ide operation "
#else
	printk(KERN_ERR "%s: Attempted to corrupt something: ide operation "
#endif
		"was pending accross suspend/resume.\n", drive->name);
	return ide_stopped;
}

int ide_disks_busy(void)
{
	int i;
	for (i=0; i<MAX_HWIFS; i++) {
		struct hwgroup_s *hwgroup = ide_hwifs[i].hwgroup;
		if (!hwgroup) continue;
		if ((hwgroup->handler) && (hwgroup->handler != panic_box))
			return 1;
	}
	return 0;
}

void ide_disk_suspend(void)
{
	int i;
	while (ide_disks_busy()) {
		printk("*");
		schedule();
	}
	for (i=0; i<MAX_HWIFS; i++) {
		struct hwgroup_s *hwgroup = ide_hwifs[i].hwgroup;

		if (!hwgroup) continue;
		hwgroup->handler_save = hwgroup->handler;
		hwgroup->handler = panic_box;
	}
	driver_blocked = 1;
	if (ide_disks_busy())
		panic("How did you get that request through?!");
}

/* unsuspend and resume should be equal in the ideal world */

void ide_disk_unsuspend(void)
{
	int i;
	for (i=0; i<MAX_HWIFS; i++) {
		struct hwgroup_s *hwgroup = ide_hwifs[i].hwgroup;

		if (!hwgroup) continue;
		hwgroup->handler = NULL; /* hwgroup->handler_save; */
		hwgroup->handler_save = NULL;
	}
	driver_blocked = 0;
}

void ide_disk_resume(void)
{
	int i;
	for (i=0; i<MAX_HWIFS; i++) {
		struct hwgroup_s *hwgroup = ide_hwifs[i].hwgroup;

		if (!hwgroup) continue;
		if (hwgroup->handler != panic_box)
			panic("Handler was not set to panic?");
		hwgroup->handler_save = NULL;
		hwgroup->handler = NULL;
	}
	driver_blocked = 0;
}

module_init(idedisk_init);
module_exit(idedisk_exit);
MODULE_LICENSE("GPL");
