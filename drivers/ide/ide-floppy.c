/*
 * linux/drivers/ide/ide-floppy.c	Version 0.9	Jul   4, 1999
 *
 * Copyright (C) 1996 - 1999 Gadi Oxman <gadio@netvision.net.il>
 */

/*
 * IDE ATAPI floppy driver.
 *
 * The driver currently doesn't have any fancy features, just the bare
 * minimum read/write support.
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
 */

#define IDEFLOPPY_VERSION "0.9"

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
#include <linux/malloc.h>
#include <linux/cdrom.h>
#include <linux/ide.h>

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
 *	With each packet command, we allocate a buffer of
 *	IDEFLOPPY_PC_BUFFER_SIZE bytes.
 */
#define IDEFLOPPY_PC_BUFFER_SIZE	256

/*
 *	In various places in the driver, we need to allocate storage
 *	for packet commands and requests, which will remain valid while
 *	we leave the driver to wait for an interrupt or a timeout event.
 */
#define IDEFLOPPY_PC_STACK		(10 + IDEFLOPPY_MAX_PC_RETRIES)

/*
 *	Our view of a packet command.
 */
typedef struct idefloppy_packet_command_s {
	u8 c[12];				/* Actual packet bytes */
	int retries;				/* On each retry, we increment retries */
	int error;				/* Error code */
	int request_transfer;			/* Bytes to transfer */
	int actually_transferred;		/* Bytes actually transferred */
	int buffer_size;			/* Size of our data buffer */
	char *b_data;				/* Pointer which runs on the buffers */
	int b_count;				/* Missing/Available data on the current buffer */
	struct request *rq;			/* The corresponding request */
	byte *buffer;				/* Data buffer */
	byte *current_position;			/* Pointer into the above buffer */
	void (*callback) (ide_drive_t *);	/* Called when this packet command is completed */
	byte pc_buffer[IDEFLOPPY_PC_BUFFER_SIZE];	/* Temporary buffer */
	unsigned long flags;			/* Status/Action bit flags: long for set_bit */
} idefloppy_pc_t;

/*
 *	Packet command flag bits.
 */
#define	PC_ABORT			0	/* Set when an error is considered normal - We won't retry */
#define PC_DMA_RECOMMENDED		2	/* 1 when we prefer to use DMA if possible */
#define	PC_DMA_IN_PROGRESS		3	/* 1 while DMA in progress */
#define	PC_DMA_ERROR			4	/* 1 when encountered problem during DMA */
#define	PC_WRITING			5	/* Data direction */

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
	ide_drive_t *drive;

	idefloppy_pc_t *pc;			/* Current packet command */
	idefloppy_pc_t *failed_pc; 		/* Last failed packet command */
	idefloppy_pc_t pc_stack[IDEFLOPPY_PC_STACK];/* Packet command stack */
	int pc_stack_index;			/* Next free packet command storage space */
	struct request rq_stack[IDEFLOPPY_PC_STACK];
	int rq_stack_index;			/* We implement a circular array */

	/*
	 *	Last error information
	 */
	byte sense_key, asc, ascq;

	/*
	 *	Device information
	 */
	int blocks, block_size, bs_factor;			/* Current format */
	idefloppy_capacity_descriptor_t capacity;		/* Last format capacity */
	idefloppy_flexible_disk_page_t flexible_disk_page;	/* Copy of the flexible disk page */
	int wp;							/* Write protect */

	unsigned int flags;			/* Status/Action flags */
} idefloppy_floppy_t;

/*
 *	Floppy flag bits values.
 */
#define IDEFLOPPY_DRQ_INTERRUPT		0	/* DRQ interrupt device */
#define IDEFLOPPY_MEDIA_CHANGED		1	/* Media may have changed */
#define IDEFLOPPY_USE_READ12		2	/* Use READ12/WRITE12 or READ10/WRITE10 */

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
 *	Special requests for our block device strategy routine.
 */
#define	IDEFLOPPY_FIRST_RQ		90

/*
 * 	IDEFLOPPY_PC_RQ is used to queue a packet command in the request queue.
 */
#define	IDEFLOPPY_PC_RQ			90

#define IDEFLOPPY_LAST_RQ		90

/*
 *	A macro which can be used to check if a given request command
 *	originated in the driver or in the buffer cache layer.
 */
#define IDEFLOPPY_RQ_CMD(cmd) 		((cmd >= IDEFLOPPY_FIRST_RQ) && (cmd <= IDEFLOPPY_LAST_RQ))

/*
 *	Error codes which are returned in rq->errors to the higher part
 *	of the driver.
 */
#define	IDEFLOPPY_ERROR_GENERAL		101

/*
 *	The ATAPI Status Register.
 */
typedef union {
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned check		:1;	/* Error occurred */
		unsigned idx		:1;	/* Reserved */
		unsigned corr		:1;	/* Correctable error occurred */
		unsigned drq		:1;	/* Data is request by the device */
		unsigned dsc		:1;	/* Media access command finished */
		unsigned reserved5	:1;	/* Reserved */
		unsigned drdy		:1;	/* Ignored for ATAPI commands (ready to accept ATA command) */
		unsigned bsy		:1;	/* The device has access to the command block */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned bsy		:1;	/* The device has access to the command block */
		unsigned drdy		:1;	/* Ignored for ATAPI commands (ready to accept ATA command) */
		unsigned reserved5	:1;	/* Reserved */
		unsigned dsc		:1;	/* Media access command finished */
		unsigned drq		:1;	/* Data is request by the device */
		unsigned corr		:1;	/* Correctable error occurred */
		unsigned idx		:1;	/* Reserved */
		unsigned check		:1;	/* Error occurred */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	} b;
} idefloppy_status_reg_t;

/*
 *	The ATAPI error register.
 */
