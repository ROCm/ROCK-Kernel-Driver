/*
 * Copyright (C) 1996 - 1999 Gadi Oxman <gadio@netvision.net.il>
 * Copyright (C) 2000 - 2002 Paul Bristow <paul@paulbristow.net>
 */

/*
 * IDE ATAPI floppy driver.
 *
 * The driver currently doesn't have any fancy features, just the bare
 * minimum read/write support.
 *
 * This driver supports the following IDE floppy drives:
 *
 * LS-120/240 SuperDisk
 * Iomega Zip 100/250
 * Iomega PC Card Clik!/PocketZip
 *
 * Many thanks to Lode Leroy <Lode.Leroy@www.ibase.be>, who tested so many
 * ALPHA patches to this driver on an EASYSTOR LS-120 ATAPI floppy drive.
 *
 * Ver 0.1   Oct 17 96   Initial test version, mostly based on ide-tape.c.
 * Ver 0.2   Oct 31 96   Minor changes.
 * Ver 0.3   Dec  2 96   Fixed error recovery bug.
 * Ver 0.4   Jan 26 97   Add support for the HDIO_GETGEO ioctl.
 * Ver 0.5   Feb 21 97   Add partitions support.
 *                       Use the minimum of the LBA and CHS capacities.
 *                       Avoid hwgroup->rq == NULL on the last irq.
 *                       Fix potential null dereferencing with DEBUG_LOG.
 * Ver 0.8   Dec  7 97   Increase irq timeout from 10 to 50 seconds.
 *                       Add media write-protect detection.
 *                       Issue START command only if TEST UNIT READY fails.
 *                       Add work-around for IOMEGA ZIP revision 21.D.
 *                       Remove idefloppy_get_capabilities().
 * Ver 0.9   Jul  4 99   Fix a bug which might have caused the number of
 *                        bytes requested on each interrupt to be zero.
 *                        Thanks to <shanos@es.co.nz> for pointing this out.
 * Ver 0.9.sv Jan 6 01   Sam Varshavchik <mrsam@courier-mta.com>
 *                       Implement low level formatting.  Reimplemented
 *                       IDEFLOPPY_CAPABILITIES_PAGE, since we need the srfp
 *                       bit.  My LS-120 drive barfs on
 *                       IDEFLOPPY_CAPABILITIES_PAGE, but maybe it's just me.
 *                       Compromise by not reporting a failure to get this
 *                       mode page.  Implemented four IOCTLs in order to
 *                       implement formatting.  IOCTls begin with 0x4600,
 *                       0x46 is 'F' as in Format.
 *            Jan 9 01   Userland option to select format verify.
 *                       Added PC_SUPPRESS_ERROR flag - some idefloppy drives
 *                       do not implement IDEFLOPPY_CAPABILITIES_PAGE, and
 *                       return a sense error.  Suppress error reporting in
 *                       this particular case in order to avoid spurious
 *                       errors in syslog.  The culprit is
 *                       idefloppy_get_capability_page(), so move it to
 *                       idefloppy_begin_format() so that it's not used
 *                       unless absolutely necessary.
 *                       If drive does not support format progress indication
 *                       monitor the dsc bit in the status register.
 *                       Also, O_NDELAY on open will allow the device to be
 *                       opened without a disk available.  This can be used to
 *                       open an unformatted disk, or get the device capacity.
 * Ver 0.91  Dec 11 99   Added IOMEGA Clik! drive support by 
 *     		   <paul@paulbristow.net>
 * Ver 0.92  Oct 22 00   Paul Bristow became official maintainer for this 
 *           		   driver.  Included Powerbook internal zip kludge.
 * Ver 0.93  Oct 24 00   Fixed bugs for Clik! drive
 *                        no disk on insert and disk change now works
 * Ver 0.94  Oct 27 00   Tidied up to remove strstr(Clik) everywhere
 * Ver 0.95  Nov  7 00   Brought across to kernel 2.4
 * Ver 0.96  Jan  7 01   Actually in line with release version of 2.4.0
 *                       including set_bit patch from Rusty Russell
 * Ver 0.97  Jul 22 01   Merge 0.91-0.96 onto 0.9.sv for ac series
 * Ver 0.97.sv Aug 3 01  Backported from 2.4.7-ac3
 * Ver 0.98  Oct 26 01   Split idefloppy_transfer_pc into two pieces to
 *                        fix a lost interrupt problem. It appears the busy
 *                        bit was being deasserted by my IOMEGA ATAPI ZIP 100
 *                        drive before the drive was actually ready.
 * Ver 0.98a Oct 29 01   Expose delay value so we can play.
 * Ver 0.99  Feb 24 02   Remove duplicate code, modify clik! detection code
 *                       to support new PocketZip drives
 */

#define IDEFLOPPY_VERSION "0.99"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/ide.h>
#include <linux/atapi.h>
#include <linux/buffer_head.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <asm/bitops.h>

/*
 *	The following are used to debug the driver.
 */
#define IDEFLOPPY_DEBUG_LOG		0
#define IDEFLOPPY_DEBUG_INFO		0
#define IDEFLOPPY_DEBUG_BUGS		1

/* #define IDEFLOPPY_DEBUG(fmt, args...) printk(KERN_INFO fmt, ## args) */
#define IDEFLOPPY_DEBUG( fmt, args... )


/*
 *	Some drives require a longer irq timeout.
 */
#define IDEFLOPPY_WAIT_CMD		(5 * WAIT_CMD)

/*
 *	After each failed packet command we issue a request sense command
 *	and retry the packet command IDEFLOPPY_MAX_PC_RETRIES times.
 */
#define IDEFLOPPY_MAX_PC_RETRIES	3

/*
 *	In various places in the driver, we need to allocate storage
 *	for packet commands and requests, which will remain valid while
 *	we leave the driver to wait for an interrupt or a timeout event.
 */
#define IDEFLOPPY_PC_STACK		(10 + IDEFLOPPY_MAX_PC_RETRIES)

/*
 *	Removable Block Access Capabilities Page
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	page_code	:6;	/* Page code - Should be 0x1b */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	ps		:1;	/* Should be 0 */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	ps		:1;	/* Should be 0 */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	page_code	:6;	/* Page code - Should be 0x1b */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		page_length;		/* Page Length - Should be 0xa */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	reserved2	:6;
	unsigned	srfp		:1;	/* Supports reporting progress of format */
	unsigned	sflp		:1;	/* System floppy type device */
	unsigned	tlun		:3;	/* Total logical units supported by the device */
	unsigned	reserved3	:3;
	unsigned	sml		:1;	/* Single / Multiple lun supported */
	unsigned	ncd		:1;	/* Non cd optical device */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	sflp		:1;	/* System floppy type device */
	unsigned	srfp		:1;	/* Supports reporting progress of format */
	unsigned	reserved2	:6;
	unsigned	ncd		:1;	/* Non cd optical device */
	unsigned	sml		:1;	/* Single / Multiple lun supported */
	unsigned	reserved3	:3;
	unsigned	tlun		:3;	/* Total logical units supported by the device */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		reserved[8];
} idefloppy_capabilities_page_t;

/*
 *	Flexible disk page.
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	page_code	:6;	/* Page code - Should be 0x5 */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	ps		:1;	/* The device is capable of saving the page */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	ps		:1;	/* The device is capable of saving the page */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	page_code	:6;	/* Page code - Should be 0x5 */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		page_length;		/* Page Length - Should be 0x1e */
	u16		transfer_rate;		/* In kilobits per second */
	u8		heads, sectors;		/* Number of heads, Number of sectors per track */
	u16		sector_size;		/* Byes per sector */
	u16		cyls;			/* Number of cylinders */
	u8		reserved10[10];
	u8		motor_delay;		/* Motor off delay */
	u8		reserved21[7];
	u16		rpm;			/* Rotations per minute */
	u8		reserved30[2];
} idefloppy_flexible_disk_page_t;
 
/*
 *	Format capacity
 */
typedef struct {
	u8		reserved[3];
	u8		length;			/* Length of the following descriptors in bytes */
} idefloppy_capacity_header_t;

typedef struct {
	u32		blocks;			/* Number of blocks */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	dc		:2;	/* Descriptor Code */
	unsigned	reserved	:6;
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	reserved	:6;
	unsigned	dc		:2;	/* Descriptor Code */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		length_msb;		/* Block Length (MSB)*/
	u16		length;			/* Block Length */
} idefloppy_capacity_descriptor_t;

