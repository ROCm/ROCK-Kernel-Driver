/*
 * linux/drivers/ide/ide-taskfile.c	Version 0.38	March 05, 2003
 *
 *  Copyright (C) 2000-2002	Michael Cornwell <cornwell@acm.org>
 *  Copyright (C) 2000-2002	Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2001-2002	Klaus Smolin
 *					IBM Storage Technology Division
 *  Copyright (C) 2003		Bartlomiej Zolnierkiewicz
 *
 *  The big the bad and the ugly.
 *
 *  Problems to be fixed because of BH interface or the lack therefore.
 *
 *  Fill me in stupid !!!
 *
 *  HOST:
 *	General refers to the Controller and Driver "pair".
 *  DATA HANDLER:
 *	Under the context of Linux it generally refers to an interrupt handler.
 *	However, it correctly describes the 'HOST'
 *  DATA BLOCK:
 *	The amount of data needed to be transfered as predefined in the
 *	setup of the device.
 *  STORAGE ATOMIC:
 *	The 'DATA BLOCK' associated to the 'DATA HANDLER', and can be as
 *	small as a single sector or as large as the entire command block
 *	request.
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
#include <linux/blkpg.h>
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
#define DTF(x...) printk(x)
#else
#define DTF(x...)
#endif

static void ata_bswap_data (void *buffer, int wcount)
{
	u16 *p = buffer;

	while (wcount--) {
		*p = *p << 8 | *p >> 8; p++;
		*p = *p << 8 | *p >> 8; p++;
	}
}


void taskfile_input_data (ide_drive_t *drive, void *buffer, u32 wcount)
{
	HWIF(drive)->ata_input_data(drive, buffer, wcount);
	if (drive->bswap)
		ata_bswap_data(buffer, wcount);
}

EXPORT_SYMBOL(taskfile_input_data);

void taskfile_output_data (ide_drive_t *drive, void *buffer, u32 wcount)
{
	if (drive->bswap) {
		ata_bswap_data(buffer, wcount);
		HWIF(drive)->ata_output_data(drive, buffer, wcount);
		ata_bswap_data(buffer, wcount);
	} else {
		HWIF(drive)->ata_output_data(drive, buffer, wcount);
	}
}

EXPORT_SYMBOL(taskfile_output_data);

int taskfile_lib_get_identify (ide_drive_t *drive, u8 *buf)
{
	ide_task_t args;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	if (drive->media == ide_disk)
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_IDENTIFY;
	else
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_PIDENTIFY;
	args.command_type = IDE_DRIVE_TASK_IN;
	args.handler	  = &task_in_intr;
	return ide_raw_taskfile(drive, &args, buf);
}

EXPORT_SYMBOL(taskfile_lib_get_identify);

#ifdef CONFIG_IDE_TASK_IOCTL_DEBUG
void debug_taskfile (ide_drive_t *drive, ide_task_t *args)
{
	printk(KERN_INFO "%s: ", drive->name);
//	printk("TF.0=x%02x ", args->tfRegister[IDE_DATA_OFFSET]);
	printk("TF.1=x%02x ", args->tfRegister[IDE_FEATURE_OFFSET]);
	printk("TF.2=x%02x ", args->tfRegister[IDE_NSECTOR_OFFSET]);
	printk("TF.3=x%02x ", args->tfRegister[IDE_SECTOR_OFFSET]);
	printk("TF.4=x%02x ", args->tfRegister[IDE_LCYL_OFFSET]);
	printk("TF.5=x%02x ", args->tfRegister[IDE_HCYL_OFFSET]);
	printk("TF.6=x%02x ", args->tfRegister[IDE_SELECT_OFFSET]);
	printk("TF.7=x%02x\n", args->tfRegister[IDE_COMMAND_OFFSET]);
	printk(KERN_INFO "%s: ", drive->name);
//	printk("HTF.0=x%02x ", args->hobRegister[IDE_DATA_OFFSET]);
	printk("HTF.1=x%02x ", args->hobRegister[IDE_FEATURE_OFFSET]);
	printk("HTF.2=x%02x ", args->hobRegister[IDE_NSECTOR_OFFSET]);
	printk("HTF.3=x%02x ", args->hobRegister[IDE_SECTOR_OFFSET]);
	printk("HTF.4=x%02x ", args->hobRegister[IDE_LCYL_OFFSET]);
	printk("HTF.5=x%02x ", args->hobRegister[IDE_HCYL_OFFSET]);
	printk("HTF.6=x%02x ", args->hobRegister[IDE_SELECT_OFFSET]);
	printk("HTF.7=x%02x\n", args->hobRegister[IDE_CONTROL_OFFSET_HOB]);
}
#endif /* CONFIG_IDE_TASK_IOCTL_DEBUG */

ide_startstop_t do_rw_taskfile (ide_drive_t *drive, ide_task_t *task)
{
	ide_hwif_t *hwif	= HWIF(drive);
	task_struct_t *taskfile	= (task_struct_t *) task->tfRegister;
	hob_struct_t *hobfile	= (hob_struct_t *) task->hobRegister;
	u8 HIHI			= (drive->addressing == 1) ? 0xE0 : 0xEF;

#ifdef CONFIG_IDE_TASK_IOCTL_DEBUG
	void debug_taskfile(drive, task);
#endif /* CONFIG_IDE_TASK_IOCTL_DEBUG */

	/* ALL Command Block Executions SHALL clear nIEN, unless otherwise */
	if (IDE_CONTROL_REG) {
		/* clear nIEN */
		hwif->OUTB(drive->ctl, IDE_CONTROL_REG);
	}
	SELECT_MASK(drive, 0);

	if (drive->addressing == 1) {
		hwif->OUTB(hobfile->feature, IDE_FEATURE_REG);
		hwif->OUTB(hobfile->sector_count, IDE_NSECTOR_REG);
		hwif->OUTB(hobfile->sector_number, IDE_SECTOR_REG);
		hwif->OUTB(hobfile->low_cylinder, IDE_LCYL_REG);
		hwif->OUTB(hobfile->high_cylinder, IDE_HCYL_REG);
	}

	hwif->OUTB(taskfile->feature, IDE_FEATURE_REG);
	hwif->OUTB(taskfile->sector_count, IDE_NSECTOR_REG);
	hwif->OUTB(taskfile->sector_number, IDE_SECTOR_REG);
	hwif->OUTB(taskfile->low_cylinder, IDE_LCYL_REG);
	hwif->OUTB(taskfile->high_cylinder, IDE_HCYL_REG);

	hwif->OUTB((taskfile->device_head & HIHI) | drive->select.all, IDE_SELECT_REG);
#ifdef CONFIG_IDE_TASKFILE_IO
	if (task->handler != NULL) {
		if (task->prehandler != NULL) {
			hwif->OUTBSYNC(drive, taskfile->command, IDE_COMMAND_REG);
			ndelay(400);	/* FIXME */
			return task->prehandler(drive, task->rq);
		}
		ide_execute_command(drive, taskfile->command, task->handler, WAIT_WORSTCASE, NULL);
		return ide_started;
	}
#else
	if (task->handler != NULL) {
		ide_execute_command(drive, taskfile->command, task->handler, WAIT_WORSTCASE, NULL);
		if (task->prehandler != NULL)
			return task->prehandler(drive, task->rq);
		return ide_started;
	}
#endif

	if (!drive->using_dma)
		return ide_stopped;

	switch (taskfile->command) {
		case WIN_WRITEDMA_ONCE:
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_EXT:
			if (!hwif->ide_dma_write(drive))
				return ide_started;
			break;
		case WIN_READDMA_ONCE:
		case WIN_READDMA:
		case WIN_READDMA_EXT:
		case WIN_IDENTIFY_DMA:
			if (!hwif->ide_dma_read(drive))
				return ide_started;
			break;
		default:
			if (task->handler == NULL)
				return ide_stopped;
	}

	return ide_stopped;
}

