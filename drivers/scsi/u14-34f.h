/*
 *   u14-34f.h - used by the low-level driver for UltraStor 14F/34F
 */

static int u14_34f_detect(Scsi_Host_Template *);
static int u14_34f_release(struct Scsi_Host *);
static int u14_34f_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int u14_34f_eh_abort(Scsi_Cmnd *);
static int u14_34f_eh_host_reset(Scsi_Cmnd *);
static int u14_34f_bios_param(struct scsi_device *, struct block_device *,
                              sector_t, int *);
static int u14_34f_slave_configure(Scsi_Device *);

#define U14_34F_VERSION "8.03.00"
