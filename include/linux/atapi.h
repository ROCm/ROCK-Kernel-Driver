/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 * Copyright (C) 2002 Marcin Dalecki <martin@dalecki.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/types.h>
#include <asm/byteorder.h>

/*
 * With each packet command, we allocate a buffer.
 * This is used for several packet
 * commands (Not for READ/WRITE commands).
 */
#define IDEFLOPPY_PC_BUFFER_SIZE	256
#define IDETAPE_PC_BUFFER_SIZE		256

/*
 * Packet flags bits.
 */

#define	PC_ABORT		0	/* set when an error is considered normal - we won't retry */
#define PC_WAIT_FOR_DSC		1	/* 1 when polling for DSC on a media access command */
#define PC_DMA_RECOMMENDED	2	/* 1 when we prefer to use DMA if possible */
#define	PC_DMA_IN_PROGRESS	3	/* 1 while DMA in progress */
#define	PC_DMA_ERROR		4	/* 1 when encountered problem during DMA */
#define	PC_WRITING		5	/* data direction */
#define	PC_SUPPRESS_ERROR	6	/* suppress error reporting */
#define PC_TRANSFORM		7	/* transform SCSI commands */

/* This struct get's shared between different drivers.
 */
struct atapi_packet_command {
	u8 c[12];			/* Actual packet bytes */
	char *buffer;			/* Data buffer */
	int buffer_size;		/* Size of our data buffer */
	char *current_position;		/* Pointer into the above buffer */
	int request_transfer;		/* Bytes to transfer */
	int actually_transferred;	/* Bytes actually transferred */

	unsigned long flags;		/* Status/Action bit flags: long for set_bit */

	/* FIXME: the following is ugly as hell, but the only way we can start
	 * actually to unify the code.
	 */
	/* driver specific data. */
	/* floppy/tape */
	int retries;				/* On each retry, we increment retries */
	int error;				/* Error code */
	char *b_data;				/* Pointer which runs on the buffers */
	unsigned int b_count;			/* Missing/Available data on the current buffer */
	u8 pc_buffer[IDEFLOPPY_PC_BUFFER_SIZE];	/* Temporary buffer */
	/* Called when this packet command is completed */
	void (*callback) (struct ata_device *, struct request *);

	/* only tape */
	struct bio *bio;

	/* only scsi */
	struct {
		unsigned int b_count;			/* Bytes transferred from current entry */
		struct scatterlist *sg;			/* Scatter gather table */
		struct scsi_cmnd *scsi_cmd;		/* SCSI command */
		void (*done)(struct scsi_cmnd *);	/* Scsi completion routine */
		unsigned long timeout;			/* Command timeout */
	} s;
};

/*
 *	ATAPI Status Register.
 */