#define CAPACITY_INVALID	0x00
#define CAPACITY_UNFORMATTED	0x01
#define CAPACITY_CURRENT	0x02
#define CAPACITY_NO_CARTRIDGE	0x03

/*
 *	Most of our global data which we need to save even as we leave the
 *	driver due to an interrupt or a timer event is stored in a variable
 *	of type idefloppy_floppy_t, defined below.
 */
typedef struct {
	struct ata_device *drive;

	struct atapi_packet_command *pc;		/* Current packet command */
	struct atapi_packet_command *failed_pc;		/* Last failed packet command */
	struct atapi_packet_command pc_stack[IDEFLOPPY_PC_STACK];	/* Packet command stack */
	int pc_stack_index;			/* Next free packet command storage space */
	struct request rq_stack[IDEFLOPPY_PC_STACK];
	int rq_stack_index;			/* We implement a circular array */

	/*
	 *	Last error information
	 */
	u8 sense_key, asc, ascq;
	u8 ticks;		/* delay this long before sending packet command */
	int progress_indication;

	/*
	 *	Device information
	 */
	int blocks, block_size, bs_factor;			/* Current format */
	idefloppy_capacity_descriptor_t capacity;		/* Last format capacity */
	idefloppy_flexible_disk_page_t flexible_disk_page;	/* Copy of the flexible disk page */
	int wp;							/* Write protect */
	int srfp;			/* Supports format progress report */
	unsigned long flags;			/* Status/Action flags */
} idefloppy_floppy_t;

#define IDEFLOPPY_TICKS_DELAY	3	/* default delay for ZIP 100 */

/*
 *	Floppy flag bits values.
 */
#define IDEFLOPPY_DRQ_INTERRUPT		0	/* DRQ interrupt device */
#define IDEFLOPPY_MEDIA_CHANGED		1	/* Media may have changed */
#define IDEFLOPPY_USE_READ12		2	/* Use READ12/WRITE12 or READ10/WRITE10 */
#define	IDEFLOPPY_FORMAT_IN_PROGRESS	3	/* Format in progress */
#define IDEFLOPPY_CLIK_DRIVE	        4       /* Avoid commands not supported in Clik drive */
#define IDEFLOPPY_ZIP_DRIVE		5	/* Requires BH algorithm for packets */

/*
 *	ATAPI floppy drive packet commands
 */
#define IDEFLOPPY_FORMAT_UNIT_CMD	0x04
#define IDEFLOPPY_INQUIRY_CMD		0x12
#define IDEFLOPPY_MODE_SELECT_CMD	0x55
#define IDEFLOPPY_MODE_SENSE_CMD	0x5a
#define IDEFLOPPY_READ10_CMD		0x28
#define IDEFLOPPY_READ12_CMD		0xa8
#define IDEFLOPPY_READ_CAPACITY_CMD	0x23
#define IDEFLOPPY_REQUEST_SENSE_CMD	0x03
#define IDEFLOPPY_PREVENT_REMOVAL_CMD	0x1e
#define IDEFLOPPY_SEEK_CMD		0x2b
#define IDEFLOPPY_START_STOP_CMD	0x1b
#define IDEFLOPPY_TEST_UNIT_READY_CMD	0x00
#define IDEFLOPPY_VERIFY_CMD		0x2f
#define IDEFLOPPY_WRITE10_CMD		0x2a
#define IDEFLOPPY_WRITE12_CMD		0xaa
#define IDEFLOPPY_WRITE_VERIFY_CMD	0x2e

/*
 *	Defines for the mode sense command
 */
#define MODE_SENSE_CURRENT		0x00
#define MODE_SENSE_CHANGEABLE		0x01
#define MODE_SENSE_DEFAULT		0x02 
#define MODE_SENSE_SAVED		0x03

/*
 *	IOCTLs used in low-level formatting.
 */

#define	IDEFLOPPY_IOCTL_FORMAT_SUPPORTED	0x4600
#define	IDEFLOPPY_IOCTL_FORMAT_GET_CAPACITY	0x4601
#define	IDEFLOPPY_IOCTL_FORMAT_START		0x4602
#define IDEFLOPPY_IOCTL_FORMAT_GET_PROGRESS	0x4603

/*
 *	Error codes which are returned in rq->errors to the higher part
 *	of the driver.
 */
#define	IDEFLOPPY_ERROR_GENERAL		101

/*
 *	Pages of the SELECT SENSE / MODE SENSE packet commands.
 */
#define	IDEFLOPPY_CAPABILITIES_PAGE	0x1b
#define IDEFLOPPY_FLEXIBLE_DISK_PAGE	0x05

/*
 *	Mode Parameter Header for the MODE SENSE packet command
 */
typedef struct {
	u16		mode_data_length;	/* Length of the following data transfer */
	u8		medium_type;		/* Medium Type */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	reserved3	:7;
	unsigned	wp		:1;	/* Write protect */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	wp		:1;	/* Write protect */
	unsigned	reserved3	:7;
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		reserved[4];
} idefloppy_mode_parameter_header_t;

/*
 *	idefloppy_end_request is used to finish servicing a request.
 *
 *	For read/write requests, we will call ata_end_request to pass to the
 *	next buffer.
 */
static int idefloppy_end_request(struct ata_device *drive, struct request *rq, int uptodate)
{
	unsigned long flags;
	struct ata_channel *ch = drive->channel;
	idefloppy_floppy_t *floppy = drive->driver_data;
	int error;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "Reached idefloppy_end_request\n");
#endif

	switch (uptodate) {
		case 0: error = IDEFLOPPY_ERROR_GENERAL; break;
		case 1: error = 0; break;
		default: error = uptodate;
	}
	if (error)
		floppy->failed_pc = NULL;
	/* Why does this happen? */
	if (!rq)
		return 0;

	if (!(rq->flags & REQ_SPECIAL)) {
		ata_end_request(drive, rq, uptodate, 0);
		return 0;
	}

	spin_lock_irqsave(ch->lock, flags);

	rq->errors = error;
	blkdev_dequeue_request(rq);
	drive->rq = NULL;
	end_that_request_last(rq);

	spin_unlock_irqrestore(ch->lock, flags);

	return 0;
}

static void idefloppy_input_buffers(struct ata_device *drive, struct request *rq,
	struct atapi_packet_command *pc, unsigned int bcount)
{
	struct bio *bio = rq->bio;
	int count;

	while (bcount) {
		if (pc->b_count == bio->bi_size) {
			rq->sector += rq->current_nr_sectors;
			rq->nr_sectors -= rq->current_nr_sectors;
			idefloppy_end_request(drive, rq, 1);
			if ((bio = rq->bio) != NULL)
				pc->b_count = 0;
		}
		if (bio == NULL) {
			printk (KERN_ERR "%s: bio == NULL in %s, bcount == %d\n", drive->name, __FUNCTION__, bcount);
			atapi_discard_data(drive, bcount);
			return;
		}
		count = min_t(unsigned int, bio->bi_size - pc->b_count, bcount);
		atapi_read(drive, bio_data(bio) + pc->b_count, count);
		bcount -= count; pc->b_count += count;
	}
}

static void idefloppy_output_buffers(struct ata_device *drive, struct request *rq,
		struct atapi_packet_command *pc, unsigned int bcount)
{
	struct bio *bio = rq->bio;
	int count;