typedef union {
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned ili		:1;	/* Illegal Length Indication */
		unsigned eom		:1;	/* End Of Media Detected */
		unsigned abrt		:1;	/* Aborted command - As defined by ATA */
		unsigned mcr		:1;	/* Media Change Requested - As defined by ATA */
		unsigned sense_key	:4;	/* Sense key of the last failed packet command */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned sense_key	:4;	/* Sense key of the last failed packet command */
		unsigned mcr		:1;	/* Media Change Requested - As defined by ATA */
		unsigned abrt		:1;	/* Aborted command - As defined by ATA */
		unsigned eom		:1;	/* End Of Media Detected */
		unsigned ili		:1;	/* Illegal Length Indication */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	} b;
} idefloppy_error_reg_t;

/*
 *	ATAPI Feature Register
 */
typedef union {
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned dma		:1;	/* Using DMA or PIO */
		unsigned reserved321	:3;	/* Reserved */
		unsigned reserved654	:3;	/* Reserved (Tag Type) */
		unsigned reserved7	:1;	/* Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned reserved7	:1;	/* Reserved */
		unsigned reserved654	:3;	/* Reserved (Tag Type) */
		unsigned reserved321	:3;	/* Reserved */
		unsigned dma		:1;	/* Using DMA or PIO */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	} b;
} idefloppy_feature_reg_t;

/*
 *	ATAPI Byte Count Register.
 */
typedef union {
	unsigned all			:16;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned low		:8;	/* LSB */
		unsigned high		:8;	/* MSB */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned high		:8;	/* MSB */
		unsigned low		:8;	/* LSB */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	} b;
} idefloppy_bcount_reg_t;

/*
 *	ATAPI Interrupt Reason Register.
 */
typedef union {
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned cod		:1;	/* Information transferred is command (1) or data (0) */
		unsigned io		:1;	/* The device requests us to read (1) or write (0) */
		unsigned reserved	:6;	/* Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned reserved	:6;	/* Reserved */
		unsigned io		:1;	/* The device requests us to read (1) or write (0) */
		unsigned cod		:1;	/* Information transferred is command (1) or data (0) */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	} b;
} idefloppy_ireason_reg_t;

/*
 *	ATAPI floppy Drive Select Register
 */
typedef union {	
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned sam_lun	:3;	/* Logical unit number */
		unsigned reserved3	:1;	/* Reserved */
		unsigned drv		:1;	/* The responding drive will be drive 0 (0) or drive 1 (1) */
		unsigned one5		:1;	/* Should be set to 1 */
		unsigned reserved6	:1;	/* Reserved */
		unsigned one7		:1;	/* Should be set to 1 */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned one7		:1;	/* Should be set to 1 */
		unsigned reserved6	:1;	/* Reserved */
		unsigned one5		:1;	/* Should be set to 1 */
		unsigned drv		:1;	/* The responding drive will be drive 0 (0) or drive 1 (1) */
		unsigned reserved3	:1;	/* Reserved */
		unsigned sam_lun	:3;	/* Logical unit number */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	} b;
} idefloppy_drivesel_reg_t;

/*
 *	ATAPI Device Control Register
 */
typedef union {			
	unsigned all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		unsigned zero0		:1;	/* Should be set to zero */
		unsigned nien		:1;	/* Device interrupt is disabled (1) or enabled (0) */
		unsigned srst		:1;	/* ATA software reset. ATAPI devices should use the new ATAPI srst. */
		unsigned one3		:1;	/* Should be set to 1 */
		unsigned reserved4567	:4;	/* Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
		unsigned reserved4567	:4;	/* Reserved */
		unsigned one3		:1;	/* Should be set to 1 */
		unsigned srst		:1;	/* ATA software reset. ATAPI devices should use the new ATAPI srst. */
		unsigned nien		:1;	/* Device interrupt is disabled (1) or enabled (0) */
		unsigned zero0		:1;	/* Should be set to zero */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	} b;
} idefloppy_control_reg_t;

/*
 *	The following is used to format the general configuration word of
 *	the ATAPI IDENTIFY DEVICE command.
 */
struct idefloppy_id_gcw {	
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned packet_size		:2;	/* Packet Size */
	unsigned reserved234		:3;	/* Reserved */
	unsigned drq_type		:2;	/* Command packet DRQ type */
	unsigned removable		:1;	/* Removable media */
	unsigned device_type		:5;	/* Device type */
	unsigned reserved13		:1;	/* Reserved */
	unsigned protocol		:2;	/* Protocol type */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned protocol		:2;	/* Protocol type */
	unsigned reserved13		:1;	/* Reserved */
	unsigned device_type		:5;	/* Device type */
	unsigned removable		:1;	/* Removable media */
	unsigned drq_type		:2;	/* Command packet DRQ type */
	unsigned reserved234		:3;	/* Reserved */
	unsigned packet_size		:2;	/* Packet Size */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
};

/*
 *	INQUIRY packet command - Data Format
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	device_type	:5;	/* Peripheral Device Type */
	unsigned	reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned	reserved1_6t0	:7;	/* Reserved */
	unsigned	rmb		:1;	/* Removable Medium Bit */
	unsigned	ansi_version	:3;	/* ANSI Version */
	unsigned	ecma_version	:3;	/* ECMA Version */
	unsigned	iso_version	:2;	/* ISO Version */
	unsigned	response_format :4;	/* Response Data Format */
	unsigned	reserved3_45	:2;	/* Reserved */
	unsigned	reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned	reserved3_7	:1;	/* AENC - Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned	device_type	:5;	/* Peripheral Device Type */
	unsigned	rmb		:1;	/* Removable Medium Bit */
	unsigned	reserved1_6t0	:7;	/* Reserved */
	unsigned	iso_version	:2;	/* ISO Version */
	unsigned	ecma_version	:3;	/* ECMA Version */
	unsigned	ansi_version	:3;	/* ANSI Version */
	unsigned	reserved3_7	:1;	/* AENC - Reserved */
	unsigned	reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned	reserved3_45	:2;	/* Reserved */
	unsigned	response_format :4;	/* Response Data Format */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		additional_length;	/* Additional Length (total_length-4) */
	u8		rsv5, rsv6, rsv7;	/* Reserved */
	u8		vendor_id[8];		/* Vendor Identification */
	u8		product_id[16];		/* Product Identification */
	u8		revision_level[4];	/* Revision Level */
	u8		vendor_specific[20];	/* Vendor Specific - Optional */
	u8		reserved56t95[40];	/* Reserved - Optional */
						/* Additional information may be returned */
} idefloppy_inquiry_result_t;

