/*
 * linux/drivers/scsi/ide-scsi.c	Version 0.9		Jul   4, 1999
 *
 * Copyright (C) 1996 - 1999 Gadi Oxman <gadio@netvision.net.il>
 */

/*
 * Emulation of a SCSI host adapter for IDE ATAPI devices.
 *
 * With this driver, one can use the Linux SCSI drivers instead of the
 * native IDE ATAPI drivers.
 *
 * Ver 0.1   Dec  3 96   Initial version.
 * Ver 0.2   Jan 26 97   Fixed bug in cleanup_module() and added emulation
 *                        of MODE_SENSE_6/MODE_SELECT_6 for cdroms. Thanks
 *                        to Janos Farkas for pointing this out.
 *                       Avoid using bitfields in structures for m68k.
 *                       Added Scatter/Gather and DMA support.
 * Ver 0.4   Dec  7 97   Add support for ATAPI PD/CD drives.
 *                       Use variable timeout for each command.
 * Ver 0.5   Jan  2 98   Fix previous PD/CD support.
 *                       Allow disabling of SCSI-6 to SCSI-10 transformation.
 * Ver 0.6   Jan 27 98   Allow disabling of SCSI command translation layer
 *                        for access through /dev/sg.
 *                       Fix MODE_SENSE_6/MODE_SELECT_6/INQUIRY translation.
 * Ver 0.7   Dec 04 98   Ignore commands where lun != 0 to avoid multiple
 *                        detection of devices with CONFIG_SCSI_MULTI_LUN
 * Ver 0.8   Feb 05 99   Optical media need translation too. Reverse 0.7.
 * Ver 0.9   Jul 04 99   Fix a bug in SG_SET_TRANSFORM.
 * Ver 0.91  Jun 10 02   Fix "off by one" error in transforms
 * Ver 0.92  Dec 31 02   Implement new SCSI mid level API
 */

#define IDESCSI_VERSION "0.92"

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/slab.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#include "scsi.h"
#include "hosts.h"
#include <scsi/sg.h>

#define IDESCSI_DEBUG_LOG		0

typedef struct idescsi_pc_s {
	u8 c[12];				/* Actual packet bytes */
	int request_transfer;			/* Bytes to transfer */
	int actually_transferred;		/* Bytes actually transferred */
	int buffer_size;			/* Size of our data buffer */
	struct request *rq;			/* The corresponding request */
	u8 *buffer;				/* Data buffer */
	u8 *current_position;			/* Pointer into the above buffer */
	struct scatterlist *sg;			/* Scatter gather table */
	int b_count;				/* Bytes transferred from current entry */
	Scsi_Cmnd *scsi_cmd;			/* SCSI command */
	void (*done)(Scsi_Cmnd *);		/* Scsi completion routine */
	unsigned long flags;			/* Status/Action flags */
	unsigned long timeout;			/* Command timeout */
} idescsi_pc_t;

/*
 *	Packet command status bits.
 */
#define PC_DMA_IN_PROGRESS		0	/* 1 while DMA in progress */
#define PC_WRITING			1	/* Data direction */
#define PC_TRANSFORM			2	/* transform SCSI commands */
#define PC_DMA_OK			4	/* Use DMA */

/*
 *	SCSI command transformation layer
 */
#define IDESCSI_TRANSFORM		0	/* Enable/Disable transformation */
#define IDESCSI_SG_TRANSFORM		1	/* /dev/sg transformation */

/*
 *	Log flags
 */
#define IDESCSI_LOG_CMD			0	/* Log SCSI commands */

typedef struct {
	ide_drive_t *drive;
	idescsi_pc_t *pc;			/* Current packet command */
	unsigned long flags;			/* Status/Action flags */
	unsigned long transform;		/* SCSI cmd translation layer */
	unsigned long log;			/* log flags */
} idescsi_scsi_t;

static inline idescsi_scsi_t *scsihost_to_idescsi(struct Scsi_Host *host)
{
	return (idescsi_scsi_t*) (&host[1]);
}

static inline idescsi_scsi_t *drive_to_idescsi(ide_drive_t *ide_drive)
{
	return scsihost_to_idescsi(ide_drive->driver_data);
}

/*
 *	Per ATAPI device status bits.
 */
#define IDESCSI_DRQ_INTERRUPT		0	/* DRQ interrupt device */

/*
 *	ide-scsi requests.
 */
#define IDESCSI_PC_RQ			90

static void idescsi_discard_data (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		(void) HWIF(drive)->INB(IDE_DATA_REG);
}

static void idescsi_output_zeros (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		HWIF(drive)->OUTB(0, IDE_DATA_REG);
}

/*
 *	PIO data transfer routines using the scatter gather table.
 */