	while (bcount) {
		if (!pc->b_count) {
			rq->sector += rq->current_nr_sectors;
			rq->nr_sectors -= rq->current_nr_sectors;
			idefloppy_end_request(drive, rq, 1);
			if ((bio = rq->bio) != NULL) {
				pc->b_data = bio_data(bio);
				pc->b_count = bio->bi_size;
			}
		}
		if (bio == NULL) {
			printk (KERN_ERR "%s: bio == NULL in idefloppy_output_buffers, bcount == %d\n", drive->name, bcount);
			atapi_write_zeros (drive, bcount);
			return;
		}
		count = min_t(unsigned int, pc->b_count, bcount);
		atapi_write(drive, pc->b_data, count);
		bcount -= count; pc->b_data += count; pc->b_count -= count;
	}
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static void idefloppy_update_buffers(struct ata_device *drive, struct request *rq)
{
	struct bio *bio = rq->bio;

	while ((bio = rq->bio) != NULL)
		idefloppy_end_request(drive, rq, 1);
}
#endif

/*
 *	idefloppy_queue_pc_head generates a new packet command request in front
 *	of the request queue, before the current request, so that it will be
 *	processed immediately, on the next pass through the driver.
 */
static void idefloppy_queue_pc_head(struct ata_device *drive,
		struct atapi_packet_command *pc, struct request *rq)
{
	memset(rq, 0, sizeof(*rq));
	rq->flags = REQ_SPECIAL;
	/* FIXME: --mdcki */
	rq->buffer = (char *) pc;
	(void) ide_do_drive_cmd (drive, rq, ide_preempt);
}

static struct atapi_packet_command *idefloppy_next_pc_storage(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (floppy->pc_stack_index==IDEFLOPPY_PC_STACK)
		floppy->pc_stack_index=0;
	return (&floppy->pc_stack[floppy->pc_stack_index++]);
}

static struct request *idefloppy_next_rq_storage(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (floppy->rq_stack_index==IDEFLOPPY_PC_STACK)
		floppy->rq_stack_index=0;
	return (&floppy->rq_stack[floppy->rq_stack_index++]);
}

/*
 *	idefloppy_analyze_error is called on each failed packet command retry
 *	to analyze the request sense.
 */
static void idefloppy_analyze_error(struct ata_device *drive, atapi_request_sense_result_t *result)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	floppy->sense_key = result->sense_key; floppy->asc = result->asc; floppy->ascq = result->ascq;
	floppy->progress_indication= result->sksv ?
		(unsigned short)get_unaligned((u16 *)(result->sk_specific)):0x10000;
#if IDEFLOPPY_DEBUG_LOG
	if (floppy->failed_pc)
		printk (KERN_INFO "ide-floppy: pc = %x, sense key = %x, asc = %x, ascq = %x\n",floppy->failed_pc->c[0],result->sense_key,result->asc,result->ascq);
	else
		printk (KERN_INFO "ide-floppy: sense key = %x, asc = %x, ascq = %x\n",result->sense_key,result->asc,result->ascq);
#endif
}

static void idefloppy_request_sense_callback(struct ata_device *drive, struct request *rq)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: Reached idefloppy_request_sense_callback\n");
#endif
	if (!floppy->pc->error) {
		idefloppy_analyze_error(drive,(atapi_request_sense_result_t *) floppy->pc->buffer);
		idefloppy_end_request(drive, rq, 1);
	} else {
		printk (KERN_ERR "Error in REQUEST SENSE itself - Aborting request!\n");
		idefloppy_end_request(drive, rq, 0);
	}
}

/*
 *	General packet command callback function.
 */
static void idefloppy_pc_callback(struct ata_device *drive, struct request *rq)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: Reached idefloppy_pc_callback\n");
#endif

	idefloppy_end_request(drive, rq, floppy->pc->error ? 0:1);
}

static void idefloppy_create_request_sense_cmd(struct atapi_packet_command *pc)
{
	atapi_init_pc(pc);
	pc->c[0] = IDEFLOPPY_REQUEST_SENSE_CMD;
	pc->c[4] = 255;
	pc->request_transfer = 18;
	pc->callback = &idefloppy_request_sense_callback;
}

/*
 *	idefloppy_retry_pc is called when an error was detected during the
 *	last packet command. We queue a request sense packet command in
 *	the head of the request list.
 */
static void idefloppy_retry_pc(struct ata_device *drive)
{
	struct atapi_packet_command *pc;
	struct request *rq;
	atapi_error_reg_t error;

	error.all = IN_BYTE(IDE_ERROR_REG);
	pc = idefloppy_next_pc_storage(drive);
	rq = idefloppy_next_rq_storage(drive);
	idefloppy_create_request_sense_cmd(pc);
	idefloppy_queue_pc_head(drive, pc, rq);
}

/*
 *	idefloppy_pc_intr is the usual interrupt handler which will be called
 *	during a packet command.
 */
static ide_startstop_t idefloppy_pc_intr(struct ata_device *drive, struct request *rq)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	atapi_status_reg_t status;
	atapi_bcount_reg_t bcount;
	atapi_ireason_reg_t ireason;
	struct atapi_packet_command *pc = floppy->pc;
	unsigned int temp;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: Reached idefloppy_pc_intr interrupt handler\n");
#endif

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_bit(PC_DMA_IN_PROGRESS, &pc->flags)) {
		if (udma_stop(drive)) {
			set_bit (PC_DMA_ERROR, &pc->flags);
		} else {
			pc->actually_transferred=pc->request_transfer;
			idefloppy_update_buffers(drive, rq);
		}
# if IDEFLOPPY_DEBUG_LOG
		printk (KERN_INFO "ide-floppy: DMA finished\n");
# endif
	}
#endif

	ata_status(drive, 0, 0);
	status.all = drive->status;					/* Clear the interrupt */

	if (!status.b.drq) {						/* No more interrupts */
#if IDEFLOPPY_DEBUG_LOG
		printk (KERN_INFO "Packet command completed, %d bytes transferred\n", pc->actually_transferred);
#endif /* IDEFLOPPY_DEBUG_LOG */
		clear_bit(PC_DMA_IN_PROGRESS, &pc->flags);

		local_irq_enable();

		if (status.b.check || test_bit(PC_DMA_ERROR, &pc->flags)) {	/* Error detected */
#if IDEFLOPPY_DEBUG_LOG
			printk (KERN_INFO "ide-floppy: %s: I/O error\n",drive->name);
#endif
			rq->errors++;
			if (pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD) {
				printk (KERN_ERR "ide-floppy: I/O error in request sense command\n");
				return ATA_OP_FINISHED;
			}
			idefloppy_retry_pc (drive);				/* Retry operation */
			return ATA_OP_FINISHED; /* queued, but not started */
		}
		pc->error = 0;
		if (floppy->failed_pc == pc)
			floppy->failed_pc=NULL;
		pc->callback(drive, rq);    /* Command finished - Call the callback function */
		return ATA_OP_FINISHED;
	}
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_and_clear_bit(PC_DMA_IN_PROGRESS, &pc->flags)) {
		printk (KERN_ERR "ide-floppy: The floppy wants to issue more interrupts in DMA mode\n");
		udma_enable(drive, 0, 1);

		return ATA_OP_FINISHED;
	}
#endif

	bcount.b.high=IN_BYTE (IDE_BCOUNTH_REG);			/* Get the number of bytes to transfer */
	bcount.b.low=IN_BYTE (IDE_BCOUNTL_REG);			/* on this interrupt */
	ireason.all=IN_BYTE (IDE_IREASON_REG);

	if (ireason.b.cod) {
		printk (KERN_ERR "ide-floppy: CoD != 0 in idefloppy_pc_intr\n");

		return ATA_OP_FINISHED;
	}
	if (ireason.b.io == test_bit(PC_WRITING, &pc->flags)) {	/* Hopefully, we will never get here */
		printk (KERN_ERR "ide-floppy: We wanted to %s, ", ireason.b.io ? "Write":"Read");
		printk (KERN_ERR "but the floppy wants us to %s !\n",ireason.b.io ? "Read":"Write");

		return ATA_OP_FINISHED;
	}
	if (!test_bit(PC_WRITING, &pc->flags)) {			/* Reading - Check that we have enough space */
		temp = pc->actually_transferred + bcount.all;
		if ( temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk (KERN_ERR "ide-floppy: The floppy wants to send us more data than expected - discarding data\n");

				atapi_discard_data (drive,bcount.all);
				ata_set_handler(drive, idefloppy_pc_intr,IDEFLOPPY_WAIT_CMD, NULL);

				return ATA_OP_CONTINUES;
			}
#if IDEFLOPPY_DEBUG_LOG
			printk (KERN_NOTICE "ide-floppy: The floppy wants to send us more data than expected - allowing transfer\n");
#endif
		}
	}
	if (test_bit (PC_WRITING, &pc->flags)) {
		if (pc->buffer != NULL)
			atapi_write(drive,pc->current_position,bcount.all);	/* Write the current buffer */
		else
			idefloppy_output_buffers(drive, rq, pc, bcount.all);
	} else {
		if (pc->buffer != NULL)
			atapi_read(drive,pc->current_position,bcount.all);	/* Read the current buffer */
		else
			idefloppy_input_buffers (drive, rq, pc, bcount.all);
	}
	pc->actually_transferred+=bcount.all;				/* Update the current position */
	pc->current_position+=bcount.all;

	ata_set_handler(drive, idefloppy_pc_intr, IDEFLOPPY_WAIT_CMD, NULL);		/* And set the interrupt handler again */

	return ATA_OP_CONTINUES;
}

