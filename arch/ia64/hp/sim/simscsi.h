/*
 * Simulated SCSI driver.
 *
 * Copyright (C) 1999, 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#ifndef SIMSCSI_H
#define SIMSCSI_H

#define SIMSCSI_REQ_QUEUE_LEN	64

#define DEFAULT_SIMSCSI_ROOT	"/var/ski-disks/sd"

extern int simscsi_detect (Scsi_Host_Template *);
extern int simscsi_release (struct Scsi_Host *);
extern const char *simscsi_info (struct Scsi_Host *);
extern int simscsi_queuecommand (Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
extern int simscsi_abort (Scsi_Cmnd *);
extern int simscsi_reset (Scsi_Cmnd *, unsigned int);
extern int simscsi_biosparam (struct scsi_device *, struct block_device *,
		sector_t, int[]);

#endif /* SIMSCSI_H */