static void idescsi_input_buffers (ide_drive_t *drive, idescsi_pc_t *pc, unsigned int bcount)
{
	int count;
	char *buf;

	while (bcount) {
		if (pc->sg - (struct scatterlist *) pc->scsi_cmd->request_buffer > pc->scsi_cmd->use_sg) {
			printk (KERN_ERR "ide-scsi: scatter gather table too small, discarding data\n");
			idescsi_discard_data (drive, bcount);
			return;
		}
		count = IDE_MIN (pc->sg->length - pc->b_count, bcount);
		buf = page_address(pc->sg->page) + pc->sg->offset;
		atapi_input_bytes (drive, buf + pc->b_count, count);
		bcount -= count; pc->b_count += count;
		if (pc->b_count == pc->sg->length) {
			pc->sg++;
			pc->b_count = 0;
		}
	}
}

static void idescsi_output_buffers (ide_drive_t *drive, idescsi_pc_t *pc, unsigned int bcount)
{
	int count;
	char *buf;

	while (bcount) {
		if (pc->sg - (struct scatterlist *) pc->scsi_cmd->request_buffer > pc->scsi_cmd->use_sg) {
			printk (KERN_ERR "ide-scsi: scatter gather table too small, padding with zeros\n");
			idescsi_output_zeros (drive, bcount);
			return;
		}
		count = IDE_MIN (pc->sg->length - pc->b_count, bcount);
		buf = page_address(pc->sg->page) + pc->sg->offset;
		atapi_output_bytes (drive, buf + pc->b_count, count);
		bcount -= count; pc->b_count += count;
		if (pc->b_count == pc->sg->length) {
			pc->sg++;
			pc->b_count = 0;
		}
	}
}

/*
 *	Most of the SCSI commands are supported directly by ATAPI devices.
 *	idescsi_transform_pc handles the few exceptions.
 */
static inline void idescsi_transform_pc1 (ide_drive_t *drive, idescsi_pc_t *pc)
{
	u8 *c = pc->c, *scsi_buf = pc->buffer, *sc = pc->scsi_cmd->cmnd;
	char *atapi_buf;

	if (!test_bit(PC_TRANSFORM, &pc->flags))
		return;
	if (drive->media == ide_cdrom || drive->media == ide_optical) {
		if (c[0] == READ_6 || c[0] == WRITE_6) {
			c[8] = c[4];		c[5] = c[3];		c[4] = c[2];
			c[3] = c[1] & 0x1f;	c[2] = 0;		c[1] &= 0xe0;
			c[0] += (READ_10 - READ_6);
		}
		if (c[0] == MODE_SENSE || c[0] == MODE_SELECT) {
			unsigned short new_len;
			if (!scsi_buf)
				return;
			if ((atapi_buf = kmalloc(pc->buffer_size + 4, GFP_ATOMIC)) == NULL)
				return;
			memset(atapi_buf, 0, pc->buffer_size + 4);
			memset (c, 0, 12);
			c[0] = sc[0] | 0x40;
			c[1] = sc[1];
			c[2] = sc[2];
			new_len = sc[4] + 4;
			c[8] = new_len;
			c[7] = new_len >> 8;
			c[9] = sc[5];
			if (c[0] == MODE_SELECT_10) {
				atapi_buf[1] = scsi_buf[0];	/* Mode data length */
				atapi_buf[2] = scsi_buf[1];	/* Medium type */
				atapi_buf[3] = scsi_buf[2];	/* Device specific parameter */
				atapi_buf[7] = scsi_buf[3];	/* Block descriptor length */
				memcpy(atapi_buf + 8, scsi_buf + 4, pc->buffer_size - 4);
			}
			pc->buffer = atapi_buf;
			pc->request_transfer += 4;
			pc->buffer_size += 4;
		}
	}
}

static inline void idescsi_transform_pc2 (ide_drive_t *drive, idescsi_pc_t *pc)
{
	u8 *atapi_buf = pc->buffer;
	u8 *sc = pc->scsi_cmd->cmnd;
	u8 *scsi_buf = pc->scsi_cmd->request_buffer;

	if (!test_bit(PC_TRANSFORM, &pc->flags))
		return;
	if (drive->media == ide_cdrom || drive->media == ide_optical) {
		if (pc->c[0] == MODE_SENSE_10 && sc[0] == MODE_SENSE) {
			scsi_buf[0] = atapi_buf[1];		/* Mode data length */
			scsi_buf[1] = atapi_buf[2];		/* Medium type */
			scsi_buf[2] = atapi_buf[3];		/* Device specific parameter */
			scsi_buf[3] = atapi_buf[7];		/* Block descriptor length */
			memcpy(scsi_buf + 4, atapi_buf + 8, pc->request_transfer - 8);
		}
		if (pc->c[0] == INQUIRY) {
			scsi_buf[2] |= 2;			/* ansi_revision */
			scsi_buf[3] = (scsi_buf[3] & 0xf0) | 2;	/* response data format */
		}
	}
	if (atapi_buf && atapi_buf != scsi_buf)
		kfree(atapi_buf);
}

