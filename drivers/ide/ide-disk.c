/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 1994-1998,2002  Linus Torvalds and authors:
 *
 *	Mark Lord	<mlord@pobox.com>
 *	Gadi Oxman	<gadio@netvision.net.il>
 *	Andre Hedrick	<andre@linux-ide.org>
 *	Jens Axboe	<axboe@suse.de>
 *	Marcin Dalecki	<martin@dalecki.de>
 *
 * This is the ATA disk device driver, as evolved from hd.c and ide.c.
 */

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
#include <linux/buffer_head.h>		/* for invalidate_bdev() */
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#ifdef CONFIG_BLK_DEV_PDC4030
# define IS_PDC4030_DRIVE (drive->channel->chipset == ide_pdc4030)
#else
# define IS_PDC4030_DRIVE (0)	/* auto-NULLs out pdc4030 code */
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
 * Perform a sanity check on the claimed "lba_capacity"
 * value for this drive (from its reported identification information).
 *
 * Returns:	1 if lba_capacity looks sensible
 *		0 otherwise
 *
 * It is called only once for each drive.
 */
static int lba_capacity_is_ok(struct hd_driveid *id)
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

/*
 * Handler for command with PIO data-in phase.
 */
static ide_startstop_t task_in_intr(struct ata_device *drive, struct request *rq)
{
	int ret;

	if (!ata_status(drive, DATA_READY, BAD_R_STAT)) {
		if (drive->status & (ERR_STAT | DRQ_STAT))
			return ata_error(drive, rq, __FUNCTION__);

		/* no data yet, so wait for another interrupt */
		ata_set_handler(drive, task_in_intr, WAIT_CMD, NULL);

		ret = ATA_OP_CONTINUES;
	} else {
		//	printk("Read: %p, rq->current_nr_sectors: %d\n", buf, (int) rq->current_nr_sectors);
		{
			unsigned long flags;
			char *buf;

			buf = ide_map_rq(rq, &flags);
			ata_read(drive, buf, SECTOR_WORDS);
			ide_unmap_rq(rq, buf, &flags);
		}

		/* First segment of the request is complete. note that this does not
		 * necessarily mean that the entire request is done!! this is only true
		 * if ata_end_request() returns 0.
		 */
		rq->errors = 0;
		--rq->current_nr_sectors;

		if (rq->current_nr_sectors <= 0) {
			if (!ata_end_request(drive, rq, 1, 0)) {
			//		printk("Request Ended stat: %02x\n", drive->status);

				return ATA_OP_FINISHED;
			}
		}

		/* still data left to transfer */
		ata_set_handler(drive, task_in_intr,  WAIT_CMD, NULL);

		ret = ATA_OP_CONTINUES;
	}

	return ret;
}

/*
 * Handler for command with PIO data-out phase.
 */
static ide_startstop_t task_out_intr(struct ata_device *drive, struct request *rq)
{
	int ret;

	if (!ata_status(drive, DRIVE_READY, drive->bad_wstat))
		return ata_error(drive, rq, __FUNCTION__);

	if (!rq->current_nr_sectors && !ata_end_request(drive, rq, 1, 0)) {
		ret = ATA_OP_FINISHED;
	} else {
		if ((rq->nr_sectors == 1) != (drive->status & DRQ_STAT)) {
			unsigned long flags;
			char *buf;

//			printk("write: %p, rq->current_nr_sectors: %d\n", buf, (int) rq->current_nr_sectors);
			buf = ide_map_rq(rq, &flags);
			ata_write(drive, buf, SECTOR_WORDS);
			ide_unmap_rq(rq, buf, &flags);

			rq->errors = 0;
			--rq->current_nr_sectors;
		}
		ata_set_handler(drive, task_out_intr, WAIT_CMD, NULL);

		ret = ATA_OP_CONTINUES;
	}

	return ret;
}

/*
 * Handler for command with Read Multiple
 */
static ide_startstop_t task_mulin_intr(struct ata_device *drive, struct request *rq)
{
	int ret;

	if (!ata_status(drive, DATA_READY, BAD_R_STAT)) {
		if (drive->status & (ERR_STAT | DRQ_STAT))
			return ata_error(drive, rq, __FUNCTION__);

		/* no data yet, so wait for another interrupt */
		ata_set_handler(drive, task_mulin_intr, WAIT_CMD, NULL);

		ret = ATA_OP_CONTINUES;
	} else {
		unsigned int msect;

		/* (ks/hs): Fixed Multi-Sector transfer */
		msect = drive->mult_count;

		do {
			unsigned int nsect;

			nsect = rq->current_nr_sectors;
			if (nsect > msect)
				nsect = msect;

#if 0
			printk("Multiread: %p, nsect: %d , rq->current_nr_sectors: %d\n",
					buf, nsect, rq->current_nr_sectors);
#endif
			{
				unsigned long flags;
				char *buf;

				buf = ide_map_rq(rq, &flags);
				ata_read(drive, buf, nsect * SECTOR_WORDS);
				ide_unmap_rq(rq, buf, &flags);
			}

			rq->errors = 0;
			rq->current_nr_sectors -= nsect;

			/* FIXME: this seems buggy */
			if (rq->current_nr_sectors <= 0) {
				if (!ata_end_request(drive, rq, 1, 0))
					return ATA_OP_FINISHED;
			}
			msect -= nsect;
		} while (msect);

		/* more data left */
		ata_set_handler(drive, task_mulin_intr, WAIT_CMD, NULL);

		ret = ATA_OP_CONTINUES;
	}

	return ret;
}

