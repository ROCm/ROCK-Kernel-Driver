/*
 * linux/drivers/ide/ide-taskfile.c	Version 0.33	April 11, 2002
 *
 *  Copyright (C) 2000-2002	Michael Cornwell <cornwell@acm.org>
 *  Copyright (C) 2000-2002	Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2001-2002	Klaus Smolin
 *					IBM Storage Technology Division
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

#define task_map_rq(rq, flags)		ide_map_buffer((rq), (flags))
#define task_unmap_rq(rq, buf, flags)	ide_unmap_buffer((buf), (flags))

inline u32 task_read_24 (ide_drive_t *drive)
{
	return	(IN_BYTE(IDE_HCYL_REG)<<16) |
		(IN_BYTE(IDE_LCYL_REG)<<8) |
		 IN_BYTE(IDE_SECTOR_REG);
}

static void ata_bswap_data (void *buffer, int wcount)
{
	u16 *p = buffer;

	while (wcount--) {
		*p = *p << 8 | *p >> 8; p++;
		*p = *p << 8 | *p >> 8; p++;
	}
}

#if SUPPORT_VLB_SYNC
/*
 * Some localbus EIDE interfaces require a special access sequence
 * when using 32-bit I/O instructions to transfer data.  We call this
 * the "vlb_sync" sequence, which consists of three successive reads
 * of the sector count register location, with interrupts disabled
 * to ensure that the reads all happen together.
 */
static inline void task_vlb_sync (ide_ioreg_t port)
{
	(void) IN_BYTE (port);
	(void) IN_BYTE (port);
	(void) IN_BYTE (port);
}
#endif /* SUPPORT_VLB_SYNC */

/*
 * This is used for most PIO data transfers *from* the IDE interface
 */
void ata_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	byte io_32bit;

	/*
	 * first check if this controller has defined a special function
	 * for handling polled ide transfers
	 */

	if (HWIF(drive)->ideproc) {
		HWIF(drive)->ideproc(ideproc_ide_input_data, drive, buffer, wcount);
		return;
	}

	io_32bit = drive->io_32bit;

	if (io_32bit) {
#if SUPPORT_VLB_SYNC
		if (io_32bit & 2) {
			unsigned long flags;
			local_irq_save(flags);
			task_vlb_sync(IDE_NSECTOR_REG);
			insl(IDE_DATA_REG, buffer, wcount);
			local_irq_restore(flags);
		} else
#endif /* SUPPORT_VLB_SYNC */
			insl(IDE_DATA_REG, buffer, wcount);
	} else {
#if SUPPORT_SLOW_DATA_PORTS
		if (drive->slow) {
			unsigned short *ptr = (unsigned short *) buffer;
			while (wcount--) {
				*ptr++ = inw_p(IDE_DATA_REG);
				*ptr++ = inw_p(IDE_DATA_REG);
			}
		} else
#endif /* SUPPORT_SLOW_DATA_PORTS */
			insw(IDE_DATA_REG, buffer, wcount<<1);
	}
}

/*
 * This is used for most PIO data transfers *to* the IDE interface
 */
void ata_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	byte io_32bit;

	if (HWIF(drive)->ideproc) {
		HWIF(drive)->ideproc(ideproc_ide_output_data, drive, buffer, wcount);
		return;
	}

	io_32bit = drive->io_32bit;

	if (io_32bit) {
#if SUPPORT_VLB_SYNC
		if (io_32bit & 2) {
			unsigned long flags;
			local_irq_save(flags);
			task_vlb_sync(IDE_NSECTOR_REG);
			outsl(IDE_DATA_REG, buffer, wcount);
			local_irq_restore(flags);
		} else
#endif /* SUPPORT_VLB_SYNC */
			outsl(IDE_DATA_REG, buffer, wcount);
	} else {
#if SUPPORT_SLOW_DATA_PORTS
		if (drive->slow) {
			unsigned short *ptr = (unsigned short *) buffer;
			while (wcount--) {
				outw_p(*ptr++, IDE_DATA_REG);
				outw_p(*ptr++, IDE_DATA_REG);
			}
		} else
#endif /* SUPPORT_SLOW_DATA_PORTS */
			outsw(IDE_DATA_REG, buffer, wcount<<1);
	}
}

/*
 * The following routines are mainly used by the ATAPI drivers.
 *
 * These routines will round up any request for an odd number of bytes,
 * so if an odd bytecount is specified, be sure that there's at least one
 * extra byte allocated for the buffer.
 */
void atapi_input_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount)
{
	if (HWIF(drive)->ideproc) {
		HWIF(drive)->ideproc(ideproc_atapi_input_bytes, drive, buffer, bytecount);
		return;
	}

	++bytecount;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		insw_swapw(IDE_DATA_REG, buffer, bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI */
	ata_input_data (drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		insw (IDE_DATA_REG, ((byte *)buffer) + (bytecount & ~0x03), 1);
}

void atapi_output_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount)
{
	if (HWIF(drive)->ideproc) {
		HWIF(drive)->ideproc(ideproc_atapi_output_bytes, drive, buffer, bytecount);
		return;
	}

	++bytecount;
#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)
	if (MACH_IS_ATARI || MACH_IS_Q40) {
		/* Atari has a byte-swapped IDE interface */
		outsw_swapw(IDE_DATA_REG, buffer, bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI */
	ata_output_data (drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		outsw (IDE_DATA_REG, ((byte *)buffer) + (bytecount & ~0x03), 1);
}

void taskfile_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	ata_input_data(drive, buffer, wcount);
	if (drive->bswap)
		ata_bswap_data(buffer, wcount);
}

void taskfile_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	if (drive->bswap) {
		ata_bswap_data(buffer, wcount);
		ata_output_data(drive, buffer, wcount);
		ata_bswap_data(buffer, wcount);
	} else {
		ata_output_data(drive, buffer, wcount);
	}
}

/*
 * Needed for PCI irq sharing
 */
int drive_is_ready (ide_drive_t *drive)
{
	byte stat = 0;
	if (drive->waiting_for_dma)
		return HWIF(drive)->dmaproc(ide_dma_test_irq, drive);
#if 0
	/* need to guarantee 400ns since last command was issued */
	udelay(1);
#endif

#ifdef CONFIG_IDEPCI_SHARE_IRQ
	/*
	 * We do a passive status test under shared PCI interrupts on
	 * cards that truly share the ATA side interrupt, but may also share
	 * an interrupt with another pci card/device.  We make no assumptions
	 * about possible isa-pnp and pci-pnp issues yet.
	 */
	if (IDE_CONTROL_REG)
		stat = GET_ALTSTAT();
	else
#endif /* CONFIG_IDEPCI_SHARE_IRQ */
	stat = GET_STAT();	/* Note: this may clear a pending IRQ!! */

	if (stat & BUSY_STAT)
		return 0;	/* drive busy:  definitely not interrupting */
	return 1;		/* drive ready: *might* be interrupting */
}

/*
 * Global for All, and taken from ide-pmac.c
 */
int wait_for_ready (ide_drive_t *drive, int timeout)
{
	byte stat = 0;

	while(--timeout) {
		stat = GET_STAT();
		if(!(stat & BUSY_STAT)) {
			if (drive->ready_stat == 0)
				break;
			else if((stat & drive->ready_stat) || (stat & ERR_STAT))
				break;
		}
		mdelay(1);
	}
	if((stat & ERR_STAT) || timeout <= 0) {
		if (stat & ERR_STAT) {
			printk(KERN_ERR "%s: wait_for_ready, error status: %x\n", drive->name, stat);
		}
		return 1;
	}
	return 0;
}

/*
 * This routine busy-waits for the drive status to be not "busy".
 * It then checks the status for all of the "good" bits and none
 * of the "bad" bits, and if all is okay it returns 0.  All other
 * cases return 1 after invoking ide_error() -- caller should just return.
 *
 * This routine should get fixed to not hog the cpu during extra long waits..
 * That could be done by busy-waiting for the first jiffy or two, and then
 * setting a timer to wake up at half second intervals thereafter,
 * until timeout is achieved, before timing out.
 */
int ide_wait_stat (ide_startstop_t *startstop, ide_drive_t *drive, byte good, byte bad, unsigned long timeout)
{
	byte stat;
	int i;
	unsigned long flags;
 
	/* bail early if we've exceeded max_failures */
	if (drive->max_failures && (drive->failures > drive->max_failures)) {
		*startstop = ide_stopped;
		return 1;
	}

	udelay(1);	/* spec allows drive 400ns to assert "BUSY" */
	if ((stat = GET_STAT()) & BUSY_STAT) {
		local_irq_set(flags);
		timeout += jiffies;
		while ((stat = GET_STAT()) & BUSY_STAT) {
			if (time_after(jiffies, timeout)) {
				local_irq_restore(flags);
				*startstop = DRIVER(drive)->error(drive, "status timeout", stat);
				return 1;
			}
		}
		local_irq_restore(flags);
	}
	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 * This fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		if (OK_STAT((stat = GET_STAT()), good, bad))
			return 0;
	}
	*startstop = DRIVER(drive)->error(drive, "status error", stat);
	return 1;
}

void debug_taskfile (ide_drive_t *drive, ide_task_t *args)
{
#ifdef CONFIG_IDE_TASK_IOCTL_DEBUG
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
//	printk("HTF.0=x%02x ", args->hobRegister[IDE_DATA_OFFSET_HOB]);
	printk("HTF.1=x%02x ", args->hobRegister[IDE_FEATURE_OFFSET_HOB]);
	printk("HTF.2=x%02x ", args->hobRegister[IDE_NSECTOR_OFFSET_HOB]);
	printk("HTF.3=x%02x ", args->hobRegister[IDE_SECTOR_OFFSET_HOB]);
	printk("HTF.4=x%02x ", args->hobRegister[IDE_LCYL_OFFSET_HOB]);
	printk("HTF.5=x%02x ", args->hobRegister[IDE_HCYL_OFFSET_HOB]);
	printk("HTF.6=x%02x ", args->hobRegister[IDE_SELECT_OFFSET_HOB]);
	printk("HTF.7=x%02x\n", args->hobRegister[IDE_CONTROL_OFFSET_HOB]);
#endif /* CONFIG_IDE_TASK_IOCTL_DEBUG */
}

ide_startstop_t do_rw_taskfile (ide_drive_t *drive, ide_task_t *task)
{
	task_struct_t *taskfile = (task_struct_t *) task->tfRegister;
	hob_struct_t *hobfile = (hob_struct_t *) task->hobRegister;
	struct hd_driveid *id = drive->id;
	byte HIHI = (drive->addressing == 1) ? 0xE0 : 0xEF;

#ifdef CONFIG_IDE_TASK_IOCTL_DEBUG
	void debug_taskfile(drive, task);
#endif /* CONFIG_IDE_TASK_IOCTL_DEBUG */

	/* ALL Command Block Executions SHALL clear nIEN, unless otherwise */
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl, IDE_CONTROL_REG);	/* clear nIEN */
	SELECT_MASK(HWIF(drive), drive, 0);

	if ((id->command_set_2 & 0x0400) &&
	    (id->cfs_enable_2 & 0x0400) &&
	    (drive->addressing == 1)) {
		OUT_BYTE(hobfile->feature, IDE_FEATURE_REG);
		OUT_BYTE(hobfile->sector_count, IDE_NSECTOR_REG);
		OUT_BYTE(hobfile->sector_number, IDE_SECTOR_REG);
		OUT_BYTE(hobfile->low_cylinder, IDE_LCYL_REG);
		OUT_BYTE(hobfile->high_cylinder, IDE_HCYL_REG);
	}

	OUT_BYTE(taskfile->feature, IDE_FEATURE_REG);
	OUT_BYTE(taskfile->sector_count, IDE_NSECTOR_REG);
	/* refers to number of sectors to transfer */
	OUT_BYTE(taskfile->sector_number, IDE_SECTOR_REG);
	/* refers to sector offset or start sector */
	OUT_BYTE(taskfile->low_cylinder, IDE_LCYL_REG);
	OUT_BYTE(taskfile->high_cylinder, IDE_HCYL_REG);

	OUT_BYTE((taskfile->device_head & HIHI) | drive->select.all, IDE_SELECT_REG);
	if (task->handler != NULL) {
		ide_set_handler (drive, task->handler, WAIT_CMD, NULL);
		OUT_BYTE(taskfile->command, IDE_COMMAND_REG);
		if (task->prehandler != NULL)
			return task->prehandler(drive, task->rq);
		return ide_started;
	}
#if 0
	switch(task->data_phase) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case TASKFILE_OUT_DMAQ:
		case TASKFILE_OUT_DMA:
			HWIF(drive)->dmaproc(ide_dma_write, drive);
			break;
		case TASKFILE_IN_DMAQ:
		case TASKFILE_IN_DMA:
			HWIF(drive)->dmaproc(ide_dma_read, drive);
			break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		default:
			if (task->handler == NULL)
				return ide_stopped;
			ide_set_handler (drive, task->handler, WAIT_WORSTCASE, NULL);
			/* Issue the command */
			OUT_BYTE(taskfile->command, IDE_COMMAND_REG);
			if (task->prehandler != NULL)
				return task->prehandler(drive, HWGROUP(drive)->rq);
	}
