#ifndef _SCSI_SCSI_EH_H
#define _SCSI_SCSI_EH_H

extern void scsi_add_timer(struct scsi_cmnd *, int,
			   void (*)(struct scsi_cmnd *));
extern int scsi_delete_timer(struct scsi_cmnd *);
extern void scsi_report_bus_reset(struct Scsi_Host *, int);
extern void scsi_report_device_reset(struct Scsi_Host *, int, int);
extern int scsi_block_when_processing_errors(struct scsi_device *);
extern void scsi_sleep(int);

/*
 * Reset request from external source
 */
#define SCSI_TRY_RESET_DEVICE	1
#define SCSI_TRY_RESET_BUS	2
#define SCSI_TRY_RESET_HOST	3

extern int scsi_reset_provider(struct scsi_device *, int);

#endif /* _SCSI_SCSI_EH_H */
