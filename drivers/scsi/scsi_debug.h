#ifndef _SCSI_DEBUG_H

#include <linux/types.h>

static int scsi_debug_slave_alloc(struct scsi_device *);
static int scsi_debug_slave_configure(struct scsi_device *);
static void scsi_debug_slave_destroy(struct scsi_device *);
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
static int scsi_debug_release(struct Scsi_Host *);

/*
 * This driver is written for the lk 2.5 series
 */
#define SCSI_DEBUG_CANQUEUE  255 	/* needs to be >= 1 */

#define SCSI_DEBUG_MAX_CMD_LEN 16

static Scsi_Host_Template sdebug_driver_template = {
	.proc_info =		scsi_debug_proc_info,
	.name =			"SCSI DEBUG",
	.release =		scsi_debug_release,
	.info =			scsi_debug_info,
	.slave_alloc =		scsi_debug_slave_alloc,
	.slave_configure =	scsi_debug_slave_configure,
	.slave_destroy =	scsi_debug_slave_destroy,
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
	.max_sectors =		4096,
	.unchecked_isa_dma = 	0,
	.use_clustering = 	ENABLE_CLUSTERING,
	.module =		THIS_MODULE,
};

#endif