#else
	//	if ((rq->cmd == WRITE) && (drive->using_dma))
	/* for dma commands we down set the handler */
	if (drive->using_dma && !(HWIF(drive)->dmaproc(((taskfile->command == WIN_WRITEDMA) || (taskfile->command == WIN_WRITEDMA_EXT)) ? ide_dma_write : ide_dma_read, drive)));
#endif
	return ide_started;
}

#if 0
/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
byte taskfile_dump_status (ide_drive_t *drive, const char *msg, byte stat)
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
#endif  /* FANCY_STATUS_DUMPS */
	printk("\n");
	if ((stat & (BUSY_STAT|ERR_STAT)) == ERR_STAT) {
		err = GET_ERR();
		printk("%s: %s: error=0x%02x", drive->name, msg, err);
#if FANCY_STATUS_DUMPS
		if (drive->media == ide_disk) {
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
					low = task_read_24(drive);
					OUT_BYTE(0x80, IDE_CONTROL_REG);
					high = task_read_24(drive);
					sectors = ((__u64)high << 24) | low;
					printk(", LBAsect=%lld", sectors);
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
				if (HWGROUP(drive)->rq)
					printk(", sector=%lu", (__u64) HWGROUP(drive)->rq->sector);
			}
		}
#endif  /* FANCY_STATUS_DUMPS */
		printk("\n");
	}
	local_irq_restore(flags);
	return err;
}
#endif

/*
 * Clean up after success/failure of an explicit taskfile operation.
 */
void ide_end_taskfile (ide_drive_t *drive, byte stat, byte err)
{
	unsigned long flags;
	struct request *rq;
	ide_task_t *args;
	task_ioreg_t command;

	spin_lock_irqsave(&ide_lock, flags);
	rq = HWGROUP(drive)->rq;
	spin_unlock_irqrestore(&ide_lock, flags);
	args = (ide_task_t *) rq->special;

	command = args->tfRegister[IDE_COMMAND_OFFSET];

	if (rq->errors == 0)
		rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);

	if (args->tf_in_flags.b.data) {
		unsigned short data = IN_WORD(IDE_DATA_REG);
		args->tfRegister[IDE_DATA_OFFSET] = (data) & 0xFF;
		args->hobRegister[IDE_DATA_OFFSET_HOB]	= (data >> 8) & 0xFF;
	}
	args->tfRegister[IDE_ERROR_OFFSET]   = err;
	args->tfRegister[IDE_NSECTOR_OFFSET] = IN_BYTE(IDE_NSECTOR_REG);
	args->tfRegister[IDE_SECTOR_OFFSET]  = IN_BYTE(IDE_SECTOR_REG);
	args->tfRegister[IDE_LCYL_OFFSET]    = IN_BYTE(IDE_LCYL_REG);
	args->tfRegister[IDE_HCYL_OFFSET]    = IN_BYTE(IDE_HCYL_REG);
	args->tfRegister[IDE_SELECT_OFFSET]  = IN_BYTE(IDE_SELECT_REG);
	args->tfRegister[IDE_STATUS_OFFSET]  = stat;
	if ((drive->id->command_set_2 & 0x0400) &&
	    (drive->id->cfs_enable_2 & 0x0400) &&
	    (drive->addressing == 1)) {
		OUT_BYTE(drive->ctl|0x80, IDE_CONTROL_REG_HOB);
		args->hobRegister[IDE_FEATURE_OFFSET_HOB] = IN_BYTE(IDE_FEATURE_REG);
		args->hobRegister[IDE_NSECTOR_OFFSET_HOB] = IN_BYTE(IDE_NSECTOR_REG);
		args->hobRegister[IDE_SECTOR_OFFSET_HOB]  = IN_BYTE(IDE_SECTOR_REG);
		args->hobRegister[IDE_LCYL_OFFSET_HOB]    = IN_BYTE(IDE_LCYL_REG);
		args->hobRegister[IDE_HCYL_OFFSET_HOB]    = IN_BYTE(IDE_HCYL_REG);
	}

#if 0
/*	taskfile_settings_update(drive, args, command); */

	if (args->posthandler != NULL)
		args->posthandler(drive, args);
#endif

	spin_lock_irqsave(&ide_lock, flags);
	blkdev_dequeue_request(rq);
	HWGROUP(drive)->rq = NULL;
	end_that_request_last(rq);
	spin_unlock_irqrestore(&ide_lock, flags);
}

#if 0
/*
 * try_to_flush_leftover_data() is invoked in response to a drive
 * unexpectedly having its DRQ_STAT bit set.  As an alternative to
 * resetting the drive, this routine tries to clear the condition
 * by read a sector's worth of data from the drive.  Of course,
 * this may not help if the drive is *waiting* for data from *us*.
 */
void task_try_to_flush_leftover_data (ide_drive_t *drive)
{
	int i = (drive->mult_count ? drive->mult_count : 1) * SECTOR_WORDS;

	if (drive->media != ide_disk)
		return;
	while (i > 0) {
		u32 buffer[16];
		unsigned int wcount = (i > 16) ? 16 : i;
		i -= wcount;
		taskfile_input_data (drive, buffer, wcount);
	}
}

/*
 * taskfile_error() takes action based on the error returned by the drive.
 */
