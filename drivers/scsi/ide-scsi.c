/*
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
 */

#define IDESCSI_VERSION "0.9"

#include <linux/module.h>
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
#include <linux/atapi.h>

#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#include <scsi/sg.h>

#include "scsi.h"
#include "hosts.h"
#include "sd.h"

/* FIXME: Right now we always register a single scsi host for every single
 * device. We should be just registering a single scsi host per ATA host chip
 * and deal properly with channels! The reentrancy efforts are therefore not
 * quite right done now.
 */

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
	struct ata_device *drive;
	struct atapi_packet_command *pc;	/* Current packet command */
	unsigned long flags;			/* Status/Action flags */
	unsigned long transform;		/* SCSI cmd translation layer */
	unsigned long log;			/* log flags */
} idescsi_scsi_t;

/*
 *	Per ATAPI device status bits.
 */
#define IDESCSI_DRQ_INTERRUPT		0	/* DRQ interrupt device */

/*
 *	ide-scsi requests.
 */
#define IDESCSI_PC_RQ			90

/*
 *	Bits of the interrupt reason register.
 */
#define IDESCSI_IREASON_COD	0x1		/* Information transferred is command */
#define IDESCSI_IREASON_IO	0x2		/* The device requests us to read */

/*
 *	PIO data transfer routines using the scatter gather table.
 */
static void idescsi_input_buffers(struct ata_device *drive, struct atapi_packet_command *pc, unsigned int bcount)
{
	int count;
	char *buf;

	while (bcount) {
		if (pc->s.sg - (struct scatterlist *) pc->s.scsi_cmd->request_buffer > pc->s.scsi_cmd->use_sg) {
			printk (KERN_ERR "ide-scsi: scatter gather table too small, discarding data\n");
			atapi_discard_data(drive, bcount);
			return;
		}
		count = min(pc->s.sg->length - pc->s.b_count, bcount);
		buf = page_address(pc->s.sg->page) + pc->s.sg->offset;
		atapi_read(drive, buf + pc->s.b_count, count);
		bcount -= count; pc->s.b_count += count;
		if (pc->s.b_count == pc->s.sg->length) {
			pc->s.sg++;
			pc->s.b_count = 0;
		}
	}
}

static void idescsi_output_buffers(struct ata_device *drive, struct atapi_packet_command *pc, unsigned int bcount)
{
	int count;
	char *buf;

	while (bcount) {
		if (pc->s.sg - (struct scatterlist *) pc->s.scsi_cmd->request_buffer > pc->s.scsi_cmd->use_sg) {
			printk (KERN_ERR "ide-scsi: scatter gather table too small, padding with zeros\n");
			atapi_write_zeros(drive, bcount);
			return;
		}
		count = min(pc->s.sg->length - pc->s.b_count, bcount);
		buf = page_address(pc->s.sg->page) + pc->s.sg->offset;
		atapi_write(drive, buf + pc->s.b_count, count);
		bcount -= count; pc->s.b_count += count;
		if (pc->s.b_count == pc->s.sg->length) {
			pc->s.sg++;
			pc->s.b_count = 0;
		}
	}
}

/*
 *	Most of the SCSI commands are supported directly by ATAPI devices.
 *	idescsi_transform_pc handles the few exceptions.
 */
