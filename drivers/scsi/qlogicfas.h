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
static int qlogicfas_biosparam(struct scsi_disk *, struct block_device *,
			       sector_t, int[]);
}

#endif /* _QLOGICFAS_H */



