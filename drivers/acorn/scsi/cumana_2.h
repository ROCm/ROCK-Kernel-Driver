/*
 *  linux/drivers/acorn/scsi/cumana_2.h
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Cumana SCSI II driver
 */
#ifndef CUMANA_2_H
#define CUMANA_2_H

extern int cumanascsi_2_detect (Scsi_Host_Template *);
extern int cumanascsi_2_release (struct Scsi_Host *);
extern const char *cumanascsi_2_info (struct Scsi_Host *);
extern int cumanascsi_2_proc_info (char *buffer, char **start, off_t offset,
					int length, int hostno, int inout);

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef CAN_QUEUE
/*
 * Default queue size
 */
#define CAN_QUEUE	1
#endif

#ifndef CMD_PER_LUN
/*
 * Default queue size
 */
#define CMD_PER_LUN	1
#endif

#ifndef SCSI_ID
/*
 * Default SCSI host ID
 */
#define SCSI_ID		7
#endif

#include <scsi/scsicam.h>

#include "fas216.h"

#define CUMANASCSI_2 {					\
proc_info:			cumanascsi_2_proc_info,	\
name:				"Cumana SCSI II",	\
detect:				cumanascsi_2_detect,	\
release:			cumanascsi_2_release,	\
info:				cumanascsi_2_info,	\
bios_param:			scsicam_bios_param,	\
can_queue:			CAN_QUEUE,		\
this_id:			SCSI_ID,		\
sg_tablesize:			SG_ALL,			\
cmd_per_lun:			CMD_PER_LUN,		\
use_clustering:			DISABLE_CLUSTERING,	\
command:			fas216_command,		\
queuecommand:			fas216_queue_command,	\
eh_host_reset_handler:		fas216_eh_host_reset,	\
eh_bus_reset_handler:		fas216_eh_bus_reset,	\
eh_device_reset_handler:	fas216_eh_device_reset,	\
eh_abort_handler:		fas216_eh_abort,	\
use_new_eh_code:		1			\
	}

#ifndef HOSTS_C

#include <asm/dma.h>

#define NR_SG	256

typedef struct {
	FAS216_Info info;

	/* other info... */
	unsigned int	status;		/* card status register	*/
	unsigned int	alatch;		/* Control register	*/
	unsigned int	terms;		/* Terminator state	*/
	unsigned int	dmaarea;	/* Pseudo DMA area	*/
	struct scatterlist sg[NR_SG];	/* Scatter DMA list	*/
} CumanaScsi2_Info;

#define CSTATUS_IRQ	(1 << 0)
#define CSTATUS_DRQ	(1 << 1)

#endif /* HOSTS_C */

#endif /* CUMANASCSI_2_H */