ide_startstop_t taskfile_error (ide_drive_t *drive, const char *msg, byte stat)
{
	struct request *rq;
	byte err;

        err = taskfile_dump_status(drive, msg, stat);
	if (drive == NULL || (rq = HWGROUP(drive)->rq) == NULL)
		return ide_stopped;
	/* retry only "normal" I/O: */
	if (rq->cmd == IDE_DRIVE_TASKFILE) {
		rq->errors = 1;
		ide_end_taskfile(drive, stat, err);
		return ide_stopped;
	}
	if (stat & BUSY_STAT || ((stat & WRERR_STAT) && !drive->nowerr)) { /* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else {
		if (drive->media == ide_disk && (stat & ERR_STAT)) {
			/* err has different meaning on cdrom and tape */
			if (err == ABRT_ERR) {
				if (drive->select.b.lba && IN_BYTE(IDE_COMMAND_REG) == WIN_SPECIFY)
					return ide_stopped;	/* some newer drives don't support WIN_SPECIFY */
			} else if ((err & (ABRT_ERR | ICRC_ERR)) == (ABRT_ERR | ICRC_ERR)) {
				drive->crc_count++;	/* UDMA crc error -- just retry the operation */
			} else if (err & (BBD_ERR | ECC_ERR))	/* retries won't help these */
				rq->errors = ERROR_MAX;
			else if (err & TRK0_ERR)	/* help it find track zero */
                                rq->errors |= ERROR_RECAL;
                }
                if ((stat & DRQ_STAT) && rq->cmd != WRITE)
                        task_try_to_flush_leftover_data(drive);
	}
	if (GET_STAT() & (BUSY_STAT|DRQ_STAT))
		OUT_BYTE(WIN_IDLEIMMEDIATE,IDE_COMMAND_REG);	/* force an abort */

	if (rq->errors >= ERROR_MAX) {
			DRIVER(drive)->end_request(drive, 0);
	} else {
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
#endif

/*
 * Handler for special commands without a data phase from ide-disk
 */

/*
 * set_multmode_intr() is invoked on completion of a WIN_SETMULT cmd.
 */
ide_startstop_t set_multmode_intr (ide_drive_t *drive)
{
	byte stat;

	if (OK_STAT(stat=GET_STAT(),READY_STAT,BAD_STAT)) {
		drive->mult_count = drive->mult_req;
	} else {
		drive->mult_req = drive->mult_count = 0;
		drive->special.b.recalibrate = 1;
		(void) ide_dump_status(drive, "set_multmode", stat);
	}
	return ide_stopped;
}

/*
 * set_geometry_intr() is invoked on completion of a WIN_SPECIFY cmd.
 */
ide_startstop_t set_geometry_intr (ide_drive_t *drive)
{
	int retries = 5;
	byte stat;

	while (((stat = GET_STAT()) & BUSY_STAT) && retries--)
		udelay(10);

	if (OK_STAT(stat, READY_STAT,BAD_STAT))
		return ide_stopped;

	if (stat & (ERR_STAT|DRQ_STAT))
		return DRIVER(drive)->error(drive, "set_geometry_intr", stat);

	if (HWGROUP(drive)->handler != NULL)	/* paranoia check */
		BUG();
	ide_set_handler(drive, &set_geometry_intr, WAIT_CMD, NULL);
	return ide_started;
}

/*
 * recal_intr() is invoked on completion of a WIN_RESTORE (recalibrate) cmd.
 */
ide_startstop_t recal_intr (ide_drive_t *drive)
{
	byte stat = GET_STAT();

	if (!OK_STAT(stat,READY_STAT,BAD_STAT))
		return DRIVER(drive)->error(drive, "recal_intr", stat);
	return ide_stopped;
}

/*
 * Handler for commands without a data phase
 */
ide_startstop_t task_no_data_intr (ide_drive_t *drive)
{
	ide_task_t *args	= HWGROUP(drive)->rq->special;
	byte stat		= GET_STAT();

	local_irq_enable();
	if (!OK_STAT(stat, READY_STAT, BAD_STAT)) {
		DTF("%s: command opcode 0x%02x\n", drive->name,
			args->tfRegister[IDE_COMMAND_OFFSET]);
		return DRIVER(drive)->error(drive, "task_no_data_intr", stat);
		/* calls ide_end_drive_cmd */
	}
	if (args)
		ide_end_drive_cmd (drive, stat, GET_ERR());

	return ide_stopped;
}

/*
 * Handler for command with PIO data-in phase, READ
 */
/*
 * FIXME before 2.4 enable ...
 *	DATA integrity issue upon error. <andre@linux-ide.org>
 */
ide_startstop_t task_in_intr (ide_drive_t *drive)
{
	byte stat		= GET_STAT();
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned long flags;

	if (!OK_STAT(stat,DATA_READY,BAD_R_STAT)) {
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
				ide_set_handler(drive, &task_in_intr, WAIT_CMD, NULL);
			return ide_started;  
		}
	}
#if 0

	/*
	 * Holding point for a brain dump of a thought :-/
	 */

	if (!OK_STAT(stat,DRIVE_READY,drive->bad_wstat)) {
		DTF("%s: READ attempting to recover last " \
			"sector counter status=0x%02x\n",
			drive->name, stat);
		rq->current_nr_sectors++;
		return DRIVER(drive)->error(drive, "task_in_intr", stat);
        }
	if (!rq->current_nr_sectors)
		if (!DRIVER(drive)->end_request(drive, 1))
			return ide_stopped;

	if (--rq->current_nr_sectors <= 0)
		if (!DRIVER(drive)->end_request(drive, 1))
			return ide_stopped;
#endif

	pBuf = task_map_rq(rq, &flags);
	DTF("Read: %p, rq->current_nr_sectors: %d, stat: %02x\n",
		pBuf, (int) rq->current_nr_sectors, stat);
	taskfile_input_data(drive, pBuf, SECTOR_WORDS);
	task_unmap_rq(rq, pBuf, &flags);
	/*
	 * FIXME :: We really can not legally get a new page/bh
	 * regardless, if this is the end of our segment.
	 * BH walking or segment can only be updated after we have a good
	 * GET_STAT(); return.
	 */
	if (--rq->current_nr_sectors <= 0)
		if (!DRIVER(drive)->end_request(drive, 1))
			return ide_stopped;
	/*
	 * ERM, it is techincally legal to leave/exit here but it makes
	 * a mess of the code ...
	 */
	if (HWGROUP(drive)->handler == NULL)
		ide_set_handler(drive, &task_in_intr,  WAIT_CMD, NULL);
	return ide_started;
}

#undef ALTSTAT_SCREW_UP

#ifdef ALTSTAT_SCREW_UP
/*
 * (ks/hs): Poll Alternate Status Register to ensure
 * that drive is not busy.
 */
byte altstat_multi_busy (ide_drive_t *drive, byte stat, const char *msg)
{
	int i;

	DTF("multi%s: ASR = %x\n", msg, stat);
	if (stat & BUSY_STAT) {
		/* (ks/hs): FIXME: Replace hard-coded 100, error handling? */
		for (i=0; i<100; i++) {
			stat = GET_ALTSTAT();
			if ((stat & BUSY_STAT) == 0)
				break;
		}
	}
	/*
	 * (ks/hs): Read Status AFTER Alternate Status Register
	 */
	return(GET_STAT());
}

/*
 * (ks/hs): Poll Alternate status register to wait for drive
 * to become ready for next transfer
 */
byte altstat_multi_poll (ide_drive_t *drive, byte stat, const char *msg)
{

	/* (ks/hs): FIXME: Error handling, time-out? */
	while (stat & BUSY_STAT)
		stat = GET_ALTSTAT();
	DTF("multi%s: nsect=1, ASR = %x\n", msg, stat);
	return(GET_STAT());	/* (ks/hs): Clear pending IRQ */
}
#endif /* ALTSTAT_SCREW_UP */

/*
 * Handler for command with Read Multiple
 */
ide_startstop_t task_mulin_intr (ide_drive_t *drive)
{
#ifdef ALTSTAT_SCREW_UP
	byte stat	= altstat_multi_busy(drive, GET_ALTSTAT(), "read");
#else
	byte stat		= GET_STAT();
#endif /* ALTSTAT_SCREW_UP */
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned int msect	= drive->mult_count;
	unsigned int nsect;
	unsigned long flags;

	if (!OK_STAT(stat,DATA_READY,BAD_R_STAT)) {
		if (stat & (ERR_STAT|DRQ_STAT)) {
			if (!rq->bio) {
				rq->current_nr_sectors += drive->mult_count;
				/*
				 * NOTE: could rewind beyond beginning :-/
				 */
			} else {
				printk("%s: MULTI-READ assume all data " \
					"transfered is bad status=0x%02x\n",
					drive->name, stat);
			}
			return DRIVER(drive)->error(drive, "task_mulin_intr", stat);
		}
		/* no data yet, so wait for another interrupt */
		if (HWGROUP(drive)->handler == NULL)
			ide_set_handler(drive, &task_mulin_intr, WAIT_CMD, NULL);
		return ide_started;
	}

#ifdef ALTSTAT_SCREW_UP
	/*
	 * Screw the request we do not support bad data-phase setups!
	 * Either read and learn the ATA standard or crash yourself!
	 */
	if (!msect) {
		/*
		 * (ks/hs): Drive supports multi-sector transfer,
		 * drive->mult_count was not set
		 */
		nsect = 1;
		while (rq->current_nr_sectors) {
			pBuf = task_map_rq(rq, &flags);
			DTF("Multiread: %p, nsect: %d, " \
				"rq->current_nr_sectors: %ld\n",
				pBuf, nsect, rq->current_nr_sectors);
//			rq->current_nr_sectors -= nsect;
			taskfile_input_data(drive, pBuf, nsect * SECTOR_WORDS);
			task_unmap_rq(rq, pBuf, &flags);
			rq->errors = 0;
			rq->current_nr_sectors -= nsect;
			stat = altstat_multi_poll(drive, GET_ALTSTAT(), "read");
		}
		DRIVER(drive)->end_request(drive, 1);
		return ide_stopped;
	}
#endif /* ALTSTAT_SCREW_UP */

	do {
		nsect = rq->current_nr_sectors;
		if (nsect > msect)
			nsect = msect;
		pBuf = task_map_rq(rq, &flags);
		DTF("Multiread: %p, nsect: %d, msect: %d, " \
			" rq->current_nr_sectors: %d\n",
			pBuf, nsect, msect, rq->current_nr_sectors);
//		rq->current_nr_sectors -= nsect;
//		msect -= nsect;
		taskfile_input_data(drive, pBuf, nsect * SECTOR_WORDS);
		task_unmap_rq(rq, pBuf, &flags);
		rq->errors = 0;
		rq->current_nr_sectors -= nsect;
		msect -= nsect;
		/*
		 * FIXME :: We really can not legally get a new page/bh
		 * regardless, if this is the end of our segment.
		 * BH walking or segment can only be updated after we have a
		 * good GET_STAT(); return.
		 */
		if (!rq->current_nr_sectors) {
			if (!DRIVER(drive)->end_request(drive, 1))
				return ide_stopped;
		}
	} while (msect);
	if (HWGROUP(drive)->handler == NULL)
		ide_set_handler(drive, &task_mulin_intr, WAIT_CMD, NULL);
	return ide_started;
}

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
		printk(KERN_ERR "%s: no DRQ after issuing %s\n",
			drive->name,
			drive->addressing ? "WRITE_EXT" : "WRITE");
		return startstop;
	}
	/* For Write_sectors we need to stuff the first sector */
	pBuf = task_map_rq(rq, &flags);
//	rq->current_nr_sectors--;
	taskfile_output_data(drive, pBuf, SECTOR_WORDS);
	rq->current_nr_sectors--;
	/*
	 * WARNING :: Interrupt could happen instantly :-/
	 */
	task_unmap_rq(rq, pBuf, &flags);
	return ide_started;
}