/*
 * This is the original routine that did the packet transfer.
 * It fails at high speeds on the Iomega ZIP drive, so there's a slower version
 * for that drive below. The algorithm is chosen based on drive type
 */
static ide_startstop_t idefloppy_transfer_pc(struct ata_device *drive, struct request *rq)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	atapi_ireason_reg_t ireason;
	int ret;

	ret = ata_status_poll(drive, DRQ_STAT, BUSY_STAT, WAIT_READY, rq);
	if (ret != ATA_OP_READY) {
		printk (KERN_ERR "ide-floppy: Strange, packet command initiated yet DRQ isn't asserted\n");

		return ret;
	}

	ireason.all = IN_BYTE(IDE_IREASON_REG);

	if (!ireason.b.cod || ireason.b.io) {
		printk (KERN_ERR "ide-floppy: (IO,CoD) != (0,1) while issuing a packet command\n");
		ret = ATA_OP_FINISHED;
	} else {
		ata_set_handler(drive, idefloppy_pc_intr, IDEFLOPPY_WAIT_CMD, NULL);	/* Set the interrupt routine */
		atapi_write(drive, floppy->pc->c, 12); /* Send the actual packet */
		ret = ATA_OP_CONTINUES;
	}

	return ret;
}


/*
 * What we have here is a classic case of a top half / bottom half
 * interrupt service routine. In interrupt mode, the device sends
 * an interrupt to signal it's ready to receive a packet. However,
 * we need to delay about 2-3 ticks before issuing the packet or we
 * gets in trouble.
 *
 * So, follow carefully. transfer_pc1 is called as an interrupt (or
 * directly). In either case, when the device says it's ready for a 
 * packet, we schedule the packet transfer to occur about 2-3 ticks
 * later in transfer_pc2.
 */
static ide_startstop_t idefloppy_transfer_pc2(struct ata_device *drive, struct request *__rq, unsigned long *wait)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	atapi_write(drive, floppy->pc->c, 12); /* Send the actual packet */
	*wait = IDEFLOPPY_WAIT_CMD;	/* Timeout for the packet command */

	return ATA_OP_CONTINUES;
}

static ide_startstop_t idefloppy_transfer_pc1(struct ata_device *drive, struct request *rq)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	atapi_ireason_reg_t ireason;
	int ret;

	ret = ata_status_poll(drive, DRQ_STAT, BUSY_STAT, WAIT_READY, rq);
	if (ret != ATA_OP_READY) {
		printk (KERN_ERR "ide-floppy: Strange, packet command initiated yet DRQ isn't asserted\n");

		return ret;
	}

	ireason.all = IN_BYTE(IDE_IREASON_REG);

	if (!ireason.b.cod || ireason.b.io) {
		printk (KERN_ERR "ide-floppy: (IO,CoD) != (0,1) while issuing a packet command\n");
		ret = ATA_OP_FINISHED;
	} else {

		/*
		 * The following delay solves a problem with ATAPI Zip 100 drives where
		 * the Busy flag was apparently being deasserted before the unit was
		 * ready to receive data. This was happening on a 1200 MHz Athlon
		 * system. 10/26/01 25msec is too short, 40 and 50msec work well.
		 * idefloppy_pc_intr will not be actually used until after the packet
		 * is moved in about 50 msec.
		 */
		ata_set_handler(drive,
				idefloppy_pc_intr,	/* service routine for packet command */
				floppy->ticks,		/* wait this long before "failing" */
				idefloppy_transfer_pc2);	/* fail == transfer_pc2 */
		ret = ATA_OP_CONTINUES;
	}

	return ret;
}

/*
 *	Issue a packet command
 */
static ide_startstop_t idefloppy_issue_pc(struct ata_device *drive, struct request *rq,
		struct atapi_packet_command *pc)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	atapi_bcount_reg_t bcount;
	int dma_ok = 0;
	ata_handler_t *pkt_xfer_routine;

#if IDEFLOPPY_DEBUG_BUGS
	if (floppy->pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD && pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD) {
		printk(KERN_ERR "ide-floppy: possible ide-floppy.c bug - Two request sense in serial were issued\n");
	}
#endif

	if (floppy->failed_pc == NULL && pc->c[0] != IDEFLOPPY_REQUEST_SENSE_CMD)
		floppy->failed_pc=pc;
	floppy->pc=pc;							/* Set the current packet command */

	if (pc->retries > IDEFLOPPY_MAX_PC_RETRIES || test_bit(PC_ABORT, &pc->flags)) {
		/*
		 *	We will "abort" retrying a packet command in case
		 *	a legitimate error code was received.
		 */
		if (!test_bit(PC_ABORT, &pc->flags)) {
			if (!test_bit(PC_SUPPRESS_ERROR, &pc->flags)) {
				;
      printk( KERN_ERR "ide-floppy: %s: I/O error, pc = %2x, key = %2x, asc = %2x, ascq = %2x\n",
				drive->name, pc->c[0], floppy->sense_key, floppy->asc, floppy->ascq);
			}
			pc->error = IDEFLOPPY_ERROR_GENERAL;		/* Giving up */
		}
		floppy->failed_pc = NULL;
		pc->callback(drive, rq);
		return ATA_OP_FINISHED;
	}
#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "Retry number - %d\n",pc->retries);
#endif

	pc->retries++;
	pc->actually_transferred=0;					/* We haven't transferred any data yet */
	pc->current_position=pc->buffer;
	bcount.all = min(pc->request_transfer, 63 * 1024);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_and_clear_bit (PC_DMA_ERROR, &pc->flags))
		udma_enable(drive, 0, 1);

	if (test_bit (PC_DMA_RECOMMENDED, &pc->flags) && drive->using_dma)
		dma_ok = udma_init(drive, rq);
#endif

	ata_irq_enable(drive, 1);
	OUT_BYTE(dma_ok ? 1:0,IDE_FEATURE_REG);			/* Use PIO/DMA */
	OUT_BYTE(bcount.b.high,IDE_BCOUNTH_REG);
	OUT_BYTE(bcount.b.low,IDE_BCOUNTL_REG);
	OUT_BYTE(drive->select.all,IDE_SELECT_REG);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (dma_ok) {							/* Begin DMA, if necessary */
		set_bit(PC_DMA_IN_PROGRESS, &pc->flags);
		udma_start(drive, rq);
	}
#endif

	/* Can we transfer the packet when we get the interrupt or wait? */
	if (test_bit (IDEFLOPPY_ZIP_DRIVE, &floppy->flags)) {
		pkt_xfer_routine = &idefloppy_transfer_pc1;	/* wait */
	} else {
		pkt_xfer_routine = &idefloppy_transfer_pc;	/* immediate */
	}

	if (test_bit(IDEFLOPPY_DRQ_INTERRUPT, &floppy->flags)) {
		ata_set_handler(drive, pkt_xfer_routine, IDEFLOPPY_WAIT_CMD, NULL);
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG);		/* Issue the packet command */

		return ATA_OP_CONTINUES;
	} else {
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG);
		return pkt_xfer_routine(drive, rq);
	}
}

static void idefloppy_rw_callback(struct ata_device *drive, struct request *rq)
{
#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: Reached idefloppy_rw_callback\n");
#endif /* IDEFLOPPY_DEBUG_LOG */

	idefloppy_end_request(drive, rq, 1);
	return;
}

static void idefloppy_create_prevent_cmd(struct atapi_packet_command *pc, int prevent)
{
#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: creating prevent removal command, prevent = %d\n", prevent);
#endif

	atapi_init_pc (pc);
	pc->c[0] = IDEFLOPPY_PREVENT_REMOVAL_CMD;
	pc->c[4] = prevent;
	pc->callback = idefloppy_pc_callback;
}

static void idefloppy_create_read_capacity_cmd(struct atapi_packet_command *pc)
{
	atapi_init_pc(pc);
	pc->c[0] = IDEFLOPPY_READ_CAPACITY_CMD;
	pc->c[7] = 255;
	pc->c[8] = 255;
	pc->request_transfer = 255;
	pc->callback = idefloppy_pc_callback;
}