/*
 *	REQUEST SENSE packet command result - Data Format.
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	error_code	:7;	/* Current error (0x70) */
	unsigned	valid		:1;	/* The information field conforms to SFF-8070i */
	u8		reserved1	:8;	/* Reserved */
	unsigned	sense_key	:4;	/* Sense Key */
	unsigned	reserved2_4	:1;	/* Reserved */
	unsigned	ili		:1;	/* Incorrect Length Indicator */
	unsigned	reserved2_67	:2;
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	valid		:1;	/* The information field conforms to SFF-8070i */
	unsigned	error_code	:7;	/* Current error (0x70) */
	u8		reserved1	:8;	/* Reserved */
	unsigned	reserved2_67	:2;
	unsigned	ili		:1;	/* Incorrect Length Indicator */
	unsigned	reserved2_4	:1;	/* Reserved */
	unsigned	sense_key	:4;	/* Sense Key */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u32		information __attribute__ ((packed));
	u8		asl;			/* Additional sense length (n-7) */
	u32		command_specific;	/* Additional command specific information */
	u8		asc;			/* Additional Sense Code */
	u8		ascq;			/* Additional Sense Code Qualifier */
	u8		replaceable_unit_code;	/* Field Replaceable Unit Code */
	u8		reserved[3];
	u8		pad[2];			/* Padding to 20 bytes */
} idefloppy_request_sense_result_t;

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

#define IDEFLOPPY_MIN(a,b)	((a)<(b) ? (a):(b))
#define	IDEFLOPPY_MAX(a,b)	((a)>(b) ? (a):(b))

/*
 *	Too bad. The drive wants to send us data which we are not ready to accept.
 *	Just throw it away.
 */
static void idefloppy_discard_data (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		IN_BYTE (IDE_DATA_REG);
}

#if IDEFLOPPY_DEBUG_BUGS
static void idefloppy_write_zeros (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		OUT_BYTE (0, IDE_DATA_REG);
}
#endif /* IDEFLOPPY_DEBUG_BUGS */

/*
 *	idefloppy_end_request is used to finish servicing a request.
 *
 *	For read/write requests, we will call ide_end_request to pass to the
 *	next buffer.
 */
static void idefloppy_end_request (byte uptodate, ide_hwgroup_t *hwgroup)
{
	ide_drive_t *drive = hwgroup->drive;
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct request *rq = hwgroup->rq;
	int error;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "Reached idefloppy_end_request\n");
#endif /* IDEFLOPPY_DEBUG_LOG */

	switch (uptodate) {
		case 0: error = IDEFLOPPY_ERROR_GENERAL; break;
		case 1: error = 0; break;
		default: error = uptodate;
	}
	if (error)
		floppy->failed_pc = NULL;
	/* Why does this happen? */
	if (!rq)
		return;
	if (!IDEFLOPPY_RQ_CMD (rq->cmd)) {
		ide_end_request (uptodate, hwgroup);
		return;
	}
	rq->errors = error;
	ide_end_drive_cmd (drive, 0, 0);
}

static void idefloppy_input_buffers (ide_drive_t *drive, idefloppy_pc_t *pc, unsigned int bcount)
{
	struct request *rq = pc->rq;
	struct buffer_head *bh = rq->bh;
	int count;

	while (bcount) {
		if (pc->b_count == bh->b_size) {
			rq->sector += rq->current_nr_sectors;
			rq->nr_sectors -= rq->current_nr_sectors;
			idefloppy_end_request (1, HWGROUP(drive));
			if ((bh = rq->bh) != NULL)
				pc->b_count = 0;
		}
		if (bh == NULL) {
			printk (KERN_ERR "%s: bh == NULL in idefloppy_input_buffers, bcount == %d\n", drive->name, bcount);
			idefloppy_discard_data (drive, bcount);
			return;
		}
		count = IDEFLOPPY_MIN (bh->b_size - pc->b_count, bcount);
		atapi_input_bytes (drive, bh->b_data + pc->b_count, count);
		bcount -= count; pc->b_count += count;
	}
}