/*
 * Handler for command with PIO data-out phase WRITE
 *
 * WOOHOO this is a CORRECT STATE DIAGRAM NOW, <andre@linux-ide.org>
 */
ide_startstop_t task_out_intr (ide_drive_t *drive)
{
	byte stat		= GET_STAT();
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned long flags;

	if (!OK_STAT(stat,DRIVE_READY,drive->bad_wstat)) {
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
		if (!DRIVER(drive)->end_request(drive, 1))
			return ide_stopped;
	if ((rq->current_nr_sectors==1) ^ (stat & DRQ_STAT)) {
		rq = HWGROUP(drive)->rq;
		pBuf = task_map_rq(rq, &flags);
		DTF("write: %p, rq->current_nr_sectors: %d\n",
			pBuf, (int) rq->current_nr_sectors);
//		rq->current_nr_sectors--;
		taskfile_output_data(drive, pBuf, SECTOR_WORDS);
		task_unmap_rq(rq, pBuf, &flags);
		rq->errors = 0;
		rq->current_nr_sectors--;
	}
	if (HWGROUP(drive)->handler == NULL)
		ide_set_handler(drive, &task_out_intr, WAIT_CMD, NULL);
	return ide_started;
}

ide_startstop_t pre_task_mulout_intr (ide_drive_t *drive, struct request *rq)
{
	ide_task_t *args = rq->special;
	ide_startstop_t startstop;

#if 0
	/*
	 * assign private copy for multi-write
	 */
	memcpy(&HWGROUP(drive)->wrq, rq, sizeof(struct request));
#endif

	if (ide_wait_stat(&startstop, drive, DATA_READY,
			drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing %s\n",
			drive->name,
			drive->addressing ? "MULTWRITE_EXT" : "MULTWRITE");
		return startstop;
	}
#if 0
	if (wait_for_ready(drive, 100))
		IDE_DEBUG(__LINE__);		//BUG();
#else
	if (!(drive_is_ready(drive))) {
		int i;
		for (i=0; i<100; i++) {
			if (drive_is_ready(drive))
				break;
		}
	}
#endif
	/*
	 * WARNING :: if the drive as not acked good status we may not
	 * move the DATA-TRANSFER T-Bar as BSY != 0. <andre@linux-ide.org>
	 */
	return args->handler(drive);
}

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
#ifdef ALTSTAT_SCREW_UP
	byte stat	= altstat_multi_busy(drive, GET_ALTSTAT(), "write");
#else
	byte stat		= GET_STAT();
#endif /* ALTSTAT_SCREW_UP */

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
				printk("%s: MULTI-WRITE assume all data " \
					"transfered is bad status=0x%02x\n",
					drive->name, stat);
			}
			return DRIVER(drive)->error(drive, "task_mulout_intr", stat);
		}
		if (!rq->bio)
			DRIVER(drive)->end_request(drive, 1);
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
			ide_set_handler(drive, &task_mulout_intr, WAIT_CMD, NULL);
		return ide_started;
	}

	if (HWGROUP(drive)->handler != NULL) {
		unsigned long lflags;
		spin_lock_irqsave(&ide_lock, lflags);
		HWGROUP(drive)->handler = NULL;
		del_timer(&HWGROUP(drive)->timer);
		spin_unlock_irqrestore(&ide_lock, lflags);
	}

#ifdef ALTSTAT_SCREW_UP
	/*
	 * Screw the request we do not support bad data-phase setups!
	 * Either read and learn the ATA standard or crash yourself!
	 */
	if (!msect) {
		nsect = 1;
		while (rq->current_nr_sectors) {
			pBuf = task_map_rq(rq, &flags);
			DTF("Multiwrite: %p, nsect: %d, " \
				"rq->current_nr_sectors: %d\n",
				pBuf, nsect, rq->current_nr_sectors);
//			rq->current_nr_sectors -= nsect;
			taskfile_output_data(drive, pBuf, nsect * SECTOR_WORDS);
			task_unmap_rq(pBuf, &flags);
			rq->errors = 0;
			rq->current_nr_sectors -= nsect;
			stat = altstat_multi_poll(drive, GET_ALTSTAT(), "write");
		}
		DRIVER(drive)->end_request(drive, 1);
		return ide_stopped;
	}
#endif /* ALTSTAT_SCREW_UP */

	do {
		nsect = rq->current_nr_sectors;
		if (nsect > msect)
			nsect = msect;
		pBuf = task_map_rq(rq, &flags);
		DTF("Multiwrite: %p, nsect: %d, msect: %d, " \
			"rq->current_nr_sectors: %ld\n",
			pBuf, nsect, msect, rq->current_nr_sectors);
		msect -= nsect;
//		rq->current_nr_sectors -= nsect;
		taskfile_output_data(drive, pBuf, nsect * SECTOR_WORDS);
		task_unmap_rq(rq, pBuf, &flags);
		rq->current_nr_sectors -= nsect;
		/*
		 * FIXME :: We really can not legally get a new page/bh
		 * regardless, if this is the end of our segment.
		 * BH walking or segment can only be updated after we
		 * have a good  GET_STAT(); return.
		 */
		if (!rq->current_nr_sectors) {
			if (!DRIVER(drive)->end_request(drive, 1))
				if (!rq->bio)
					return ide_stopped;
		}
	} while (msect);
	rq->errors = 0;
	if (HWGROUP(drive)->handler == NULL)
		ide_set_handler(drive, &task_mulout_intr, WAIT_CMD, NULL);
	return ide_started;
}

/* Called by internal to feature out type of command being called */
ide_pre_handler_t * ide_pre_handler_parser (struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile)
{
	switch(taskfile->command) {
				/* IDE_DRIVE_TASK_RAW_WRITE */
		case CFA_WRITE_MULTI_WO_ERASE:
		case WIN_MULTWRITE:
		case WIN_MULTWRITE_EXT:
			return &pre_task_mulout_intr;
			
				/* IDE_DRIVE_TASK_OUT */
		case WIN_WRITE:
		case WIN_WRITE_EXT:
		case WIN_WRITE_VERIFY:
		case WIN_WRITE_BUFFER:
		case CFA_WRITE_SECT_WO_ERASE:
		case WIN_DOWNLOAD_MICROCODE:
			return &pre_task_out_intr;
				/* IDE_DRIVE_TASK_OUT */
		case WIN_SMART:
			if (taskfile->feature == SMART_WRITE_LOG_SECTOR)
				return &pre_task_out_intr;
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_QUEUED:
		case WIN_WRITEDMA_EXT:
		case WIN_WRITEDMA_QUEUED_EXT:
				/* IDE_DRIVE_TASK_OUT */
		default:
			break;
	}
	return(NULL);
}

/* Called by internal to feature out type of command being called */
ide_handler_t * ide_handler_parser (struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile)
{
	switch(taskfile->command) {
		case WIN_IDENTIFY:
		case WIN_PIDENTIFY:
		case CFA_TRANSLATE_SECTOR:
		case WIN_READ_BUFFER:
		case WIN_READ:
		case WIN_READ_EXT:
			return &task_in_intr;
		case WIN_SECURITY_DISABLE:
		case WIN_SECURITY_ERASE_UNIT:
		case WIN_SECURITY_SET_PASS:
		case WIN_SECURITY_UNLOCK:
		case WIN_DOWNLOAD_MICROCODE:
		case CFA_WRITE_SECT_WO_ERASE:
		case WIN_WRITE_BUFFER:
		case WIN_WRITE_VERIFY:
		case WIN_WRITE:
		case WIN_WRITE_EXT:
			return &task_out_intr;
		case WIN_MULTREAD:
		case WIN_MULTREAD_EXT:
			return &task_mulin_intr;
		case CFA_WRITE_MULTI_WO_ERASE:
		case WIN_MULTWRITE:
		case WIN_MULTWRITE_EXT:
			return &task_mulout_intr;
		case WIN_SMART:
			switch(taskfile->feature) {
				case SMART_READ_VALUES:
				case SMART_READ_THRESHOLDS:
				case SMART_READ_LOG_SECTOR:
					return &task_in_intr;
				case SMART_WRITE_LOG_SECTOR:
					return &task_out_intr;
				default:
					return &task_no_data_intr;
			}
		case CFA_REQ_EXT_ERROR_CODE:
		case CFA_ERASE_SECTORS:
		case WIN_VERIFY:
		case WIN_VERIFY_EXT:
		case WIN_SEEK:
			return &task_no_data_intr;
		case WIN_SPECIFY:
			return &set_geometry_intr;
		case WIN_RECAL:
	//	case WIN_RESTORE:
			return &recal_intr;
		case WIN_NOP:
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
			return &task_no_data_intr;
		case WIN_SETMULT:
			return &set_multmode_intr;
		case WIN_READ_NATIVE_MAX:
		case WIN_SET_MAX:
		case WIN_READ_NATIVE_MAX_EXT:
		case WIN_SET_MAX_EXT:
		case WIN_SECURITY_ERASE_PREPARE:
		case WIN_SECURITY_FREEZE_LOCK:
		case WIN_DOORLOCK:
		case WIN_DOORUNLOCK:
		case WIN_SETFEATURES:
			return &task_no_data_intr;
		case DISABLE_SEAGATE:
		case EXABYTE_ENABLE_NEST:
			return &task_no_data_intr;
#ifdef CONFIG_BLK_DEV_IDEDMA
		case WIN_READDMA:
		case WIN_IDENTIFY_DMA:
		case WIN_READDMA_QUEUED:
		case WIN_READDMA_EXT:
		case WIN_READDMA_QUEUED_EXT:
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_QUEUED:
		case WIN_WRITEDMA_EXT:
		case WIN_WRITEDMA_QUEUED_EXT:
#endif
		case WIN_FORMAT:
		case WIN_INIT:
		case WIN_DEVICE_RESET:
		case WIN_QUEUED_SERVICE:
		case WIN_PACKETCMD:
		default:
			return(NULL);
	}	
}

ide_post_handler_t * ide_post_handler_parser (struct hd_drive_task_hdr *taskfile, struct hd_drive_hob_hdr *hobfile)
{
	switch(taskfile->command) {
		case WIN_SPECIFY:	/* set_geometry_intr */
		case WIN_RESTORE:	/* recal_intr */
		case WIN_SETMULT:	/* set_multmode_intr */
		default:
			return(NULL);
	}
}