static void idefloppy_create_format_unit_cmd(struct atapi_packet_command *pc,
		int b, int l, int flags)
{
	atapi_init_pc (pc);
	pc->c[0] = IDEFLOPPY_FORMAT_UNIT_CMD;
	pc->c[1] = 0x17;

	memset(pc->buffer, 0, 12);
	pc->buffer[1] = 0xA2;
	/* Default format list header, byte 1: FOV/DCRT/IMM bits set */

	if (flags & 1)				/* Verify bit on... */
		pc->buffer[1] ^= 0x20;		/* ... turn off DCRT bit */
	pc->buffer[3] = 8;

	put_unaligned(htonl(b), (unsigned int *)(&pc->buffer[4]));
	put_unaligned(htonl(l), (unsigned int *)(&pc->buffer[8]));
	pc->buffer_size = 12;
	set_bit(PC_WRITING, &pc->flags);
	pc->callback = idefloppy_pc_callback;
}

/*
 *	A mode sense command is used to "sense" floppy parameters.
 */
static void idefloppy_create_mode_sense_cmd(struct atapi_packet_command *pc, u8 page_code, u8 type)
{
	unsigned short length = sizeof(idefloppy_mode_parameter_header_t);

	atapi_init_pc(pc);
	pc->c[0] = IDEFLOPPY_MODE_SENSE_CMD;
	pc->c[1] = 0;
	pc->c[2] = page_code + (type << 6);

	switch (page_code) {
		case IDEFLOPPY_CAPABILITIES_PAGE:
			length += 12;
			break;
		case IDEFLOPPY_FLEXIBLE_DISK_PAGE:
			length += 32;
			break;
		default:
			printk (KERN_ERR "ide-floppy: unsupported page code in create_mode_sense_cmd\n");
	}
	put_unaligned(htons(length), (unsigned short *) &pc->c[7]);
	pc->request_transfer = length;
	pc->callback = idefloppy_pc_callback;
}

static void idefloppy_create_start_stop_cmd(struct atapi_packet_command *pc, int start)
{
	atapi_init_pc(pc);
	pc->c[0] = IDEFLOPPY_START_STOP_CMD;
	pc->c[4] = start;
	pc->callback = idefloppy_pc_callback;
}

static void idefloppy_create_test_unit_ready_cmd(struct atapi_packet_command *pc)
{
	atapi_init_pc(pc);
	pc->c[0] = IDEFLOPPY_TEST_UNIT_READY_CMD;
	pc->callback = idefloppy_pc_callback;
}

static void idefloppy_create_rw_cmd(idefloppy_floppy_t *floppy,
		struct atapi_packet_command *pc, struct request *rq, sector_t sector)
{
	int block = sector / floppy->bs_factor;
	int blocks = rq->nr_sectors / floppy->bs_factor;
	int cmd = rq_data_dir(rq);

#if IDEFLOPPY_DEBUG_LOG
	printk ("create_rw1%d_cmd: block == %d, blocks == %d\n",
		2 * test_bit (IDEFLOPPY_USE_READ12, &floppy->flags), block, blocks);
#endif

	atapi_init_pc(pc);
	if (test_bit (IDEFLOPPY_USE_READ12, &floppy->flags)) {
		pc->c[0] = cmd == READ ? IDEFLOPPY_READ12_CMD : IDEFLOPPY_WRITE12_CMD;
		put_unaligned(htonl (blocks), (unsigned int *) &pc->c[6]);
	} else {
		pc->c[0] = cmd == READ ? IDEFLOPPY_READ10_CMD : IDEFLOPPY_WRITE10_CMD;
		put_unaligned(htons (blocks), (unsigned short *) &pc->c[7]);
	}
	put_unaligned(htonl(block), (unsigned int *) &pc->c[2]);
	pc->callback = idefloppy_rw_callback;
	pc->b_data = rq->buffer;
	pc->b_count = cmd == READ ? 0 : rq->bio->bi_size;
	if (rq->flags & REQ_RW)
		set_bit(PC_WRITING, &pc->flags);
	pc->buffer = NULL;
	pc->request_transfer = pc->buffer_size = blocks * floppy->block_size;
	set_bit(PC_DMA_RECOMMENDED, &pc->flags);
}

/*
 * This is our request handling function.
 */
static ide_startstop_t idefloppy_do_request(struct ata_device *drive, struct request *rq, sector_t block)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct atapi_packet_command *pc;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "rq_status: %d, rq_dev: %u, flags: %lx, errors: %d\n",rq->rq_status,(unsigned int) rq->rq_dev,rq->flags,rq->errors);
	printk (KERN_INFO "sector: %ld, nr_sectors: %ld, current_nr_sectors: %d\n",rq->sector,rq->nr_sectors,rq->current_nr_sectors);
#endif

	if (rq->errors >= ERROR_MAX) {
		if (floppy->failed_pc != NULL)
			printk (KERN_ERR "ide-floppy: %s: I/O error, pc = %2x, key = %2x, asc = %2x, ascq = %2x\n",
				drive->name, floppy->failed_pc->c[0], floppy->sense_key, floppy->asc, floppy->ascq);
		else
			printk (KERN_ERR "ide-floppy: %s: I/O error\n", drive->name);

		idefloppy_end_request(drive, rq, 0);

		return ATA_OP_FINISHED;
	}
	if (rq->flags & REQ_CMD) {
		if (rq->sector % floppy->bs_factor || rq->nr_sectors % floppy->bs_factor) {
			printk ("%s: unsupported r/w request size\n", drive->name);

			idefloppy_end_request(drive, rq, 0);

			return ATA_OP_FINISHED;
		}
		pc = idefloppy_next_pc_storage(drive);
		idefloppy_create_rw_cmd (floppy, pc, rq, block);
	} else if (rq->flags & REQ_SPECIAL) {
		/* FIXME: --mdcki */
		pc = (struct atapi_packet_command *) rq->buffer;
	} else {
		blk_dump_rq_flags(rq, "ide-floppy: unsupported command in queue");

		idefloppy_end_request(drive, rq, 0);

		return ATA_OP_FINISHED;
	}

	return idefloppy_issue_pc(drive, rq, pc);
}

/*
 *	idefloppy_queue_pc_tail adds a special packet command request to the
 *	tail of the request queue, and waits for it to be serviced.
 */
