/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#ifndef _H_OS_SCSIPORT
#define _H_OS_SCSIPORT
#include <scsi.h>
/* Function prototypes. */
int elx_revoke(Scsi_Device * pScsiDevice);
int elx_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int elx_abort_handler(Scsi_Cmnd *);
int elx_reset_lun_handler(Scsi_Cmnd *);

struct buf {
	void *av_forw;
	void *av_back;
	int b_bcount;		/* transfer count */
	int b_error;		/* expanded error field */
	int b_resid;		/* words not transferred after error */
	int b_flags;		/* see defines below */
#define B_ERROR         0x0004	/* transaction aborted */
#define B_READ          0x0040	/* read when I/O occurs */
#define B_WRITE         0x0100	/* non-read pseudo-flag */
	Scsi_Cmnd *cmnd;
	int isdone;
};

/* refer to the SCSI ANSI X3.131-1986 standard for information */
struct sc_cmd {			/* structure of the SCSI cmd block */
	uint8_t scsi_op_code;	/* first byte of SCSI cmd block */
	uint8_t lun;		/* second byte of SCSI cmd block */
	uint8_t scsi_bytes[14];	/* other bytes of SCSI cmd block */
};
#define SCSI_RELEASE_UNIT                       0x17
#define SCSI_REQUEST_SENSE                      0x03
#define SCSI_RESERVE_UNIT                       0x16

struct scsi {
	uint8_t scsi_length;	/* byte length of scsi cmd (6,10, or 12) */
	uint8_t scsi_id;	/* the target SCSI ID */
	uint8_t scsi_lun;	/* which LUN on the target */
	uint8_t flags;		/* flags for use with the physical scsi command */
#define SC_NODISC       0x80	/* don't allow disconnections */
#define SC_ASYNC        0x08	/* asynchronous data xfer */
	struct sc_cmd scsi_cmd;	/* the actual SCSI cmd */
};

struct sc_buf {
	struct buf bufstruct;	/* buffer structure containing request
				   for device -- MUST BE FIRST! */
	struct scsi scsi_command;	/* the information relating strictly
					   to the scsi command itself */
	uint32_t timeout_value;	/* timeout value for the command,
				   in units of seconds */
	uint32_t cmd_flag;
#define FLAG_ABORT              0x01

	uint8_t status_validity;	/* least significant bit - scsi_status
					 * valid, next least significant bit -
					 * card status valid */

#define SC_SCSI_ERROR           1	/* scsi status reflects error */
#define SC_ADAPTER_ERROR        2	/* general card status reflects err */
	uint8_t scsi_status;	/* returned SCSI Bus status */
#define SCSI_STATUS_MASK        0x3e	/* mask for useful bits */
#define SC_GOOD_STATUS          0x00	/* target completed successfully */
#define SC_CHECK_CONDITION      0x02	/* target is reporting an error,
					 * exception, or abnormal condition */
#define SC_BUSY_STATUS          0x08	/* target is busy and cannot accept
					 * a command from initiator */
#define SC_INTMD_GOOD           0x10	/* intermediate status good when using
					 * linked commands */
#define SC_RESERVATION_CONFLICT 0x18	/* attempted to access a LUN which is
					 * reserved by another initiator */
#define SC_COMMAND_TERMINATED   0x22	/* Command has been terminated by
					 * the device. */
#define SC_QUEUE_FULL           0x28	/* Device's command queue is full */

	uint8_t general_card_status;	/* SCSI adapter card status byte */
#define SC_HOST_IO_BUS_ERR      0x01	/* Host I/O Bus error condition */
#define SC_SCSI_BUS_FAULT       0x02	/* failure of the SCSI Bus */
#define SC_CMD_TIMEOUT          0x04	/* cmd didn't complete before timeout */
#define SC_NO_DEVICE_RESPONSE   0x08	/* target device did not respond */
#define SC_ADAPTER_HDW_FAILURE  0x10	/* indicating a hardware failure */
#define SC_ADAPTER_SFW_FAILURE  0x20	/* indicating a microcode failure */
#define SC_FUSE_OR_TERMINAL_PWR 0x40	/* indicating bad fuse or termination */
#define SC_SCSI_BUS_RESET       0x80	/* detected external SCSI bus reset */

	uint8_t adap_q_status;	/* adapter's device queue status. This */
#define SC_DID_NOT_CLEAR_Q      0x1	/* SCSI adapter device driver has not */

	uint8_t flags;		/* flags to SCSI adapter driver */
#define SC_RESUME                0x01	/* resume transaction queueing for this
					 * id/lun beginning with this sc_buf */
#define SC_FAILOVER              0x10
#define SC_LOAD_BALANCE_FAILOVER 0x20

	uint32_t qfull_retry_count;
	struct dev_info *current_devp;
	struct dev_info *fover_devp;
	struct Scsi_Host *fover_host;
};

#endif				/* _H_OS_SCSIPORT */