/* Called by ioctl to feature out type of command being called */
int ide_cmd_type_parser (ide_task_t *args)
{
	struct hd_drive_task_hdr *taskfile = (struct hd_drive_task_hdr *) args->tfRegister;
	struct hd_drive_hob_hdr *hobfile = (struct hd_drive_hob_hdr *) args->hobRegister;

	args->prehandler	= ide_pre_handler_parser(taskfile, hobfile);
	args->handler		= ide_handler_parser(taskfile, hobfile);
	args->posthandler	= ide_post_handler_parser(taskfile, hobfile);

	switch(args->tfRegister[IDE_COMMAND_OFFSET]) {
		case WIN_IDENTIFY:
		case WIN_PIDENTIFY:
			return IDE_DRIVE_TASK_IN;
		case CFA_TRANSLATE_SECTOR:
		case WIN_READ:
		case WIN_READ_EXT:
		case WIN_READ_BUFFER:
			return IDE_DRIVE_TASK_IN;
		case WIN_WRITE:
		case WIN_WRITE_EXT:
		case WIN_WRITE_VERIFY:
		case WIN_WRITE_BUFFER:
		case CFA_WRITE_SECT_WO_ERASE:
		case WIN_DOWNLOAD_MICROCODE:
			return IDE_DRIVE_TASK_RAW_WRITE;
		case WIN_MULTREAD:
		case WIN_MULTREAD_EXT:
			return IDE_DRIVE_TASK_IN;
		case CFA_WRITE_MULTI_WO_ERASE:
		case WIN_MULTWRITE:
		case WIN_MULTWRITE_EXT:
			return IDE_DRIVE_TASK_RAW_WRITE;
		case WIN_SECURITY_DISABLE:
		case WIN_SECURITY_ERASE_UNIT:
		case WIN_SECURITY_SET_PASS:
		case WIN_SECURITY_UNLOCK:
			return IDE_DRIVE_TASK_OUT;
		case WIN_SMART:
			args->tfRegister[IDE_LCYL_OFFSET] = SMART_LCYL_PASS;
			args->tfRegister[IDE_HCYL_OFFSET] = SMART_HCYL_PASS;
			switch(args->tfRegister[IDE_FEATURE_OFFSET]) {
				case SMART_READ_VALUES:
				case SMART_READ_THRESHOLDS:
				case SMART_READ_LOG_SECTOR:
					return IDE_DRIVE_TASK_IN;
				case SMART_WRITE_LOG_SECTOR:
					return IDE_DRIVE_TASK_OUT;
				default:
					return IDE_DRIVE_TASK_NO_DATA;
			}
#ifdef CONFIG_BLK_DEV_IDEDMA
		case WIN_READDMA:
		case WIN_IDENTIFY_DMA:
		case WIN_READDMA_QUEUED:
		case WIN_READDMA_EXT:
		case WIN_READDMA_QUEUED_EXT:
			return IDE_DRIVE_TASK_IN;
		case WIN_WRITEDMA:
		case WIN_WRITEDMA_QUEUED:
		case WIN_WRITEDMA_EXT:
		case WIN_WRITEDMA_QUEUED_EXT:
			return IDE_DRIVE_TASK_RAW_WRITE;
#endif
		case WIN_SETFEATURES:
			switch(args->tfRegister[IDE_FEATURE_OFFSET]) {
				case SETFEATURES_EN_8BIT:
				case SETFEATURES_EN_WCACHE:
					return IDE_DRIVE_TASK_NO_DATA;
				case SETFEATURES_XFER:
					return IDE_DRIVE_TASK_SET_XFER;
				case SETFEATURES_DIS_DEFECT:
				case SETFEATURES_EN_APM:
				case SETFEATURES_DIS_MSN:
				case SETFEATURES_DIS_RETRY:
				case SETFEATURES_EN_AAM:
				case SETFEATURES_RW_LONG:
				case SETFEATURES_SET_CACHE:
				case SETFEATURES_DIS_RLA:
				case SETFEATURES_EN_RI:
				case SETFEATURES_EN_SI:
				case SETFEATURES_DIS_RPOD:
				case SETFEATURES_DIS_WCACHE:
				case SETFEATURES_EN_DEFECT:
				case SETFEATURES_DIS_APM:
				case SETFEATURES_EN_ECC:
				case SETFEATURES_EN_MSN:
				case SETFEATURES_EN_RETRY:
				case SETFEATURES_EN_RLA:
				case SETFEATURES_PREFETCH:
				case SETFEATURES_4B_RW_LONG:
				case SETFEATURES_DIS_AAM:
				case SETFEATURES_EN_RPOD:
				case SETFEATURES_DIS_RI:
				case SETFEATURES_DIS_SI:
				default:
					return IDE_DRIVE_TASK_NO_DATA;
			}
		case WIN_NOP:
		case CFA_REQ_EXT_ERROR_CODE:
		case CFA_ERASE_SECTORS:
		case WIN_VERIFY:
		case WIN_VERIFY_EXT:
		case WIN_SEEK:
		case WIN_SPECIFY:
		case WIN_RESTORE:
		case WIN_DIAGNOSE:
		case WIN_FLUSH_CACHE:
		case WIN_FLUSH_CACHE_EXT:
		case WIN_STANDBYNOW1:
		case WIN_STANDBYNOW2:
		case WIN_SLEEPNOW1:
		case WIN_SLEEPNOW2:
		case WIN_SETIDLE1:
		case DISABLE_SEAGATE:
		case WIN_CHECKPOWERMODE1:
		case WIN_CHECKPOWERMODE2:
		case WIN_GETMEDIASTATUS:
		case WIN_MEDIAEJECT:
		case WIN_SETMULT:
		case WIN_READ_NATIVE_MAX:
		case WIN_SET_MAX:
		case WIN_READ_NATIVE_MAX_EXT:
		case WIN_SET_MAX_EXT:
		case WIN_SECURITY_ERASE_PREPARE:
		case WIN_SECURITY_FREEZE_LOCK:
		case EXABYTE_ENABLE_NEST:
		case WIN_DOORLOCK:
		case WIN_DOORUNLOCK:
			return IDE_DRIVE_TASK_NO_DATA;
		case WIN_FORMAT:
		case WIN_INIT:
		case WIN_DEVICE_RESET:
		case WIN_QUEUED_SERVICE:
		case WIN_PACKETCMD:
		default:
			return IDE_DRIVE_TASK_INVALID;
	}
}

/*
 * NOTICE: This is additions from IBM to provide a discrete interface,
 * for selective taskregister access operations.  Nice JOB Klaus!!!
 * Glad to be able to work and co-develop this with you and IBM.
 */
ide_startstop_t flagged_taskfile (ide_drive_t *drive, ide_task_t *task)
{
	task_struct_t *taskfile = (task_struct_t *) task->tfRegister;
	hob_struct_t *hobfile = (hob_struct_t *) task->hobRegister;
	struct hd_driveid *id = drive->id;
#if DEBUG_TASKFILE
	byte status;
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
		if ((id->command_set_2 & 0x0400) &&
		    (id->cfs_enable_2 & 0x0400) &&
		    (drive->addressing == 1)) {
			task->tf_out_flags.all |= (IDE_HOB_STD_OUT_FLAGS << 8);
		}
        }

	if (task->tf_in_flags.all == 0) {
		task->tf_in_flags.all = IDE_TASKFILE_STD_IN_FLAGS;
		if ((id->command_set_2 & 0x0400) &&
		    (id->cfs_enable_2 & 0x0400) &&
		    (drive->addressing == 1)) {
			task->tf_in_flags.all |= (IDE_HOB_STD_IN_FLAGS  << 8);
		}
        }

	/* ALL Command Block Executions SHALL clear nIEN, unless otherwise */
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl, IDE_CONTROL_REG);	/* clear nIEN */
	SELECT_MASK(HWIF(drive), drive, 0);

#if DEBUG_TASKFILE
	status = GET_STAT();
	if (status & 0x80) {
		printk("flagged_taskfile -> Bad status. Status = %02x. wait 100 usec ...\n", status);
		udelay(100);
		status = GET_STAT();
		printk("flagged_taskfile -> Status = %02x\n", status);
	}
#endif

	if (task->tf_out_flags.b.data) {
		unsigned short data =  taskfile->data + (hobfile->data << 8);
		OUT_WORD(data, IDE_DATA_REG);
	}

	/* (ks) send hob registers first */
	if (task->tf_out_flags.b.nsector_hob)
		OUT_BYTE(hobfile->sector_count, IDE_NSECTOR_REG);
	if (task->tf_out_flags.b.sector_hob)
		OUT_BYTE(hobfile->sector_number, IDE_SECTOR_REG);
	if (task->tf_out_flags.b.lcyl_hob)
		OUT_BYTE(hobfile->low_cylinder, IDE_LCYL_REG);
	if (task->tf_out_flags.b.hcyl_hob)
		OUT_BYTE(hobfile->high_cylinder, IDE_HCYL_REG);

	/* (ks) Send now the standard registers */
	if (task->tf_out_flags.b.error_feature)
		OUT_BYTE(taskfile->feature, IDE_FEATURE_REG);
	/* refers to number of sectors to transfer */
	if (task->tf_out_flags.b.nsector)
		OUT_BYTE(taskfile->sector_count, IDE_NSECTOR_REG);
	/* refers to sector offset or start sector */
	if (task->tf_out_flags.b.sector)
		OUT_BYTE(taskfile->sector_number, IDE_SECTOR_REG);
	if (task->tf_out_flags.b.lcyl)
		OUT_BYTE(taskfile->low_cylinder, IDE_LCYL_REG);
	if (task->tf_out_flags.b.hcyl)
		OUT_BYTE(taskfile->high_cylinder, IDE_HCYL_REG);

        /*
	 * (ks) In the flagged taskfile approch, we will used all specified
	 * registers and the register value will not be changed. Except the
	 * select bit (master/slave) in the drive_head register. We must make
	 * sure that the desired drive is selected.
	 */
	OUT_BYTE(taskfile->device_head | drive->select.all, IDE_SELECT_REG);
	switch(task->data_phase) {

   	        case TASKFILE_OUT_DMAQ:
		case TASKFILE_OUT_DMA:
			HWIF(drive)->dmaproc(ide_dma_write, drive);
			break;

		case TASKFILE_IN_DMAQ:
		case TASKFILE_IN_DMA:
			HWIF(drive)->dmaproc(ide_dma_read, drive);
			break;

	        default:
 			if (task->handler == NULL)
				return ide_stopped;

			ide_set_handler (drive, task->handler, WAIT_WORSTCASE, NULL);
			/* Issue the command */
			OUT_BYTE(taskfile->command, IDE_COMMAND_REG);
			if (task->prehandler != NULL)
				return task->prehandler(drive, HWGROUP(drive)->rq);
	}

	return ide_started;
}