static int idefloppy_queue_pc_tail(struct ata_device *drive, struct atapi_packet_command *pc)
{
	struct request rq;

	memset(&rq, 0, sizeof(rq));
	/* FIXME: --mdcki */
	rq.buffer = (char *) pc;
	rq.flags = REQ_SPECIAL;

	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

/*
 *	Look at the flexible disk page parameters. We will ignore the CHS
 *	capacity parameters and use the LBA parameters instead.
 */
static int idefloppy_get_flexible_disk_page(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct atapi_packet_command pc;
	idefloppy_mode_parameter_header_t *header;
	idefloppy_flexible_disk_page_t *page;
	int capacity, lba_capacity;

	idefloppy_create_mode_sense_cmd (&pc, IDEFLOPPY_FLEXIBLE_DISK_PAGE, MODE_SENSE_CURRENT);
	if (idefloppy_queue_pc_tail (drive,&pc)) {
		printk (KERN_ERR "ide-floppy: Can't get flexible disk page parameters\n");
		return 1;
	}
	header = (idefloppy_mode_parameter_header_t *) pc.buffer;
	floppy->wp = header->wp;
	page = (idefloppy_flexible_disk_page_t *) (header + 1);

	page->transfer_rate = ntohs (page->transfer_rate);
	page->sector_size = ntohs (page->sector_size);
	page->cyls = ntohs (page->cyls);
	page->rpm = ntohs (page->rpm);
	capacity = page->cyls * page->heads * page->sectors * page->sector_size;
	if (memcmp (page, &floppy->flexible_disk_page, sizeof (idefloppy_flexible_disk_page_t)))
		printk (KERN_INFO "%s: %dkB, %d/%d/%d CHS, %d kBps, %d sector size, %d rpm\n",
			drive->name, capacity / 1024, page->cyls, page->heads, page->sectors,
			page->transfer_rate / 8, page->sector_size, page->rpm);

	floppy->flexible_disk_page = *page;
	drive->bios_cyl = page->cyls;
	drive->bios_head = page->heads;
	drive->bios_sect = page->sectors;
	lba_capacity = floppy->blocks * floppy->block_size;
	if (capacity < lba_capacity) {
		printk (KERN_NOTICE "%s: The disk reports a capacity of %d bytes, "
			"but the drive only handles %d\n",
			drive->name, lba_capacity, capacity);
		floppy->blocks = floppy->block_size ? capacity / floppy->block_size : 0;
	}
	return 0;
}

static int idefloppy_get_capability_page(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct atapi_packet_command pc;
	idefloppy_mode_parameter_header_t *header;
	idefloppy_capabilities_page_t *page;

	floppy->srfp=0;
	idefloppy_create_mode_sense_cmd(&pc, IDEFLOPPY_CAPABILITIES_PAGE,
						 MODE_SENSE_CURRENT);

	set_bit(PC_SUPPRESS_ERROR, &pc.flags);
	if (idefloppy_queue_pc_tail (drive,&pc)) {
		return 1;
	}

	header = (idefloppy_mode_parameter_header_t *) pc.buffer;
	page= (idefloppy_capabilities_page_t *)(header+1);
	floppy->srfp=page->srfp;
	return (0);
}

/*
 *	Determine if a media is present in the floppy drive, and if so,
 *	its LBA capacity.
 */
static int idefloppy_get_capacity(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct atapi_packet_command pc;
	idefloppy_capacity_header_t *header;
	idefloppy_capacity_descriptor_t *descriptor;
	int i, descriptors, rc = 1, blocks, length;

	drive->bios_cyl = 0;
	drive->bios_head = drive->bios_sect = 0;
	floppy->blocks = floppy->bs_factor = 0;
	drive->part[0].nr_sects = 0;

	idefloppy_create_read_capacity_cmd (&pc);
	if (idefloppy_queue_pc_tail (drive, &pc)) {
		printk (KERN_ERR "ide-floppy: Can't get floppy parameters\n");
		return 1;
	}
	header = (idefloppy_capacity_header_t *) pc.buffer;
	descriptors = header->length / sizeof (idefloppy_capacity_descriptor_t);
	descriptor = (idefloppy_capacity_descriptor_t *) (header + 1);

	for (i = 0; i < descriptors; i++, descriptor++) {
                blocks = descriptor->blocks = ntohl (descriptor->blocks);
                length = descriptor->length = ntohs (descriptor->length);

		if (!i)
		{
			switch (descriptor->dc) {
                case CAPACITY_UNFORMATTED: /* Clik! drive returns this instead of CAPACITY_CURRENT */
                        if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags))
                                break; /* If it is not a clik drive, break out (maintains previous driver behaviour) */
                case CAPACITY_CURRENT: /* Normal Zip/LS-120 disks */
                        if (memcmp (descriptor, &floppy->capacity, sizeof (idefloppy_capacity_descriptor_t)))
                                printk (KERN_INFO "%s: %dkB, %d blocks, %d sector size\n", drive->name, blocks * length / 1024, blocks, length);
                        floppy->capacity = *descriptor;
                        if (!length || length % 512)
                                printk (KERN_NOTICE "%s: %d bytes block size not supported\n", drive->name, length);
                        else {
                                floppy->blocks = blocks;
                                floppy->block_size = length;
                                if ((floppy->bs_factor = length / 512) != 1)
                                        printk (KERN_NOTICE "%s: warning: non 512 bytes block size not fully supported\n", drive->name);
                                rc = 0;
                        }
                        break;
                case CAPACITY_NO_CARTRIDGE:
                        /* This is a KERN_ERR so it appears on screen for the user to see */
                        printk (KERN_ERR "%s: No disk in drive\n", drive->name);
                                        break;
                case CAPACITY_INVALID:
                        printk (KERN_ERR "%s: Invalid capacity for disk in drive\n", drive->name);
                                        break;
		}
		}
		if (!i) {
		IDEFLOPPY_DEBUG( "Descriptor 0 Code: %d\n", descriptor->dc);
		}
		IDEFLOPPY_DEBUG( "Descriptor %d: %dkB, %d blocks, %d sector size\n", i, blocks * length / 1024, blocks, length);
	}

	/* Clik! disk does not support get_flexible_disk_page */
        if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags))
	{
		(void) idefloppy_get_flexible_disk_page (drive);
	}

	drive->part[0].nr_sects = floppy->blocks * floppy->bs_factor;
	return rc;
}

/*
** Obtain the list of formattable capacities.
** Very similar to idefloppy_get_capacity, except that we push the capacity
** descriptors to userland, instead of our own structures.
**
** Userland gives us the following structure:
**
** struct idefloppy_format_capacities {
**        int nformats;
**        struct {
**                int nblocks;
**                int blocksize;
**                } formats[];
**        } ;
**
** userland initializes nformats to the number of allocated formats[]
** records.  On exit we set nformats to the number of records we've
** actually initialized.
**
*/

static int idefloppy_get_format_capacities(struct ata_device *drive,
		struct inode *inode,
		struct file *file,
		int *arg)	/* Cheater */
{
        struct atapi_packet_command pc;
	idefloppy_capacity_header_t *header;
        idefloppy_capacity_descriptor_t *descriptor;
	int i, descriptors, blocks, length;
	int u_array_size;
	int u_index;
	int *argp;

	if (get_user(u_array_size, arg))
		return (-EFAULT);

	if (u_array_size <= 0)
		return (-EINVAL);

	idefloppy_create_read_capacity_cmd(&pc);
	if (idefloppy_queue_pc_tail(drive, &pc)) {
		printk (KERN_ERR "ide-floppy: Can't get floppy parameters\n");
                return (-EIO);
        }
        header = (idefloppy_capacity_header_t *) pc.buffer;
        descriptors = header->length /
		sizeof (idefloppy_capacity_descriptor_t);
	descriptor = (idefloppy_capacity_descriptor_t *) (header + 1);

	u_index=0;
	argp=arg+1;

	/*
	** We always skip the first capacity descriptor.  That's the
	** current capacity.  We are interested in the remaining descriptors,
	** the formattable capacities.
	*/

	for (i=0; i<descriptors; i++, descriptor++)
	{
		if (u_index >= u_array_size)
			break;	/* User-supplied buffer too small */
		if (i == 0)
			continue;	/* Skip the first descriptor */

		blocks = ntohl (descriptor->blocks);
		length = ntohs (descriptor->length);

		if (put_user(blocks, argp))
			return (-EFAULT);
		++argp;

		if (put_user(length, argp))
			return (-EFAULT);
		++argp;

		++u_index;
	}

	if (put_user(u_index, arg))
		return (-EFAULT);
	return (0);
}

/*
** Send ATAPI_FORMAT_UNIT to the drive.
**
** Userland gives us the following structure:
**
** struct idefloppy_format_command {
**        int nblocks;
**        int blocksize;
**        int flags;
**        } ;
**
** flags is a bitmask, currently, the only defined flag is:
**
**        0x01 - verify media after format.
*/

static int idefloppy_begin_format(struct ata_device *drive,
				  struct inode *inode,
				  struct file *file,
				  int *arg)
{
	int blocks;
	int length;
	int flags;
	struct atapi_packet_command pc;

	if (get_user(blocks, arg)
	    || get_user(length, arg+1)
	    || get_user(flags, arg+2))
	{
		return (-EFAULT);
	}

	(void) idefloppy_get_capability_page (drive);	/* Get the SFRP bit */
	idefloppy_create_format_unit_cmd(&pc, blocks, length, flags);
	if (idefloppy_queue_pc_tail(drive, &pc))
                return -EIO;

	return (0);
}

/*
** Get ATAPI_FORMAT_UNIT progress indication.
**
** Userland gives a pointer to an int.  The int is set to a progresss
** indicator 0-65536, with 65536=100%.
**
** If the drive does not support format progress indication, we just check
** the dsc bit, and return either 0 or 65536.
*/