static void idefloppy_output_buffers (ide_drive_t *drive, idefloppy_pc_t *pc, unsigned int bcount)
{
	struct request *rq = pc->rq;
	struct buffer_head *bh = rq->bh;
	int count;
	
	while (bcount) {
		if (!pc->b_count) {
			rq->sector += rq->current_nr_sectors;
			rq->nr_sectors -= rq->current_nr_sectors;
			idefloppy_end_request (1, HWGROUP(drive));
			if ((bh = rq->bh) != NULL) {
				pc->b_data = bh->b_data;
				pc->b_count = bh->b_size;
			}
		}
		if (bh == NULL) {
			printk (KERN_ERR "%s: bh == NULL in idefloppy_output_buffers, bcount == %d\n", drive->name, bcount);
			idefloppy_write_zeros (drive, bcount);
			return;
		}
		count = IDEFLOPPY_MIN (pc->b_count, bcount);
		atapi_output_bytes (drive, pc->b_data, count);
		bcount -= count; pc->b_data += count; pc->b_count -= count;
	}
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static void idefloppy_update_buffers (ide_drive_t *drive, idefloppy_pc_t *pc)
{
	struct request *rq = pc->rq;
	struct buffer_head *bh = rq->bh;

	while ((bh = rq->bh) != NULL)
		idefloppy_end_request (1, HWGROUP(drive));
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

/*
 *	idefloppy_queue_pc_head generates a new packet command request in front
 *	of the request queue, before the current request, so that it will be
 *	processed immediately, on the next pass through the driver.
 */
static void idefloppy_queue_pc_head (ide_drive_t *drive,idefloppy_pc_t *pc,struct request *rq)
{
	ide_init_drive_cmd (rq);
	rq->buffer = (char *) pc;
	rq->cmd = IDEFLOPPY_PC_RQ;
	(void) ide_do_drive_cmd (drive, rq, ide_preempt);
}

static idefloppy_pc_t *idefloppy_next_pc_storage (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (floppy->pc_stack_index==IDEFLOPPY_PC_STACK)
		floppy->pc_stack_index=0;
	return (&floppy->pc_stack[floppy->pc_stack_index++]);
}

static struct request *idefloppy_next_rq_storage (ide_drive_t *drive)
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
static void idefloppy_analyze_error (ide_drive_t *drive,idefloppy_request_sense_result_t *result)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	floppy->sense_key = result->sense_key; floppy->asc = result->asc; floppy->ascq = result->ascq;
#if IDEFLOPPY_DEBUG_LOG
	if (floppy->failed_pc)
		printk (KERN_INFO "ide-floppy: pc = %x, sense key = %x, asc = %x, ascq = %x\n",floppy->failed_pc->c[0],result->sense_key,result->asc,result->ascq);
	else
		printk (KERN_INFO "ide-floppy: sense key = %x, asc = %x, ascq = %x\n",result->sense_key,result->asc,result->ascq);
#endif /* IDEFLOPPY_DEBUG_LOG */
}

static void idefloppy_request_sense_callback (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: Reached idefloppy_request_sense_callback\n");
#endif /* IDEFLOPPY_DEBUG_LOG */
	if (!floppy->pc->error) {
		idefloppy_analyze_error (drive,(idefloppy_request_sense_result_t *) floppy->pc->buffer);
		idefloppy_end_request (1,HWGROUP (drive));
	} else {
		printk (KERN_ERR "Error in REQUEST SENSE itself - Aborting request!\n");
		idefloppy_end_request (0,HWGROUP (drive));
	}
}

/*
 *	General packet command callback function.
 */
static void idefloppy_pc_callback (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	
#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: Reached idefloppy_pc_callback\n");
#endif /* IDEFLOPPY_DEBUG_LOG */

	idefloppy_end_request (floppy->pc->error ? 0:1, HWGROUP(drive));
}

/*
 *	idefloppy_init_pc initializes a packet command.
 */
static void idefloppy_init_pc (idefloppy_pc_t *pc)
{
	memset (pc->c, 0, 12);
	pc->retries = 0;
	pc->flags = 0;
	pc->request_transfer = 0;
	pc->buffer = pc->pc_buffer;
	pc->buffer_size = IDEFLOPPY_PC_BUFFER_SIZE;
	pc->b_data = NULL;
	pc->callback = &idefloppy_pc_callback;
}

static void idefloppy_create_request_sense_cmd (idefloppy_pc_t *pc)
{
	idefloppy_init_pc (pc);	
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
static void idefloppy_retry_pc (ide_drive_t *drive)
{
	idefloppy_pc_t *pc;
	struct request *rq;
	idefloppy_error_reg_t error;

	error.all = IN_BYTE (IDE_ERROR_REG);
	pc = idefloppy_next_pc_storage (drive);
	rq = idefloppy_next_rq_storage (drive);
	idefloppy_create_request_sense_cmd (pc);
	idefloppy_queue_pc_head (drive, pc, rq);
}

/*
 *	idefloppy_pc_intr is the usual interrupt handler which will be called
 *	during a packet command.
 */
static ide_startstop_t idefloppy_pc_intr (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_status_reg_t status;
	idefloppy_bcount_reg_t bcount;
	idefloppy_ireason_reg_t ireason;
	idefloppy_pc_t *pc=floppy->pc;
	struct request *rq = pc->rq;
	unsigned int temp;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: Reached idefloppy_pc_intr interrupt handler\n");
#endif /* IDEFLOPPY_DEBUG_LOG */	

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_bit (PC_DMA_IN_PROGRESS, &pc->flags)) {
		if (HWIF(drive)->dmaproc(ide_dma_end, drive)) {
			set_bit (PC_DMA_ERROR, &pc->flags);
		} else {
			pc->actually_transferred=pc->request_transfer;
			idefloppy_update_buffers (drive, pc);
		}
#if IDEFLOPPY_DEBUG_LOG
		printk (KERN_INFO "ide-floppy: DMA finished\n");
#endif /* IDEFLOPPY_DEBUG_LOG */
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */

	status.all = GET_STAT();					/* Clear the interrupt */

	if (!status.b.drq) {						/* No more interrupts */
#if IDEFLOPPY_DEBUG_LOG
		printk (KERN_INFO "Packet command completed, %d bytes transferred\n", pc->actually_transferred);
#endif /* IDEFLOPPY_DEBUG_LOG */
		clear_bit (PC_DMA_IN_PROGRESS, &pc->flags);

		ide__sti();	/* local CPU only */

		if (status.b.check || test_bit (PC_DMA_ERROR, &pc->flags)) {	/* Error detected */
#if IDEFLOPPY_DEBUG_LOG
			printk (KERN_INFO "ide-floppy: %s: I/O error\n",drive->name);
#endif /* IDEFLOPPY_DEBUG_LOG */
			rq->errors++;
			if (pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD) {
				printk (KERN_ERR "ide-floppy: I/O error in request sense command\n");
				return ide_do_reset (drive);
			}
			idefloppy_retry_pc (drive);				/* Retry operation */
			return ide_stopped; /* queued, but not started */
		}
		pc->error = 0;
		if (floppy->failed_pc == pc)
			floppy->failed_pc=NULL;
		pc->callback(drive);			/* Command finished - Call the callback function */
		return ide_stopped;
	}
#ifdef CONFIG_BLK_DEV_IDEDMA
	if (test_and_clear_bit (PC_DMA_IN_PROGRESS, &pc->flags)) {
		printk (KERN_ERR "ide-floppy: The floppy wants to issue more interrupts in DMA mode\n");
		(void) HWIF(drive)->dmaproc(ide_dma_off, drive);
		return ide_do_reset (drive);
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */
	bcount.b.high=IN_BYTE (IDE_BCOUNTH_REG);			/* Get the number of bytes to transfer */
	bcount.b.low=IN_BYTE (IDE_BCOUNTL_REG);			/* on this interrupt */
	ireason.all=IN_BYTE (IDE_IREASON_REG);

	if (ireason.b.cod) {
		printk (KERN_ERR "ide-floppy: CoD != 0 in idefloppy_pc_intr\n");
		return ide_do_reset (drive);
	}
	if (ireason.b.io == test_bit (PC_WRITING, &pc->flags)) {	/* Hopefully, we will never get here */
		printk (KERN_ERR "ide-floppy: We wanted to %s, ", ireason.b.io ? "Write":"Read");
		printk (KERN_ERR "but the floppy wants us to %s !\n",ireason.b.io ? "Read":"Write");
		return ide_do_reset (drive);
	}
	if (!test_bit (PC_WRITING, &pc->flags)) {			/* Reading - Check that we have enough space */
		temp = pc->actually_transferred + bcount.all;
		if ( temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk (KERN_ERR "ide-floppy: The floppy wants to send us more data than expected - discarding data\n");
				idefloppy_discard_data (drive,bcount.all);
				ide_set_handler (drive,&idefloppy_pc_intr,IDEFLOPPY_WAIT_CMD, NULL);
				return ide_started;
			}
#if IDEFLOPPY_DEBUG_LOG
			printk (KERN_NOTICE "ide-floppy: The floppy wants to send us more data than expected - allowing transfer\n");
#endif /* IDEFLOPPY_DEBUG_LOG */
		}
	}
	if (test_bit (PC_WRITING, &pc->flags)) {
		if (pc->buffer != NULL)
			atapi_output_bytes (drive,pc->current_position,bcount.all);	/* Write the current buffer */
		else
			idefloppy_output_buffers (drive, pc, bcount.all);
	} else {
		if (pc->buffer != NULL)
			atapi_input_bytes (drive,pc->current_position,bcount.all);	/* Read the current buffer */
		else
			idefloppy_input_buffers (drive, pc, bcount.all);
	}
	pc->actually_transferred+=bcount.all;				/* Update the current position */
	pc->current_position+=bcount.all;

	ide_set_handler (drive,&idefloppy_pc_intr,IDEFLOPPY_WAIT_CMD, NULL);		/* And set the interrupt handler again */
	return ide_started;
}

static ide_startstop_t idefloppy_transfer_pc (ide_drive_t *drive)
{
	ide_startstop_t startstop;
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_ireason_reg_t ireason;

	if (ide_wait_stat (&startstop,drive,DRQ_STAT,BUSY_STAT,WAIT_READY)) {
		printk (KERN_ERR "ide-floppy: Strange, packet command initiated yet DRQ isn't asserted\n");
		return startstop;
	}
	ireason.all=IN_BYTE (IDE_IREASON_REG);
	if (!ireason.b.cod || ireason.b.io) {
		printk (KERN_ERR "ide-floppy: (IO,CoD) != (0,1) while issuing a packet command\n");
		return ide_do_reset (drive);
	}
	ide_set_handler (drive, &idefloppy_pc_intr, IDEFLOPPY_WAIT_CMD, NULL);	/* Set the interrupt routine */
	atapi_output_bytes (drive, floppy->pc->c, 12);		/* Send the actual packet */
	return ide_started;
}

/*
 *	Issue a packet command
 */
static ide_startstop_t idefloppy_issue_pc (ide_drive_t *drive, idefloppy_pc_t *pc)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_bcount_reg_t bcount;
	int dma_ok = 0;

#if IDEFLOPPY_DEBUG_BUGS
	if (floppy->pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD && pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD) {
		printk (KERN_ERR "ide-floppy: possible ide-floppy.c bug - Two request sense in serial were issued\n");
	}
#endif /* IDEFLOPPY_DEBUG_BUGS */

	if (floppy->failed_pc == NULL && pc->c[0] != IDEFLOPPY_REQUEST_SENSE_CMD)
		floppy->failed_pc=pc;
	floppy->pc=pc;							/* Set the current packet command */

	if (pc->retries > IDEFLOPPY_MAX_PC_RETRIES || test_bit (PC_ABORT, &pc->flags)) {
		/*
		 *	We will "abort" retrying a packet command in case
		 *	a legitimate error code was received.
		 */
		if (!test_bit (PC_ABORT, &pc->flags)) {
			printk (KERN_ERR "ide-floppy: %s: I/O error, pc = %2x, key = %2x, asc = %2x, ascq = %2x\n",
				drive->name, pc->c[0], floppy->sense_key, floppy->asc, floppy->ascq);
			pc->error = IDEFLOPPY_ERROR_GENERAL;		/* Giving up */
		}
		floppy->failed_pc=NULL;
		pc->callback(drive);
		return ide_stopped;
	}
#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "Retry number - %d\n",pc->retries);
#endif /* IDEFLOPPY_DEBUG_LOG */

	pc->retries++;
	pc->actually_transferred=0;					/* We haven't transferred any data yet */
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
	OUT_BYTE (dma_ok ? 1:0,IDE_FEATURE_REG);			/* Use PIO/DMA */
	OUT_BYTE (bcount.b.high,IDE_BCOUNTH_REG);
	OUT_BYTE (bcount.b.low,IDE_BCOUNTL_REG);
	OUT_BYTE (drive->select.all,IDE_SELECT_REG);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (dma_ok) {							/* Begin DMA, if necessary */
		set_bit (PC_DMA_IN_PROGRESS, &pc->flags);
		(void) (HWIF(drive)->dmaproc(ide_dma_begin, drive));
	}
#endif /* CONFIG_BLK_DEV_IDEDMA */

	if (test_bit (IDEFLOPPY_DRQ_INTERRUPT, &floppy->flags)) {
		ide_set_handler (drive, &idefloppy_transfer_pc, IDEFLOPPY_WAIT_CMD, NULL);
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG);		/* Issue the packet command */
		return ide_started;
	} else {
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG);
		return idefloppy_transfer_pc (drive);
	}
}