static ide_startstop_t task_mulout_intr(struct ata_device *drive, struct request *rq)
{
	int ok;
	int ret;

	/*
	 * FIXME: the drive->status checks here seem to be messy.
	 *
	 * (ks/hs): Handle last IRQ on multi-sector transfer,
	 * occurs after all data was sent in this chunk
	 */

	ok = ata_status(drive, DATA_READY, BAD_R_STAT);

	if (!ok || !rq->nr_sectors) {
		if (drive->status & (ERR_STAT | DRQ_STAT))
			return ata_error(drive, rq, __FUNCTION__);
	}
	if (!rq->nr_sectors) {
		ata_end_request(drive, rq, 1, rq->hard_nr_sectors);
		rq->bio = NULL;
		ret = ATA_OP_FINISHED;
	} else if (!ok) {
		/* not ready yet, so wait for next IRQ */
		ata_set_handler(drive, task_mulout_intr, WAIT_CMD, NULL);

		ret = ATA_OP_CONTINUES;
	} else {
		int mcount = drive->mult_count;

		/* prepare for next IRQ */
		ata_set_handler(drive, task_mulout_intr, WAIT_CMD, NULL);

		do {
			char *buf;
			int nsect = rq->current_nr_sectors;
			unsigned long flags;

			if (nsect > mcount)
				nsect = mcount;
			mcount -= nsect;

			buf = bio_kmap_irq(rq->bio, &flags) + ide_rq_offset(rq);
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
			rq->errors = 0; /* FIXME: why?  --bzolnier */

			/*
			 * Ok, we're all setup for the interrupt re-entering us on the
			 * last transfer.
			 */
			ata_write(drive, buf, nsect * SECTOR_WORDS);
			bio_kunmap_irq(buf, &flags);
		} while (mcount);

		ret = ATA_OP_CONTINUES;
	}

	return ret;
}

/*
 * Issue a READ or WRITE command to a disk, using LBA if supported, or CHS
 * otherwise, to address sectors.  It also takes care of issuing special
 * DRIVE_CMDs.
 */