static inline void idescsi_free_bio (struct bio *bio)
{
	struct bio *bhp;

	while (bio) {
		bhp = bio;
		bio = bio->bi_next;
		bio_put(bhp);
	}
}

static void hexdump(u8 *x, int len)
{
	int i;

	printk("[ ");
	for (i = 0; i < len; i++)
		printk("%x ", x[i]);
	printk("]\n");
}

static int idescsi_check_condition(ide_drive_t *drive, struct request *failed_command)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	idescsi_pc_t   *pc;
	struct request *rq;
	u8             *buf;

	/* stuff a sense request in front of our current request */
	pc = kmalloc (sizeof (idescsi_pc_t), GFP_ATOMIC);
	rq = kmalloc (sizeof (struct request), GFP_ATOMIC);
	buf = kmalloc(SCSI_SENSE_BUFFERSIZE, GFP_ATOMIC);
	if (pc == NULL || rq == NULL || buf == NULL) {
		if (pc) kfree(pc);
		if (rq) kfree(rq);
		if (buf) kfree(buf);
		return -ENOMEM;
	}
	memset (pc, 0, sizeof (idescsi_pc_t));
	memset (buf, 0, SCSI_SENSE_BUFFERSIZE);
	ide_init_drive_cmd(rq);
	rq->special = (char *) pc;
	pc->rq = rq;
	pc->buffer = buf;
	pc->c[0] = REQUEST_SENSE;
	pc->c[4] = pc->request_transfer = pc->buffer_size = SCSI_SENSE_BUFFERSIZE;
	rq->flags = REQ_SENSE;
	pc->timeout = jiffies + WAIT_READY;
	/* NOTE! Save the failed packet command in "rq->buffer" */
	rq->buffer = (void *) failed_command->special;
	pc->scsi_cmd = ((idescsi_pc_t *) failed_command->special)->scsi_cmd;
	if (test_bit(IDESCSI_LOG_CMD, &scsi->log)) {
		printk ("ide-scsi: %s: queue cmd = ", drive->name);
		hexdump(pc->c, 6);
	}
	return ide_do_drive_cmd(drive, rq, ide_preempt);
}

static int idescsi_end_request (ide_drive_t *drive, int uptodate, int nrsecs)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	struct request *rq = HWGROUP(drive)->rq;
	idescsi_pc_t *pc = (idescsi_pc_t *) rq->special;
	int log = test_bit(IDESCSI_LOG_CMD, &scsi->log);
	struct Scsi_Host *host;
	u8 *scsi_buf;
	unsigned long flags;

	if (!(rq->flags & (REQ_SPECIAL|REQ_SENSE))) {
		ide_end_request(drive, uptodate, nrsecs);
		return 0;
	}
	ide_end_drive_cmd (drive, 0, 0);
	if (rq->flags & REQ_SENSE) {
		idescsi_pc_t *opc = (idescsi_pc_t *) rq->buffer;
		if (log) {
			printk ("ide-scsi: %s: wrap up check %lu, rst = ", drive->name, opc->scsi_cmd->serial_number);
			hexdump(pc->buffer,16);
		}
		memcpy((void *) opc->scsi_cmd->sense_buffer, pc->buffer, SCSI_SENSE_BUFFERSIZE);
		kfree(pc->buffer);
		kfree(pc);
		kfree(rq);
		pc = opc;
		rq = pc->rq;
		pc->scsi_cmd->result = (CHECK_CONDITION << 1) | (DID_OK << 16);
	} else if (rq->errors >= ERROR_MAX) {
		pc->scsi_cmd->result = DID_ERROR << 16;
		if (log)
			printk ("ide-scsi: %s: I/O error for %lu\n", drive->name, pc->scsi_cmd->serial_number);
	} else if (rq->errors) {
		if (log)
			printk ("ide-scsi: %s: check condition for %lu\n", drive->name, pc->scsi_cmd->serial_number);
		if (!idescsi_check_condition(drive, rq))
			/* we started a request sense, so we'll be back, exit for now */
			return 0;
		pc->scsi_cmd->result = (CHECK_CONDITION << 1) | (DID_OK << 16);
	} else {
		pc->scsi_cmd->result = DID_OK << 16;
		idescsi_transform_pc2 (drive, pc);
		if (log) {
			printk ("ide-scsi: %s: suc %lu", drive->name, pc->scsi_cmd->serial_number);
			if (!test_bit(PC_WRITING, &pc->flags) && pc->actually_transferred && pc->actually_transferred <= 1024 && pc->buffer) {
				printk(", rst = ");
				scsi_buf = pc->scsi_cmd->request_buffer;
				hexdump(scsi_buf, IDE_MIN(16, pc->scsi_cmd->request_bufflen));
			} else printk("\n");
		}
	}
	host = pc->scsi_cmd->device->host;
	spin_lock_irqsave(host->host_lock, flags);
	pc->done(pc->scsi_cmd);
	spin_unlock_irqrestore(host->host_lock, flags);
	idescsi_free_bio(rq->bio);
	kfree(pc);
	kfree(rq);
	scsi->pc = NULL;
	return 0;
}