ide_startstop_t flagged_task_no_data_intr (ide_drive_t *drive)
{
	byte stat = GET_STAT();

	local_irq_enable();

	if (!OK_STAT(stat, READY_STAT, BAD_STAT)) {
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

	ide_end_drive_cmd (drive, stat, GET_ERR());

	return ide_stopped;
}

/*
 * Handler for command with PIO data-in phase
 */
ide_startstop_t flagged_task_in_intr (ide_drive_t *drive)
{
	byte stat		= GET_STAT();
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
	while (((stat = GET_STAT()) & BUSY_STAT) && retries--)
		udelay(10);
	ide_end_drive_cmd (drive, stat, GET_ERR());

	return ide_stopped;
}

ide_startstop_t flagged_task_mulin_intr (ide_drive_t *drive)
{
	byte stat		= GET_STAT();
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
	while (((stat = GET_STAT()) & BUSY_STAT) && retries--)
		udelay(10);
	ide_end_drive_cmd (drive, stat, GET_ERR());

	return ide_stopped;
}

/*
 * Pre handler for command with PIO data-out phase
 */
ide_startstop_t flagged_pre_task_out_intr (ide_drive_t *drive, struct request *rq)
{
	byte stat		= GET_STAT();
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
	byte stat		= GET_STAT();
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;

	if (!OK_STAT(stat, DRIVE_READY, BAD_W_STAT)) 
		return DRIVER(drive)->error(drive, "flagged_task_out_intr", stat);
	
	if (!rq->current_nr_sectors) { 
		ide_end_drive_cmd (drive, stat, GET_ERR());
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
	byte stat		= GET_STAT();
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
	byte stat		= GET_STAT();
	struct request *rq	= HWGROUP(drive)->rq;
	char *pBuf		= NULL;
	unsigned int msect, nsect;

	msect = drive->mult_count;
	if (msect == 0)
		return DRIVER(drive)->error(drive, "flagged_task_mulout_intr (multimode not set)", stat);

	if (!OK_STAT(stat, DRIVE_READY, BAD_W_STAT)) 
		return DRIVER(drive)->error(drive, "flagged_task_mulout_intr", stat);
	
	if (!rq->current_nr_sectors) { 
		ide_end_drive_cmd (drive, stat, GET_ERR());
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

/*
 * This function is intended to be used prior to invoking ide_do_drive_cmd().
 */
void ide_init_drive_taskfile (struct request *rq)
{
	memset(rq, 0, sizeof(*rq));
	rq->flags = REQ_DRIVE_TASKFILE;
}

int ide_diag_taskfile (ide_drive_t *drive, ide_task_t *args, unsigned long data_size, byte *buf)
{
	struct request rq;

	ide_init_drive_taskfile(&rq);
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
			rq.current_nr_sectors = rq.nr_sectors = (args->hobRegister[IDE_NSECTOR_OFFSET_HOB] << 8) | args->tfRegister[IDE_NSECTOR_OFFSET];
		/*	rq.hard_cur_sectors	*/
		else
			rq.current_nr_sectors = rq.nr_sectors = data_size / SECTOR_SIZE;
		/*	rq.hard_cur_sectors	*/
	}

	if (args->tf_out_flags.all == 0) {
		/*
		 * clean up kernel settings for driver sanity, regardless.
		 * except for discrete diag services.
		 */
		args->posthandler = ide_post_handler_parser(
				(struct hd_drive_task_hdr *) args->tfRegister,
				(struct hd_drive_hob_hdr *) args->hobRegister);

	}
	rq.special = args;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

int ide_raw_taskfile (ide_drive_t *drive, ide_task_t *args, byte *buf)
{
	return ide_diag_taskfile(drive, args, 0, buf);
}
	
#ifdef CONFIG_IDE_TASK_IOCTL_DEBUG
char * ide_ioctl_verbose (unsigned int cmd)
{
	return("unknown");
}

char * ide_task_cmd_verbose (byte task)
{
	return("unknown");
}
#endif /* CONFIG_IDE_TASK_IOCTL_DEBUG */

#define MAX_DMA		(256*SECTOR_WORDS)

int ide_taskfile_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	ide_task_request_t	*req_task;
	ide_task_t		args;
	byte *outbuf		= NULL;
	byte *inbuf		= NULL;
	task_ioreg_t *argsptr	= args.tfRegister;
	task_ioreg_t *hobsptr	= args.hobRegister;
	int err			= 0;
	int tasksize		= sizeof(struct ide_task_request_s);
	int taskin		= 0;
	int taskout		= 0;
	byte io_32bit		= drive->io_32bit;

//	printk("IDE Taskfile ...\n");

	req_task = kmalloc(tasksize, GFP_KERNEL);
	if (req_task == NULL) return -ENOMEM;
	memset(req_task, 0, tasksize);
	if (copy_from_user(req_task, (void *) arg, tasksize)) {
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
		if (copy_from_user(outbuf, (void *)arg + outtotal, taskout)) {
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
		if (copy_from_user(inbuf, (void *)arg + intotal , taskin)) {
			err = -EFAULT;
			goto abort;
		}
	}

	memset (&args, 0, sizeof (ide_task_t) );
	memcpy(argsptr, req_task->io_ports, HDIO_DRIVE_TASK_HDR_SIZE);
	memcpy(hobsptr, req_task->hob_ports, HDIO_DRIVE_HOB_HDR_SIZE);

	args.tf_in_flags  = req_task->in_flags;
	args.tf_out_flags = req_task->out_flags;
	args.data_phase   = req_task->data_phase;
	args.command_type = req_task->req_cmd;

#ifdef CONFIG_IDE_TASK_IOCTL_DEBUG
	DTF("%s: ide_ioctl_cmd %s:  ide_task_cmd %s\n",
		drive->name,
		ide_ioctl_verbose(cmd),
		ide_task_cmd_verbose(args.tfRegister[IDE_COMMAND_OFFSET]));
#endif /* CONFIG_IDE_TASK_IOCTL_DEBUG */

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
			args.posthandler = NULL;
			err = ide_diag_taskfile(drive, &args, taskout, outbuf);
			args.prehandler = NULL;
			args.handler = &task_in_intr;
			args.posthandler = NULL;
			err = ide_diag_taskfile(drive, &args, taskin, inbuf);
			break;
#else
			err = -EFAULT;
			goto abort;
#endif
		case TASKFILE_MULTI_OUT:
			if (!drive->mult_count) {
				/* (hs): give up if multcount is not set */
				printk("%s: %s Multimode Write " \
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
				printk("%s: %s Multimode Read failure " \
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

	if (copy_to_user((void *)arg, req_task, tasksize)) {
		err = -EFAULT;
		goto abort;
	}
	if (taskout) {
		int outtotal = tasksize;
		if (copy_to_user((void *)arg+outtotal, outbuf, taskout)) {
			err = -EFAULT;
			goto abort;
		}
	}
	if (taskin) {
		int intotal = tasksize + taskout;
		if (copy_to_user((void *)arg+intotal, inbuf, taskin)) {
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

int ide_ata66_check (ide_drive_t *drive, ide_task_t *args);
int set_transfer(ide_drive_t *drive, ide_task_t *args);

/*
 * FIXME : this needs to map into at taskfile. <andre@linux-ide.org>
 */
int ide_cmd_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
#if 1
	int err = 0;
	byte args[4], *argbuf = args;
	byte xfer_rate = 0;
	int argsize = 4;
	ide_task_t tfargs;

	if (NULL == (void *) arg) {
		struct request rq;
		ide_init_drive_cmd(&rq);
		return ide_do_drive_cmd(drive, &rq, ide_wait);
	}

	if (copy_from_user(args, (void *)arg, 4))
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
		if ((HWIF(drive)->speedproc) != NULL)
			HWIF(drive)->speedproc(drive, xfer_rate);
		ide_driveid_update(drive);
	}
abort:
	if (copy_to_user((void *)arg, argbuf, argsize))
		err = -EFAULT;
	if (argsize > 4)
		kfree(argbuf);
	return err;

#else

	int err = 0;
	byte args[4], *argbuf = args;
	byte xfer_rate = 0;
	int argsize = 0;
	ide_task_t tfargs;

	if (NULL == (void *) arg) {
		struct request rq;
		ide_init_drive_cmd(&rq);
		return ide_do_drive_cmd(drive, &rq, ide_wait);
	}

	if (copy_from_user(args, (void *)arg, 4))
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
		argsize = (SECTOR_WORDS * 4 * args[3]);
		argbuf = kmalloc(argsize, GFP_KERNEL);
		if (argbuf == NULL)
			return -ENOMEM;
	}

	if (set_transfer(drive, &tfargs)) {
		xfer_rate = args[1];
		if (ide_ata66_check(drive, &tfargs))
			goto abort;
	}

	tfargs.command_type = ide_cmd_type_parser(&tfargs);
	err = ide_raw_taskfile(drive, &tfargs, argbuf);

	if (!err && xfer_rate) {
		/* active-retuning-calls future */
		if ((HWIF(drive)->speedproc) != NULL)
			HWIF(drive)->speedproc(drive, xfer_rate);
		ide_driveid_update(drive);
	}
abort:

	args[0] = tfargs.tfRegister[IDE_COMMAND_OFFSET];
	args[1] = tfargs.tfRegister[IDE_FEATURE_OFFSET];
	args[2] = tfargs.tfRegister[IDE_NSECTOR_OFFSET];
	args[3] = 0;

	if (copy_to_user((void *)arg, argbuf, 4))
		err = -EFAULT;
	if (argbuf != NULL) {
		if (copy_to_user((void *)arg, argbuf + 4, argsize))
			err = -EFAULT;
		kfree(argbuf);
	}
	return err;

#endif

}

/*
 * FIXME : this needs to map into at taskfile. <andre@linux-ide.org>
 */
int ide_task_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	byte args[7], *argbuf = args;
	int argsize = 7;

	if (copy_from_user(args, (void *)arg, 7))
		return -EFAULT;
	err = ide_wait_cmd_task(drive, argbuf);
	if (copy_to_user((void *)arg, argbuf, argsize))
		err = -EFAULT;
	return err;
}

EXPORT_SYMBOL(drive_is_ready);
EXPORT_SYMBOL(wait_for_ready);

EXPORT_SYMBOL(task_read_24);
EXPORT_SYMBOL(ata_input_data);
EXPORT_SYMBOL(ata_output_data);
EXPORT_SYMBOL(atapi_input_bytes);
EXPORT_SYMBOL(atapi_output_bytes);
EXPORT_SYMBOL(taskfile_input_data);
EXPORT_SYMBOL(taskfile_output_data);

EXPORT_SYMBOL(ide_wait_stat);
EXPORT_SYMBOL(do_rw_taskfile);
EXPORT_SYMBOL(flagged_taskfile);
EXPORT_SYMBOL(ide_end_taskfile);

EXPORT_SYMBOL(set_multmode_intr);
EXPORT_SYMBOL(set_geometry_intr);
EXPORT_SYMBOL(recal_intr);

EXPORT_SYMBOL(task_no_data_intr);
EXPORT_SYMBOL(task_in_intr);
EXPORT_SYMBOL(task_mulin_intr);
EXPORT_SYMBOL(pre_task_out_intr);
EXPORT_SYMBOL(task_out_intr);
EXPORT_SYMBOL(pre_task_mulout_intr);
EXPORT_SYMBOL(task_mulout_intr);

EXPORT_SYMBOL(ide_init_drive_taskfile);
EXPORT_SYMBOL(ide_raw_taskfile);
EXPORT_SYMBOL(ide_pre_handler_parser);
EXPORT_SYMBOL(ide_handler_parser);
EXPORT_SYMBOL(ide_post_handler_parser);
EXPORT_SYMBOL(ide_cmd_type_parser);
EXPORT_SYMBOL(ide_taskfile_ioctl);
EXPORT_SYMBOL(ide_cmd_ioctl);
EXPORT_SYMBOL(ide_task_ioctl);

/*
 * Beginning of Taskfile OPCODE Library and feature sets.
 */

/*
 *  All hosts that use the 80c ribbon must use!
 *  The name is derived from upper byte of word 93 and the 80c ribbon.
 */
byte eighty_ninty_three (ide_drive_t *drive)
{
#if 0
	if (!HWIF(drive)->udma_four)
		return 0;

	if (drive->id->major_rev_num) {
		int hssbd = 0;
		int i;
		/*
		 * Determime highest Supported SPEC
		 */
		for (i=1; i<=15; i++)
			if (drive->id->major_rev_num & (1<<i))
				hssbd++;

		switch (hssbd) {
			case 7:

			case 6:
			case 5:
		/* ATA-4 and older do not support above Ultra 33 */
			default:
				return 0;
		}
	}

	return ((byte) (
#ifndef CONFIG_IDEDMA_IVB
		(drive->id->hw_config & 0x4000) &&
#endif /* CONFIG_IDEDMA_IVB */
		 (drive->id->hw_config & 0x6000)) ? 1 : 0);

#else

	return ((byte) ((HWIF(drive)->udma_four) &&
#ifndef CONFIG_IDEDMA_IVB
			(drive->id->hw_config & 0x4000) &&
#endif /* CONFIG_IDEDMA_IVB */
			(drive->id->hw_config & 0x6000)) ? 1 : 0);
#endif
}

int ide_ata66_check (ide_drive_t *drive, ide_task_t *args)
{
	if (!HWIF(drive)->udma_four) {
		printk("%s: Speed warnings UDMA 3/4/5 is not functional.\n",
			HWIF(drive)->name);
		return 1;
	}
	if ((args->tfRegister[IDE_COMMAND_OFFSET] == WIN_SETFEATURES) &&
	    (args->tfRegister[IDE_SECTOR_OFFSET] > XFER_UDMA_2) &&
	    (args->tfRegister[IDE_FEATURE_OFFSET] == SETFEATURES_XFER)) {
#ifndef CONFIG_IDEDMA_IVB
		if ((drive->id->hw_config & 0x6000) == 0) {
#else /* !CONFIG_IDEDMA_IVB */
		if (((drive->id->hw_config & 0x2000) == 0) ||
		    ((drive->id->hw_config & 0x4000) == 0)) {
#endif /* CONFIG_IDEDMA_IVB */
			printk("%s: Speed warnings UDMA 3/4/5 is not functional.\n", drive->name);
			return 1;
		}
	}
	return 0;
}

/*
 * Backside of HDIO_DRIVE_CMD call of SETFEATURES_XFER.
 * 1 : Safe to update drive->id DMA registers.
 * 0 : OOPs not allowed.
 */
int set_transfer (ide_drive_t *drive, ide_task_t *args)
{
	if ((args->tfRegister[IDE_COMMAND_OFFSET] == WIN_SETFEATURES) &&
	    (args->tfRegister[IDE_SECTOR_OFFSET] >= XFER_SW_DMA_0) &&
	    (args->tfRegister[IDE_FEATURE_OFFSET] == SETFEATURES_XFER) &&
	    (drive->id->dma_ultra ||
	     drive->id->dma_mword ||
	     drive->id->dma_1word))
		return 1;

	return 0;
}

byte ide_auto_reduce_xfer (ide_drive_t *drive)
{
	if (!drive->crc_count)
		return drive->current_speed;
	drive->crc_count = 0;

	switch(drive->current_speed) {
		case XFER_UDMA_7:	return XFER_UDMA_6;
		case XFER_UDMA_6:	return XFER_UDMA_5;
		case XFER_UDMA_5:	return XFER_UDMA_4;
		case XFER_UDMA_4:	return XFER_UDMA_3;
		case XFER_UDMA_3:	return XFER_UDMA_2;
		case XFER_UDMA_2:	return XFER_UDMA_1;
		case XFER_UDMA_1:	return XFER_UDMA_0;
			/*
			 * OOPS we do not goto non Ultra DMA modes
			 * without iCRC's available we force
			 * the system to PIO and make the user
			 * invoke the ATA-1 ATA-2 DMA modes.
			 */
		case XFER_UDMA_0:
		default:		return XFER_PIO_4;
	}
}

int taskfile_lib_get_identify (ide_drive_t *drive, byte *buf)
{
	ide_task_t args;
	memset(&args, 0, sizeof(ide_task_t));
	args.tfRegister[IDE_NSECTOR_OFFSET]	= 0x01;
	if (drive->media == ide_disk)
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_IDENTIFY;
	else
		args.tfRegister[IDE_COMMAND_OFFSET]	= WIN_PIDENTIFY;
	args.command_type			= ide_cmd_type_parser(&args);
	return ide_raw_taskfile(drive, &args, buf);
}

/*
 * Update the 
 */
int ide_driveid_update (ide_drive_t *drive)
{
#if 0
	struct hd_driveid *id;

	id = kmalloc(SECTOR_WORDS*4, GFP_ATOMIC);
	if (!id)
		return 0;

	taskfile_lib_get_identify(drive, (char *)&id);

	ide_fix_driveid(id);
	if (id) {
		drive->id->dma_ultra = id->dma_ultra;
		drive->id->dma_mword = id->dma_mword;
		drive->id->dma_1word = id->dma_1word;
		/* anything more ? */
		kfree(id);
	}
	return 1;
#else
	/*
	 * Re-read drive->id for possible DMA mode
	 * change (copied from ide-probe.c)
	 */
	struct hd_driveid *id;
	unsigned long timeout, flags;

	SELECT_MASK(HWIF(drive), drive, 1);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl,IDE_CONTROL_REG);
	ide_delay_50ms();
	OUT_BYTE(WIN_IDENTIFY, IDE_COMMAND_REG);
	timeout = jiffies + WAIT_WORSTCASE;
	do {
		if (time_after(jiffies, timeout)) {
			SELECT_MASK(HWIF(drive), drive, 0);
			return 0;	/* drive timed-out */
		}
		ide_delay_50ms();	/* give drive a breather */
	} while (IN_BYTE(IDE_ALTSTATUS_REG) & BUSY_STAT);
	ide_delay_50ms();	/* wait for IRQ and DRQ_STAT */
	if (!OK_STAT(GET_STAT(),DRQ_STAT,BAD_R_STAT)) {
		SELECT_MASK(HWIF(drive), drive, 0);
		printk("%s: CHECK for good STATUS\n", drive->name);
		return 0;
	}
	local_irq_save(flags);
	SELECT_MASK(HWIF(drive), drive, 0);
	id = kmalloc(SECTOR_WORDS*4, GFP_ATOMIC);
	if (!id) {
		local_irq_restore(flags);
		return 0;
	}
	ata_input_data(drive, id, SECTOR_WORDS);
	(void) GET_STAT();	/* clear drive IRQ */
	local_irq_enable();
	local_irq_restore(flags);
	ide_fix_driveid(id);
	if (id) {
		drive->id->dma_ultra = id->dma_ultra;
		drive->id->dma_mword = id->dma_mword;
		drive->id->dma_1word = id->dma_1word;
		/* anything more ? */
		kfree(id);
	}

	return 1;
#endif
}


/*
 * Similar to ide_wait_stat(), except it never calls ide_error internally.
 * This is a kludge to handle the new ide_config_drive_speed() function,
 * and should not otherwise be used anywhere.  Eventually, the tuneproc's
 * should be updated to return ide_startstop_t, in which case we can get
 * rid of this abomination again.  :)   -ml
 *
 * It is gone..........
 *
 * const char *msg == consider adding for verbose errors.
 */
int ide_config_drive_speed (ide_drive_t *drive, byte speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	int	i, error	= 1;
	byte stat;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
	hwif->dmaproc(ide_dma_host_off, drive);
#endif /* (CONFIG_BLK_DEV_IDEDMA) && !(CONFIG_DMA_NONPCI) */

	/*
	 * Don't use ide_wait_cmd here - it will
	 * attempt to set_geometry and recalibrate,
	 * but for some reason these don't work at
	 * this point (lost interrupt).
	 */
        /*
         * Select the drive, and issue the SETFEATURES command
         */
	disable_irq(hwif->irq);	/* disable_irq_nosync ?? */
	udelay(1);
	SELECT_DRIVE(HWIF(drive), drive);
	SELECT_MASK(HWIF(drive), drive, 0);
	udelay(1);
	if (IDE_CONTROL_REG)
		OUT_BYTE(drive->ctl | 2, IDE_CONTROL_REG);
	OUT_BYTE(speed, IDE_NSECTOR_REG);
	OUT_BYTE(SETFEATURES_XFER, IDE_FEATURE_REG);
	OUT_BYTE(WIN_SETFEATURES, IDE_COMMAND_REG);
	if ((IDE_CONTROL_REG) && (drive->quirk_list == 2))
		OUT_BYTE(drive->ctl, IDE_CONTROL_REG);
	udelay(1);
	/*
	 * Wait for drive to become non-BUSY
	 */
	if ((stat = GET_STAT()) & BUSY_STAT) {
		unsigned long flags, timeout;
		local_irq_set(flags);
		timeout = jiffies + WAIT_CMD;
		while ((stat = GET_STAT()) & BUSY_STAT) {
			if (time_after(jiffies, timeout))
				break;
		}
		local_irq_restore(flags);
	}

	/*
	 * Allow status to settle, then read it again.
	 * A few rare drives vastly violate the 400ns spec here,
	 * so we'll wait up to 10usec for a "good" status
	 * rather than expensively fail things immediately.
	 * This fix courtesy of Matthew Faupel & Niccolo Rigacci.
	 */
	for (i = 0; i < 10; i++) {
		udelay(1);
		if (OK_STAT((stat = GET_STAT()), DRIVE_READY, BUSY_STAT|DRQ_STAT|ERR_STAT)) {
			error = 0;
			break;
		}
	}

	SELECT_MASK(HWIF(drive), drive, 0);

	enable_irq(hwif->irq);

	if (error) {
		(void) ide_dump_status(drive, "set_drive_speed_status", stat);
		return error;
	}

	drive->id->dma_ultra &= ~0xFF00;
	drive->id->dma_mword &= ~0x0F00;
	drive->id->dma_1word &= ~0x0F00;

#if defined(CONFIG_BLK_DEV_IDEDMA) && !defined(CONFIG_DMA_NONPCI)
	if (speed >= XFER_SW_DMA_0)
		hwif->dmaproc(ide_dma_host_on, drive);
#endif /* (CONFIG_BLK_DEV_IDEDMA) && !(CONFIG_DMA_NONPCI) */

	switch(speed) {
		case XFER_UDMA_7:   drive->id->dma_ultra |= 0x8080; break;
		case XFER_UDMA_6:   drive->id->dma_ultra |= 0x4040; break;
		case XFER_UDMA_5:   drive->id->dma_ultra |= 0x2020; break;
		case XFER_UDMA_4:   drive->id->dma_ultra |= 0x1010; break;
		case XFER_UDMA_3:   drive->id->dma_ultra |= 0x0808; break;
		case XFER_UDMA_2:   drive->id->dma_ultra |= 0x0404; break;
		case XFER_UDMA_1:   drive->id->dma_ultra |= 0x0202; break;
		case XFER_UDMA_0:   drive->id->dma_ultra |= 0x0101; break;
		case XFER_MW_DMA_2: drive->id->dma_mword |= 0x0404; break;
		case XFER_MW_DMA_1: drive->id->dma_mword |= 0x0202; break;
		case XFER_MW_DMA_0: drive->id->dma_mword |= 0x0101; break;
		case XFER_SW_DMA_2: drive->id->dma_1word |= 0x0404; break;
		case XFER_SW_DMA_1: drive->id->dma_1word |= 0x0202; break;
		case XFER_SW_DMA_0: drive->id->dma_1word |= 0x0101; break;
		default: break;
	}
	if (!drive->init_speed)
		drive->init_speed = speed;
	drive->current_speed = speed;
	return error;
}

EXPORT_SYMBOL(eighty_ninty_three);
EXPORT_SYMBOL(ide_auto_reduce_xfer);
EXPORT_SYMBOL(set_transfer);
EXPORT_SYMBOL(taskfile_lib_get_identify);
EXPORT_SYMBOL(ide_driveid_update);
EXPORT_SYMBOL(ide_config_drive_speed);

#ifdef CONFIG_PKT_TASK_IOCTL

#if 0
{

{ /* start cdrom */

	struct cdrom_info *info = drive->driver_data;

	if (info->dma) {
		if (info->cmd == READ) {
			info->dma = !HWIF(drive)->dmaproc(ide_dma_read, drive);
		} else if (info->cmd == WRITE) {
			info->dma = !HWIF(drive)->dmaproc(ide_dma_write, drive);
		} else {
			printk("ide-cd: DMA set, but not allowed\n");
		}
	}

	/* Set up the controller registers. */
	OUT_BYTE (info->dma, IDE_FEATURE_REG);
	OUT_BYTE (0, IDE_NSECTOR_REG);
	OUT_BYTE (0, IDE_SECTOR_REG);

	OUT_BYTE (xferlen & 0xff, IDE_LCYL_REG);
	OUT_BYTE (xferlen >> 8  , IDE_HCYL_REG);
	if (IDE_CONTROL_REG)
		OUT_BYTE (drive->ctl, IDE_CONTROL_REG);

	if (info->dma)
		(void) (HWIF(drive)->dmaproc(ide_dma_begin, drive));

	if (CDROM_CONFIG_FLAGS (drive)->drq_interrupt) {
		ide_set_handler (drive, handler, WAIT_CMD, cdrom_timer_expiry);
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG); /* packet command */
		return ide_started;
	} else {
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG); /* packet command */
		return (*handler) (drive);
	}

} /* end cdrom */

{ /* start floppy */

	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_bcount_reg_t bcount;
	int dma_ok = 0;

	floppy->pc=pc;		/* Set the current packet command */

	pc->retries++;
	pc->actually_transferred=0; /* We haven't transferred any data yet */
	pc->current_position=pc->buffer;
	bcount.all = IDE_MIN(pc->request_transfer, 63 * 1024);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_and_clear_bit (PC_DMA_ERROR, &pc->flags)) {
		(void) HWIF(drive)->dmaproc(ide_dma_off, drive);
	}
	if (test_bit (PC_DMA_RECOMMENDED, &pc->flags) && drive->using_dma)
		dma_ok=!HWIF(drive)->dmaproc(test_bit (PC_WRITING, &pc->flags) ? ide_dma_write : ide_dma_read, drive);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	if (IDE_CONTROL_REG)
		OUT_BYTE (drive->ctl,IDE_CONTROL_REG);
	OUT_BYTE (dma_ok ? 1:0,IDE_FEATURE_REG);	/* Use PIO/DMA */
	OUT_BYTE (bcount.b.high,IDE_BCOUNTH_REG);
	OUT_BYTE (bcount.b.low,IDE_BCOUNTL_REG);
	OUT_BYTE (drive->select.all,IDE_SELECT_REG);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (dma_ok) {	/* Begin DMA, if necessary */
		set_bit (PC_DMA_IN_PROGRESS, &pc->flags);
		(void) (HWIF(drive)->dmaproc(ide_dma_begin, drive));
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */

} /* end floppy */

{ /* start tape */

	idetape_tape_t *tape = drive->driver_data;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_and_clear_bit (PC_DMA_ERROR, &pc->flags)) {
		printk (KERN_WARNING "ide-tape: DMA disabled, reverting to PIO\n");
		(void) HWIF(drive)->dmaproc(ide_dma_off, drive);
	}
	if (test_bit (PC_DMA_RECOMMENDED, &pc->flags) && drive->using_dma)
		dma_ok=!HWIF(drive)->dmaproc(test_bit (PC_WRITING, &pc->flags) ? ide_dma_write : ide_dma_read, drive);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	if (IDE_CONTROL_REG)
		OUT_BYTE (drive->ctl,IDE_CONTROL_REG);
	OUT_BYTE (dma_ok ? 1:0,IDE_FEATURE_REG);	/* Use PIO/DMA */
	OUT_BYTE (bcount.b.high,IDE_BCOUNTH_REG);
	OUT_BYTE (bcount.b.low,IDE_BCOUNTL_REG);
	OUT_BYTE (drive->select.all,IDE_SELECT_REG);
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (dma_ok) {	/* Begin DMA, if necessary */
		set_bit (PC_DMA_IN_PROGRESS, &pc->flags);
		(void) (HWIF(drive)->dmaproc(ide_dma_begin, drive));
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
	if (test_bit(IDETAPE_DRQ_INTERRUPT, &tape->flags)) {
		ide_set_handler(drive, &idetape_transfer_pc, IDETAPE_WAIT_CMD, NULL);
		OUT_BYTE(WIN_PACKETCMD, IDE_COMMAND_REG);
		return ide_started;
	} else {
		OUT_BYTE(WIN_PACKETCMD, IDE_COMMAND_REG);
		return idetape_transfer_pc(drive);
	}

} /* end tape */

}
#endif

int pkt_taskfile_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
#if 0
	switch(req_task->data_phase) {
		case TASKFILE_P_OUT_DMAQ:
		case TASKFILE_P_IN_DMAQ:
		case TASKFILE_P_OUT_DMA:
		case TASKFILE_P_IN_DMA:
		case TASKFILE_P_OUT:
		case TASKFILE_P_IN:
	}
#endif
	return -ENOMSG;
}

EXPORT_SYMBOL(pkt_taskfile_ioctl);

#endif /* CONFIG_PKT_TASK_IOCTL */