EXPORT_SYMBOL(do_rw_taskfile);

/*
 * set_multmode_intr() is invoked on completion of a WIN_SETMULT cmd.
 */
ide_startstop_t set_multmode_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u8 stat;

	if (OK_STAT(stat = hwif->INB(IDE_STATUS_REG),READY_STAT,BAD_STAT)) {
		drive->mult_count = drive->mult_req;
	} else {
		drive->mult_req = drive->mult_count = 0;
		drive->special.b.recalibrate = 1;
		(void) ide_dump_status(drive, "set_multmode", stat);
	}
	return ide_stopped;
}

EXPORT_SYMBOL(set_multmode_intr);

/*
 * set_geometry_intr() is invoked on completion of a WIN_SPECIFY cmd.
 */
ide_startstop_t set_geometry_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	int retries = 5;
	u8 stat;

	while (((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) && retries--)
		udelay(10);

	if (OK_STAT(stat, READY_STAT, BAD_STAT))
		return ide_stopped;

	if (stat & (ERR_STAT|DRQ_STAT))
		return DRIVER(drive)->error(drive, "set_geometry_intr", stat);

	if (HWGROUP(drive)->handler != NULL)
		BUG();
	ide_set_handler(drive, &set_geometry_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

EXPORT_SYMBOL(set_geometry_intr);

/*
 * recal_intr() is invoked on completion of a WIN_RESTORE (recalibrate) cmd.
 */
ide_startstop_t recal_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u8 stat;

	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG), READY_STAT, BAD_STAT))
		return DRIVER(drive)->error(drive, "recal_intr", stat);
	return ide_stopped;
}

EXPORT_SYMBOL(recal_intr);

/*
 * Handler for commands without a data phase
 */
ide_startstop_t task_no_data_intr (ide_drive_t *drive)
{
	ide_task_t *args	= HWGROUP(drive)->rq->special;
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat;

	local_irq_enable();
	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG),READY_STAT,BAD_STAT)) {
		DTF("%s: command opcode 0x%02x\n", drive->name,
			args->tfRegister[IDE_COMMAND_OFFSET]);
		return DRIVER(drive)->error(drive, "task_no_data_intr", stat);
		/* calls ide_end_drive_cmd */
	}
	if (args)
		ide_end_drive_cmd(drive, stat, hwif->INB(IDE_ERROR_REG));

	return ide_stopped;
}

EXPORT_SYMBOL(task_no_data_intr);

/*
 * old taskfile PIO handlers, to be killed as soon as possible.
 */
#ifndef CONFIG_IDE_TASKFILE_IO

#define task_map_rq(rq, flags)		ide_map_buffer((rq), (flags))
#define task_unmap_rq(rq, buf, flags)	ide_unmap_buffer((rq), (buf), (flags))

/*
 * Handler for command with PIO data-in phase, READ
 */
/*
 * FIXME before 2.4 enable ...
 *	DATA integrity issue upon error. <andre@linux-ide.org>
 */
ide_startstop_t task_in_intr (ide_drive_t *drive)
{
	struct request *rq	= HWGROUP(drive)->rq;
	ide_hwif_t *hwif	= HWIF(drive);
	char *pBuf		= NULL;
	u8 stat;
	unsigned long flags;

	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG),DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
#if 0
			DTF("%s: attempting to recover last " \
				"sector counter status=0x%02x\n",
				drive->name, stat);
			/*
			 * Expect a BUG BOMB if we attempt to rewind the
			 * offset in the BH aka PAGE in the current BLOCK
			 * segment.  This is different than the HOST segment.
			 */
#endif
			if (!rq->bio)
				rq->current_nr_sectors++;
			return DRIVER(drive)->error(drive, "task_in_intr", stat);
		}
		if (!(stat & BUSY_STAT)) {
			DTF("task_in_intr to Soon wait for next interrupt\n");
			if (HWGROUP(drive)->handler == NULL)
				ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);
			return ide_started;  
		}
	}

	pBuf = task_map_rq(rq, &flags);
	DTF("Read: %p, rq->current_nr_sectors: %d, stat: %02x\n",
		pBuf, (int) rq->current_nr_sectors, stat);
	taskfile_input_data(drive, pBuf, SECTOR_WORDS);
	task_unmap_rq(rq, pBuf, &flags);
	/*
	 * FIXME :: We really can not legally get a new page/bh
	 * regardless, if this is the end of our segment.
	 * BH walking or segment can only be updated after we have a good
	 * hwif->INB(IDE_STATUS_REG); return.
	 */
	if (--rq->current_nr_sectors <= 0)
		if (!DRIVER(drive)->end_request(drive, 1, 0))
			return ide_stopped;
	/*
	 * ERM, it is techincally legal to leave/exit here but it makes
	 * a mess of the code ...
	 */
	if (HWGROUP(drive)->handler == NULL)
		ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

EXPORT_SYMBOL(task_in_intr);

/*
 * Handler for command with Read Multiple
 */
ide_startstop_t task_mulin_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned int msect	= drive->mult_count;
	unsigned int nsect;
	unsigned long flags;
	u8 stat;

	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG),DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			if (!rq->bio) {
				rq->current_nr_sectors += drive->mult_count;
				/*
				 * NOTE: could rewind beyond beginning :-/
				 */
			} else {
				printk(KERN_ERR "%s: MULTI-READ assume all data " \
					"transfered is bad status=0x%02x\n",
					drive->name, stat);
			}
			return DRIVER(drive)->error(drive, "task_mulin_intr", stat);
		}
		/* no data yet, so wait for another interrupt */
		if (HWGROUP(drive)->handler == NULL)
			ide_set_handler(drive, &task_mulin_intr, WAIT_WORSTCASE, NULL);
		return ide_started;
	}

	do {
		nsect = rq->current_nr_sectors;
		if (nsect > msect)
			nsect = msect;
		pBuf = task_map_rq(rq, &flags);
		DTF("Multiread: %p, nsect: %d, msect: %d, " \
			" rq->current_nr_sectors: %d\n",
			pBuf, nsect, msect, rq->current_nr_sectors);
		taskfile_input_data(drive, pBuf, nsect * SECTOR_WORDS);
		task_unmap_rq(rq, pBuf, &flags);
		rq->errors = 0;
		rq->current_nr_sectors -= nsect;
		msect -= nsect;
		/*
		 * FIXME :: We really can not legally get a new page/bh
		 * regardless, if this is the end of our segment.
		 * BH walking or segment can only be updated after we have a
		 * good hwif->INB(IDE_STATUS_REG); return.
		 */
		if (!rq->current_nr_sectors) {
			if (!DRIVER(drive)->end_request(drive, 1, 0))
				return ide_stopped;
		}
	} while (msect);
	if (HWGROUP(drive)->handler == NULL)
		ide_set_handler(drive, &task_mulin_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

EXPORT_SYMBOL(task_mulin_intr);

/*
 * VERIFY ME before 2.4 ... unexpected race is possible based on details
 * RMK with 74LS245/373/374 TTL buffer logic because of passthrough.
 */
ide_startstop_t pre_task_out_intr (ide_drive_t *drive, struct request *rq)
{
	char *pBuf		= NULL;
	unsigned long flags;
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, DATA_READY,
			drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing WRITE%s\n",
			drive->name,
			drive->addressing ? "_EXT" : "");
		return startstop;
	}
	/* For Write_sectors we need to stuff the first sector */
	pBuf = task_map_rq(rq, &flags);
	taskfile_output_data(drive, pBuf, SECTOR_WORDS);
	rq->current_nr_sectors--;
	task_unmap_rq(rq, pBuf, &flags);
	return ide_started;
}