static inline unsigned long get_timeout(idescsi_pc_t *pc)
{
	return IDE_MAX(WAIT_CMD, pc->timeout - jiffies);
}

/*
 *	Our interrupt handler.
 */
static ide_startstop_t idescsi_pc_intr (ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	idescsi_pc_t *pc=scsi->pc;
	struct request *rq = pc->rq;
	atapi_bcount_t bcount;
	atapi_status_t status;
	atapi_ireason_t ireason;
	atapi_feature_t feature;

	unsigned int temp;

#if IDESCSI_DEBUG_LOG
	printk (KERN_INFO "ide-scsi: Reached idescsi_pc_intr interrupt handler\n");
#endif /* IDESCSI_DEBUG_LOG */

	if (test_and_clear_bit (PC_DMA_IN_PROGRESS, &pc->flags)) {
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: DMA complete\n", drive->name);
#endif /* IDESCSI_DEBUG_LOG */
		pc->actually_transferred=pc->request_transfer;
		(void) HWIF(drive)->ide_dma_end(drive);
	}

	feature.all = 0;
	/* Clear the interrupt */
	status.all = HWIF(drive)->INB(IDE_STATUS_REG);

	if (!status.b.drq) {
		/* No more interrupts */
		if (test_bit(IDESCSI_LOG_CMD, &scsi->log))
			printk (KERN_INFO "Packet command completed, %d bytes transferred\n", pc->actually_transferred);
		local_irq_enable();
		if (status.b.check)
			rq->errors++;
		idescsi_end_request (drive, 1, 0);
		return ide_stopped;
	}
	bcount.b.low	= HWIF(drive)->INB(IDE_BCOUNTL_REG);
	bcount.b.high	= HWIF(drive)->INB(IDE_BCOUNTH_REG);
	ireason.all	= HWIF(drive)->INB(IDE_IREASON_REG);

	if (ireason.b.cod) {
		printk(KERN_ERR "ide-scsi: CoD != 0 in idescsi_pc_intr\n");
		return ide_do_reset (drive);
	}
	if (ireason.b.io) {
		temp = pc->actually_transferred + bcount.all;
		if (temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk(KERN_ERR "ide-scsi: The scsi wants to "
					"send us more data than expected "
					"- discarding data\n");
				temp = pc->buffer_size - pc->actually_transferred;
				if (temp) {
					clear_bit(PC_WRITING, &pc->flags);
					if (pc->sg)
						idescsi_input_buffers(drive, pc, temp);
					else
						atapi_input_bytes(drive, pc->current_position, temp);
					printk(KERN_ERR "ide-scsi: transferred %d of %d bytes\n", temp, bcount.all);
				}
				pc->actually_transferred += temp;
				pc->current_position += temp;
				idescsi_discard_data(drive, bcount.all - temp);
				ide_set_handler(drive, &idescsi_pc_intr, get_timeout(pc), NULL);
				return ide_started;
			}
#if IDESCSI_DEBUG_LOG
			printk (KERN_NOTICE "ide-scsi: The scsi wants to send us more data than expected - allowing transfer\n");
#endif /* IDESCSI_DEBUG_LOG */
		}
	}
	if (ireason.b.io) {
		clear_bit(PC_WRITING, &pc->flags);
		if (pc->sg)
			idescsi_input_buffers(drive, pc, bcount.all);
		else
			HWIF(drive)->atapi_input_bytes(drive, pc->current_position, bcount.all);
	} else {
		set_bit(PC_WRITING, &pc->flags);
		if (pc->sg)
			idescsi_output_buffers (drive, pc, bcount.all);
		else
			HWIF(drive)->atapi_output_bytes(drive, pc->current_position, bcount.all);
	}
	/* Update the current position */
	pc->actually_transferred += bcount.all;
	pc->current_position += bcount.all;

	ide_set_handler(drive, &idescsi_pc_intr, get_timeout(pc), NULL);	/* And set the interrupt handler again */
	return ide_started;
}

static ide_startstop_t idescsi_transfer_pc(ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	idescsi_pc_t *pc = scsi->pc;
	atapi_ireason_t ireason;
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop,drive,DRQ_STAT,BUSY_STAT,WAIT_READY)) {
		printk(KERN_ERR "ide-scsi: Strange, packet command "
			"initiated yet DRQ isn't asserted\n");
		return startstop;
	}
	ireason.all	= HWIF(drive)->INB(IDE_IREASON_REG);
	if (!ireason.b.cod || ireason.b.io) {
		printk(KERN_ERR "ide-scsi: (IO,CoD) != (0,1) while "
				"issuing a packet command\n");
		return ide_do_reset (drive);
	}
	if (HWGROUP(drive)->handler != NULL)
		BUG();
	/* Set the interrupt routine */
	ide_set_handler(drive, &idescsi_pc_intr, get_timeout(pc), NULL);
	/* Send the actual packet */
	atapi_output_bytes(drive, scsi->pc->c, 12);
	if (test_bit (PC_DMA_OK, &pc->flags)) {
		set_bit (PC_DMA_IN_PROGRESS, &pc->flags);
		(void) (HWIF(drive)->ide_dma_begin(drive));
	}
	return ide_started;
}