typedef union {
	u8 all			: 8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u8 check	: 1;	/* Error occurred */
		u8 idx		: 1;	/* Reserved */
		u8 corr		: 1;	/* Correctable error occurred */
		u8 drq		: 1;	/* Data is request by the device */
		u8 dsc		: 1;	/* Media access command finished / Buffer availability */
		u8 reserved5	: 1;	/* Reserved */
		u8 drdy		: 1;	/* Ignored for ATAPI commands (ready to accept ATA command) */
		u8 bsy		: 1;	/* The device has access to the command block */
#elif defined(__BIG_ENDIAN_BITFIELD)
		u8 bsy		: 1;
		u8 drdy		: 1;
		u8 reserved5	: 1;
		u8 dsc		: 1;
		u8 drq		: 1;
		u8 corr		: 1;
		u8 idx		: 1;
		u8 check	: 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_status_reg_t;

/*
 *	ATAPI error register.
 */
typedef union {
	u8 all			: 8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u8 ili		: 1;	/* Illegal Length Indication */
		u8 eom		: 1;	/* End Of Media Detected */
		u8 abrt		: 1;	/* Aborted command - As defined by ATA */
		u8 mcr		: 1;	/* Media Change Requested - As defined by ATA */
		u8 sense_key	: 4;	/* Sense key of the last failed packet command */
#elif defined(__BIG_ENDIAN_BITFIELD)
		u8 sense_key	: 4;
		u8 mcr		: 1;
		u8 abrt		: 1;
		u8 eom		: 1;
		u8 ili		: 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_error_reg_t;

/* Currently unused, but please do not remove.  --bkz */
/*
 *	ATAPI Feature Register.
 */
typedef union {
	u8 all			: 8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u8 dma		: 1;	/* Using DMA or PIO */
		u8 reserved321	: 3;	/* Reserved */
		u8 reserved654	: 3;	/* Reserved (Tag Type) */
		u8 reserved7	: 1;	/* Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
		u8 reserved7	: 1;
		u8 reserved654	: 3;
		u8 reserved321	: 3;
		u8 dma		: 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_feature_reg_t;

/*
 *	ATAPI Byte Count Register.
 */
typedef union {
	u16 all			: 16;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u8 low;			/* LSB */
		u8 high;		/* MSB */
#elif defined(__BIG_ENDIAN_BITFIELD)
		u8 high;
		u8 low;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_bcount_reg_t;

/*
 *	ATAPI Interrupt Reason Register.
 */
typedef union {
	u8 all			: 8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u8 cod		: 1;	/* Information transferred is command (1) or data (0) */
		u8 io		: 1;	/* The device requests us to read (1) or write (0) */
		u8 reserved	: 6;	/* Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
		u8 reserved	: 6;
		u8 io		: 1;
		u8 cod		: 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_ireason_reg_t;

/* Currently unused, but please do not remove.  --bkz */
/*
 *	ATAPI Drive Select Register.
 */
typedef union {
	u8 all			:8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u8 sam_lun	:3;	/* Logical unit number */
		u8 reserved3	:1;	/* Reserved */
		u8 drv		:1;	/* The responding drive will be drive 0 (0) or drive 1 (1) */
		u8 one5		:1;	/* Should be set to 1 */
		u8 reserved6	:1;	/* Reserved */
		u8 one7		:1;	/* Should be set to 1 */
#elif defined(__BIG_ENDIAN_BITFIELD)
		u8 one7		:1;
		u8 reserved6	:1;
		u8 one5		:1;
		u8 drv		:1;
		u8 reserved3	:1;
		u8 sam_lun	:3;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_drivesel_reg_t;

/* Currently unused, but please do not remove.  --bkz */
/*
 *	ATAPI Device Control Register.
 */
typedef union {
	u8 all			: 8;
	struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
		u8 zero0	: 1;	/* Should be set to zero */
		u8 nien		: 1;	/* Device interrupt is disabled (1) or enabled (0) */
		u8 srst		: 1;	/* ATA software reset. ATAPI devices should use the new ATAPI srst. */
		u8 one3		: 1;	/* Should be set to 1 */
		u8 reserved4567	: 4;	/* Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
		u8 reserved4567	: 4;
		u8 one3		: 1;
		u8 srst		: 1;
		u8 nien		: 1;
		u8 zero0	: 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	} b;
} atapi_control_reg_t;

/*
 *	The following is used to format the general configuration word
 *	of the ATAPI IDENTIFY DEVICE command.
 */
struct atapi_id_gcw {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 packet_size		: 2;	/* Packet Size */
	u8 reserved234		: 3;	/* Reserved */
	u8 drq_type		: 2;	/* Command packet DRQ type */
	u8 removable		: 1;	/* Removable media */
	u8 device_type		: 5;	/* Device type */
	u8 reserved13		: 1;	/* Reserved */
	u8 protocol		: 2;	/* Protocol type */
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8 protocol		: 2;
	u8 reserved13		: 1;
	u8 device_type		: 5;
	u8 removable		: 1;
	u8 drq_type		: 2;
	u8 reserved234		: 3;
	u8 packet_size		: 2;
#else
#error "Please fix <asm/byteorder.h>"
#endif
};

/*
 *	INQUIRY packet command - Data Format.
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8	device_type	: 5;	/* Peripheral Device Type */
	u8	reserved0_765	: 3;	/* Peripheral Qualifier - Reserved */
	u8	reserved1_6t0	: 7;	/* Reserved */
	u8	rmb		: 1;	/* Removable Medium Bit */
	u8	ansi_version	: 3;	/* ANSI Version */
	u8	ecma_version	: 3;	/* ECMA Version */
	u8	iso_version	: 2;	/* ISO Version */
	u8	response_format : 4;	/* Response Data Format */
	u8	reserved3_45	: 2;	/* Reserved */
	u8	reserved3_6	: 1;	/* TrmIOP - Reserved */
	u8	reserved3_7	: 1;	/* AENC - Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8	reserved0_765	: 3;
	u8	device_type	: 5;
	u8	rmb		: 1;
	u8	reserved1_6t0	: 7;
	u8	iso_version	: 2;
	u8	ecma_version	: 3;
	u8	ansi_version	: 3;
	u8	reserved3_7	: 1;
	u8	reserved3_6	: 1;
	u8	reserved3_45	: 2;
	u8	response_format : 4;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	u8	additional_length;	/* Additional Length (total_length-4) */
	u8	rsv5, rsv6, rsv7;	/* Reserved */
	u8	vendor_id[8];		/* Vendor Identification */
	u8	product_id[16];		/* Product Identification */
	u8	revision_level[4];	/* Revision Level */
	u8	vendor_specific[20];	/* Vendor Specific - Optional */
	u8	reserved56t95[40];	/* Reserved - Optional */
					/* Additional information may be returned */
} atapi_inquiry_result_t;

/*
 *	REQUEST SENSE packet command result - Data Format.
 */
typedef struct atapi_request_sense {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8	error_code	: 7;	/* Error Code (0x70 - current or 0x71 - deferred) */
	u8	valid		: 1;	/* The information field conforms to standard */
	u8	reserved1	: 8;	/* Reserved (Segment Number) */
	u8	sense_key	: 4;	/* Sense Key */
	u8	reserved2_4	: 1;	/* Reserved */
	u8	ili		: 1;	/* Incorrect Length Indicator */
	u8	eom		: 1;	/* End Of Medium */
	u8	filemark	: 1;	/* Filemark */
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8	valid		: 1;
	u8	error_code	: 7;
	u8	reserved1	: 8;
	u8	filemark	: 1;
	u8	eom		: 1;
	u8	ili		: 1;
	u8	reserved2_4	: 1;
	u8	sense_key	: 4;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	u32	information __attribute__ ((packed));
	u8	asl;			/* Additional sense length (n-7) */
	u32	command_specific;	/* Additional command specific information */
	u8	asc;			/* Additional Sense Code */
	u8	ascq;			/* Additional Sense Code Qualifier */
	u8	replaceable_unit_code;	/* Field Replaceable Unit Code */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8	sk_specific1	: 7;	/* Sense Key Specific */
	u8	sksv		: 1;	/* Sense Key Specific information is valid */
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8	sksv		: 1;	/* Sense Key Specific information is valid */
	u8	sk_specific1	: 7;	/* Sense Key Specific */
#else
#error "Please fix <asm/byteorder.h>"
#endif
	u8	sk_specific[2];		/* Sense Key Specific */
	u8	pad[2];			/* Padding to 20 bytes */
} atapi_request_sense_result_t;


extern void atapi_init_pc(struct atapi_packet_command *pc);

extern void atapi_discard_data(struct ata_device *, unsigned int);
extern void atapi_write_zeros(struct ata_device *, unsigned int);

extern void atapi_read(struct ata_device *, u8 *, unsigned int);
extern void atapi_write(struct ata_device *, u8 *, unsigned int);

typedef enum {
	ide_wait,	/* insert rq at end of list, and wait for it */
	ide_preempt,	/* insert rq in front of current request */
	ide_end		/* insert rq at end of list, but don't wait for it */
} ide_action_t;

extern int ide_do_drive_cmd(struct ata_device *, struct request *, ide_action_t);