EXPORT_SYMBOL(pre_task_out_intr);

/*
 * Handler for command with PIO data-out phase WRITE
 *
 * WOOHOO this is a CORRECT STATE DIAGRAM NOW, <andre@linux-ide.org>
 */
ide_startstop_t task_out_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned long flags;
	u8 stat;

	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG), DRIVE_READY, drive->bad_wstat)) {
		DTF("%s: WRITE attempting to recover last " \
			"sector counter status=0x%02x\n",
			drive->name, stat);
		rq->current_nr_sectors++;
		return DRIVER(drive)->error(drive, "task_out_intr", stat);
	}
	/*
	 * Safe to update request for partial completions.
	 * We have a good STATUS CHECK!!!
	 */
	if (!rq->current_nr_sectors)
		if (!DRIVER(drive)->end_request(drive, 1, 0))
			return ide_stopped;
	if ((rq->current_nr_sectors==1) ^ (stat & DRQ_STAT)) {
		rq = HWGROUP(drive)->rq;
		pBuf = task_map_rq(rq, &flags);
		DTF("write: %p, rq->current_nr_sectors: %d\n",
			pBuf, (int) rq->current_nr_sectors);
		taskfile_output_data(drive, pBuf, SECTOR_WORDS);
		task_unmap_rq(rq, pBuf, &flags);
		rq->errors = 0;
		rq->current_nr_sectors--;
	}
	if (HWGROUP(drive)->handler == NULL)
		ide_set_handler(drive, &task_out_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

EXPORT_SYMBOL(task_out_intr);

ide_startstop_t pre_task_mulout_intr (ide_drive_t *drive, struct request *rq)
{
	ide_task_t *args = rq->special;
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, DATA_READY,
			drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing %s\n",
			drive->name,
			drive->addressing ? "MULTWRITE_EXT" : "MULTWRITE");
		return startstop;
	}
	if (!(drive_is_ready(drive))) {
		int i;
		for (i=0; i<100; i++) {
			if (drive_is_ready(drive))
				break;
		}
	}

	/*
	 * WARNING :: if the drive as not acked good status we may not
	 * move the DATA-TRANSFER T-Bar as BSY != 0. <andre@linux-ide.org>
	 */
	return args->handler(drive);
}

EXPORT_SYMBOL(pre_task_mulout_intr);

/*
 * FIXME before enabling in 2.4 ... DATA integrity issue upon error.
 */
/*
 * Handler for command write multiple
 * Called directly from execute_drive_cmd for the first bunch of sectors,
 * afterwards only by the ISR
 */