/*
 *	Issue a packet command
 */
static ide_startstop_t idescsi_issue_pc (ide_drive_t *drive, idescsi_pc_t *pc)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);
	atapi_feature_t feature;
	atapi_bcount_t bcount;
	struct request *rq = pc->rq;

	scsi->pc=pc;							/* Set the current packet command */
	pc->actually_transferred=0;					/* We haven't transferred any data yet */
	pc->current_position=pc->buffer;
	bcount.all = IDE_MIN(pc->request_transfer, 63 * 1024);		/* Request to transfer the entire buffer at once */

	feature.all = 0;
	if (drive->using_dma && rq->bio) {
		if (test_bit(PC_WRITING, &pc->flags))
			feature.b.dma = !HWIF(drive)->ide_dma_write(drive);
		else
			feature.b.dma = !HWIF(drive)->ide_dma_read(drive);
	}

	SELECT_DRIVE(drive);
	if (IDE_CONTROL_REG)
		HWIF(drive)->OUTB(drive->ctl, IDE_CONTROL_REG);

	HWIF(drive)->OUTB(feature.all, IDE_FEATURE_REG);
	HWIF(drive)->OUTB(bcount.b.high, IDE_BCOUNTH_REG);
	HWIF(drive)->OUTB(bcount.b.low, IDE_BCOUNTL_REG);

	if (feature.b.dma)
		set_bit(PC_DMA_OK, &pc->flags);

	if (test_bit(IDESCSI_DRQ_INTERRUPT, &scsi->flags)) {
		if (HWGROUP(drive)->handler != NULL)
			BUG();
		ide_set_handler(drive, &idescsi_transfer_pc,
				get_timeout(pc), NULL);
		/* Issue the packet command */
		HWIF(drive)->OUTB(WIN_PACKETCMD, IDE_COMMAND_REG);
		return ide_started;
	} else {
		/* Issue the packet command */
		HWIF(drive)->OUTB(WIN_PACKETCMD, IDE_COMMAND_REG);
		return idescsi_transfer_pc(drive);
	}
}

/*
 *	idescsi_do_request is our request handling function.
 */
static ide_startstop_t idescsi_do_request (ide_drive_t *drive, struct request *rq, sector_t block)
{
#if IDESCSI_DEBUG_LOG
	printk (KERN_INFO "rq_status: %d, dev: %s, cmd: %x, errors: %d\n",rq->rq_status, rq->rq_disk->disk_name,rq->cmd[0],rq->errors);
	printk (KERN_INFO "sector: %ld, nr_sectors: %ld, current_nr_sectors: %d\n",rq->sector,rq->nr_sectors,rq->current_nr_sectors);
#endif /* IDESCSI_DEBUG_LOG */

	if (rq->flags & (REQ_SPECIAL|REQ_SENSE)) {
		return idescsi_issue_pc (drive, (idescsi_pc_t *) rq->special);
	}
	blk_dump_rq_flags(rq, "ide-scsi: unsup command");
	idescsi_end_request (drive, 0, 0);
	return ide_stopped;
}

static void idescsi_add_settings(ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);

/*
 *			drive	setting name	read/write	ioctl	ioctl		data type	min	max	mul_factor	div_factor	data pointer		set function
 */
	ide_add_setting(drive,	"bios_cyl",	SETTING_RW,	-1,	-1,		TYPE_INT,	0,	1023,	1,		1,		&drive->bios_cyl,	NULL);
	ide_add_setting(drive,	"bios_head",	SETTING_RW,	-1,	-1,		TYPE_BYTE,	0,	255,	1,		1,		&drive->bios_head,	NULL);
	ide_add_setting(drive,	"bios_sect",	SETTING_RW,	-1,	-1,		TYPE_BYTE,	0,	63,	1,		1,		&drive->bios_sect,	NULL);
	ide_add_setting(drive,	"transform",	SETTING_RW,	-1,	-1,		TYPE_INT,	0,	3,	1,		1,		&scsi->transform,	NULL);
	ide_add_setting(drive,	"log",		SETTING_RW,	-1,	-1,		TYPE_INT,	0,	1,	1,		1,		&scsi->log,		NULL);
}

/*
 *	Driver initialization.
 */
