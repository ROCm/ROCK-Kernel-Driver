#ifndef _I2O_SCSI_H
#define _I2O_SCSI_H

#include <linux/types.h>
#include <linux/kdev_t.h>

#define I2O_SCSI_ID 15
#define I2O_SCSI_CAN_QUEUE 4
#define I2O_SCSI_CMD_PER_LUN 6

static int i2o_scsi_detect(Scsi_Host_Template *);
static const char *i2o_scsi_info(struct Scsi_Host *);
static int i2o_scsi_command(Scsi_Cmnd *);
static int i2o_scsi_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int i2o_scsi_abort(Scsi_Cmnd *);
static int i2o_scsi_bus_reset(Scsi_Cmnd *);
static int i2o_scsi_host_reset(Scsi_Cmnd *);
static int i2o_scsi_device_reset(Scsi_Cmnd *);
static int i2o_scsi_bios_param(Disk *, struct block_device *, int *);
static int i2o_scsi_release(struct Scsi_Host *host);

#define I2OSCSI {                                       		\
		  next: NULL,						\
                  proc_name:         		"i2o_scsi",   		\
                  name:              		"I2O SCSI Layer", 	\
                  detect:            		i2o_scsi_detect,	\
                  release:	     		i2o_scsi_release,	\
                  info:              		i2o_scsi_info,		\
                  command:           		i2o_scsi_command,	\
                  queuecommand:      		i2o_scsi_queuecommand,	\
                  eh_abort_handler:             i2o_scsi_abort,		\
                  eh_bus_reset_handler:         i2o_scsi_bus_reset,	\
                  eh_device_reset_handler:      i2o_scsi_device_reset,	\
                  eh_host_reset_handler:	i2o_scsi_host_reset,	\
  		  bios_param:        i2o_scsi_bios_param,   		\
                  can_queue:         I2O_SCSI_CAN_QUEUE,    		\
                  this_id:           I2O_SCSI_ID,           		\
                  sg_tablesize:      8,                     		\
                  cmd_per_lun:       I2O_SCSI_CMD_PER_LUN,  		\
                  unchecked_isa_dma: 0,                     		\
                  use_clustering:    ENABLE_CLUSTERING     		\
                  }

#endif