static void idefloppy_rw_callback (ide_drive_t *drive)
{
#if IDEFLOPPY_DEBUG_LOG	
	printk (KERN_INFO "ide-floppy: Reached idefloppy_rw_callback\n");
#endif /* IDEFLOPPY_DEBUG_LOG */

	idefloppy_end_request(1, HWGROUP(drive));
	return;
}

static void idefloppy_create_prevent_cmd (idefloppy_pc_t *pc, int prevent)
{
#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "ide-floppy: creating prevent removal command, prevent = %d\n", prevent);
#endif /* IDEFLOPPY_DEBUG_LOG */

	idefloppy_init_pc (pc);
	pc->c[0] = IDEFLOPPY_PREVENT_REMOVAL_CMD;
	pc->c[4] = prevent;
}

static void idefloppy_create_read_capacity_cmd (idefloppy_pc_t *pc)
{
	idefloppy_init_pc (pc);
	pc->c[0] = IDEFLOPPY_READ_CAPACITY_CMD;
	pc->c[7] = 255;
	pc->c[8] = 255;
	pc->request_transfer = 255;
}

/*
 *	A mode sense command is used to "sense" floppy parameters.
 */
static void idefloppy_create_mode_sense_cmd (idefloppy_pc_t *pc, byte page_code, byte type)
{
	unsigned short length = sizeof (idefloppy_mode_parameter_header_t);
	
	idefloppy_init_pc (pc);
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
	put_unaligned (htons (length), (unsigned short *) &pc->c[7]);
	pc->request_transfer = length;
}