static void idescsi_setup (ide_drive_t *drive, idescsi_scsi_t *scsi)
{
	DRIVER(drive)->busy++;
	drive->ready_stat = 0;
	if (drive->id && (drive->id->config & 0x0060) == 0x20)
		set_bit (IDESCSI_DRQ_INTERRUPT, &scsi->flags);
	set_bit(IDESCSI_TRANSFORM, &scsi->transform);
	clear_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
#if IDESCSI_DEBUG_LOG
	set_bit(IDESCSI_LOG_CMD, &scsi->log);
#endif /* IDESCSI_DEBUG_LOG */
	idescsi_add_settings(drive);
	DRIVER(drive)->busy--;
}

static int idescsi_cleanup (ide_drive_t *drive)
{
	struct Scsi_Host *scsihost = drive->driver_data;

	if (ide_unregister_subdriver(drive))
		return 1;
	
	/* FIXME?: Are these two statements necessary? */
	drive->driver_data = NULL;
	drive->disk->fops = ide_fops;

	scsi_remove_host(scsihost);
	scsi_host_put(scsihost);
	return 0;
}

static int idescsi_attach(ide_drive_t *drive);

/*
 *	IDE subdriver functions, registered with ide.c
 */
static ide_driver_t idescsi_driver = {
	.owner			= THIS_MODULE,
	.name			= "ide-scsi",
	.version		= IDESCSI_VERSION,
	.media			= ide_scsi,
	.busy			= 0,
	.supports_dsc_overlap	= 0,
	.attach			= idescsi_attach,
	.cleanup		= idescsi_cleanup,
	.do_request		= idescsi_do_request,
	.end_request		= idescsi_end_request,
	.drives			= LIST_HEAD_INIT(idescsi_driver.drives),
};

static int idescsi_ide_open(struct inode *inode, struct file *filp)
{
	ide_drive_t *drive = inode->i_bdev->bd_disk->private_data;
	drive->usage++;
	return 0;
}

static int idescsi_ide_release(struct inode *inode, struct file *filp)
{
	ide_drive_t *drive = inode->i_bdev->bd_disk->private_data;
	drive->usage--;
	return 0;
}

static int idescsi_ide_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	return generic_ide_ioctl(bdev, cmd, arg);
}

static struct block_device_operations idescsi_ops = {
	.owner		= THIS_MODULE,
	.open		= idescsi_ide_open,
	.release	= idescsi_ide_release,
	.ioctl		= idescsi_ide_ioctl,
};

static int idescsi_attach(ide_drive_t *drive);

static int idescsi_slave_configure(Scsi_Device * sdp)
{
	/* Configure detected device */
	scsi_adjust_queue_depth(sdp, MSG_SIMPLE_TAG, sdp->host->cmd_per_lun);
	return 0;
}

static const char *idescsi_info (struct Scsi_Host *host)
{
	return "SCSI host adapter emulation for IDE ATAPI devices";
}

static int idescsi_ioctl (Scsi_Device *dev, int cmd, void *arg)
{
	idescsi_scsi_t *scsi = scsihost_to_idescsi(dev->host);

	if (cmd == SG_SET_TRANSFORM) {
		if (arg)
			set_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
		else
			clear_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
		return 0;
	} else if (cmd == SG_GET_TRANSFORM)
		return put_user(test_bit(IDESCSI_SG_TRANSFORM, &scsi->transform), (int *) arg);
	return -EINVAL;
}

static inline struct bio *idescsi_kmalloc_bio (int count)
{
	struct bio *bh, *bhp, *first_bh;

	if ((first_bh = bhp = bh = bio_alloc(GFP_ATOMIC, 1)) == NULL)
		goto abort;
	bio_init(bh);
	bh->bi_vcnt = 1;
	while (--count) {
		if ((bh = bio_alloc(GFP_ATOMIC, 1)) == NULL)
			goto abort;
		bio_init(bh);
		bh->bi_vcnt = 1;
		bhp->bi_next = bh;
		bhp = bh;
		bh->bi_next = NULL;
	}
	return first_bh;
abort:
	idescsi_free_bio (first_bh);
	return NULL;
}

static inline int idescsi_set_direction (idescsi_pc_t *pc)
{
	switch (pc->c[0]) {
		case READ_6: case READ_10: case READ_12:
			clear_bit (PC_WRITING, &pc->flags);
			return 0;
		case WRITE_6: case WRITE_10: case WRITE_12:
			set_bit (PC_WRITING, &pc->flags);
			return 0;
		default:
			return 1;
	}
}