ide_startstop_t task_mulout_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif		= HWIF(drive);
	u8 stat				= hwif->INB(IDE_STATUS_REG);
	struct request *rq		= HWGROUP(drive)->rq;
	char *pBuf			= NULL;
	ide_startstop_t startstop	= ide_stopped;
	unsigned int msect		= drive->mult_count;
	unsigned int nsect;
	unsigned long flags;

	/*
	 * (ks/hs): Handle last IRQ on multi-sector transfer,
	 * occurs after all data was sent in this chunk
	 */
	if (rq->current_nr_sectors == 0) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			if (!rq->bio) {
                                rq->current_nr_sectors += drive->mult_count;
				/*
				 * NOTE: could rewind beyond beginning :-/
				 */
			} else {
				printk(KERN_ERR "%s: MULTI-WRITE assume all data " \
					"transfered is bad status=0x%02x\n",
					drive->name, stat);
			}
			return DRIVER(drive)->error(drive, "task_mulout_intr", stat);
		}
		if (!rq->bio)
			DRIVER(drive)->end_request(drive, 1, 0);
		return startstop;
	}
	/*
	 * DON'T be lazy code the above and below togather !!!
	 */
	if (!OK_STAT(stat,DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			if (!rq->bio) {
				rq->current_nr_sectors += drive->mult_count;
				/*
				 * NOTE: could rewind beyond beginning :-/
				 */
			} else {
				printk("%s: MULTI-WRITE assume all data " \
					"transfered is bad status=0x%02x\n",
					drive->name, stat);
			}
			return DRIVER(drive)->error(drive, "task_mulout_intr", stat);
		}
		/* no data yet, so wait for another interrupt */
		if (HWGROUP(drive)->handler == NULL)
			ide_set_handler(drive, &task_mulout_intr, WAIT_WORSTCASE, NULL);
		return ide_started;
	}

	if (HWGROUP(drive)->handler != NULL) {
		unsigned long lflags;
		spin_lock_irqsave(&ide_lock, lflags);
		HWGROUP(drive)->handler = NULL;
		del_timer(&HWGROUP(drive)->timer);
		spin_unlock_irqrestore(&ide_lock, lflags);
	}

	do {
		nsect = rq->current_nr_sectors;
		if (nsect > msect)
			nsect = msect;
		pBuf = task_map_rq(rq, &flags);
		DTF("Multiwrite: %p, nsect: %d, msect: %d, " \
			"rq->current_nr_sectors: %ld\n",
			pBuf, nsect, msect, rq->current_nr_sectors);
		msect -= nsect;
		taskfile_output_data(drive, pBuf, nsect * SECTOR_WORDS);
		task_unmap_rq(rq, pBuf, &flags);
		rq->current_nr_sectors -= nsect;
		/*
		 * FIXME :: We really can not legally get a new page/bh
		 * regardless, if this is the end of our segment.
		 * BH walking or segment can only be updated after we
		 * have a good  hwif->INB(IDE_STATUS_REG); return.
		 */
		if (!rq->current_nr_sectors) {
			if (!DRIVER(drive)->end_request(drive, 1, 0))
				if (!rq->bio)
					return ide_stopped;
		}
	} while (msect);
	rq->errors = 0;
	if (HWGROUP(drive)->handler == NULL)
		ide_set_handler(drive, &task_mulout_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

EXPORT_SYMBOL(task_mulout_intr);

#else /* !CONFIG_IDE_TASKFILE_IO */

static u8 wait_drive_not_busy(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	int retries = 100;
	u8 stat;

	/*
	 * Last sector was transfered, wait until drive is ready.
	 * This can take up to 10 usec, but we will wait max 1 ms
	 * (drive_cmd_intr() waits that long).
	 */
	while (((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) && retries--)
		udelay(10);

	if (!retries)
		printk(KERN_ERR "%s: drive still BUSY!\n", drive->name);

	return stat;
}

/*
 * Handler for command with PIO data-in phase (Read).
 */
ide_startstop_t task_in_intr (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	u8 stat, good_stat;

	good_stat = DATA_READY;
	stat = HWIF(drive)->INB(IDE_STATUS_REG);
check_status:
	if (!OK_STAT(stat, good_stat, BAD_R_STAT)) {
		if (stat & (ERR_STAT | DRQ_STAT))
			return DRIVER(drive)->error(drive, __FUNCTION__, stat);
		/* BUSY_STAT: No data yet, so wait for another IRQ. */
		ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);
		return ide_started;
	}

	/*
	 * Complete previously submitted bios (if any).
	 * Status was already verifyied.
	 */
	while (rq->bio != rq->cbio)
		if (!DRIVER(drive)->end_request(drive, 1, bio_sectors(rq->bio)))
			return ide_stopped;
	/* Complete rq->buffer based request (ioctls). */
	if (!rq->bio && !rq->nr_sectors) {
		ide_end_drive_cmd(drive, stat, HWIF(drive)->INB(IDE_ERROR_REG));
		return ide_stopped;
	}

	rq->errors = 0;
	task_sectors(drive, rq, 1, IDE_PIO_IN);

	/* If it was the last datablock check status and finish transfer. */
	if (!rq->nr_sectors) {
		good_stat = 0;
		stat = wait_drive_not_busy(drive);
		goto check_status;
	}

	/* Still data left to transfer. */
	ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}
EXPORT_SYMBOL(task_in_intr);

/*
 * Handler for command with PIO data-in phase (Read Multiple).
 */
ide_startstop_t task_mulin_intr (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	unsigned int msect = drive->mult_count;
	unsigned int nsect;
	u8 stat, good_stat;

	good_stat = DATA_READY;
	stat = HWIF(drive)->INB(IDE_STATUS_REG);
check_status:
	if (!OK_STAT(stat, good_stat, BAD_R_STAT)) {
		if (stat & (ERR_STAT | DRQ_STAT))
			return DRIVER(drive)->error(drive, __FUNCTION__, stat);
		/* BUSY_STAT: No data yet, so wait for another IRQ. */
		ide_set_handler(drive, &task_mulin_intr, WAIT_WORSTCASE, NULL);
		return ide_started;
	}

	/*
	 * Complete previously submitted bios (if any).
	 * Status was already verifyied.
	 */
	while (rq->bio != rq->cbio)
		if (!DRIVER(drive)->end_request(drive, 1, bio_sectors(rq->bio)))
			return ide_stopped;
	/* Complete rq->buffer based request (ioctls). */
	if (!rq->bio && !rq->nr_sectors) {
		ide_end_drive_cmd(drive, stat, HWIF(drive)->INB(IDE_ERROR_REG));
		return ide_stopped;
	}

	rq->errors = 0;
	do {
		nsect = rq->current_nr_sectors;
		if (nsect > msect)
			nsect = msect;

		task_sectors(drive, rq, nsect, IDE_PIO_IN);

		if (!rq->nr_sectors)
			msect = 0;
		else
			msect -= nsect;
	} while (msect);

	/* If it was the last datablock check status and finish transfer. */
	if (!rq->nr_sectors) {
		good_stat = 0;
		stat = wait_drive_not_busy(drive);
		goto check_status;
	}

	/* Still data left to transfer. */
	ide_set_handler(drive, &task_mulin_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}
EXPORT_SYMBOL(task_mulin_intr);

/*
 * Handler for command with PIO data-out phase (Write).
 */
ide_startstop_t task_out_intr (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	u8 stat;

	stat = HWIF(drive)->INB(IDE_STATUS_REG);
	if (!OK_STAT(stat, DRIVE_READY, drive->bad_wstat)) {
		if ((stat & (ERR_STAT | DRQ_STAT)) ||
		    ((stat & WRERR_STAT) && !drive->nowerr))
			return DRIVER(drive)->error(drive, __FUNCTION__, stat);
		if (stat & BUSY_STAT) {
			/* Not ready yet, so wait for another IRQ. */
			ide_set_handler(drive, &task_out_intr, WAIT_WORSTCASE, NULL);
			return ide_started;
		}
	}

	/* Deal with unexpected ATA data phase. */
	if ((!(stat & DATA_READY) && rq->nr_sectors) ||
	    ((stat & DATA_READY) && !rq->nr_sectors))
		return DRIVER(drive)->error(drive, __FUNCTION__, stat);

	/* 
	 * Complete previously submitted bios (if any).
	 * Status was already verifyied.
	 */
	while (rq->bio != rq->cbio)
		if (!DRIVER(drive)->end_request(drive, 1, bio_sectors(rq->bio)))
			return ide_stopped;
	/* Complete rq->buffer based request (ioctls). */
	if (!rq->bio && !rq->nr_sectors) {
		ide_end_drive_cmd(drive, stat, HWIF(drive)->INB(IDE_ERROR_REG));
		return ide_stopped;
	}

	/* Still data left to transfer. */
	ide_set_handler(drive, &task_out_intr, WAIT_WORSTCASE, NULL);

	rq->errors = 0;
	task_sectors(drive, rq, 1, IDE_PIO_OUT);

	return ide_started;
}

EXPORT_SYMBOL(task_out_intr);

ide_startstop_t pre_task_out_intr (ide_drive_t *drive, struct request *rq)
{
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, DATA_READY,
			  drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing WRITE%s\n",
				drive->name, drive->addressing ? "_EXT" : "");
		return startstop;
	}

	if (!drive->unmask)
		local_irq_disable();

	return task_out_intr(drive);
}
EXPORT_SYMBOL(pre_task_out_intr);

/*
 * Handler for command with PIO data-out phase (Write Multiple).
 */
ide_startstop_t task_mulout_intr (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	unsigned int msect = drive->mult_count;
	unsigned int nsect;
	u8 stat;

	stat = HWIF(drive)->INB(IDE_STATUS_REG);
	if (!OK_STAT(stat, DRIVE_READY, drive->bad_wstat)) {
		if ((stat & (ERR_STAT | DRQ_STAT)) ||
		    ((stat & WRERR_STAT) && !drive->nowerr))
			return DRIVER(drive)->error(drive, __FUNCTION__, stat);
		if (stat & BUSY_STAT) {
			/* Not ready yet, so wait for another IRQ. */
			ide_set_handler(drive, &task_mulout_intr, WAIT_WORSTCASE, NULL);
			return ide_started;
		}
	}

	/* Deal with unexpected ATA data phase. */
	if ((!(stat & DATA_READY) && rq->nr_sectors) ||
	    ((stat & DATA_READY) && !rq->nr_sectors))
		return DRIVER(drive)->error(drive, __FUNCTION__, stat);

	/* 
	 * Complete previously submitted bios (if any).
	 * Status was already verifyied.
	 */
	while (rq->bio != rq->cbio)
		if (!DRIVER(drive)->end_request(drive, 1, bio_sectors(rq->bio)))
			return ide_stopped;
	/* Complete rq->buffer based request (ioctls). */
	if (!rq->bio && !rq->nr_sectors) {
		ide_end_drive_cmd(drive, stat, HWIF(drive)->INB(IDE_ERROR_REG));
		return ide_stopped;
	}

	/* Still data left to transfer. */
	ide_set_handler(drive, &task_mulout_intr, WAIT_WORSTCASE, NULL);

	rq->errors = 0;
	do {
		nsect = rq->current_nr_sectors;
		if (nsect > msect)
			nsect = msect;

		task_sectors(drive, rq, nsect, IDE_PIO_OUT);

		if (!rq->nr_sectors)
			msect = 0;
		else
			msect -= nsect;
	} while (msect);

	return ide_started;
}
EXPORT_SYMBOL(task_mulout_intr);