static ide_startstop_t idedisk_do_request(struct ata_device *drive, struct request *rq, sector_t block)
{
	struct ata_taskfile args;
	struct ata_taskfile *ar;
	struct hd_driveid *id = drive->id;
	u8 cmd;

	/* Special drive commands don't need any kind of setup.
	 */
	if (rq->flags & REQ_SPECIAL) {
		ar = rq->special;
		cmd  = ar->cmd;
	} else  {
		unsigned int sectors;

		/* FIXME: this check doesn't make sense */
		if (!(rq->flags & REQ_CMD)) {
			blk_dump_rq_flags(rq, "idedisk_do_request - bad command");
			ata_end_request(drive, rq, 0, 0);

			return ATA_OP_FINISHED;
		}

		if (IS_PDC4030_DRIVE) {
			extern ide_startstop_t promise_do_request(struct ata_device *, struct request *, sector_t);

			return promise_do_request(drive, rq, block);
		}

		/*
		 * start a tagged operation
		 */
		if (drive->using_tcq) {
			int st = blk_queue_start_tag(&drive->queue, rq);

			if (ata_pending_commands(drive) > drive->max_depth)
				drive->max_depth = ata_pending_commands(drive);
			if (ata_pending_commands(drive) > drive->max_last_depth)
				drive->max_last_depth = ata_pending_commands(drive);

			if (st) {
				BUG_ON(!ata_pending_commands(drive));

				return ATA_OP_CONTINUES;
			}
		}
		ar = &args;

		memset(&args, 0, sizeof(args));
		sectors = rq->nr_sectors;
		/* Dispatch depending up on the drive access method. */
		if ((drive->id->cfs_enable_2 & 0x0400) && (drive->addressing)) {
			/* LBA 48 bit */
			/*
			 * 268435455  == 137439 MB or 28bit limit
			 * 320173056  == 163929 MB or 48bit addressing
			 * 1073741822 == 549756 MB or 48bit addressing fake drive
			 */
			if (sectors == 65536)
				sectors = 0;

			if (blk_rq_tagged(rq)) {
				args.taskfile.feature = sectors;
				args.hobfile.feature = sectors >> 8;
				args.taskfile.sector_count = rq->tag << 3;
			} else {
				args.taskfile.sector_count = sectors;
				args.hobfile.sector_count = sectors >> 8;
			}

			args.taskfile.sector_number = block;		/* low lba */
			args.taskfile.low_cylinder = (block >>= 8);	/* mid lba */
			args.taskfile.high_cylinder = (block >>= 8);	/* hi  lba */
			args.taskfile.device_head = drive->select.all;

			args.hobfile.sector_number = (block >>= 8);	/* low lba */
			args.hobfile.low_cylinder = (block >>= 8);	/* mid lba */
			args.hobfile.high_cylinder = (block >>= 8);	/* hi  lba */
		} else if (drive->select.b.lba) {
			/* LBA 28 bit  */
			if (sectors == 256)
				sectors = 0;

			if (blk_rq_tagged(rq)) {
				args.taskfile.feature = sectors;
				args.taskfile.sector_count = rq->tag << 3;
			} else
				args.taskfile.sector_count = sectors;

			args.taskfile.sector_number = block;
			args.taskfile.low_cylinder = (block >>= 8);
			args.taskfile.high_cylinder = (block >>= 8);
			args.taskfile.device_head = ((block >> 8) & 0x0f);
		} else {
			/* CHS */
			unsigned int track	= (block / drive->sect);
			unsigned int sect	= (block % drive->sect) + 1;
			unsigned int head	= (track % drive->head);
			unsigned int cyl	= (track / drive->head);

			if (sectors == 256)
				sectors = 0;

			if (blk_rq_tagged(rq)) {
				args.taskfile.feature = sectors;
				args.taskfile.sector_count = rq->tag << 3;
			} else
				args.taskfile.sector_count = sectors;

			args.taskfile.sector_number = sect;
			args.taskfile.low_cylinder = cyl;
			args.taskfile.high_cylinder = (cyl>>8);
			args.taskfile.device_head = head;
		}
		args.taskfile.device_head |= drive->select.all;

		/*
		 * Decode with physical ATA command to use and setup associated data.
		 */

		if (rq_data_dir(rq) == READ) {
			args.command_type = IDE_DRIVE_TASK_IN;
			if (drive->addressing) {
				if (drive->using_tcq) {
					cmd = WIN_READDMA_QUEUED_EXT;
				} else if (drive->using_dma) {
					cmd = WIN_READDMA_EXT;
				} else if (drive->mult_count) {
					args.XXX_handler = task_mulin_intr;
					cmd = WIN_MULTREAD_EXT;
				} else {
					args.XXX_handler = task_in_intr;
					cmd = WIN_READ_EXT;
				}
			} else {
				if (drive->using_tcq) {
					cmd = WIN_READDMA_QUEUED;
				} else if (drive->using_dma) {
					cmd = WIN_READDMA;
				} else if (drive->mult_count) {
					args.XXX_handler = task_mulin_intr;
					cmd = WIN_MULTREAD;
				} else {
					args.XXX_handler = task_in_intr;
					cmd = WIN_READ;
				}
			}
		} else {
			args.command_type = IDE_DRIVE_TASK_RAW_WRITE;
			if (drive->addressing) {
				if (drive->using_tcq) {
					cmd = WIN_WRITEDMA_QUEUED_EXT;
				} else if (drive->using_dma) {
					cmd = WIN_WRITEDMA_EXT;
				} else if (drive->mult_count) {
					args.XXX_handler = task_mulout_intr;
					cmd = WIN_MULTWRITE_EXT;
				} else {
					args.XXX_handler = task_out_intr;
					cmd = WIN_WRITE_EXT;
				}
			} else {
				if (drive->using_tcq) {
					cmd = WIN_WRITEDMA_QUEUED;
				} else if (drive->using_dma) {
					cmd = WIN_WRITEDMA;
				} else if (drive->mult_count) {
					args.XXX_handler = task_mulout_intr;
					cmd = WIN_MULTWRITE;
				} else {
					args.XXX_handler = task_out_intr;
					cmd = WIN_WRITE;
				}
			}
		}
#ifdef DEBUG
		printk("%s: %sing: ", drive->name,
				(rq_data_dir(rq)==READ) ? "read" : "writ");
		if (lba)	printk("LBAsect=%lld, ", block);
		else		printk("CHS=%d/%d/%d, ", cyl, head, sect);
		printk("sectors=%ld, ", rq->nr_sectors);
		printk("buffer=%p\n", rq->buffer);
#endif
		ar->cmd = cmd;
		rq->special = ar;
	}

	/* (ks/hs): Moved to start, do not use for multiple out commands.
	 * FIXME: why not?! */
	if (!(cmd == CFA_WRITE_MULTI_WO_ERASE ||
	      cmd == WIN_MULTWRITE ||
	      cmd == WIN_MULTWRITE_EXT)) {
		ata_irq_enable(drive, 1);
		ata_mask(drive);
	}

	if ((id->command_set_2 & 0x0400) && (id->cfs_enable_2 & 0x0400) &&
	    (drive->addressing == 1))
		ata_out_regfile(drive, &ar->hobfile);

	ata_out_regfile(drive, &ar->taskfile);

	OUT_BYTE((ar->taskfile.device_head & (drive->addressing ? 0xE0 : 0xEF)) | drive->select.all,
			IDE_SELECT_REG);

	/* FIXME: this is actually distingushing between PIO and DMA requests.
	 */
	if (ar->XXX_handler) {
		if (ar->command_type == IDE_DRIVE_TASK_IN ||
		    ar->command_type == IDE_DRIVE_TASK_NO_DATA) {

			ata_set_handler(drive, ar->XXX_handler, WAIT_CMD, NULL);
			OUT_BYTE(cmd, IDE_COMMAND_REG);

			return ATA_OP_CONTINUES;
		}

		/* FIXME: Warning check for race between handlers for writing
		 * first block of data.  However since we are well inside the
		 * boundaries of the seek, we should be okay.
		 */
		if (ar->command_type == IDE_DRIVE_TASK_RAW_WRITE) {
			ide_startstop_t ret;

			OUT_BYTE(cmd, IDE_COMMAND_REG);

			ret = ata_status_poll(drive, DATA_READY, drive->bad_wstat,
						WAIT_DRQ, rq);
			if (ret != ATA_OP_READY) {
				printk(KERN_ERR "%s: no DRQ after issuing %s\n",
						drive->name, drive->mult_count ? "MULTWRITE" : "WRITE");

				return ret;
			}

			/* FIXME: This doesn't make the slightest sense.
			 * (ks/hs): Fixed Multi Write
			 */
			if (!(cmd == CFA_WRITE_MULTI_WO_ERASE ||
			      cmd == WIN_MULTWRITE ||
			      cmd == WIN_MULTWRITE_EXT)) {
				unsigned long flags;
				char *buf = ide_map_rq(rq, &flags);

				ata_set_handler(drive, ar->XXX_handler, WAIT_CMD, NULL);

				/* For Write_sectors we need to stuff the first sector */
				/* FIXME: what if !rq->current_nr_sectors  --bzolnier */
				ata_write(drive, buf, SECTOR_WORDS);

				rq->current_nr_sectors--;
				ide_unmap_rq(rq, buf, &flags);

				return ATA_OP_CONTINUES;
			} else {
				int i;

				/* Polling wait until the drive is ready.
				 *
				 * Stuff the first sector(s) by calling the
				 * handler driectly therafter.
				 *
				 * FIXME: Replace hard-coded 100, what about
				 * error handling?
				 *
				 * FIXME: Whatabout the IRE clearing and not clearing case?!
				 */

				for (i = 0; i < 100; ++i) {
					if (ata_status_irq(drive))
						break;
				}
				if (!ata_status_irq(drive)) {
					/* We are compleatly missing an error
					 * return path here.
					 * FIXME: We have only one? -alat
					 */
					printk(KERN_ERR "DISASTER WAITING TO HAPPEN! Try to Stop it!\n");
					return ata_error(drive, rq, __FUNCTION__);
				}

				/* will set handler for us */
				return ar->XXX_handler(drive, rq);
			}
		}
	} else {
		/*
		 * FIXME: This is a gross hack, need to unify tcq dma proc and
		 * regular dma proc. It should now be easier.
		 *
		 * FIXME: Handle the alternateives by a command type.
		 */

		/* FIXME: ATA_OP_CONTINUES?  --bzolnier */
		/* Not started a request - BUG() ot ATA_OP_FINISHED to avoid lockup ? - alat*/
		if (!drive->using_dma)
			return ATA_OP_CONTINUES;

		/* for dma commands we don't set the handler */
		if (cmd == WIN_WRITEDMA ||
		    cmd == WIN_WRITEDMA_EXT ||
		    cmd == WIN_READDMA ||
		    cmd == WIN_READDMA_EXT)
			return udma_init(drive, rq);
#ifdef CONFIG_BLK_DEV_IDE_TCQ
		else if (cmd == WIN_WRITEDMA_QUEUED ||
			 cmd == WIN_WRITEDMA_QUEUED_EXT ||
			 cmd == WIN_READDMA_QUEUED ||
			 cmd == WIN_READDMA_QUEUED_EXT)
			return udma_tcq_init(drive, rq);
#endif
		else {
			printk(KERN_ERR "%s: unknown command %x\n",
					__FUNCTION__, cmd);

			return ATA_OP_FINISHED;
		}
	}

	/* not reached */
	return ATA_OP_CONTINUES;
}