static inline struct bio *idescsi_dma_bio(ide_drive_t *drive, idescsi_pc_t *pc)
{
	struct bio *bh = NULL, *first_bh = NULL;
	int segments = pc->scsi_cmd->use_sg;
	struct scatterlist *sg = pc->scsi_cmd->request_buffer;

	if (!drive->using_dma || !pc->request_transfer || pc->request_transfer % 1024)
		return NULL;
	if (idescsi_set_direction(pc))
		return NULL;
	if (segments) {
		if ((first_bh = bh = idescsi_kmalloc_bio (segments)) == NULL)
			return NULL;
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: building DMA table, %d segments, %dkB total\n", drive->name, segments, pc->request_transfer >> 10);
#endif /* IDESCSI_DEBUG_LOG */
		while (segments--) {
			bh->bi_io_vec[0].bv_page = sg->page;
			bh->bi_io_vec[0].bv_len = sg->length;
			bh->bi_io_vec[0].bv_offset = sg->offset;
			bh->bi_size = sg->length;
			bh = bh->bi_next;
			sg++;
		}
	} else {
		if ((first_bh = bh = idescsi_kmalloc_bio (1)) == NULL)
			return NULL;
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: building DMA table for a single buffer (%dkB)\n", drive->name, pc->request_transfer >> 10);
#endif /* IDESCSI_DEBUG_LOG */
		bh->bi_io_vec[0].bv_page = virt_to_page(pc->scsi_cmd->request_buffer);
		bh->bi_io_vec[0].bv_offset = offset_in_page(pc->scsi_cmd->request_buffer);
		bh->bi_io_vec[0].bv_len = pc->request_transfer;
		bh->bi_size = pc->request_transfer;
	}
	return first_bh;
}

static inline int should_transform(ide_drive_t *drive, Scsi_Cmnd *cmd)
{
	idescsi_scsi_t *scsi = drive_to_idescsi(drive);

	/* this was a layering violation and we can't support it
	   anymore, sorry. */
#if 0
	struct gendisk *disk = cmd->request->rq_disk;

	if (disk) {
		struct Scsi_Device_Template **p = disk->private_data;
		if (strcmp((*p)->scsi_driverfs_driver.name, "sg") == 0)
			return test_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
	}
#endif
	return test_bit(IDESCSI_TRANSFORM, &scsi->transform);
}

static int idescsi_queue (Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	idescsi_scsi_t *scsi = scsihost_to_idescsi(cmd->device->host);
	ide_drive_t *drive = scsi->drive;
	struct request *rq = NULL;
	idescsi_pc_t *pc = NULL;

	if (!drive) {
		printk (KERN_ERR "ide-scsi: drive id %d not present\n", cmd->device->id);
		goto abort;
	}
	scsi = drive_to_idescsi(drive);
	pc = kmalloc (sizeof (idescsi_pc_t), GFP_ATOMIC);
	rq = kmalloc (sizeof (struct request), GFP_ATOMIC);
	if (rq == NULL || pc == NULL) {
		printk (KERN_ERR "ide-scsi: %s: out of memory\n", drive->name);
		goto abort;
	}

	memset (pc->c, 0, 12);
	pc->flags = 0;
	pc->rq = rq;
	memcpy (pc->c, cmd->cmnd, cmd->cmd_len);
	if (cmd->use_sg) {
		pc->buffer = NULL;
		pc->sg = cmd->request_buffer;
	} else {
		pc->buffer = cmd->request_buffer;
		pc->sg = NULL;
	}
	pc->b_count = 0;
	pc->request_transfer = pc->buffer_size = cmd->request_bufflen;
	pc->scsi_cmd = cmd;
	pc->done = done;
	pc->timeout = jiffies + cmd->timeout_per_command;

	if (should_transform(drive, cmd))
		set_bit(PC_TRANSFORM, &pc->flags);
	idescsi_transform_pc1 (drive, pc);

	if (test_bit(IDESCSI_LOG_CMD, &scsi->log)) {
		printk ("ide-scsi: %s: que %lu, cmd = ", drive->name, cmd->serial_number);
		hexdump(cmd->cmnd, cmd->cmd_len);
		if (memcmp(pc->c, cmd->cmnd, cmd->cmd_len)) {
			printk ("ide-scsi: %s: que %lu, tsl = ", drive->name, cmd->serial_number);
			hexdump(pc->c, 12);
		}
	}

	ide_init_drive_cmd (rq);
	rq->special = (char *) pc;
	rq->bio = idescsi_dma_bio (drive, pc);
	rq->flags = REQ_SPECIAL;
	spin_unlock_irq(cmd->device->host->host_lock);
	(void) ide_do_drive_cmd (drive, rq, ide_end);
	spin_lock_irq(cmd->device->host->host_lock);
	return 0;
abort:
	if (pc) kfree (pc);
	if (rq) kfree (rq);
	cmd->result = DID_ERROR << 16;
	done(cmd);
	return 1;
}