ide_startstop_t pre_task_mulout_intr (ide_drive_t *drive, struct request *rq)
{
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, DATA_READY,
			  drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing MULTWRITE%s\n",
				drive->name, drive->addressing ? "_EXT" : "");
		return startstop;
	}

	if (!drive->unmask)
		local_irq_disable();

	return task_mulout_intr(drive);
}
EXPORT_SYMBOL(pre_task_mulout_intr);

#endif /* !CONFIG_IDE_TASKFILE_IO */

int ide_diag_taskfile (ide_drive_t *drive, ide_task_t *args, unsigned long data_size, u8 *buf)
{
	struct request rq;

	memset(&rq, 0, sizeof(rq));
	rq.flags = REQ_DRIVE_TASKFILE;
	rq.buffer = buf;

	/*
	 * (ks) We transfer currently only whole sectors.
	 * This is suffient for now.  But, it would be great,
	 * if we would find a solution to transfer any size.
	 * To support special commands like READ LONG.
	 */
	if (args->command_type != IDE_DRIVE_TASK_NO_DATA) {
		if (data_size == 0)
			rq.nr_sectors = (args->hobRegister[IDE_NSECTOR_OFFSET] << 8) | args->tfRegister[IDE_NSECTOR_OFFSET];
		else
			rq.nr_sectors = data_size / SECTOR_SIZE;

		rq.hard_nr_sectors = rq.nr_sectors;
		rq.hard_cur_sectors = rq.current_nr_sectors = rq.nr_sectors;
	}

	rq.special = args;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

EXPORT_SYMBOL(ide_diag_taskfile);

int ide_raw_taskfile (ide_drive_t *drive, ide_task_t *args, u8 *buf)
{
	return ide_diag_taskfile(drive, args, 0, buf);
}

EXPORT_SYMBOL(ide_raw_taskfile);

#define MAX_DMA		(256*SECTOR_WORDS)

ide_startstop_t flagged_taskfile(ide_drive_t *, ide_task_t *);
ide_startstop_t flagged_task_no_data_intr(ide_drive_t *);
ide_startstop_t flagged_task_in_intr(ide_drive_t *);
ide_startstop_t flagged_task_mulin_intr(ide_drive_t *);
ide_startstop_t flagged_pre_task_out_intr(ide_drive_t *, struct request *);
ide_startstop_t flagged_task_out_intr(ide_drive_t *);
ide_startstop_t flagged_pre_task_mulout_intr(ide_drive_t *, struct request *);
ide_startstop_t flagged_task_mulout_intr(ide_drive_t *);

int ide_taskfile_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	ide_task_request_t	*req_task;
	ide_task_t		args;
	u8 *outbuf		= NULL;
	u8 *inbuf		= NULL;
	task_ioreg_t *argsptr	= args.tfRegister;
	task_ioreg_t *hobsptr	= args.hobRegister;
	int err			= 0;
	int tasksize		= sizeof(struct ide_task_request_s);
	int taskin		= 0;
	int taskout		= 0;
	u8 io_32bit		= drive->io_32bit;
	char __user *buf = (char __user *)arg;

//	printk("IDE Taskfile ...\n");

	req_task = kmalloc(tasksize, GFP_KERNEL);
	if (req_task == NULL) return -ENOMEM;
	memset(req_task, 0, tasksize);
	if (copy_from_user(req_task, buf, tasksize)) {
		kfree(req_task);
		return -EFAULT;
	}

	taskout = (int) req_task->out_size;
	taskin  = (int) req_task->in_size;

	if (taskout) {
		int outtotal = tasksize;
		outbuf = kmalloc(taskout, GFP_KERNEL);
		if (outbuf == NULL) {
			err = -ENOMEM;
			goto abort;
		}
		memset(outbuf, 0, taskout);
		if (copy_from_user(outbuf, buf + outtotal, taskout)) {
			err = -EFAULT;
			goto abort;
		}
	}

	if (taskin) {
		int intotal = tasksize + taskout;
		inbuf = kmalloc(taskin, GFP_KERNEL);
		if (inbuf == NULL) {
			err = -ENOMEM;
			goto abort;
		}
		memset(inbuf, 0, taskin);
		if (copy_from_user(inbuf, buf + intotal, taskin)) {
			err = -EFAULT;
			goto abort;
		}
	}

	memset(&args, 0, sizeof(ide_task_t));
	memcpy(argsptr, req_task->io_ports, HDIO_DRIVE_TASK_HDR_SIZE);
	memcpy(hobsptr, req_task->hob_ports, HDIO_DRIVE_HOB_HDR_SIZE);

	args.tf_in_flags  = req_task->in_flags;
	args.tf_out_flags = req_task->out_flags;
	args.data_phase   = req_task->data_phase;
	args.command_type = req_task->req_cmd;

	drive->io_32bit = 0;
	switch(req_task->data_phase) {
		case TASKFILE_OUT_DMAQ:
		case TASKFILE_OUT_DMA:
			err = ide_diag_taskfile(drive, &args, taskout, outbuf);
			break;
		case TASKFILE_IN_DMAQ:
		case TASKFILE_IN_DMA:
			err = ide_diag_taskfile(drive, &args, taskin, inbuf);
			break;
		case TASKFILE_IN_OUT:
#if 0
			args.prehandler = &pre_task_out_intr;
			args.handler = &task_out_intr;
			err = ide_diag_taskfile(drive, &args, taskout, outbuf);
			args.prehandler = NULL;
			args.handler = &task_in_intr;
			err = ide_diag_taskfile(drive, &args, taskin, inbuf);
			break;
#else
			err = -EFAULT;
			goto abort;
#endif
		case TASKFILE_MULTI_OUT:
			if (!drive->mult_count) {
				/* (hs): give up if multcount is not set */
				printk(KERN_ERR "%s: %s Multimode Write " \
					"multcount is not set\n",
					drive->name, __FUNCTION__);
				err = -EPERM;
				goto abort;
			}
			if (args.tf_out_flags.all != 0) {
				args.prehandler = &flagged_pre_task_mulout_intr;
				args.handler = &flagged_task_mulout_intr;
			} else {
				args.prehandler = &pre_task_mulout_intr;
				args.handler = &task_mulout_intr;
			}
			err = ide_diag_taskfile(drive, &args, taskout, outbuf);
			break;
		case TASKFILE_OUT:
			if (args.tf_out_flags.all != 0) {
				args.prehandler = &flagged_pre_task_out_intr;
				args.handler    = &flagged_task_out_intr;
			} else {
				args.prehandler = &pre_task_out_intr;
				args.handler = &task_out_intr;
			}
			err = ide_diag_taskfile(drive, &args, taskout, outbuf);
			break;
		case TASKFILE_MULTI_IN:
			if (!drive->mult_count) {
				/* (hs): give up if multcount is not set */
				printk(KERN_ERR "%s: %s Multimode Read failure " \
					"multcount is not set\n",
					drive->name, __FUNCTION__);
				err = -EPERM;
				goto abort;
			}
			if (args.tf_out_flags.all != 0) {
				args.handler = &flagged_task_mulin_intr;
			} else {
				args.handler = &task_mulin_intr;
			}
			err = ide_diag_taskfile(drive, &args, taskin, inbuf);
			break;
		case TASKFILE_IN:
			if (args.tf_out_flags.all != 0) {
				args.handler = &flagged_task_in_intr;
			} else {
				args.handler = &task_in_intr;
			}
			err = ide_diag_taskfile(drive, &args, taskin, inbuf);
			break;
		case TASKFILE_NO_DATA:
			if (args.tf_out_flags.all != 0) {
				args.handler = &flagged_task_no_data_intr;
			} else {
				args.handler = &task_no_data_intr;
			}
			err = ide_diag_taskfile(drive, &args, 0, NULL);
			break;
		default:
			err = -EFAULT;
			goto abort;
	}

	memcpy(req_task->io_ports, &(args.tfRegister), HDIO_DRIVE_TASK_HDR_SIZE);
	memcpy(req_task->hob_ports, &(args.hobRegister), HDIO_DRIVE_HOB_HDR_SIZE);
	req_task->in_flags  = args.tf_in_flags;
	req_task->out_flags = args.tf_out_flags;

	if (copy_to_user(buf, req_task, tasksize)) {
		err = -EFAULT;
		goto abort;
	}
	if (taskout) {
		int outtotal = tasksize;
		if (copy_to_user(buf + outtotal, outbuf, taskout)) {
			err = -EFAULT;
			goto abort;
		}
	}
	if (taskin) {
		int intotal = tasksize + taskout;
		if (copy_to_user(buf + intotal, inbuf, taskin)) {
			err = -EFAULT;
			goto abort;
		}
	}
abort:
	kfree(req_task);
	if (outbuf != NULL)
		kfree(outbuf);
	if (inbuf != NULL)
		kfree(inbuf);

//	printk("IDE Taskfile ioctl ended. rc = %i\n", err);

	drive->io_32bit = io_32bit;

	return err;
}