static int idedisk_open(struct inode *inode, struct file *__fp, struct ata_device *drive)
{
	MOD_INC_USE_COUNT;
	if (drive->removable && drive->usage == 1) {
		check_disk_change(inode->i_rdev);

		/*
		 * Ignore the return code from door_lock, since the open() has
		 * already succeeded once, and the door_lock is irrelevant at this
		 * time.
		 */
		if (drive->doorlocking) {
			struct ata_taskfile args;

			memset(&args, 0, sizeof(args));
			args.cmd = WIN_DOORLOCK;
			if (ide_raw_taskfile(drive, &args, NULL))
				drive->doorlocking = 0;
		}
	}

	return 0;
}

static int flush_cache(struct ata_device *drive)
{
	struct ata_taskfile args;

	memset(&args, 0, sizeof(args));

	if (drive->id->cfs_enable_2 & 0x2400)
		args.cmd = WIN_FLUSH_CACHE_EXT;
	else
		args.cmd = WIN_FLUSH_CACHE;

	return ide_raw_taskfile(drive, &args, NULL);
}

static void idedisk_release(struct inode *inode, struct file *filp, struct ata_device *drive)
{
	if (drive->removable && !drive->usage) {
		/* XXX I don't think this is up to the lowlevel drivers..  --hch */
		invalidate_bdev(inode->i_bdev, 0);

		if (drive->doorlocking) {
			struct ata_taskfile args;

			memset(&args, 0, sizeof(args));
			args.cmd = WIN_DOORUNLOCK;
			if (ide_raw_taskfile(drive, &args, NULL))
				drive->doorlocking = 0;
		}
	}
	if ((drive->id->cfs_enable_2 & 0x3000) && drive->wcache)
		if (flush_cache(drive))
			printk (KERN_INFO "%s: Write Cache FAILED Flushing!\n",
				drive->name);
	MOD_DEC_USE_COUNT;
}

static int idedisk_check_media_change(struct ata_device *drive)
{
	/* if removable, always assume it was changed */
	return drive->removable;
}

static sector_t idedisk_capacity(struct ata_device *drive)
{
	return drive->capacity - drive->sect0;
}

/*
 * This is tightly woven into the driver->special can not touch.
 * DON'T do it again until a total personality rewrite is committed.
 */