static int idefloppy_get_format_progress(struct ata_device *drive,
		struct inode *inode,
		struct file *file,
		int *arg)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct atapi_packet_command pc;
	int progress_indication=0x10000;

	if (floppy->srfp)
	{
		idefloppy_create_request_sense_cmd(&pc);
		if (idefloppy_queue_pc_tail (drive, &pc))
		{
			return (-EIO);
		}

		if (floppy->sense_key == 2 && floppy->asc == 4 &&
		    floppy->ascq == 4)
		{
			progress_indication=floppy->progress_indication;
		}
		/* Else assume format_unit has finished, and we're
		 * at 0x10000
		 */
	}
	else
	{
		atapi_status_reg_t status;
		unsigned long flags;

		local_irq_save(flags);
		ata_status(drive, 0, 0);
		status.all = drive->status;
		local_irq_restore(flags);

		progress_indication= !status.b.dsc ? 0:0x10000;
	}
	if (put_user(progress_indication, arg))
		return (-EFAULT);

	return (0);
}

/*
 *	Our special ide-floppy ioctl's.
 *
 *	Currently there aren't any ioctl's.
 */
static int idefloppy_ioctl(struct ata_device *drive, struct inode *inode, struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	struct atapi_packet_command pc;
	idefloppy_floppy_t *floppy = drive->driver_data;
	int prevent = (arg) ? 1 : 0;

	switch (cmd) {
	case CDROMEJECT:
		prevent = 0;
		/* fall through */
	case CDROM_LOCKDOOR:
		if (drive->usage > 1)
			return -EBUSY;

		/* The IOMEGA Clik! Drive doesn't support this command - no room for an eject mechanism */
                if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags)) {
			idefloppy_create_prevent_cmd(&pc, prevent);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}
		if (cmd == CDROMEJECT) {
			idefloppy_create_start_stop_cmd (&pc, 2);
			(void) idefloppy_queue_pc_tail (drive, &pc);
		}
		return 0;
	case IDEFLOPPY_IOCTL_FORMAT_SUPPORTED:
		return (0);
	case IDEFLOPPY_IOCTL_FORMAT_GET_CAPACITY:
		return (idefloppy_get_format_capacities(drive, inode, file,
							(int *)arg));
	case IDEFLOPPY_IOCTL_FORMAT_START:

		if (!(file->f_mode & 2))
			return (-EPERM);

		{
			idefloppy_floppy_t *floppy = drive->driver_data;

			if (drive->usage > 1)
			{
				/* Don't format if someone is using the disk */

				clear_bit(IDEFLOPPY_FORMAT_IN_PROGRESS,
					  &floppy->flags);
				return -EBUSY;
			}
			else
			{
				int rc;

				set_bit(IDEFLOPPY_FORMAT_IN_PROGRESS,
					&floppy->flags);

				rc=idefloppy_begin_format(drive, inode,
							      file,
							      (int *)arg);

				if (rc)
					clear_bit(IDEFLOPPY_FORMAT_IN_PROGRESS,
						  &floppy->flags);
				return (rc);

			/*
			** Note, the bit will be cleared when the device is
			** closed.  This is the cleanest way to handle the
			** situation where the drive does not support
			** format progress reporting.
			*/
			}
		}
	case IDEFLOPPY_IOCTL_FORMAT_GET_PROGRESS:
		return (idefloppy_get_format_progress(drive, inode, file,
						      (int *)arg));
	}
 	return -EIO;
}

/*
 *	Our open/release functions
 */
static int idefloppy_open(struct inode *inode, struct file *filp, struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct atapi_packet_command pc;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "Reached idefloppy_open\n");
#endif

	MOD_INC_USE_COUNT;
	if (drive->usage == 1) {
		clear_bit(IDEFLOPPY_FORMAT_IN_PROGRESS, &floppy->flags);
		/* Just in case */

		idefloppy_create_test_unit_ready_cmd(&pc);
		if (idefloppy_queue_pc_tail(drive, &pc)) {
			idefloppy_create_start_stop_cmd (&pc, 1);
			(void) idefloppy_queue_pc_tail (drive, &pc);
		}

		if (idefloppy_get_capacity (drive)
		   && (filp->f_flags & O_NDELAY) == 0
		    /*
		    ** Allow O_NDELAY to open a drive without a disk, or with
		    ** an unreadable disk, so that we can get the format
		    ** capacity of the drive or begin the format - Sam
		    */
		    ) {
			drive->usage--;
			MOD_DEC_USE_COUNT;
			return -EIO;
		}

		if (floppy->wp && (filp->f_mode & 2)) {
			drive->usage--;
			MOD_DEC_USE_COUNT;
			return -EROFS;
		}
		set_bit (IDEFLOPPY_MEDIA_CHANGED, &floppy->flags);
		/* IOMEGA Clik! drives do not support lock/unlock commands */
                if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags)) {
			idefloppy_create_prevent_cmd (&pc, 1);
			(void) idefloppy_queue_pc_tail (drive, &pc);
		}
		check_disk_change(inode->i_rdev);
	}
	else if (test_bit(IDEFLOPPY_FORMAT_IN_PROGRESS, &floppy->flags))
	{
		drive->usage--;
		MOD_DEC_USE_COUNT;
		return -EBUSY;
	}
	return 0;
}

static void idefloppy_release(struct inode *inode, struct file *filp, struct ata_device *drive)
{
	struct atapi_packet_command pc;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "Reached idefloppy_release\n");
#endif

	if (!drive->usage) {
		idefloppy_floppy_t *floppy = drive->driver_data;

		/* IOMEGA Clik! drives do not support lock/unlock commands */
                if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags)) {
			idefloppy_create_prevent_cmd (&pc, 0);
			(void) idefloppy_queue_pc_tail (drive, &pc);
		}

		clear_bit(IDEFLOPPY_FORMAT_IN_PROGRESS, &floppy->flags);
	}
	MOD_DEC_USE_COUNT;
}

/*
 *	Check media change. Use a simple algorithm for now.
 */
static int idefloppy_check_media_change(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	return test_and_clear_bit(IDEFLOPPY_MEDIA_CHANGED, &floppy->flags);
}

/*
 *	Return the current floppy capacity to ide.c.
 */
static unsigned long idefloppy_capacity(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	unsigned long capacity = floppy->blocks * floppy->bs_factor;

	return capacity;
}

/*
 *	idefloppy_identify_device checks if we can support a drive,
 *	based on the ATAPI IDENTIFY command results.
 */
