/*
 * Cumana Generic NCR5380 driver defines
 *
 * Copyright 1995, Russell King
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
 * $Log: ecoscsi.h,v $
 * Revision 1.1  1998/02/23 02:45:24  davem
 * Merge to 2.1.88
 *
 */

#ifndef ECOSCSI_NCR5380_H
#define ECOSCSI_NCR5380_H

#define ECOSCSI_PUBLIC_RELEASE 1


#ifndef ASM
int ecoscsi_abort (Scsi_Cmnd *);
int ecoscsi_detect (Scsi_Host_Template *);
int ecoscsi_release (struct Scsi_Host *);
const char *ecoscsi_info (struct Scsi_Host *);
int ecoscsi_reset(Scsi_Cmnd *, unsigned int);
int ecoscsi_queue_command (Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int ecoscsi_proc_info (char *buffer, char **start, off_t offset,
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

#define ECOSCSI_NCR5380 {					\
name:		"Serial Port EcoSCSI NCR5380",			\
detect:		ecoscsi_detect,					\
release:	ecoscsi_release,				\
info:		ecoscsi_info,					\
queuecommand:	ecoscsi_queue_command,				\
abort:		ecoscsi_abort, 					\
reset:		ecoscsi_reset,					\
can_queue:	CAN_QUEUE,		/* can queue */		\
this_id:	7,			/* id */		\
sg_tablesize:	SG_ALL,						\
cmd_per_lun:	CMD_PER_LUN,		/* cmd per lun */	\
use_clustering:	DISABLE_CLUSTERING				\
	}

#ifndef HOSTS_C
#define NCR5380_implementation_fields \
    int port, ctrl

#define NCR5380_local_declare() \
        struct Scsi_Host *_instance

#define NCR5380_setup(instance) \
        _instance = instance

#define NCR5380_read(reg) ecoscsi_read(_instance, reg)
#define NCR5380_write(reg, value) ecoscsi_write(_instance, reg, value)

#define do_NCR5380_intr do_ecoscsi_intr
#define NCR5380_queue_command ecoscsi_queue_command
#define NCR5380_abort ecoscsi_abort
#define NCR5380_reset ecoscsi_reset
#define NCR5380_proc_info ecoscsi_proc_info

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#endif /* ndef HOSTS_C */
#endif /* ndef ASM */
#endif /* ECOSCSI_NCR5380_H */