static int set_multcount(struct ata_device *drive, int arg)
{
	struct ata_taskfile args;

	/* Setting multi mode count on this channel type is not supported/not
	 * handled.
	 */
	if (IS_PDC4030_DRIVE)
		return -EIO;

	/* Hugh, we still didn't detect the devices capabilities.
	 */
	if (!drive->id)
		return -EIO;

	if (arg > drive->id->max_multsect)
		arg = drive->id->max_multsect;

	memset(&args, 0, sizeof(args));
	args.taskfile.sector_count = arg;
	args.cmd = WIN_SETMULT;
	if (!ide_raw_taskfile(drive, &args, NULL)) {
		/* all went well track this setting as valid */
		drive->mult_count = arg;

		return 0;
	} else
		drive->mult_count = 0; /* reset */

	return -EIO;
}

static int set_nowerr(struct ata_device *drive, int arg)
{
	drive->nowerr = arg;
	drive->bad_wstat = arg ? BAD_R_STAT : BAD_W_STAT;

	return 0;
}

static int write_cache(struct ata_device *drive, int arg)
{
	struct ata_taskfile args;

	if (!(drive->id->cfs_enable_2 & 0x3000))
		return 1;

	memset(&args, 0, sizeof(args));
	args.taskfile.feature	= (arg) ? SETFEATURES_EN_WCACHE : SETFEATURES_DIS_WCACHE;
	args.cmd = WIN_SETFEATURES;
	ide_raw_taskfile(drive, &args, NULL);

	drive->wcache = arg;

	return 0;
}

static int idedisk_standby(struct ata_device *drive)
{
	struct ata_taskfile args;

	memset(&args, 0, sizeof(args));
	args.cmd = WIN_STANDBYNOW1;
	return ide_raw_taskfile(drive, &args, NULL);
}

static int set_acoustic(struct ata_device *drive, int arg)
{
	struct ata_taskfile args;

	memset(&args, 0, sizeof(args));
	args.taskfile.feature = (arg)?SETFEATURES_EN_AAM:SETFEATURES_DIS_AAM;
	args.taskfile.sector_count = arg;
	args.cmd = WIN_SETFEATURES;
	ide_raw_taskfile(drive, &args, NULL);

	drive->acoustic = arg;

	return 0;
}

#ifdef CONFIG_BLK_DEV_IDE_TCQ
static int set_using_tcq(struct ata_device *drive, int arg)
{
	if (!drive->driver)
		return -EPERM;

	if (!drive->channel->udma_setup)
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

	if (udma_tcq_enable(drive, arg))
		return -EIO;

	return 0;
}
#endif

static int probe_lba_addressing(struct ata_device *drive, int arg)
{
	drive->addressing =  0;

	if (!(drive->id->cfs_enable_2 & 0x0400))
                return -EIO;

	drive->addressing = arg;
	return 0;
}

static int set_lba_addressing(struct ata_device *drive, int arg)
{
	return (probe_lba_addressing(drive, arg));
}

static int idedisk_suspend(struct device *dev, u32 state, u32 level)
{
	struct ata_device *drive = dev->driver_data;

	/* I hope that every freeze operations from the upper levels have
	 * already been done...
	 */

	BUG_ON(in_interrupt());

	if (level != SUSPEND_SAVE_STATE)
		return 0;

	/* wait until all commands are finished */
	/* FIXME: waiting for spinlocks should be done instead. */
	while (drive->channel->handler)
		yield();

	/* set the drive to standby */
	printk(KERN_INFO "suspending: %s ", drive->name);
	if (ata_ops(drive)) {
		if (ata_ops(drive)->standby)
			ata_ops(drive)->standby(drive);
	}
	drive->blocked = 1;

	return 0;
}

static int idedisk_resume(struct device *dev, u32 level)
{
	struct ata_device *drive = dev->driver_data;

	if (level != RESUME_RESTORE_STATE)
		return 0;
	if (!drive->blocked)
		panic("ide: Resume but not suspended?\n");

	drive->blocked = 0;
	return 0;
}


/* This is just a hook for the overall driver tree.
 */

static struct device_driver disk_devdrv = {
	.lock = RW_LOCK_UNLOCKED,
	.suspend = idedisk_suspend,
	.resume = idedisk_resume,
};

/*
 * Queries for true maximum capacity of the drive.
 * Returns maximum LBA address (> 0) of the drive, 0 if failed.
 */
static unsigned long native_max_address(struct ata_device *drive)
{
	struct ata_taskfile args;
	unsigned long addr = 0;

	if (!(drive->id->command_set_1 & 0x0400) &&
	    !(drive->id->cfs_enable_2 & 0x0100))
		return addr;

	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(args));
	args.taskfile.device_head = 0x40;
	args.cmd = WIN_READ_NATIVE_MAX;
	ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if (!(drive->status & ERR_STAT)) {
		addr = ((args.taskfile.device_head & 0x0f) << 24)
		     | (args.taskfile.high_cylinder << 16)
		     | (args.taskfile.low_cylinder <<  8)
		     | args.taskfile.sector_number;
	}

	addr++;	/* since the return value is (maxlba - 1), we add 1 */

	return addr;
}

static u64 native_max_address_ext(struct ata_device *drive)
{
	struct ata_taskfile args;
	u64 addr = 0;

	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(args));
	args.taskfile.device_head = 0x40;
	args.cmd = WIN_READ_NATIVE_MAX_EXT;
        ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if (!(drive->status & ERR_STAT)) {
		u32 high = (args.hobfile.high_cylinder << 16) |
			   (args.hobfile.low_cylinder << 8) |
			    args.hobfile.sector_number;
		u32 low  = (args.taskfile.high_cylinder << 16) |
			   (args.taskfile.low_cylinder << 8) |
			    args.taskfile.sector_number;
		addr = ((u64)high << 24) | low;
	}

	addr++;	/* since the return value is (maxlba - 1), we add 1 */

	return addr;
}