static int idefloppy_identify_device(struct ata_device *drive,struct hd_driveid *id)
{
	struct atapi_id_gcw gcw;
#if IDEFLOPPY_DEBUG_INFO
	unsigned short mask,i;
	char buffer[80];
#endif /* IDEFLOPPY_DEBUG_INFO */

	*((unsigned short *) &gcw) = id->config;

#ifdef CONFIG_PPC
	/* kludge for Apple PowerBook internal zip */
	if ((gcw.device_type == 5) && !strstr(id->model, "CD-ROM")
	    && strstr(id->model, "ZIP"))
		gcw.device_type = 0;			
#endif

#if IDEFLOPPY_DEBUG_INFO
	printk (KERN_INFO "Dumping ATAPI Identify Device floppy parameters\n");
	switch (gcw.protocol) {
		case 0: case 1: sprintf (buffer, "ATA");break;
		case 2:	sprintf (buffer, "ATAPI");break;
		case 3: sprintf (buffer, "Reserved (Unknown to ide-floppy)");break;
	}
	printk (KERN_INFO "Protocol Type: %s\n", buffer);
	switch (gcw.device_type) {
		case 0: sprintf (buffer, "Direct-access Device");break;
		case 1: sprintf (buffer, "Streaming Tape Device");break;
		case 2: case 3: case 4: sprintf (buffer, "Reserved");break;
		case 5: sprintf (buffer, "CD-ROM Device");break;
		case 6: sprintf (buffer, "Reserved");
		case 7: sprintf (buffer, "Optical memory Device");break;
		case 0x1f: sprintf (buffer, "Unknown or no Device type");break;
		default: sprintf (buffer, "Reserved");
	}
	printk (KERN_INFO "Device Type: %x - %s\n", gcw.device_type, buffer);
	printk (KERN_INFO "Removable: %s\n",gcw.removable ? "Yes":"No");	
	switch (gcw.drq_type) {
		case 0: sprintf (buffer, "Microprocessor DRQ");break;
		case 1: sprintf (buffer, "Interrupt DRQ");break;
		case 2: sprintf (buffer, "Accelerated DRQ");break;
		case 3: sprintf (buffer, "Reserved");break;
	}
	printk (KERN_INFO "Command Packet DRQ Type: %s\n", buffer);
	switch (gcw.packet_size) {
		case 0: sprintf (buffer, "12 bytes");break;
		case 1: sprintf (buffer, "16 bytes");break;
		default: sprintf (buffer, "Reserved");break;
	}
	printk (KERN_INFO "Command Packet Size: %s\n", buffer);
	printk (KERN_INFO "Model: %.40s\n",id->model);
	printk (KERN_INFO "Firmware Revision: %.8s\n",id->fw_rev);
	printk (KERN_INFO "Serial Number: %.20s\n",id->serial_no);
	printk (KERN_INFO "Write buffer size(?): %d bytes\n",id->buf_size*512);
	printk (KERN_INFO "DMA: %s",id->capability & 0x01 ? "Yes\n":"No\n");
	printk (KERN_INFO "LBA: %s",id->capability & 0x02 ? "Yes\n":"No\n");
	printk (KERN_INFO "IORDY can be disabled: %s",id->capability & 0x04 ? "Yes\n":"No\n");
	printk (KERN_INFO "IORDY supported: %s",id->capability & 0x08 ? "Yes\n":"Unknown\n");
	printk (KERN_INFO "ATAPI overlap supported: %s",id->capability & 0x20 ? "Yes\n":"No\n");
	printk (KERN_INFO "PIO Cycle Timing Category: %d\n",id->tPIO);
	printk (KERN_INFO "DMA Cycle Timing Category: %d\n",id->tDMA);
	printk (KERN_INFO "Single Word DMA supported modes:\n");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_1word & mask)
			printk (KERN_INFO "   Mode %d%s\n", i, (id->dma_1word & (mask << 8)) ? " (active)" : "");
	}
	printk (KERN_INFO "Multi Word DMA supported modes:\n");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_mword & mask)
			printk (KERN_INFO "   Mode %d%s\n", i, (id->dma_mword & (mask << 8)) ? " (active)" : "");
	}
	if (id->field_valid & 0x0002) {
		printk (KERN_INFO "Enhanced PIO Modes: %s\n",id->eide_pio_modes & 1 ? "Mode 3":"None");
		if (id->eide_dma_min == 0)
			sprintf (buffer, "Not supported");
		else
			sprintf (buffer, "%d ns",id->eide_dma_min);
		printk (KERN_INFO "Minimum Multi-word DMA cycle per word: %s\n", buffer);
		if (id->eide_dma_time == 0)
			sprintf (buffer, "Not supported");
		else
			sprintf (buffer, "%d ns",id->eide_dma_time);
		printk (KERN_INFO "Manufacturer\'s Recommended Multi-word cycle: %s\n", buffer);
		if (id->eide_pio == 0)
			sprintf (buffer, "Not supported");
		else
			sprintf (buffer, "%d ns",id->eide_pio);
		printk (KERN_INFO "Minimum PIO cycle without IORDY: %s\n", buffer);
		if (id->eide_pio_iordy == 0)
			sprintf (buffer, "Not supported");
		else
			sprintf (buffer, "%d ns",id->eide_pio_iordy);
		printk (KERN_INFO "Minimum PIO cycle with IORDY: %s\n", buffer);
	} else
		printk (KERN_INFO "According to the device, fields 64-70 are not valid.\n");
#endif /* IDEFLOPPY_DEBUG_INFO */

	if (gcw.protocol != 2)
		printk (KERN_ERR "ide-floppy: Protocol is not ATAPI\n");
	else if (gcw.device_type != 0)
		printk (KERN_ERR "ide-floppy: Device type is not set to floppy\n");
	else if (!gcw.removable)
		printk (KERN_ERR "ide-floppy: The removable flag is not set\n");
	else if (gcw.drq_type == 3) {
		printk (KERN_ERR "ide-floppy: Sorry, DRQ type %d not supported\n", gcw.drq_type);
	} else if (gcw.packet_size != 0) {
		printk (KERN_ERR "ide-floppy: Packet size is not 12 bytes long\n");
	} else
		return 1;
	return 0;
}

/*
 *	Driver initialization.
 */
static void idefloppy_setup(struct ata_device *drive, idefloppy_floppy_t *floppy)
{
	struct atapi_id_gcw gcw;
	int i;

	*((unsigned short *) &gcw) = drive->id->config;
	drive->driver_data = floppy;
	drive->ready_stat = 0;
	memset (floppy, 0, sizeof (idefloppy_floppy_t));
	floppy->drive = drive;
	floppy->pc = floppy->pc_stack;
	if (gcw.drq_type == 1)
		set_bit (IDEFLOPPY_DRQ_INTERRUPT, &floppy->flags);
	/*
	 *	We used to check revisions here. At this point however
	 *	I'm giving up. Just assume they are all broken, its easier.
	 *
	 *	The actual reason for the workarounds was likely
	 *	a driver bug after all rather than a firmware bug,
	 *	and the workaround below used to hide it. It should
	 *	be fixed as of version 1.9, but to be on the safe side
	 *	we'll leave the limitation below for the 2.2.x tree.
	 */

	if (strcmp(drive->id->model, "IOMEGA ZIP 100 ATAPI") == 0)
		blk_queue_max_sectors(&drive->queue, 64);

	/*
	*      Guess what?  The IOMEGA Clik! drive also needs the
	*      above fix.  It makes nasty clicking noises without
	*      it, so please don't remove this.
	*/
	if (strncmp(drive->id->model, "IOMEGA Clik!", 11) == 0) {
		blk_queue_max_sectors(&drive->queue, 64);
		set_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags);
	}

	(void) idefloppy_get_capacity (drive);

	for (i = 0; i < MAX_DRIVES; ++i) {
		struct ata_channel *hwif = drive->channel;

		if (drive != &hwif->drives[i]) continue;
		hwif->gd->de_arr[i] = drive->de;
		if (drive->removable)
			hwif->gd->flags[i] |= GENHD_FL_REMOVABLE;
		break;
	}
}

static int idefloppy_cleanup(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (ide_unregister_subdriver (drive))
		return 1;
	drive->driver_data = NULL;
	kfree (floppy);
	return 0;
}

static void idefloppy_attach(struct ata_device *drive);

/*
 *	IDE subdriver functions, registered with ide.c
 */
static struct ata_operations idefloppy_driver = {
	.owner =		THIS_MODULE,
	.attach =		idefloppy_attach,
	.cleanup =		idefloppy_cleanup,
	.standby =		NULL,
	.do_request =		idefloppy_do_request,
	.end_request =		idefloppy_end_request,
	.ioctl =		idefloppy_ioctl,
	.open =			idefloppy_open,
	.release =		idefloppy_release,
	.check_media_change =	idefloppy_check_media_change,
	.revalidate =		NULL, /* use default method */
	.capacity =		idefloppy_capacity,
};

static void idefloppy_attach(struct ata_device *drive)
{
	idefloppy_floppy_t *floppy;
	char *req;
	struct ata_channel *channel;
	int unit;

	if (drive->type != ATA_FLOPPY)
		return;

	req = drive->driver_req;
	if (req[0] != '\0' && strcmp(req, "ide-floppy"))
		return;

	if (!idefloppy_identify_device (drive, drive->id)) {
		printk (KERN_ERR "ide-floppy: %s: not supported by this version of driver\n",
				drive->name);
		return;
	}

	if (drive->scsi) {
		printk(KERN_INFO "ide-floppy: passing drive %s to ide-scsi emulation.\n",
				drive->name);
		return;
	}
	if (!(floppy = (idefloppy_floppy_t *) kmalloc (sizeof (idefloppy_floppy_t), GFP_KERNEL))) {
		printk(KERN_ERR "ide-floppy: %s: Can't allocate a floppy structure\n",
				drive->name);
		return;
	}
	if (ide_register_subdriver(drive, &idefloppy_driver)) {
		printk(KERN_ERR "ide-floppy: %s: Failed to register the driver with ide.c\n", drive->name);
		kfree (floppy);
		return;
	}

	idefloppy_setup(drive, floppy);

	channel = drive->channel;
	unit = drive - channel->drives;

	ata_revalidate(mk_kdev(channel->major, unit << PARTN_BITS));
}

MODULE_DESCRIPTION("ATAPI FLOPPY Driver");

static void __exit idefloppy_exit(void)
{
	unregister_ata_driver(&idefloppy_driver);
}

int __init idefloppy_init(void)
{
	return ata_driver_module(&idefloppy_driver);
}

module_init(idefloppy_init);
module_exit(idefloppy_exit);
MODULE_LICENSE("GPL");
