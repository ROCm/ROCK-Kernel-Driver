#ifndef _QLOGICFAS_H
#define _QLOGICFAS_H

static int qlogicfas_detect(Scsi_Host_Template * );
static const char * qlogicfas_info(struct Scsi_Host *);
static int qlogicfas_command(Scsi_Cmnd *);
static int qlogicfas_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
static int qlogicfas_abort(Scsi_Cmnd *);
static int qlogicfas_bus_reset(Scsi_Cmnd *);
static int qlogicfas_device_reset(Scsi_Cmnd *);
static int qlogicfas_host_reset(Scsi_Cmnd *);
static int qlogicfas_biosparam(struct scsi_device *, struct block_device *,
			       sector_t, int[]);

#define QLOGICFAS {						\
	.detect         		= qlogicfas_detect,	\
	.info           		= qlogicfas_info,		\
	.command     			= qlogicfas_command, 	\
	.queuecommand			= qlogicfas_queuecommand,	\
	.eh_abort_handler          	= qlogicfas_abort,	\
	.eh_bus_reset_handler		= qlogicfas_bus_reset,	\
	.eh_device_reset_handler        = qlogicfas_device_reset,	\
	.eh_host_reset_handler          = qlogicfas_host_reset,	\
	.bios_param     = qlogicfas_biosparam,			\
	.can_queue      = 0,					\
	.this_id        = -1,					\
	.sg_tablesize   = SG_ALL,					\
	.cmd_per_lun    = 1,					\
	.use_clustering = DISABLE_CLUSTERING			\
}
#endif /* _QLOGICFAS_H */



