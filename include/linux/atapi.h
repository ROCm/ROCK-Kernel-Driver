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

/*
 * With each packet command, we allocate a buffer.
 * This is used for several packet
 * commands (Not for READ/WRITE commands).
 */
#define IDEFLOPPY_PC_BUFFER_SIZE	256
#define IDETAPE_PC_BUFFER_SIZE		256

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

extern void atapi_init_pc(struct atapi_packet_command *pc);

extern void atapi_discard_data(struct ata_device *, unsigned int);
extern void atapi_write_zeros(struct ata_device *, unsigned int);

extern void atapi_read(struct ata_device *, u8 *, unsigned int);
extern void atapi_write(struct ata_device *, u8 *, unsigned int);