EXPORT_SYMBOL(ide_taskfile_ioctl);

int ide_wait_cmd (ide_drive_t *drive, u8 cmd, u8 nsect, u8 feature, u8 sectors, u8 *buf)
{
	struct request rq;
	u8 buffer[4];

	if (!buf)
		buf = buffer;
	memset(buf, 0, 4 + SECTOR_WORDS * 4 * sectors);
	ide_init_drive_cmd(&rq);
	rq.buffer = buf;
	*buf++ = cmd;
	*buf++ = nsect;
	*buf++ = feature;
	*buf++ = sectors;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

EXPORT_SYMBOL(ide_wait_cmd);

/*
 * FIXME : this needs to map into at taskfile. <andre@linux-ide.org>
 */
int ide_cmd_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	u8 args[4], *argbuf = args;
	u8 xfer_rate = 0;
	int argsize = 4;
	ide_task_t tfargs;

	if (NULL == (void *) arg) {
		struct request rq;
		ide_init_drive_cmd(&rq);
		return ide_do_drive_cmd(drive, &rq, ide_wait);
	}

	if (copy_from_user(args, (void __user *)arg, 4))
		return -EFAULT;

	memset(&tfargs, 0, sizeof(ide_task_t));
	tfargs.tfRegister[IDE_FEATURE_OFFSET] = args[2];
	tfargs.tfRegister[IDE_NSECTOR_OFFSET] = args[3];
	tfargs.tfRegister[IDE_SECTOR_OFFSET]  = args[1];
	tfargs.tfRegister[IDE_LCYL_OFFSET]    = 0x00;
	tfargs.tfRegister[IDE_HCYL_OFFSET]    = 0x00;
	tfargs.tfRegister[IDE_SELECT_OFFSET]  = 0x00;
	tfargs.tfRegister[IDE_COMMAND_OFFSET] = args[0];

	if (args[3]) {
		argsize = 4 + (SECTOR_WORDS * 4 * args[3]);
		argbuf = kmalloc(argsize, GFP_KERNEL);
		if (argbuf == NULL)
			return -ENOMEM;
		memcpy(argbuf, args, 4);
	}
	if (set_transfer(drive, &tfargs)) {
		xfer_rate = args[1];
		if (ide_ata66_check(drive, &tfargs))
			goto abort;
	}

	err = ide_wait_cmd(drive, args[0], args[1], args[2], args[3], argbuf);

	if (!err && xfer_rate) {
		/* active-retuning-calls future */
		ide_set_xfer_rate(drive, xfer_rate);
		ide_driveid_update(drive);
	}
abort:
	if (copy_to_user((void __user *)arg, argbuf, argsize))
		err = -EFAULT;
	if (argsize > 4)
		kfree(argbuf);
	return err;
}

EXPORT_SYMBOL(ide_cmd_ioctl);