static void idefloppy_create_start_stop_cmd (idefloppy_pc_t *pc, int start)
{
	idefloppy_init_pc (pc);
	pc->c[0] = IDEFLOPPY_START_STOP_CMD;
	pc->c[4] = start;
}

static void idefloppy_create_test_unit_ready_cmd(idefloppy_pc_t *pc)
{
	idefloppy_init_pc(pc);
	pc->c[0] = IDEFLOPPY_TEST_UNIT_READY_CMD;
}

static void idefloppy_create_rw_cmd (idefloppy_floppy_t *floppy, idefloppy_pc_t *pc, struct request *rq, unsigned long sector)
{
	int block = sector / floppy->bs_factor;
	int blocks = rq->nr_sectors / floppy->bs_factor;
	
#if IDEFLOPPY_DEBUG_LOG
	printk ("create_rw1%d_cmd: block == %d, blocks == %d\n",
		2 * test_bit (IDEFLOPPY_USE_READ12, &floppy->flags), block, blocks);
#endif /* IDEFLOPPY_DEBUG_LOG */

	idefloppy_init_pc (pc);
	if (test_bit (IDEFLOPPY_USE_READ12, &floppy->flags)) {
		pc->c[0] = rq->cmd == READ ? IDEFLOPPY_READ12_CMD : IDEFLOPPY_WRITE12_CMD;
		put_unaligned (htonl (blocks), (unsigned int *) &pc->c[6]);
	} else {
		pc->c[0] = rq->cmd == READ ? IDEFLOPPY_READ10_CMD : IDEFLOPPY_WRITE10_CMD;
		put_unaligned (htons (blocks), (unsigned short *) &pc->c[7]);
	}
	put_unaligned (htonl (block), (unsigned int *) &pc->c[2]);
	pc->callback = &idefloppy_rw_callback;
	pc->rq = rq;
	pc->b_data = rq->buffer;
	pc->b_count = rq->cmd == READ ? 0 : rq->bh->b_size;
	if (rq->cmd == WRITE)
		set_bit (PC_WRITING, &pc->flags);
	pc->buffer = NULL;
	pc->request_transfer = pc->buffer_size = blocks * floppy->block_size;
	set_bit (PC_DMA_RECOMMENDED, &pc->flags);
}

/*
 *	idefloppy_do_request is our request handling function.	
 */
static ide_startstop_t idefloppy_do_request (ide_drive_t *drive, struct request *rq, unsigned long block)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t *pc;

#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "rq_status: %d, rq_dev: %u, cmd: %d, errors: %d\n",rq->rq_status,(unsigned int) rq->rq_dev,rq->cmd,rq->errors);
	printk (KERN_INFO "sector: %ld, nr_sectors: %ld, current_nr_sectors: %ld\n",rq->sector,rq->nr_sectors,rq->current_nr_sectors);