static inline void idescsi_transform_pc1(struct ata_device *drive, struct atapi_packet_command *pc)
{
	u8 *c = pc->c;
	char *scsi_buf = pc->buffer;
	u8 *sc = pc->s.scsi_cmd->cmnd;
	char *atapi_buf;

	if (!test_bit(PC_TRANSFORM, &pc->flags))
		return;
	if (drive->type == ATA_ROM || drive->type == ATA_MOD) {
		if (c[0] == READ_6 || c[0] == WRITE_6) {
			c[8] = c[4];		c[5] = c[3];		c[4] = c[2];
			c[3] = c[1] & 0x1f;	c[2] = 0;		c[1] &= 0xe0;
			c[0] += (READ_10 - READ_6);
		}
		if (c[0] == MODE_SENSE || c[0] == MODE_SELECT) {
			if (!scsi_buf)
				return;
			if ((atapi_buf = kmalloc(pc->buffer_size + 4, GFP_ATOMIC)) == NULL)
				return;
			memset(atapi_buf, 0, pc->buffer_size + 4);
			memset (c, 0, 12);
			c[0] = sc[0] | 0x40;	c[1] = sc[1];		c[2] = sc[2];
			c[8] = sc[4] + 4;	c[9] = sc[5];
			if (sc[4] + 4 > 255)
				c[7] = sc[4] + 4 - 255;
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

static inline void idescsi_transform_pc2(struct ata_device *drive, struct atapi_packet_command *pc)
{
	u8 *atapi_buf = pc->buffer;
	u8 *sc = pc->s.scsi_cmd->cmnd;
	u8 *scsi_buf = pc->s.scsi_cmd->request_buffer;

	if (!test_bit(PC_TRANSFORM, &pc->flags))
		return;
	if (drive->type == ATA_ROM || drive->type == ATA_MOD) {
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

static inline void idescsi_free_bio(struct bio *bio)
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

static inline idescsi_scsi_t *idescsi_private(struct Scsi_Host *host)
{
	return (idescsi_scsi_t*) &host[1];
}

static int idescsi_end_request(struct ata_device *drive, struct request *rq, int uptodate)
{
	unsigned long flags;
	struct Scsi_Host *host = drive->driver_data;
	idescsi_scsi_t *scsi = idescsi_private(host);
	struct atapi_packet_command *pc = (struct atapi_packet_command *) rq->special;
	int log = test_bit(IDESCSI_LOG_CMD, &scsi->log);
	u8 *scsi_buf;

	if (!(rq->flags & REQ_PC)) {
		ata_end_request(drive, rq, uptodate, 0);
		return 0;
	}

	spin_lock_irqsave(drive->channel->lock, flags);

	blkdev_dequeue_request(rq);
	drive->rq = NULL;
	end_that_request_last(rq);

	spin_unlock_irqrestore(drive->channel->lock, flags);

	if (rq->errors >= ERROR_MAX) {
		pc->s.scsi_cmd->result = DID_ERROR << 16;
		if (log)
			printk ("ide-scsi: %s: I/O error for %lu\n", drive->name, pc->s.scsi_cmd->serial_number);
	} else if (rq->errors) {
		pc->s.scsi_cmd->result = (CHECK_CONDITION << 1) | (DID_OK << 16);
		if (log)
			printk ("ide-scsi: %s: check condition for %lu\n", drive->name, pc->s.scsi_cmd->serial_number);
	} else {
		pc->s.scsi_cmd->result = DID_OK << 16;
		idescsi_transform_pc2 (drive, pc);
		if (log) {
			printk ("ide-scsi: %s: suc %lu", drive->name, pc->s.scsi_cmd->serial_number);
			if (!test_bit(PC_WRITING, &pc->flags) && pc->actually_transferred && pc->actually_transferred <= 1024 && pc->buffer) {
				printk(", rst = ");
				scsi_buf = pc->s.scsi_cmd->request_buffer;
				hexdump(scsi_buf, min(16U, pc->s.scsi_cmd->request_bufflen));
			} else printk("\n");
		}
	}
	host = pc->s.scsi_cmd->host;
	pc->s.done(pc->s.scsi_cmd);
	idescsi_free_bio(rq->bio);
	kfree(pc); kfree(rq);
	scsi->pc = NULL;

	return 0;
}

static inline unsigned long get_timeout(struct atapi_packet_command *pc)
{
	return max((unsigned long) WAIT_CMD, pc->s.timeout - jiffies);
}

/*
 *	Our interrupt handler.
 */
static ide_startstop_t idescsi_pc_intr(struct ata_device *drive, struct request *rq)
{
	struct Scsi_Host *host = drive->driver_data;
	idescsi_scsi_t *scsi = idescsi_private(host);
	u8 ireason;
	int bcount;
	struct atapi_packet_command *pc=scsi->pc;
	unsigned int temp;

#ifdef DEBUG
	printk (KERN_INFO "ide-scsi: Reached idescsi_pc_intr interrupt handler\n");
#endif

	if (test_and_clear_bit (PC_DMA_IN_PROGRESS, &pc->flags)) {
#ifdef DEBUG
		printk ("ide-scsi: %s: DMA complete\n", drive->name);
#endif
		pc->actually_transferred=pc->request_transfer;
		udma_stop(drive);
	}

	/* Clear the interrupt */
	if (ata_status(drive, 0, DRQ_STAT)) {	/* No more interrupts */
		if (test_bit(IDESCSI_LOG_CMD, &scsi->log))
			printk (KERN_INFO "Packet command completed, %d bytes transferred\n", pc->actually_transferred);
		local_irq_enable();
		if (drive->status & ERR_STAT)
			rq->errors++;
		idescsi_end_request(drive, rq, 1);

		return ATA_OP_FINISHED;
	}
	bcount = IN_BYTE (IDE_BCOUNTH_REG) << 8 | IN_BYTE (IDE_BCOUNTL_REG);
	ireason = IN_BYTE (IDE_IREASON_REG);

	if (ireason & IDESCSI_IREASON_COD) {
		printk (KERN_ERR "ide-scsi: CoD != 0 in idescsi_pc_intr\n");
		return ATA_OP_FINISHED;
	}
	if (ireason & IDESCSI_IREASON_IO) {
		temp = pc->actually_transferred + bcount;
		if ( temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk (KERN_ERR "ide-scsi: The scsi wants to send us more data than expected - discarding data\n");
				temp = pc->buffer_size - pc->actually_transferred;
				if (temp) {
					clear_bit(PC_WRITING, &pc->flags);
					if (pc->s.sg)
						idescsi_input_buffers(drive, pc, temp);
					else
						atapi_read(drive, pc->current_position, temp);
					printk(KERN_ERR "ide-scsi: transferred %d of %d bytes\n", temp, bcount);
				}
				pc->actually_transferred += temp;
				pc->current_position += temp;
				atapi_discard_data(drive,bcount - temp);

				ata_set_handler(drive, idescsi_pc_intr, get_timeout(pc), NULL);

				return ATA_OP_CONTINUES;
			}
#ifdef DEBUG
			printk (KERN_NOTICE "ide-scsi: The scsi wants to send us more data than expected - allowing transfer\n");
#endif
		}
	}
	if (ireason & IDESCSI_IREASON_IO) {
		clear_bit(PC_WRITING, &pc->flags);
		if (pc->s.sg)
			idescsi_input_buffers (drive, pc, bcount);
		else
			atapi_read(drive,pc->current_position,bcount);
	} else {
		set_bit(PC_WRITING, &pc->flags);
		if (pc->s.sg)
			idescsi_output_buffers (drive, pc, bcount);
		else
			atapi_write(drive,pc->current_position,bcount);
	}
	pc->actually_transferred+=bcount;				/* Update the current position */
	pc->current_position+=bcount;

	/* And set the interrupt handler again */
	ata_set_handler(drive, idescsi_pc_intr, get_timeout(pc), NULL);

	return ATA_OP_CONTINUES;
}

static ide_startstop_t idescsi_transfer_pc(struct ata_device *drive, struct request *rq)
{
	struct Scsi_Host *host = drive->driver_data;
	idescsi_scsi_t *scsi = idescsi_private(host);
	struct atapi_packet_command *pc = scsi->pc;
	u8 ireason;
	int ret;

	ret = ata_status_poll(drive, DRQ_STAT, BUSY_STAT,
				WAIT_READY, rq);
	if (ret != ATA_OP_READY) {
		printk (KERN_ERR "ide-scsi: Strange, packet command initiated yet DRQ isn't asserted\n");

		return ret;
	}

	ireason = IN_BYTE(IDE_IREASON_REG);

	if ((ireason & (IDESCSI_IREASON_IO | IDESCSI_IREASON_COD)) != IDESCSI_IREASON_COD) {
		printk (KERN_ERR "ide-scsi: (IO,CoD) != (0,1) while issuing a packet command\n");
		ret = ATA_OP_FINISHED;
	} else {
		ata_set_handler(drive, idescsi_pc_intr, get_timeout(pc), NULL);
		atapi_write(drive, scsi->pc->c, 12);
		ret = ATA_OP_CONTINUES;
	}

	return ret;
}

/*
 *	Issue a packet command
 */
static ide_startstop_t idescsi_issue_pc(struct ata_device *drive, struct request *rq,
		struct atapi_packet_command *pc)
{
	struct Scsi_Host *host = drive->driver_data;
	idescsi_scsi_t *scsi = idescsi_private(host);
	int bcount;
	int dma_ok = 0;

	scsi->pc=pc;							/* Set the current packet command */
	pc->actually_transferred=0;					/* We haven't transferred any data yet */
	pc->current_position=pc->buffer;
	bcount = min(pc->request_transfer, 63 * 1024);		/* Request to transfer the entire buffer at once */

	if (drive->using_dma && rq->bio)
		dma_ok = udma_init(drive, rq);

	ata_select(drive, 10);
	ata_irq_enable(drive, 1);
	OUT_BYTE(dma_ok,IDE_FEATURE_REG);
	OUT_BYTE(bcount >> 8,IDE_BCOUNTH_REG);
	OUT_BYTE(bcount & 0xff,IDE_BCOUNTL_REG);

	if (dma_ok) {
		set_bit(PC_DMA_IN_PROGRESS, &pc->flags);
		udma_start(drive, rq);
	}
	if (test_bit(IDESCSI_DRQ_INTERRUPT, &scsi->flags)) {
		ata_set_handler(drive, idescsi_transfer_pc, get_timeout(pc), NULL);

		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG);
		return ATA_OP_CONTINUES;
	} else {
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG);
		return idescsi_transfer_pc(drive, rq);
	}
}

/*
 * This is our request handling function.
 */
static ide_startstop_t idescsi_do_request(struct ata_device *drive, struct request *rq, sector_t block)
{
	int ret;

#ifdef DEBUG
	printk(KERN_INFO "rq_status: %d, cmd: %d, errors: %d\n",
			rq->rq_status,
			(unsigned int)
			rq->cmd,
			rq->errors);
	printk(KERN_INFO "sector: %lu, nr_sectors: %lu, current_nr_sectors: %lu\n",
			rq->sector,
			rq->nr_sectors,
			rq->current_nr_sectors);
#endif

	if (rq->flags & REQ_PC) {
		ret = idescsi_issue_pc(drive, rq, (struct atapi_packet_command *) rq->special);
	} else {
	    blk_dump_rq_flags(rq, "ide-scsi: unsup command");
	    idescsi_end_request(drive, rq, 0);
	    ret = ATA_OP_FINISHED;
	}

	return ret;
}

static int idescsi_open(struct inode *inode, struct file *filp, struct ata_device *drive)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static void idescsi_release(struct inode *inode, struct file *filp, struct ata_device *drive)
{
	MOD_DEC_USE_COUNT;
}

static Scsi_Host_Template template;
static int idescsi_cleanup (struct ata_device *drive)
{
	if (ide_unregister_subdriver (drive)) {
		return 1;
	}
	scsi_unregister_host(&template);

	return 0;
}

static void idescsi_revalidate(struct ata_device *_dummy)
{
	/* The partition information will be handled by the SCSI layer.
	 */
}

static void idescsi_attach(struct ata_device *drive);

/*
 *	IDE subdriver functions, registered with ide.c
 */
static struct ata_operations ata_ops = {
	owner:			THIS_MODULE,
	attach:			idescsi_attach,
	cleanup:		idescsi_cleanup,
	do_request:		idescsi_do_request,
	end_request:		idescsi_end_request,
	open:			idescsi_open,
	release:		idescsi_release,
	revalidate:		idescsi_revalidate,
};

static int idescsi_detect(Scsi_Host_Template *host_template)
{
	return register_ata_driver(&ata_ops);
}

static const char *idescsi_info(struct Scsi_Host *host)
{
	static const char *msg = "SCSI host adapter emulation for ATAPI devices";

	return msg;
}

static int idescsi_ioctl(Scsi_Device *dev, int cmd, void *arg)
{
	idescsi_scsi_t *scsi = idescsi_private(dev->host);

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

static inline struct bio *idescsi_kmalloc_bio(int count)
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
	idescsi_free_bio(first_bh);

	return NULL;
}

static inline int idescsi_set_direction(struct atapi_packet_command *pc)
{
	switch (pc->c[0]) {
		case READ_6:
		case READ_10:
		case READ_12:
			clear_bit(PC_WRITING, &pc->flags);
			return 0;
		case WRITE_6:
		case WRITE_10:
		case WRITE_12:
			set_bit(PC_WRITING, &pc->flags);
			return 0;
		default:
			return 1;
	}
}

static inline struct bio *idescsi_dma_bio(struct ata_device *drive, struct atapi_packet_command *pc)
{
	struct bio *bh = NULL, *first_bh = NULL;
	int segments = pc->s.scsi_cmd->use_sg;
	struct scatterlist *sg = pc->s.scsi_cmd->request_buffer;

	if (!drive->using_dma || !pc->request_transfer || pc->request_transfer % 1024)
		return NULL;
	if (idescsi_set_direction(pc))
		return NULL;
	if (segments) {
		if ((first_bh = bh = idescsi_kmalloc_bio (segments)) == NULL)
			return NULL;
#ifdef DEBUG
		printk ("ide-scsi: %s: building DMA table, %d segments, %dkB total\n", drive->name, segments, pc->request_transfer >> 10);
#endif
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
#ifdef DEBUG
		printk ("ide-scsi: %s: building DMA table for a single buffer (%dkB)\n", drive->name, pc->request_transfer >> 10);
#endif
		bh->bi_io_vec[0].bv_page = virt_to_page(pc->s.scsi_cmd->request_buffer);
		bh->bi_io_vec[0].bv_len = pc->request_transfer;
		bh->bi_io_vec[0].bv_offset = (unsigned long) pc->s.scsi_cmd->request_buffer & ~PAGE_MASK;
		bh->bi_size = pc->request_transfer;
	}
	return first_bh;
}

static inline int should_transform(struct ata_device *drive, Scsi_Cmnd *cmd)
{
	struct Scsi_Host *host = drive->driver_data;
	idescsi_scsi_t *scsi = idescsi_private(host);

	if (major(cmd->request->rq_dev) == SCSI_GENERIC_MAJOR)
		return test_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
	return test_bit(IDESCSI_TRANSFORM, &scsi->transform);
}

static int idescsi_queue(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	idescsi_scsi_t *scsi = idescsi_private(cmd->host);
	struct ata_device *drive = scsi->drive;
	struct request *rq = NULL;
	struct atapi_packet_command *pc = NULL;

	pc = kmalloc(sizeof(*pc), GFP_ATOMIC);
	rq = kmalloc(sizeof(*rq), GFP_ATOMIC);
	if (rq == NULL || pc == NULL) {
		printk (KERN_ERR "ide-scsi: %s: out of memory\n", drive->name);
		goto abort;
	}

	memset (pc->c, 0, 12);
	pc->flags = 0;
	memcpy (pc->c, cmd->cmnd, cmd->cmd_len);
	if (cmd->use_sg) {
		pc->buffer = NULL;
		pc->s.sg = cmd->request_buffer;
	} else {
		pc->buffer = cmd->request_buffer;
		pc->s.sg = NULL;
	}
	pc->s.b_count = 0;
	pc->request_transfer = pc->buffer_size = cmd->request_bufflen;
	pc->s.scsi_cmd = cmd;
	pc->s.done = done;
	pc->s.timeout = jiffies + cmd->timeout_per_command;

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

	memset(rq, 0, sizeof(*rq));
	rq->flags = REQ_PC;
	rq->special = (char *) pc;
	rq->bio = idescsi_dma_bio (drive, pc);
	ide_do_drive_cmd(drive, rq, ide_end);

	return 0;
abort:
	if (pc)
		kfree (pc);
	if (rq)
		kfree (rq);
	cmd->result = DID_ERROR << 16;
	done(cmd);

	return 0;
}

/* FIXME: This needs further investigation.
 */
static int idescsi_device_reset(Scsi_Cmnd *cmd)
{
	return SUCCESS;
}

static int idescsi_bios(Disk *disk, struct block_device *dev, int *parm)
{
	idescsi_scsi_t *scsi = idescsi_private(disk->device->host);
	struct ata_device *drive = scsi->drive;

	if (drive->bios_cyl && drive->bios_head && drive->bios_sect) {
		parm[0] = drive->bios_head;
		parm[1] = drive->bios_sect;
		parm[2] = drive->bios_cyl;
	}
	return 0;
}

static Scsi_Host_Template template = {
	module:		THIS_MODULE,
	name:		"idescsi",
	detect:		idescsi_detect,
	release:	NULL, /* unregister_ata_driver is always
				 called before scsi_unregister_host,
				 there never controllers left to
				 release by that point. */
	info:		idescsi_info,
	ioctl:		idescsi_ioctl,
	queuecommand:	idescsi_queue,
	eh_device_reset_handler:
			idescsi_device_reset,
	bios_param:	idescsi_bios,
	can_queue:	10,
	this_id:	-1,
	sg_tablesize:	256,
	cmd_per_lun:	5,
	use_clustering:	DISABLE_CLUSTERING,
	emulated:	1,
	/* FIXME: Buggy generic SCSI code doesn't remove /proc/entires! */
	proc_name:	"atapi"
};

/*
 *	Driver initialization.
 */
static void idescsi_attach(struct ata_device *drive)
{
	idescsi_scsi_t *scsi;
	struct Scsi_Host *host;


	if (drive->type == ATA_DISK)
		return;

	host = scsi_register(&template, sizeof(idescsi_scsi_t));
	if (!host) {
		printk (KERN_ERR
			"ide-scsi: %s: Can't allocate a scsi host structure\n",
			drive->name);
		return;
	}

	host->max_lun = drive->last_lun + 1;
	host->max_id = 1;

	if (ide_register_subdriver(drive, &ata_ops)) {
		printk (KERN_ERR "ide-scsi: %s: Failed to register the driver with ide.c\n", drive->name);
		scsi_unregister(host);
		return;
	}

	drive->driver_data = host;
	drive->ready_stat = 0;

	scsi = idescsi_private(host);
	memset(scsi,0, sizeof (*scsi));
	scsi->drive = drive;

	if (drive->id && (drive->id->config & 0x0060) == 0x20)
		set_bit (IDESCSI_DRQ_INTERRUPT, &scsi->flags);
	set_bit(IDESCSI_TRANSFORM, &scsi->transform);
	clear_bit(IDESCSI_SG_TRANSFORM, &scsi->transform);
#ifdef DEBUG
	set_bit(IDESCSI_LOG_CMD, &scsi->log);
#endif
}

static int __init init_idescsi_module(void)
{
	return scsi_register_host(&template);
}

static void __exit exit_idescsi_module(void)
{
	unregister_ata_driver(&ata_ops);
}

module_init(init_idescsi_module);
module_exit(exit_idescsi_module);
MODULE_LICENSE("GPL");