#ifdef CONFIG_IDEDISK_STROKE
/*
 * Sets maximum virtual LBA address of the drive.
 * Returns new maximum virtual LBA address (> 0) or 0 on failure.
 */
static sector_t set_max_address(struct ata_device *drive, sector_t addr_req)
{
	struct ata_taskfile args;
	sector_t addr_set = 0;

	addr_req--;
	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(args));

	args.taskfile.sector_number = (addr_req >> 0);
	args.taskfile.low_cylinder = (addr_req >> 8);
	args.taskfile.high_cylinder = (addr_req >> 16);

	args.taskfile.device_head = ((addr_req >> 24) & 0x0f) | 0x40;
	args.cmd = WIN_SET_MAX;
	ide_raw_taskfile(drive, &args, NULL);

	/* if OK, read new maximum address value */
	if (!(drive->status & ERR_STAT)) {
		addr_set = ((args.taskfile.device_head & 0x0f) << 24)
			 | (args.taskfile.high_cylinder << 16)
			 | (args.taskfile.low_cylinder <<  8)
			 | args.taskfile.sector_number;
	}
	addr_set++;
	return addr_set;
}

static u64 set_max_address_ext(struct ata_device *drive, u64 addr_req)
{
	struct ata_taskfile args;
	u64 addr_set = 0;

	addr_req--;
	/* Create IDE/ATA command request structure */
	memset(&args, 0, sizeof(args));

	args.taskfile.sector_number = (addr_req >>  0);
	args.taskfile.low_cylinder = (addr_req >>= 8);
	args.taskfile.high_cylinder = (addr_req >>= 8);
	args.taskfile.device_head = 0x40;
	args.cmd = WIN_SET_MAX_EXT;

	args.hobfile.sector_number = (addr_req >>= 8);
	args.hobfile.low_cylinder = (addr_req >>= 8);
	args.hobfile.high_cylinder = (addr_req >>= 8);
	args.hobfile.device_head = 0x40;

	ide_raw_taskfile(drive, &args, NULL);

	/* if OK, compute maximum address value */
	if (!(drive->status & ERR_STAT)) {
		u32 high = (args.hobfile.high_cylinder << 16) |
			   (args.hobfile.low_cylinder << 8) |
			    args.hobfile.sector_number;
		u32 low  = (args.taskfile.high_cylinder << 16) |
			   (args.taskfile.low_cylinder << 8) |
			    args.taskfile.sector_number;
		addr_set = ((u64)high << 24) | low;
	}
	return addr_set;
}

#endif