#endif /* IDEFLOPPY_DEBUG_LOG */

	if (rq->errors >= ERROR_MAX) {
		if (floppy->failed_pc != NULL)
			printk (KERN_ERR "ide-floppy: %s: I/O error, pc = %2x, key = %2x, asc = %2x, ascq = %2x\n",
				drive->name, floppy->failed_pc->c[0], floppy->sense_key, floppy->asc, floppy->ascq);
		else
			printk (KERN_ERR "ide-floppy: %s: I/O error\n", drive->name);
		idefloppy_end_request (0, HWGROUP(drive));
		return ide_stopped;
	}
	switch (rq->cmd) {
		case READ:
		case WRITE:
			if (rq->sector % floppy->bs_factor || rq->nr_sectors % floppy->bs_factor) {
				printk ("%s: unsupported r/w request size\n", drive->name);
				idefloppy_end_request (0, HWGROUP(drive));
				return ide_stopped;
			}
			pc = idefloppy_next_pc_storage (drive);
			idefloppy_create_rw_cmd (floppy, pc, rq, block);
			break;
		case IDEFLOPPY_PC_RQ:
			pc = (idefloppy_pc_t *) rq->buffer;
			break;
		default:
			printk (KERN_ERR "ide-floppy: unsupported command %x in request queue\n", rq->cmd);
			idefloppy_end_request (0,HWGROUP (drive));
			return ide_stopped;
	}
	pc->rq = rq;
	return idefloppy_issue_pc (drive, pc);
}

/*
 *	idefloppy_queue_pc_tail adds a special packet command request to the
 *	tail of the request queue, and waits for it to be serviced.
 */
static int idefloppy_queue_pc_tail (ide_drive_t *drive,idefloppy_pc_t *pc)
{
	struct request rq;

	ide_init_drive_cmd (&rq);
	rq.buffer = (char *) pc;
	rq.cmd = IDEFLOPPY_PC_RQ;
	return ide_do_drive_cmd (drive, &rq, ide_wait);
}

/*
 *	Look at the flexible disk page parameters. We will ignore the CHS
 *	capacity parameters and use the LBA parameters instead.
 */
static int idefloppy_get_flexible_disk_page (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t pc;
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

/*
 *	Determine if a media is present in the floppy drive, and if so,
 *	its LBA capacity.
 */
static int idefloppy_get_capacity (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t pc;
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
		if (!i && descriptor->dc == CAPACITY_CURRENT) {
			if (memcmp (descriptor, &floppy->capacity, sizeof (idefloppy_capacity_descriptor_t))) {
				printk (KERN_INFO "%s: %dkB, %d blocks, %d sector size, %s \n",
					drive->name, blocks * length / 1024, blocks, length,
					drive->using_dma ? ", DMA":"");
			}
			floppy->capacity = *descriptor;
			if (!length || length % 512)
				printk (KERN_ERR "%s: %d bytes block size not supported\n", drive->name, length);
			else {
				floppy->blocks = blocks;
				floppy->block_size = length;
				if ((floppy->bs_factor = length / 512) != 1)
					printk (KERN_NOTICE "%s: warning: non 512 bytes block size not fully supported\n", drive->name);
				rc = 0;
			}
		}
#if IDEFLOPPY_DEBUG_INFO
		if (!i) printk (KERN_INFO "Descriptor 0 Code: %d\n", descriptor->dc);
		printk (KERN_INFO "Descriptor %d: %dkB, %d blocks, %d sector size\n", i, blocks * length / 1024, blocks, length);
#endif /* IDEFLOPPY_DEBUG_INFO */
	}
	(void) idefloppy_get_flexible_disk_page (drive);
	drive->part[0].nr_sects = floppy->blocks * floppy->bs_factor;
	return rc;
}

/*
 *	Our special ide-floppy ioctl's.
 *
 *	Currently there aren't any ioctl's.
 */
static int idefloppy_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	idefloppy_pc_t pc;

	if (cmd == CDROMEJECT) {
		if (drive->usage > 1)
			return -EBUSY;
		idefloppy_create_prevent_cmd (&pc, 0);
		(void) idefloppy_queue_pc_tail (drive, &pc);
		idefloppy_create_start_stop_cmd (&pc, 2);
		(void) idefloppy_queue_pc_tail (drive, &pc);
		return 0;
	}
 	return -EIO;
}

/*
 *	Our open/release functions
 */
