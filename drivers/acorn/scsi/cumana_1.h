/*
 * Cumana Generic NCR5380 driver defines
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * ALPHA RELEASE 1.
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

/*
 * $Log: cumana_1.h,v $
 * Revision 1.1  1998/02/23 02:45:22  davem
 * Merge to 2.1.88
 *
 */

#ifndef CUMANA_NCR5380_H
#define CUMANA_NCR5380_H

#define CUMANASCSI_PUBLIC_RELEASE 1


#ifndef ASM
int cumanascsi_abort (Scsi_Cmnd *);
int cumanascsi_detect (Scsi_Host_Template *);
int cumanascsi_release (struct Scsi_Host *);
const char *cumanascsi_info (struct Scsi_Host *);
int cumanascsi_reset(Scsi_Cmnd *, unsigned int);
int cumanascsi_queue_command (Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int cumanascsi_proc_info (char *buffer, char **start, off_t offset,
			int length, int hostno, int inout);

#ifndef NULL
#define NULL 0
#endif

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 16
#endif

#include <scsi/scsicam.h>

#define CUMANA_NCR5380 {						\
name:			"Cumana 16-bit SCSI",				\
detect:			cumanascsi_detect,				\
release:		cumanascsi_release,	/* Release */		\
info:			cumanascsi_info,				\
queuecommand:		cumanascsi_queue_command,			\
abort:			cumanascsi_abort,			 	\
reset:			cumanascsi_reset,				\
bios_param:		scsicam_bios_param,	/* biosparam */		\
can_queue:		CAN_QUEUE,		/* can queue */		\
this_id:		7,			/* id */		\
sg_tablesize:		SG_ALL,			/* sg_tablesize */	\
cmd_per_lun:		CMD_PER_LUN,		/* cmd per lun */	\
unchecked_isa_dma:	0,			/* unchecked_isa_dma */	\
use_clustering:		DISABLE_CLUSTERING				\
	}

#ifndef HOSTS_C

#define NCR5380_implementation_fields \
    int port, ctrl

#define NCR5380_local_declare() \
        struct Scsi_Host *_instance

#define NCR5380_setup(instance) \
        _instance = instance

#define NCR5380_read(reg) cumanascsi_read(_instance, reg)
#define NCR5380_write(reg, value) cumanascsi_write(_instance, reg, value)

#define do_NCR5380_intr do_cumanascsi_intr
#define NCR5380_queue_command cumanascsi_queue_command
#define NCR5380_abort cumanascsi_abort
#define NCR5380_reset cumanascsi_reset
#define NCR5380_proc_info cumanascsi_proc_info

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#endif /* ndef HOSTS_C */
#endif /* ndef ASM */
#endif /* CUMANA_NCR5380_H */

