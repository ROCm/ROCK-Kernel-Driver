#ifndef _SCSI_DEBUG_H

#include <linux/types.h>
#include <linux/kdev_t.h>

static int scsi_debug_detect(struct SHT *);
static int scsi_debug_slave_attach(struct scsi_device *);
static void scsi_debug_slave_detach(struct scsi_device *);
static int scsi_debug_release(struct Scsi_Host *);
/* static int scsi_debug_command(struct scsi_cmnd *); */
static int scsi_debug_queuecommand(struct scsi_cmnd *, 
				   void (*done) (struct scsi_cmnd *));
static int scsi_debug_ioctl(struct scsi_device *, int, void *);
static int scsi_debug_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int[]);
static int scsi_debug_abort(struct scsi_cmnd *);
static int scsi_debug_bus_reset(struct scsi_cmnd *);
static int scsi_debug_device_reset(struct scsi_cmnd *);
static int scsi_debug_host_reset(struct scsi_cmnd *);
static int scsi_debug_proc_info(char *, char **, off_t, int, int, int);
static const char * scsi_debug_info(struct Scsi_Host *);

#ifndef NULL
#define NULL 0
#endif

/*
 * This driver is written for the lk 2.5 series
 */
#define SCSI_DEBUG_CANQUEUE  255 	/* needs to be >= 1 */

#define SCSI_DEBUG_MAX_CMD_LEN 16

static Scsi_Host_Template driver_template = {
	.proc_info =		scsi_debug_proc_info,
	.name =			"SCSI DEBUG",
	.info =			scsi_debug_info,
	.detect =		scsi_debug_detect,
	.slave_attach =		scsi_debug_slave_attach,
	.slave_detach =		scsi_debug_slave_detach,
	.release =		scsi_debug_release,
	.ioctl =		scsi_debug_ioctl,
	.queuecommand =		scsi_debug_queuecommand,
	.eh_abort_handler =	scsi_debug_abort,
	.eh_bus_reset_handler = scsi_debug_bus_reset,
	.eh_device_reset_handler = scsi_debug_device_reset,
	.eh_host_reset_handler = scsi_debug_host_reset,
	.bios_param =		scsi_debug_biosparam,
	.can_queue =		SCSI_DEBUG_CANQUEUE,
	.this_id =		7,
	.sg_tablesize =		64,
	.cmd_per_lun =		3,
	.unchecked_isa_dma = 	0,
	.use_clustering = 	ENABLE_CLUSTERING,
};	/* the name 'driver_template' is used by scsi_module.c */

#endif
