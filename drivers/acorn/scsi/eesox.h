/*
 *  linux/drivers/acorn/scsi/eesox.h
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  EESOX SCSI driver
 */
#ifndef EESOXSCSI_H
#define EESOXSCSI_H

extern int eesoxscsi_detect (Scsi_Host_Template *);
extern int eesoxscsi_release (struct Scsi_Host *);
extern const char *eesoxscsi_info (struct Scsi_Host *);
extern int eesoxscsi_proc_info (char *buffer, char **start, off_t offset,
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

#define EESOXSCSI {					\
proc_info:			eesoxscsi_proc_info,	\
name:				"EESOX SCSI",		\
detect:				eesoxscsi_detect,	\
release:			eesoxscsi_release,	\
info:				eesoxscsi_info,		\
bios_param:			scsicam_bios_param,	\
can_queue:			CAN_QUEUE,		\
this_id:			SCSI_ID,		\
sg_tablesize:			SG_ALL,			\
cmd_per_lun:			CAN_QUEUE,		\
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

struct control {
	unsigned int	io_port;
	unsigned int	control;
};

typedef struct {
	FAS216_Info info;

	struct control control;

	unsigned int	dmaarea;	/* Pseudo DMA area	*/
	struct scatterlist sg[NR_SG];	/* Scatter DMA list	*/
} EESOXScsi_Info;

#endif /* HOSTS_C */

#endif /* EESOXSCSI_H */