int ide_wait_cmd_task (ide_drive_t *drive, u8 *buf)
{
	struct request rq;

	ide_init_drive_cmd(&rq);
	rq.flags = REQ_DRIVE_TASK;
	rq.buffer = buf;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

EXPORT_SYMBOL(ide_wait_cmd_task);

/*
 * FIXME : this needs to map into at taskfile. <andre@linux-ide.org>
 */
int ide_task_ioctl (ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	void __user *p = (void __user *)arg;
	int err = 0;
	u8 args[7], *argbuf = args;
	int argsize = 7;

	if (copy_from_user(args, p, 7))
		return -EFAULT;
	err = ide_wait_cmd_task(drive, argbuf);
	if (copy_to_user(p, argbuf, argsize))
		err = -EFAULT;
	return err;
}

EXPORT_SYMBOL(ide_task_ioctl);

/*
 * NOTICE: This is additions from IBM to provide a discrete interface,
 * for selective taskregister access operations.  Nice JOB Klaus!!!
 * Glad to be able to work and co-develop this with you and IBM.
 */
ide_startstop_t flagged_taskfile (ide_drive_t *drive, ide_task_t *task)
{
	ide_hwif_t *hwif	= HWIF(drive);
	task_struct_t *taskfile	= (task_struct_t *) task->tfRegister;
	hob_struct_t *hobfile	= (hob_struct_t *) task->hobRegister;
#if DEBUG_TASKFILE
	u8 status;
#endif


#ifdef CONFIG_IDE_TASK_IOCTL_DEBUG
	void debug_taskfile(drive, task);
#endif /* CONFIG_IDE_TASK_IOCTL_DEBUG */

	/*
	 * (ks) Check taskfile in/out flags.
	 * If set, then execute as it is defined.
	 * If not set, then define default settings.
	 * The default values are:
	 *	write and read all taskfile registers (except data) 
	 *	write and read the hob registers (sector,nsector,lcyl,hcyl)
	 */
	if (task->tf_out_flags.all == 0) {
		task->tf_out_flags.all = IDE_TASKFILE_STD_OUT_FLAGS;
		if (drive->addressing == 1)
			task->tf_out_flags.all |= (IDE_HOB_STD_OUT_FLAGS << 8);
        }

	if (task->tf_in_flags.all == 0) {
		task->tf_in_flags.all = IDE_TASKFILE_STD_IN_FLAGS;
		if (drive->addressing == 1)
			task->tf_in_flags.all |= (IDE_HOB_STD_IN_FLAGS  << 8);
        }

	/* ALL Command Block Executions SHALL clear nIEN, unless otherwise */
	if (IDE_CONTROL_REG)
		/* clear nIEN */
		hwif->OUTB(drive->ctl, IDE_CONTROL_REG);
	SELECT_MASK(drive, 0);

#if DEBUG_TASKFILE
	status = hwif->INB(IDE_STATUS_REG);
	if (status & 0x80) {
		printk("flagged_taskfile -> Bad status. Status = %02x. wait 100 usec ...\n", status);
		udelay(100);
		status = hwif->INB(IDE_STATUS_REG);
		printk("flagged_taskfile -> Status = %02x\n", status);
	}
#endif

	if (task->tf_out_flags.b.data) {
		u16 data =  taskfile->data + (hobfile->data << 8);
		hwif->OUTW(data, IDE_DATA_REG);
	}

	/* (ks) send hob registers first */
	if (task->tf_out_flags.b.nsector_hob)
		hwif->OUTB(hobfile->sector_count, IDE_NSECTOR_REG);
	if (task->tf_out_flags.b.sector_hob)
		hwif->OUTB(hobfile->sector_number, IDE_SECTOR_REG);
	if (task->tf_out_flags.b.lcyl_hob)
		hwif->OUTB(hobfile->low_cylinder, IDE_LCYL_REG);
	if (task->tf_out_flags.b.hcyl_hob)
		hwif->OUTB(hobfile->high_cylinder, IDE_HCYL_REG);

	/* (ks) Send now the standard registers */
	if (task->tf_out_flags.b.error_feature)
		hwif->OUTB(taskfile->feature, IDE_FEATURE_REG);
	/* refers to number of sectors to transfer */
	if (task->tf_out_flags.b.nsector)
		hwif->OUTB(taskfile->sector_count, IDE_NSECTOR_REG);
	/* refers to sector offset or start sector */
	if (task->tf_out_flags.b.sector)
		hwif->OUTB(taskfile->sector_number, IDE_SECTOR_REG);
	if (task->tf_out_flags.b.lcyl)
		hwif->OUTB(taskfile->low_cylinder, IDE_LCYL_REG);
	if (task->tf_out_flags.b.hcyl)
		hwif->OUTB(taskfile->high_cylinder, IDE_HCYL_REG);

        /*
	 * (ks) In the flagged taskfile approch, we will used all specified
	 * registers and the register value will not be changed. Except the
	 * select bit (master/slave) in the drive_head register. We must make
	 * sure that the desired drive is selected.
	 */
	hwif->OUTB(taskfile->device_head | drive->select.all, IDE_SELECT_REG);
	switch(task->data_phase) {

   	        case TASKFILE_OUT_DMAQ:
		case TASKFILE_OUT_DMA:
			hwif->ide_dma_write(drive);
			break;

		case TASKFILE_IN_DMAQ:
		case TASKFILE_IN_DMA:
			hwif->ide_dma_read(drive);
			break;

	        default:
 			if (task->handler == NULL)
				return ide_stopped;

			/* Issue the command */
			ide_execute_command(drive, taskfile->command, task->handler, WAIT_WORSTCASE, NULL);
			if (task->prehandler != NULL)
				return task->prehandler(drive, HWGROUP(drive)->rq);
	}

	return ide_started;
}

EXPORT_SYMBOL(flagged_taskfile);

ide_startstop_t flagged_task_no_data_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u8 stat;

	local_irq_enable();

	if (!OK_STAT(stat = hwif->INB(IDE_STATUS_REG), READY_STAT, BAD_STAT)) {
		if (stat & ERR_STAT) {
			return DRIVER(drive)->error(drive, "flagged_task_no_data_intr", stat);
		}
		/*
		 * (ks) Unexpected ATA data phase detected.
		 * This should not happen. But, it can !
		 * I am not sure, which function is best to clean up
		 * this situation.  I choose: ide_error(...)
		 */
 		return DRIVER(drive)->error(drive, "flagged_task_no_data_intr (unexpected phase)", stat); 
	}

	ide_end_drive_cmd(drive, stat, hwif->INB(IDE_ERROR_REG));

	return ide_stopped;
}

/*
 * Handler for command with PIO data-in phase
 */
ide_startstop_t flagged_task_in_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat			= hwif->INB(IDE_STATUS_REG);
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	int retries             = 5;

	if (rq->current_nr_sectors == 0) 
		return DRIVER(drive)->error(drive, "flagged_task_in_intr (no data requested)", stat); 

	if (!OK_STAT(stat, DATA_READY, BAD_R_STAT)) {
		if (stat & ERR_STAT) {
			return DRIVER(drive)->error(drive, "flagged_task_in_intr", stat);
		}
		/*
		 * (ks) Unexpected ATA data phase detected.
		 * This should not happen. But, it can !
		 * I am not sure, which function is best to clean up
		 * this situation.  I choose: ide_error(...)
		 */
		return DRIVER(drive)->error(drive, "flagged_task_in_intr (unexpected data phase)", stat); 
	}

	pBuf = rq->buffer + ((rq->nr_sectors - rq->current_nr_sectors) * SECTOR_SIZE);
	DTF("Read - rq->current_nr_sectors: %d, status: %02x\n", (int) rq->current_nr_sectors, stat);

	taskfile_input_data(drive, pBuf, SECTOR_WORDS);

	if (--rq->current_nr_sectors != 0) {
		/*
                 * (ks) We don't know which command was executed. 
		 * So, we wait the 'WORSTCASE' value.
                 */
		ide_set_handler(drive, &flagged_task_in_intr,  WAIT_WORSTCASE, NULL);
		return ide_started;
	}
	/*
	 * (ks) Last sector was transfered, wait until drive is ready. 
	 * This can take up to 10 usec. We willl wait max 50 us.
	 */
	while (((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) && retries--)
		udelay(10);
	ide_end_drive_cmd (drive, stat, hwif->INB(IDE_ERROR_REG));

	return ide_stopped;
}