static int idefloppy_open (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t pc;
	
#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "Reached idefloppy_open\n");
#endif /* IDEFLOPPY_DEBUG_LOG */

	MOD_INC_USE_COUNT;
	if (drive->usage == 1) {
		idefloppy_create_test_unit_ready_cmd(&pc);
		if (idefloppy_queue_pc_tail(drive, &pc)) {
			idefloppy_create_start_stop_cmd (&pc, 1);
			(void) idefloppy_queue_pc_tail (drive, &pc);
		}
		if (idefloppy_get_capacity (drive)) {
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
		idefloppy_create_prevent_cmd (&pc, 1);
		(void) idefloppy_queue_pc_tail (drive, &pc);
		check_disk_change(inode->i_rdev);
	}
	return 0;
}

static void idefloppy_release (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	idefloppy_pc_t pc;
	
#if IDEFLOPPY_DEBUG_LOG
	printk (KERN_INFO "Reached idefloppy_release\n");
#endif /* IDEFLOPPY_DEBUG_LOG */

	if (!drive->usage) {
		invalidate_buffers (inode->i_rdev);
		idefloppy_create_prevent_cmd (&pc, 0);
		(void) idefloppy_queue_pc_tail (drive, &pc);
	}
	MOD_DEC_USE_COUNT;
}

/*
 *	Check media change. Use a simple algorithm for now.
 */
static int idefloppy_media_change (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	
	return test_and_clear_bit (IDEFLOPPY_MEDIA_CHANGED, &floppy->flags);
}

/*
 *	Revalidate the new media. Should set blk_size[]
 */
static void idefloppy_revalidate (ide_drive_t *drive)
{
	grok_partitions(HWIF(drive)->gd, drive->select.b.unit,
			1<<PARTN_BITS,
			current_capacity(drive));
}

/*
 *	Return the current floppy capacity to ide.c.
 */
static unsigned long idefloppy_capacity (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	unsigned long capacity = floppy->blocks * floppy->bs_factor;

	return capacity;
}

/*
 *	idefloppy_identify_device checks if we can support a drive,
 *	based on the ATAPI IDENTIFY command results.
 */
static int idefloppy_identify_device (ide_drive_t *drive,struct hd_driveid *id)
{
	struct idefloppy_id_gcw gcw;
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

static void idefloppy_add_settings(ide_drive_t *drive)
{
	int major = HWIF(drive)->major;
	int minor = drive->select.b.unit << PARTN_BITS;

	ide_add_setting(drive,	"bios_cyl",		SETTING_RW,					-1,			-1,			TYPE_INT,	0,	1023,				1,	1,	&drive->bios_cyl,		NULL);
	ide_add_setting(drive,	"bios_head",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	255,				1,	1,	&drive->bios_head,		NULL);
	ide_add_setting(drive,	"bios_sect",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	63,				1,	1,	&drive->bios_sect,		NULL);
	ide_add_setting(drive,	"breada_readahead",	SETTING_RW,					BLKRAGET,		BLKRASET,		TYPE_INT,	0,	255,				1,	2,	&read_ahead[major],		NULL);
	ide_add_setting(drive,	"file_readahead",	SETTING_RW,					BLKFRAGET,		BLKFRASET,		TYPE_INTA,	0,	INT_MAX,			1,	1024,	&max_readahead[major][minor],	NULL);
	ide_add_setting(drive,	"max_kb_per_request",	SETTING_RW,					BLKSECTGET,		BLKSECTSET,		TYPE_INTA,	1,	255,				1,	2,	&max_sectors[major][minor],	NULL);

}

/*
 *	Driver initialization.
 */
static void idefloppy_setup (ide_drive_t *drive, idefloppy_floppy_t *floppy)
{
	struct idefloppy_id_gcw gcw;
	int major = HWIF(drive)->major, i;
	int minor = drive->select.b.unit << PARTN_BITS;

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
	{
		for (i = 0; i < 1 << PARTN_BITS; i++)
			max_sectors[major][minor + i] = 64;
	}

	(void) idefloppy_get_capacity (drive);
	idefloppy_add_settings(drive);
	for (i = 0; i < MAX_DRIVES; ++i) {
		ide_hwif_t *hwif = HWIF(drive);

		if (drive != &hwif->drives[i]) continue;
		hwif->gd->de_arr[i] = drive->de;
		if (drive->removable)
			hwif->gd->flags[i] |= GENHD_FL_REMOVABLE;
		break;
	}
}

static int idefloppy_cleanup (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (ide_unregister_subdriver (drive))
		return 1;
	drive->driver_data = NULL;
	kfree (floppy);
	return 0;
}

#ifdef CONFIG_PROC_FS

static ide_proc_entry_t idefloppy_proc[] = {
	{ "geometry",	S_IFREG|S_IRUGO,	proc_ide_read_geometry,	NULL },
	{ NULL, 0, NULL, NULL }
};

#else

#define	idefloppy_proc	NULL

#endif	/* CONFIG_PROC_FS */

/*
 *	IDE subdriver functions, registered with ide.c
 */
static ide_driver_t idefloppy_driver = {
	"ide-floppy",		/* name */
	IDEFLOPPY_VERSION,	/* version */
	ide_floppy,		/* media */
	0,			/* busy */
	1,			/* supports_dma */
	0,			/* supports_dsc_overlap */
	idefloppy_cleanup,	/* cleanup */
	idefloppy_do_request,	/* do_request */
	idefloppy_end_request,	/* end_request */
	idefloppy_ioctl,	/* ioctl */
	idefloppy_open,		/* open */
	idefloppy_release,	/* release */
	idefloppy_media_change,	/* media_change */
	idefloppy_revalidate,	/* media_change */
	NULL,			/* pre_reset */
	idefloppy_capacity,	/* capacity */
	NULL,			/* special */
	idefloppy_proc		/* proc */
};

int idefloppy_init (void);
static ide_module_t idefloppy_module = {
	IDE_DRIVER_MODULE,
	idefloppy_init,
	&idefloppy_driver,
	NULL
};

/*
 *	idefloppy_init will register the driver for each floppy.
 */
int idefloppy_init (void)
{
	ide_drive_t *drive;
	idefloppy_floppy_t *floppy;
	int failed = 0;

	MOD_INC_USE_COUNT;
	while ((drive = ide_scan_devices (ide_floppy, idefloppy_driver.name, NULL, failed++)) != NULL) {
		if (!idefloppy_identify_device (drive, drive->id)) {
			printk (KERN_ERR "ide-floppy: %s: not supported by this version of ide-floppy\n", drive->name);
			continue;
		}
		if (drive->scsi) {
			printk("ide-floppy: passing drive %s to ide-scsi emulation.\n", drive->name);
			continue;
		}
		if ((floppy = (idefloppy_floppy_t *) kmalloc (sizeof (idefloppy_floppy_t), GFP_KERNEL)) == NULL) {
			printk (KERN_ERR "ide-floppy: %s: Can't allocate a floppy structure\n", drive->name);
			continue;
		}
		if (ide_register_subdriver (drive, &idefloppy_driver, IDE_SUBDRIVER_VERSION)) {
			printk (KERN_ERR "ide-floppy: %s: Failed to register the driver with ide.c\n", drive->name);
			kfree (floppy);
			continue;
		}
		idefloppy_setup (drive, floppy);
		failed--;
	}
	ide_register_module(&idefloppy_module);
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE
int init_module (void)
{
	return idefloppy_init ();
}

void cleanup_module (void)
{
	ide_drive_t *drive;
	int failed = 0;

	while ((drive = ide_scan_devices (ide_floppy, idefloppy_driver.name, &idefloppy_driver, failed)) != NULL) {
		if (idefloppy_cleanup (drive)) {
			printk ("%s: cleanup_module() called while still busy\n", drive->name);
			failed++;
		}
		/* We must remove proc entries defined in this module.
		   Otherwise we oops while accessing these entries */
		if (drive->proc)
			ide_remove_proc_entries(drive->proc, idefloppy_proc);
	}
	ide_unregister_module(&idefloppy_module);
}
#endif /* MODULE */