static void idedisk_setup(struct ata_device *drive)
{
	int i;

	struct hd_driveid *id = drive->id;
	sector_t capacity;
	sector_t set_max;
	int drvid = -1;
	struct ata_channel *ch = drive->channel;

	if (id == NULL)
		return;

	/*
	 * CompactFlash cards and their brethern look just like hard drives
	 * to us, but they are removable and don't have a doorlock mechanism.
	 */
	if (drive->removable && !drive_is_flashcard(drive)) {
		/* Removable disks (eg. SYQUEST); ignore 'WD' drives.
		 */
		if (!strncmp(id->model, "WD", 2))
			drive->doorlocking = 1;
	}

	for (i = 0; i < MAX_DRIVES; ++i) {
		if (drive != &ch->drives[i])
		    continue;
		drvid = i;
		ch->gd->de_arr[i] = drive->de;
		if (drive->removable)
			ch->gd->flags[i] |= GENHD_FL_REMOVABLE;
		break;
	}

	/* Register us within the device tree.
	 */
	if (drvid != -1) {
		sprintf(drive->dev.bus_id, "sd@%x,%x", ch->unit, drvid);
		strcpy(drive->dev.name, "ATA-Disk");
		drive->dev.driver = &disk_devdrv;
		drive->dev.parent = &ch->dev;
		drive->dev.driver_data = drive;
		device_register(&drive->dev);
	}

	/* Extract geometry if we did not already have one for the drive */
	if (!drive->cyl || !drive->head || !drive->sect) {
		drive->cyl     = drive->bios_cyl  = id->cyls;
		drive->head    = drive->bios_head = id->heads;
		drive->sect    = drive->bios_sect = id->sectors;
	}

	/* Handle logical geometry translation by the drive. */
	if ((id->field_valid & 1) && id->cur_cyls &&
	    id->cur_heads && (id->cur_heads <= 16) && id->cur_sectors) {
		drive->cyl  = id->cur_cyls;
		drive->head = id->cur_heads;
		drive->sect = id->cur_sectors;
	}

	/* Use physical geometry if what we have still makes no sense. */
	if (drive->head > 16 && id->heads && id->heads <= 16) {
		drive->cyl  = id->cyls;
		drive->head = id->heads;
		drive->sect = id->sectors;
	}

	/* Calculate drive capacity, and select LBA if possible.
	 * drive->id != NULL is spected
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
	capacity = drive->cyl * drive->head * drive->sect;
	set_max = native_max_address(drive);

	drive->capacity = 0;
	drive->select.b.lba = 0;

	if (id->cfs_enable_2 & 0x0400) {
		u64 set_max_ext;
		u64 capacity_2;
		capacity_2 = capacity;
		capacity_2 = id->lba_capacity_2;

		drive->cyl = (unsigned int) capacity_2 / (drive->head * drive->sect);
		drive->head = drive->bios_head = 255;
		drive->sect = drive->bios_sect = 63;

		drive->select.b.lba = 1;
		set_max_ext = native_max_address_ext(drive);
		if (set_max_ext > capacity_2) {
#ifdef CONFIG_IDEDISK_STROKE
			set_max_ext = native_max_address_ext(drive);
			set_max_ext = set_max_address_ext(drive, set_max_ext);
			if (set_max_ext) {
				drive->capacity = capacity_2 = set_max_ext;
				drive->cyl = (unsigned int) set_max_ext / (drive->head * drive->sect);
				drive->select.b.lba = 1;
				drive->id->lba_capacity_2 = capacity_2;
                        }
#else
			printk("%s: setmax_ext LBA %llu, native  %llu\n",
				drive->name,
			       (long long) set_max_ext,
			       (long long) capacity_2);
#endif
		}
		drive->bios_cyl	= drive->cyl;
		drive->capacity	= capacity_2;
	} else {

		/*
		 * Determine capacity, and use LBA if the drive properly
		 * supports it.
		 */

		if ((id->capability & 2) && lba_capacity_is_ok(id)) {
			capacity = id->lba_capacity;
			drive->cyl = capacity / (drive->head * drive->sect);
			drive->select.b.lba = 1;
		}

		if (set_max > capacity) {
#ifdef CONFIG_IDEDISK_STROKE
			set_max = native_max_address(drive);
			set_max = set_max_address(drive, set_max);
			if (set_max) {
				drive->capacity = capacity = set_max;
				drive->cyl = set_max / (drive->head * drive->sect);
				drive->select.b.lba = 1;
				drive->id->lba_capacity = capacity;
			}
#else
			printk("%s: setmax LBA %lu, native  %lu\n",
					drive->name, set_max, capacity);
#endif
		}

		drive->capacity = capacity;

		if ((id->command_set_2 & 0x0400) && (id->cfs_enable_2 & 0x0400)) {
			drive->capacity = id->lba_capacity_2;
			drive->head = 255;
			drive->sect = 63;
			drive->cyl = (unsigned long)(drive->capacity) / (drive->head * drive->sect);
		}
	}

	/*
	 * If possible, give fdisk access to more of the drive,
	 * by correcting bios_cyls:
	 */
	capacity = idedisk_capacity(drive);
	if ((capacity >= (drive->bios_cyl * drive->bios_sect * drive->bios_head)) &&
	    (!drive->forced_geom) && drive->bios_sect && drive->bios_head)
		drive->bios_cyl = (capacity / drive->bios_sect) / drive->bios_head;
	printk(KERN_INFO " %s: %ld sectors", drive->name, capacity);

#if 0
	/* Right now we avoid this calculation, since it can result in the
	 * usage of not supported compiler internal functions on 32 bit hosts.
	 * However since the calculation appears to be an interesting piece of
	 * number theory let's preserve the formula here.
	 */

	/* Give size in megabytes (MB), not mebibytes (MiB).
	 * We compute the exact rounded value, avoiding overflow.
	 */
	printk(" (%ld MB)", (capacity - capacity/625 + 974)/1950);
#endif

	/* Only print cache size when it was specified.
	 */
	if (id->buf_size)
		printk (" w/%dKiB Cache", id->buf_size/2);

	printk(", CHS=%d/%d/%d",
	       drive->bios_cyl, drive->bios_head, drive->bios_sect);
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (drive->using_dma)
		udma_print(drive);
#endif
	printk("\n");

	drive->mult_count = 0;
#if 0
	if (id->max_multsect) {

		/* FIXME: reenable this again after making it to use
		 * the same code path as the ioctl stuff.
		 */

#ifdef CONFIG_IDEDISK_MULTI_MODE
		id->multsect = ((id->max_multsect/2) > 1) ? id->max_multsect : 0;
		id->multsect_valid = id->multsect ? 1 : 0;
		drive->mult_req = id->multsect_valid ? id->max_multsect : INITIAL_MULT_COUNT;
		if (drive->mult_req)
			drive->special_cmd |= ATA_SPECIAL_MMODE;
#else
		/* original, pre IDE-NFG, per request of AC */
		drive->mult_req = INITIAL_MULT_COUNT;
		if (drive->mult_req > id->max_multsect)
			drive->mult_req = id->max_multsect;
		if (drive->mult_req || ((id->multsect_valid & 1) && id->multsect))
			drive->special_cmd |= ATA_SPECIAL_MMODE;
#endif
	}
#endif

	/* FIXME: Nowadays there are many chipsets out there which *require* 32
	 * bit IO. Those will most propably not work properly with drives not
	 * supporting this. But right now we don't do anything about this. We
	 * dont' even *warn* the user!
	 */

	drive->channel->no_io_32bit = id->dword_io ? 1 : 0;

	if (drive->id->cfs_enable_2 & 0x3000)
		write_cache(drive, (id->cfs_enable_2 & 0x3000));

	probe_lba_addressing(drive, 1);
}

