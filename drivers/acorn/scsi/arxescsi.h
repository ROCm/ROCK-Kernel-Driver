/*
 * ARXE SCSI card driver
 *
 * Copyright (C) 1997-2000 Russell King
 *
 * Changes to support ARXE 16-bit SCSI card by Stefan Hanske
 */
#ifndef ARXE_SCSI_H
#define ARXE_SCSI_H

#define MANU_ARXE 	0x0041
#define PROD_ARXE_SCSI	0x00be

extern int arxescsi_detect (Scsi_Host_Template *);
extern int arxescsi_release (struct Scsi_Host *);
extern const char *arxescsi_info (struct Scsi_Host *);
extern int arxescsi_proc_info (char *buffer, char **start, off_t offset,
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

#define ARXEScsi {					\
proc_info:			arxescsi_proc_info,	\
name:				"ARXE SCSI card",	\
detect:				arxescsi_detect,	\
release:			arxescsi_release,	\
info:				arxescsi_info,		\
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

typedef struct {
    FAS216_Info info;

    /* other info... */
    unsigned int	cstatus;	/* card status register	*/
    unsigned int	dmaarea;	/* Pseudo DMA area	*/
} ARXEScsi_Info;

#define CSTATUS_IRQ	(1 << 0)
#define CSTATUS_DRQ	(1 << 0)

#endif /* HOSTS_C */

#endif /* ARXE_SCSI_H */
