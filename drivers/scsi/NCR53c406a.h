#ifndef _NCR53C406A_H
#define _NCR53C406A_H

/*
 *  NCR53c406a.h
 * 
 *  Copyright (C) 1994 Normunds Saumanis (normunds@rx.tech.swh.lv)
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 */

#ifndef NULL
#define NULL 0
#endif

/* NOTE:  scatter-gather support only works in PIO mode.
 * Use SG_NONE if DMA mode is enabled!
 */
#define NCR53c406a { \
     proc_name:         	"NCR53c406a"		/* proc_name */,        \
     name:              	"NCR53c406a"		/* name */,             \
     detect:            	NCR53c406a_detect	/* detect */,           \
     info:              	NCR53c406a_info		/* info */,             \
     command:           	NCR53c406a_command	/* command */,          \
     queuecommand:      	NCR53c406a_queue	/* queuecommand */,     \
     eh_abort_handler:  	NCR53c406a_abort	/* abort */,            \
     eh_bus_reset_handler:      NCR53c406a_bus_reset	/* reset */,            \
     eh_device_reset_handler:   NCR53c406a_device_reset	/* reset */,            \
     eh_host_reset_handler:     NCR53c406a_host_reset	/* reset */,            \
     bios_param:        	NCR53c406a_biosparm	/* biosparm */,         \
     can_queue:         	1			/* can_queue */,        \
     this_id:           	7			/* SCSI ID of the chip */, \
     sg_tablesize:      	32			/*SG_ALL*/ /*SG_NONE*/, \
     cmd_per_lun:       	1			/* commands per lun */, \
     unchecked_isa_dma: 	1			/* unchecked_isa_dma */, \
     use_clustering:    	ENABLE_CLUSTERING                               \
}

static int NCR53c406a_detect(Scsi_Host_Template *);
static const char *NCR53c406a_info(struct Scsi_Host *);
static int NCR53c406a_command(Scsi_Cmnd *);
static int NCR53c406a_queue(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
static int NCR53c406a_abort(Scsi_Cmnd *);
static int NCR53c406a_bus_reset(Scsi_Cmnd *);
static int NCR53c406a_device_reset(Scsi_Cmnd *);
static int NCR53c406a_host_reset(Scsi_Cmnd *);
static int NCR53c406a_biosparm(Disk *, struct block_device *, sector_t, int[]);

#endif				/* _NCR53C406A_H */