static int idedisk_cleanup(struct ata_device *drive)
{
	int ret;

	if (!drive)
	    return 0;

	if ((drive->id->cfs_enable_2 & 0x3000) && drive->wcache) {
		if (flush_cache(drive))
			printk (KERN_INFO "%s: Write Cache FAILED Flushing!\n",
				drive->name);
	}
	ret = ata_unregister_device(drive);

	/* FIXME: This is killing the kernel with BUG 185 at asm/spinlocks.h
	 * horribly.  Check whatever we did REGISTER the device properly
	 * in front?
	 */
#if 0
	put_device(&drive->device);
#endif

	return ret;
}

static int idedisk_ioctl(struct ata_device *drive,
		struct inode *inode, struct file *__fp,
		unsigned int cmd, unsigned long arg)
{
	struct hd_driveid *id = drive->id;

	switch (cmd) {
		case HDIO_GET_ADDRESS: {
			unsigned long val = drive->addressing;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;
			return 0;
		}

		case HDIO_SET_ADDRESS: {
			int val;

			if (arg < 0 || arg > 2)
				return -EINVAL;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			val = set_lba_addressing(drive, arg);
			spin_unlock_irq(drive->channel->lock);

			return val;
		}

		case HDIO_GET_MULTCOUNT: {
			unsigned long val = drive->mult_count & 0xFF;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;
			return 0;
		}

		case HDIO_SET_MULTCOUNT: {
			int val;

			if (!id)
				return -EBUSY;

			if (arg < 0 || arg > (id ? id->max_multsect : 0))
				return -EINVAL;

			val = set_multcount(drive, arg);

			return val;
		}

		case HDIO_GET_NOWERR: {
			unsigned long val = drive->nowerr;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;

			return 0;
		}

		case HDIO_SET_NOWERR: {
			int val;

			if (arg < 0 || arg > 1)
				return -EINVAL;

			val = set_nowerr(drive, arg);

			return val;
		}

		case HDIO_GET_WCACHE: {
			unsigned long val = drive->wcache;

			if (put_user(val, (unsigned long *) arg))
				return -EFAULT;

			return 0;
		}

		case HDIO_SET_WCACHE: {
			int val;

			if (arg < 0 || arg > 1)
				return -EINVAL;

			val = write_cache(drive, arg);

			return val;
		}

		case HDIO_GET_ACOUSTIC: {
			unsigned long val = drive->acoustic;

			if (put_user(val, (u8 *) arg))
				return -EFAULT;
			return 0;
		}

		case HDIO_SET_ACOUSTIC: {
			int val;

			if (arg < 0 || arg > 254)
				return -EINVAL;

			val = set_acoustic(drive, arg);

			return val;
		}

#ifdef CONFIG_BLK_DEV_IDE_TCQ
		case HDIO_GET_QDMA: {
			/* Foolup hdparm 0 means off 1 on -alat */
			/* FIXME: hdparm have only -Q do we need something like:
			 * hdparm -q 1/0 - TCQ on/off
			 * hdparm -Q 1-MAX - TCQ queue_depth ?
			 */
			u8 val = ( drive->using_tcq ?  drive->queue_depth : 0 );

			if (put_user(val, (u8 *) arg))
				return -EFAULT;

			return 0;
		}

		case HDIO_SET_QDMA: {
			int val;

			if (arg < 0 || arg > IDE_MAX_TAG)
				return -EINVAL;

			if (ide_spin_wait_hwgroup(drive))
				return -EBUSY;

			val = set_using_tcq(drive, arg);
			spin_unlock_irq(drive->channel->lock);

			return val;
		}
#endif
		default:
			return -EINVAL;
	}
}

static void idedisk_attach(struct ata_device *drive);

/*
 * Subdriver functions.
 */
static struct ata_operations idedisk_driver = {
	.owner =		THIS_MODULE,
	.attach =		idedisk_attach,
	.cleanup =		idedisk_cleanup,
	.standby =		idedisk_standby,
	.do_request =		idedisk_do_request,
	.end_request =		NULL,
	.ioctl =		idedisk_ioctl,
	.open =			idedisk_open,
	.release =		idedisk_release,
	.check_media_change =	idedisk_check_media_change,
	.revalidate =		NULL, /* use default method */
	.capacity =		idedisk_capacity,
};

static void idedisk_attach(struct ata_device *drive)
{
	char *req;
	struct ata_channel *channel;
	int unit;

	if (drive->type != ATA_DISK)
		return;

	req = drive->driver_req;
	if (req[0] != '\0' && strcmp(req, "ide-disk"))
		return;

	if (ata_register_device(drive, &idedisk_driver)) {
		printk(KERN_ERR "%s: Failed to register the driver with ide.c\n", drive->name);
		return;
	}

	idedisk_setup(drive);
	if ((!drive->head || drive->head > 16) && !drive->select.b.lba) {
		printk(KERN_ERR "%s: INVALID GEOMETRY: %d PHYSICAL HEADS?\n", drive->name, drive->head);
		idedisk_cleanup(drive);
		return;
	}

	channel = drive->channel;
	unit = drive - channel->drives;

	ata_revalidate(mk_kdev(channel->major, unit << PARTN_BITS));
}

static void __exit idedisk_exit(void)
{
	unregister_ata_driver(&idedisk_driver);
}

int __init idedisk_init(void)
{
	return ata_driver_module(&idedisk_driver);
}

module_init(idedisk_init);
module_exit(idedisk_exit);

MODULE_DESCRIPTION("ATA DISK Driver");
MODULE_LICENSE("GPL");