ide_startstop_t flagged_task_mulin_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat			= hwif->INB(IDE_STATUS_REG);
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	int retries             = 5;
	unsigned int msect, nsect;

	if (rq->current_nr_sectors == 0) 
		return DRIVER(drive)->error(drive, "flagged_task_mulin_intr (no data requested)", stat); 

	msect = drive->mult_count;
	if (msect == 0) 
		return DRIVER(drive)->error(drive, "flagged_task_mulin_intr (multimode not set)", stat); 

	if (!OK_STAT(stat, DATA_READY, BAD_R_STAT)) {
		if (stat & ERR_STAT) {
			return DRIVER(drive)->error(drive, "flagged_task_mulin_intr", stat);
		}
		/*
		 * (ks) Unexpected ATA data phase detected.
		 * This should not happen. But, it can !
		 * I am not sure, which function is best to clean up
		 * this situation.  I choose: ide_error(...)
		 */
		return DRIVER(drive)->error(drive, "flagged_task_mulin_intr (unexpected data phase)", stat); 
	}

	nsect = (rq->current_nr_sectors > msect) ? msect : rq->current_nr_sectors;
	pBuf = rq->buffer + ((rq->nr_sectors - rq->current_nr_sectors) * SECTOR_SIZE);

	DTF("Multiread: %p, nsect: %d , rq->current_nr_sectors: %ld\n",
	    pBuf, nsect, rq->current_nr_sectors);

	taskfile_input_data(drive, pBuf, nsect * SECTOR_WORDS);

	rq->current_nr_sectors -= nsect;
	if (rq->current_nr_sectors != 0) {
		/*
                 * (ks) We don't know which command was executed. 
		 * So, we wait the 'WORSTCASE' value.
                 */
		ide_set_handler(drive, &flagged_task_mulin_intr,  WAIT_WORSTCASE, NULL);
		return ide_started;
	}

	/*
	 * (ks) Last sector was transfered, wait until drive is ready. 
	 * This can take up to 10 usec. We willl wait max 50 us.
	 */
	while (((stat = hwif->INB(IDE_STATUS_REG)) & BUSY_STAT) && retries--)
		udelay(10);
	ide_end_drive_cmd (drive, stat, hwif->INB(IDE_ERROR_REG));

	return ide_stopped;
}

/*
 * Pre handler for command with PIO data-out phase
 */
ide_startstop_t flagged_pre_task_out_intr (ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat			= hwif->INB(IDE_STATUS_REG);
	ide_startstop_t startstop;

	if (!rq->current_nr_sectors) {
		return DRIVER(drive)->error(drive, "flagged_pre_task_out_intr (write data not specified)", stat);
	}

	if (ide_wait_stat(&startstop, drive, DATA_READY,
			BAD_W_STAT, WAIT_DRQ)) {
		printk(KERN_ERR "%s: No DRQ bit after issuing write command.\n", drive->name);
		return startstop;
	}

	taskfile_output_data(drive, rq->buffer, SECTOR_WORDS);
	--rq->current_nr_sectors;

	return ide_started;
}

ide_startstop_t flagged_task_out_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat			= hwif->INB(IDE_STATUS_REG);
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;

	if (!OK_STAT(stat, DRIVE_READY, BAD_W_STAT)) 
		return DRIVER(drive)->error(drive, "flagged_task_out_intr", stat);
	
	if (!rq->current_nr_sectors) { 
		ide_end_drive_cmd (drive, stat, hwif->INB(IDE_ERROR_REG));
		return ide_stopped;
	}

	if (!OK_STAT(stat, DATA_READY, BAD_W_STAT)) {
		/*
		 * (ks) Unexpected ATA data phase detected.
		 * This should not happen. But, it can !
		 * I am not sure, which function is best to clean up
		 * this situation.  I choose: ide_error(...)
		 */
		return DRIVER(drive)->error(drive, "flagged_task_out_intr (unexpected data phase)", stat); 
	}

	pBuf = rq->buffer + ((rq->nr_sectors - rq->current_nr_sectors) * SECTOR_SIZE);
	DTF("Write - rq->current_nr_sectors: %d, status: %02x\n",
		(int) rq->current_nr_sectors, stat);

	taskfile_output_data(drive, pBuf, SECTOR_WORDS);
	--rq->current_nr_sectors;

	/*
	 * (ks) We don't know which command was executed. 
	 * So, we wait the 'WORSTCASE' value.
	 */
	ide_set_handler(drive, &flagged_task_out_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}

ide_startstop_t flagged_pre_task_mulout_intr (ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat			= hwif->INB(IDE_STATUS_REG);
	char *pBuf		= NULL;
	ide_startstop_t startstop;
	unsigned int msect, nsect;

	if (!rq->current_nr_sectors) 
		return DRIVER(drive)->error(drive, "flagged_pre_task_mulout_intr (write data not specified)", stat);

	msect = drive->mult_count;
	if (msect == 0)
		return DRIVER(drive)->error(drive, "flagged_pre_task_mulout_intr (multimode not set)", stat);

	if (ide_wait_stat(&startstop, drive, DATA_READY,
			BAD_W_STAT, WAIT_DRQ)) {
		printk(KERN_ERR "%s: No DRQ bit after issuing write command.\n", drive->name);
		return startstop;
	}

	nsect = (rq->current_nr_sectors > msect) ? msect : rq->current_nr_sectors;
	pBuf = rq->buffer + ((rq->nr_sectors - rq->current_nr_sectors) * SECTOR_SIZE);
	DTF("Multiwrite: %p, nsect: %d , rq->current_nr_sectors: %ld\n",
	    pBuf, nsect, rq->current_nr_sectors);

	taskfile_output_data(drive, pBuf, nsect * SECTOR_WORDS);

	rq->current_nr_sectors -= nsect;

	return ide_started;
}

ide_startstop_t flagged_task_mulout_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	u8 stat			= hwif->INB(IDE_STATUS_REG);
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned int msect, nsect;

	msect = drive->mult_count;
	if (msect == 0)
		return DRIVER(drive)->error(drive, "flagged_task_mulout_intr (multimode not set)", stat);

	if (!OK_STAT(stat, DRIVE_READY, BAD_W_STAT)) 
		return DRIVER(drive)->error(drive, "flagged_task_mulout_intr", stat);
	
	if (!rq->current_nr_sectors) { 
		ide_end_drive_cmd (drive, stat, hwif->INB(IDE_ERROR_REG));
		return ide_stopped;
	}

	if (!OK_STAT(stat, DATA_READY, BAD_W_STAT)) {
		/*
		 * (ks) Unexpected ATA data phase detected.
		 * This should not happen. But, it can !
		 * I am not sure, which function is best to clean up
		 * this situation.  I choose: ide_error(...)
		 */
		return DRIVER(drive)->error(drive, "flagged_task_mulout_intr (unexpected data phase)", stat); 
	}

	nsect = (rq->current_nr_sectors > msect) ? msect : rq->current_nr_sectors;
	pBuf = rq->buffer + ((rq->nr_sectors - rq->current_nr_sectors) * SECTOR_SIZE);
	DTF("Multiwrite: %p, nsect: %d , rq->current_nr_sectors: %ld\n",
	    pBuf, nsect, rq->current_nr_sectors);

	taskfile_output_data(drive, pBuf, nsect * SECTOR_WORDS);
	rq->current_nr_sectors -= nsect;

	/*
	 * (ks) We don't know which command was executed. 
	 * So, we wait the 'WORSTCASE' value.
	 */
	ide_set_handler(drive, &flagged_task_mulout_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}