static int idescsi_abort (Scsi_Cmnd *cmd)
{
	int countdown = 8;
	unsigned long flags;
	idescsi_scsi_t *scsi = scsihost_to_idescsi(cmd->device->host);
	ide_drive_t *drive = scsi->drive;

	printk (KERN_ERR "ide-scsi: abort called for %lu\n", cmd->serial_number);
	while (countdown--) {
		/* is cmd active?
		 *  need to lock so this stuff doesn't change under us */
		spin_lock_irqsave(&ide_lock, flags);
		if (scsi->pc && scsi->pc->scsi_cmd && 
				scsi->pc->scsi_cmd->serial_number == cmd->serial_number) {
			/* yep - let's give it some more time - 
			 * we can do that, we're in _our_ error kernel thread */
			spin_unlock_irqrestore(&ide_lock, flags);
			scsi_sleep(HZ);
			continue;
		}
		/* no, but is it queued in the ide subsystem? */
		if (elv_queue_empty(drive->queue)) {
			spin_unlock_irqrestore(&ide_lock, flags);
			return SUCCESS;
		}
		spin_unlock_irqrestore(&ide_lock, flags);
		schedule_timeout(HZ/10);
	}
	return FAILED;
}

static int idescsi_reset (Scsi_Cmnd *cmd)
{
	unsigned long flags;
	struct request *req;
	idescsi_scsi_t *idescsi = scsihost_to_idescsi(cmd->device->host);
	ide_drive_t *drive = idescsi->drive;

	printk (KERN_ERR "ide-scsi: reset called for %lu\n", cmd->serial_number);
	/* first null the handler for the drive and let any process
	 * doing IO (on another CPU) run to (partial) completion
	 * the lock prevents processing new requests */
	spin_lock_irqsave(&ide_lock, flags);
	while (HWGROUP(drive)->handler) {
		HWGROUP(drive)->handler = NULL;
		schedule_timeout(1);
	}
	/* now nuke the drive queue */
	while ((req = elv_next_request(drive->queue))) {
		blkdev_dequeue_request(req);
		end_that_request_last(req);
	}
	/* FIXME - this will probably leak memory */
	HWGROUP(drive)->rq = NULL;
	if (drive_to_idescsi(drive))
		drive_to_idescsi(drive)->pc = NULL;
	spin_unlock_irqrestore(&ide_lock, flags);
	/* finally, reset the drive (and its partner on the bus...) */
	ide_do_reset (drive);	
	return SUCCESS;
}

static int idescsi_bios(struct scsi_device *sdev, struct block_device *bdev,
		sector_t capacity, int *parm)
{
	idescsi_scsi_t *idescsi = scsihost_to_idescsi(sdev->host);
	ide_drive_t *drive = idescsi->drive;

	if (drive->bios_cyl && drive->bios_head && drive->bios_sect) {
		parm[0] = drive->bios_head;
		parm[1] = drive->bios_sect;
		parm[2] = drive->bios_cyl;
	}
	return 0;
}

static Scsi_Host_Template idescsi_template = {
	.module			= THIS_MODULE,
	.name			= "idescsi",
	.info			= idescsi_info,
	.slave_configure        = idescsi_slave_configure,
	.ioctl			= idescsi_ioctl,
	.queuecommand		= idescsi_queue,
	.eh_abort_handler	= idescsi_abort,
	.eh_device_reset_handler = idescsi_reset,
	.bios_param		= idescsi_bios,
	.can_queue		= 40,
	.this_id		= -1,
	.sg_tablesize		= 256,
	.cmd_per_lun		= 5,
	.max_sectors		= 128,
	.use_clustering		= DISABLE_CLUSTERING,
	.emulated		= 1,
	.proc_name		= "ide-scsi",
};

static int idescsi_attach(ide_drive_t *drive)
{
	idescsi_scsi_t *idescsi;
	struct Scsi_Host *host;
	static int warned;
	int err;

	if (!warned && drive->media == ide_cdrom) {
		printk(KERN_WARNING "ide-scsi is deprecated for cd burning! Use ide-cd and give dev=/dev/hdX as device\n");
		warned = 1;
	}

	if (!strstr("ide-scsi", drive->driver_req) ||
	    !drive->present ||
	    drive->media == ide_disk ||
	    !(host = scsi_host_alloc(&idescsi_template,sizeof(idescsi_scsi_t))))
		return 1;

	host->max_id = 1;
	host->max_lun = 1;
	drive->driver_data = host;
	idescsi = scsihost_to_idescsi(host);
	idescsi->drive = drive;
	err = ide_register_subdriver (drive, &idescsi_driver,
				      IDE_SUBDRIVER_VERSION);
	if (!err) {
		idescsi_setup (drive, idescsi);
		drive->disk->fops = &idescsi_ops;
		err = scsi_add_host(host, &drive->gendev);
		if (!err) {
			scsi_scan_host(host);
			return 0;
		}
		/* fall through on error */
		ide_unregister_subdriver(drive);
	}

	scsi_host_put(host);
	return err;
}

static int __init init_idescsi_module(void)
{
	return ide_register_driver(&idescsi_driver);
}

static void __exit exit_idescsi_module(void)
{
	ide_unregister_driver(&idescsi_driver);
}

module_init(init_idescsi_module);
module_exit(exit_idescsi_module);
MODULE_LICENSE("GPL");
