/*
 *        eata.h - used by the low-level driver for EATA/DMA SCSI host adapters.
 */

static int eata2x_detect(Scsi_Host_Template *);
static int eata2x_release(struct Scsi_Host *);
static int eata2x_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int eata2x_eh_abort(Scsi_Cmnd *);
static int eata2x_eh_host_reset(Scsi_Cmnd *);
static int eata2x_bios_param(struct scsi_device *, struct block_device *,
                             sector_t, int *);
static int eata2x_slave_configure(Scsi_Device *);

#define EATA_VERSION "8.03.00"

