/*
 * We should not even be trying to compile this if we are not doing
 * a module.
 */
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/fs.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include <scsi/scsi_driver.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsicam.h>
#include "scsi.h"

#include "scsi_logging.h"


/*
 * This source file contains the symbol table used by scsi loadable
 * modules.
 */
EXPORT_SYMBOL(scsi_register_driver);
EXPORT_SYMBOL(scsi_register_interface);
EXPORT_SYMBOL(scsi_host_alloc);
EXPORT_SYMBOL(scsi_add_host);
EXPORT_SYMBOL(scsi_scan_host);
EXPORT_SYMBOL(scsi_remove_host);
EXPORT_SYMBOL(scsi_host_get);
EXPORT_SYMBOL(scsi_host_put);
EXPORT_SYMBOL(scsi_host_lookup);
EXPORT_SYMBOL(scsi_register);
EXPORT_SYMBOL(scsi_unregister);
EXPORT_SYMBOL(scsicam_bios_param);
EXPORT_SYMBOL(scsi_partsize);
EXPORT_SYMBOL(scsi_bios_ptable);
EXPORT_SYMBOL(scsi_ioctl);
EXPORT_SYMBOL(scsi_print_command);
EXPORT_SYMBOL(__scsi_print_command);
EXPORT_SYMBOL(scsi_print_sense);
EXPORT_SYMBOL(scsi_print_req_sense);
EXPORT_SYMBOL(scsi_print_msg);
EXPORT_SYMBOL(scsi_print_status);
EXPORT_SYMBOL(scsi_sense_key_string);
EXPORT_SYMBOL(scsi_extd_sense_format);
EXPORT_SYMBOL(kernel_scsi_ioctl);
EXPORT_SYMBOL(scsi_block_when_processing_errors);
EXPORT_SYMBOL(scsi_ioctl_send_command);
EXPORT_SYMBOL(scsi_set_medium_removal);
#if defined(CONFIG_SCSI_LOGGING)	/* { */
EXPORT_SYMBOL(scsi_logging_level);
#endif

EXPORT_SYMBOL(scsi_allocate_request);
EXPORT_SYMBOL(scsi_release_request);
EXPORT_SYMBOL(scsi_wait_req);
EXPORT_SYMBOL(scsi_do_req);
EXPORT_SYMBOL(scsi_get_command);
EXPORT_SYMBOL(scsi_put_command);

EXPORT_SYMBOL(scsi_report_bus_reset);
EXPORT_SYMBOL(scsi_report_device_reset);
EXPORT_SYMBOL(scsi_block_requests);
EXPORT_SYMBOL(scsi_unblock_requests);
EXPORT_SYMBOL(scsi_adjust_queue_depth);
EXPORT_SYMBOL(scsi_track_queue_full);

EXPORT_SYMBOL(scsi_get_host_dev);
EXPORT_SYMBOL(scsi_free_host_dev);

EXPORT_SYMBOL(scsi_sleep);

EXPORT_SYMBOL(scsi_io_completion);

EXPORT_SYMBOL(scsi_add_device);
EXPORT_SYMBOL(scsi_remove_device);
EXPORT_SYMBOL(scsi_device_cancel);

EXPORT_SYMBOL(__scsi_mode_sense);
EXPORT_SYMBOL(scsi_mode_sense);

/*
 * This symbol is for the highlevel drivers (e.g. sg) only.
 */
EXPORT_SYMBOL(scsi_reset_provider);

EXPORT_SYMBOL(scsi_device_types);

/*
 * This is for st to find the bounce limit
 */
EXPORT_SYMBOL(scsi_calculate_bounce_limit);

/*
 * Externalize timers so that HBAs can safely start/restart commands.
 */
EXPORT_SYMBOL(scsi_add_timer);
EXPORT_SYMBOL(scsi_delete_timer);
