/*
 *	seagate.h Copyright (C) 1992 Drew Eckhardt 
 *	low level scsi driver header for ST01/ST02 by
 *		Drew Eckhardt 
 *
 *	<drew@colorado.edu>
 */

#ifndef _SEAGATE_H
#define SEAGATE_H

static int seagate_st0x_detect(Scsi_Host_Template *);
static int seagate_st0x_command(Scsi_Cmnd *);
static int seagate_st0x_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));

static int seagate_st0x_abort(Scsi_Cmnd *);
static const char *seagate_st0x_info(struct Scsi_Host *);
static int seagate_st0x_bus_reset(Scsi_Cmnd *);
static int seagate_st0x_device_reset(Scsi_Cmnd *);
static int seagate_st0x_host_reset(Scsi_Cmnd *);

#define SEAGATE_ST0X  {  detect:         seagate_st0x_detect,			\
			 info:           seagate_st0x_info,			\
			 command:        seagate_st0x_command,			\
			 queuecommand:   seagate_st0x_queue_command,		\
			 eh_abort_handler:	seagate_st0x_abort,		\
			 eh_bus_reset_handler:  seagate_st0x_bus_reset,		\
			 eh_host_reset_handler: seagate_st0x_host_reset,	\
			 eh_device_reset_handler:seagate_st0x_device_reset,	\
			 can_queue:      1,					\
			 this_id:        7,					\
			 sg_tablesize:   SG_ALL,				\
			 cmd_per_lun:    1,					\
			 use_clustering: DISABLE_CLUSTERING}

#endif /* _SEAGATE_H */
