/*
 * ipr.c -- driver for IBM Power Linux RAID adapters
 *
 * Written By: Brian King, IBM Corporation
 *
 * Copyright (C) 2003 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*
 * Notes:
 *
 * This driver is used to control the following SCSI adapters:
 *
 * IBM iSeries: 5702, 5703, 2780, 5709, 570A, 570B
 *
 * IBM pSeries: PCI-X Dual Channel Ultra 320 SCSI RAID Adapter
 *              PCI-X Dual Channel Ultra 320 SCSI Adapter
 *              PCI-X Dual Channel Ultra 320 SCSI RAID Enablement Card
 *              Embedded SCSI adapter on p615 and p655 systems
 *
 * Supported Hardware Features:
 *	- Ultra 320 SCSI controller
 *	- PCI-X host interface
 *	- Embedded PowerPC RISC Processor and Hardware XOR DMA Engine
 *	- Non-Volatile Write Cache
 *	- Supports attachment of non-RAID disks, tape, and optical devices
 *	- RAID Levels 0, 5, 10
 *	- Hot spare
 *	- Background Parity Checking
 *	- Background Data Scrubbing
 *	- Ability to increase the capacity of an existing RAID 5 disk array
 *		by adding disks
 *
 * Driver Features:
 *	- Tagged command queuing
 *	- Adapter microcode download
 *	- PCI hot plug
 *	- SCSI device hot plug
 *
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/kdev_t.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/pci_ids.h>
#include <linux/ctype.h>
#include <linux/devfs_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/semaphore.h>
#include <asm/page.h>
#ifdef CONFIG_COMPAT
#include <linux/ioctl32.h>
#endif
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_cmnd.h>
#include "ipr.h"

/*
 *  Function Prototypes
 */
int ipr_biosparam(struct scsi_device *, struct block_device *, sector_t, int *);
const char * ipr_ioa_info(struct Scsi_Host *);
int ipr_release(struct Scsi_Host *);
int ipr_eh_abort(struct scsi_cmnd *);
int ipr_eh_dev_reset(struct scsi_cmnd *);
int ipr_eh_host_reset(struct scsi_cmnd *);
int ipr_slave_alloc(struct scsi_device *);
int ipr_slave_configure(struct scsi_device *);
void ipr_slave_destroy(struct scsi_device *);
int ipr_queue(struct scsi_cmnd *, void (*done) (struct scsi_cmnd *));
int ipr_task_thread(void *);
irqreturn_t ipr_isr(int, void *, struct pt_regs *);
static int __devinit ipr_probe_ioa_part2(struct ipr_ioa_cfg *);
static void ipr_initiate_ioa_reset(struct ipr_ioa_cfg *,
				   enum ipr_shutdown_type);
static void ipr_initiate_ioa_bringdown(struct ipr_ioa_cfg *,
				       enum ipr_shutdown_type);
static void ipr_reset_ioa_job(struct ipr_cmnd *);
static void ipr_reset_start_timer(struct ipr_cmnd *,
				  unsigned long);
static void ipr_reset_timer_done(struct ipr_cmnd *);
static int ipr_reset_shutdown_ioa(struct ipr_cmnd *);
static int ipr_reset_alert(struct ipr_cmnd *);
static int ipr_reset_wait_to_start_bist(struct ipr_cmnd *);
static int ipr_reset_start_bist(struct ipr_cmnd *);
static int ipr_reset_restore_cfg_space(struct ipr_cmnd *);
static int ipr_reset_wait_for_dump(struct ipr_cmnd *);
static int ipr_reset_enable_ioa(struct ipr_cmnd *);
static int ipr_ioafp_indentify_hrrq(struct ipr_cmnd *);
static void ipr_inquiry(struct ipr_cmnd *, u8, u8, u32, u8);
static int ipr_ioafp_std_inquiry(struct ipr_cmnd *);
static int ipr_ioafp_page0_inquiry(struct ipr_cmnd *);
static int ipr_ioafp_inq_page_supp(struct ipr_ioa_cfg *, u8);
static int ipr_ioafp_page3_inquiry(struct ipr_cmnd *);
static int ipr_ioafp_query_ioa_cfg(struct ipr_cmnd *);
static void ipr_init_res_entry(struct ipr_resource_entry *);
static int ipr_init_res_table(struct ipr_cmnd *);
static int ipr_ioafp_mode_sense_page28(struct ipr_cmnd *);
static int ipr_ioafp_mode_select_page28(struct ipr_cmnd *);
static int ipr_init_devices(struct ipr_cmnd *);
static int ipr_wait_for_dev_init_done(struct ipr_cmnd *ipr_cmd);
static int ipr_ioa_reset_done(struct ipr_cmnd *);
static int ipr_ioa_bringdown_done(struct ipr_cmnd *);
static void ipr_shutdown(struct device *dev);
static u32 ipr_shutdown_ioa(struct ipr_ioa_cfg *);
static void ipr_send_hcam(struct ipr_ioa_cfg *, u8,
			  struct ipr_hostrcb *);
static void ipr_process_ccn(struct ipr_cmnd *);
static void ipr_process_error(struct ipr_cmnd *);
static void ipr_handle_log_data(struct ipr_ioa_cfg *, struct ipr_hostrcb *);
static int ipr_open(struct inode *, struct file *);
static int ipr_close(struct inode *, struct file *);
static int ipr_ioctl(struct inode *, struct file *,
		     unsigned int, unsigned long);
static void ipr_get_unit_check_buffer(struct ipr_ioa_cfg *);
static void ipr_unit_check_no_data(struct ipr_ioa_cfg *);
static void ipr_worker_thread(void *data);
static void ipr_erp_start(struct ipr_ioa_cfg *, struct ipr_cmnd *);
static void ipr_erp_cancel_all(struct ipr_cmnd *);
static void ipr_erp_request_sense(struct ipr_cmnd *);
static void ipr_erp_done(struct ipr_cmnd *);
static u16 ipr_adjust_urc(u32, struct ipr_res_addr, u32, int, char *);
static void ipr_do_req(struct ipr_cmnd *, void (*) (struct ipr_cmnd *),
		       void (*) (struct ipr_cmnd *), u32);
static void ipr_timeout(struct ipr_cmnd *);
static int ipr_get_ioa_smart_dump(struct ipr_ioa_cfg *);
static void ipr_remove(struct pci_dev *pdev);
static void __exit ipr_exit(void);
static int __devinit ipr_alloc_mem(struct ipr_ioa_cfg *);
static void ipr_dev_init(struct ipr_ioa_cfg *, struct ipr_resource_entry *,
			 struct ipr_hostrcb *);
static void ipr_af_init(struct ipr_ioa_cfg *, struct ipr_resource_entry *,
			struct ipr_hostrcb *, struct ipr_cmnd *);
static void ipr_vset_init_job(struct ipr_cmnd *);
static int ipr_start_unit(struct ipr_cmnd *);
static int ipr_vset_init_done(struct ipr_cmnd *);
static void ipr_dasd_init_job(struct ipr_cmnd *);
static int ipr_std_inquiry(struct ipr_cmnd *);
static int ipr_set_supported_devs(struct ipr_cmnd *);
static int ipr_set_dasd_timeouts(struct ipr_cmnd *);
static void ipr_build_query_res_state(struct ipr_resource_entry *,
				      struct ipr_cmnd *, u32);
static int ipr_query_res_state(struct ipr_cmnd *);
static void ipr_build_mode_sense(struct ipr_cmnd *,
				 u32, u8, u32, u8);
static void ipr_mode_sense(struct ipr_cmnd *, struct ipr_resource_entry *,
			   u8, u32, u8);
static u32 ipr_blocking_mode_sense(struct ipr_ioa_cfg *,
				   struct ipr_resource_entry *,
				   u8, u32, u8);
static int ipr_mode_sense_pg0x01_cur(struct ipr_cmnd *);
static int ipr_mode_sense_pg0x01_changeable(struct ipr_cmnd *);
static void ipr_build_mode_select(struct ipr_cmnd *,
			    u32, u8, u32, u8);
static void ipr_mode_select(struct ipr_cmnd *, struct ipr_resource_entry *,
			    u8, u32, u8);
static u32 ipr_blocking_mode_select(struct ipr_ioa_cfg *,
				    struct ipr_resource_entry *,
				    u8, u32, u8);
static int ipr_mode_select_pg0x01(struct ipr_cmnd *);
static int ipr_mode_sense_pg0x0a_cur(struct ipr_cmnd *);
static int ipr_mode_sense_pg0x0a_changeable(struct ipr_cmnd *);
static int ipr_mode_select_pg0x0a(struct ipr_cmnd *);
static int ipr_mode_sense_pg0x20(struct ipr_cmnd *);
static int ipr_mode_select_pg0x20(struct ipr_cmnd *);
static int ipr_dev_init_done(struct ipr_cmnd *);
static u8 ipr_set_page0x0a(struct ipr_mode_pages *, struct ipr_mode_pages *);
static int __devinit ipr_probe(struct pci_dev *pdev,
			       const struct pci_device_id *dev_id);

/*
 *   Global Data
 */
static struct list_head ipr_ioa_head;
static int ipr_verbose = IPR_DEFAULT_DEBUG_LEVEL;
static int ipr_unsafe = 1;		/* xxx set to 0 for ship */
static int ipr_disable_tcq = 0;
static int ipr_safe_settings = 0;
static spinlock_t ipr_driver_lock;
static int ipr_major = 0;
static unsigned long ipr_minors[IPR_NUM_MINORS / BITS_PER_LONG];

/**
 * ipr_show_fw_version - Show the firmware version
 * @class_dev:	class device struct
 * @buf:		buffer
 * 
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ipr_show_fw_version(struct class_device *class_dev, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(class_dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;
	struct ipr_inquiry_page3 *ucode_vpd = &ioa_cfg->vpd_cbs->page3_data;

	return snprintf(buf, 20, "%02X%02X%02X%02X\n",
			ucode_vpd->major_release, ucode_vpd->card_type,
			ucode_vpd->minor_release[0],
			ucode_vpd->minor_release[1]);
}

/**
 * ipr_show_dev - Show the adapter's major/minor numbers
 * @class_dev:	class device struct
 * @buf:		buffer
 * 
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_show_dev(struct class_device *class_dev, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(class_dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;

	return snprintf(buf, 20, "%d:%d\n", ipr_major, ioa_cfg->minor_num);
}

/**
 * ipr_show_major - Show the adapter type
 * @class_dev:	class device struct
 * @buf:		buffer
 * 
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_show_type(struct class_device *class_dev, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(class_dev);
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)shost->hostdata;

	return snprintf(buf, 20, "%X\n", ioa_cfg->type);
}

static struct class_device_attribute ipr_fw_version_attr = {
	.attr = {
		.name =		"fw_version",
		.mode =		S_IRUGO,
	},
	.show = ipr_show_fw_version,
};

static struct class_device_attribute ipr_dev_attr = {
	.attr = {
		.name =		"dev",
		.mode =		S_IRUGO,
	},
	.show = ipr_show_dev,
};

static struct class_device_attribute ipr_type_attr = {
	.attr = {
		.name =		"type",
		.mode =		S_IRUGO,
	},
	.show = ipr_show_type,
};

static struct class_device_attribute *ipr_ioa_attrs[] = {
	&ipr_fw_version_attr,
	&ipr_dev_attr,
	&ipr_type_attr,
	NULL,
};

static struct scsi_host_template driver_template = {
	.name = "IPR",
	.info = ipr_ioa_info,
	.queuecommand = ipr_queue,
	.eh_abort_handler = ipr_eh_abort,
	.eh_device_reset_handler = ipr_eh_dev_reset,
	.eh_host_reset_handler = ipr_eh_host_reset,
	.slave_alloc = ipr_slave_alloc,
	.slave_configure = ipr_slave_configure,
	.slave_destroy = ipr_slave_destroy,
	.bios_param = ipr_biosparam,
	.can_queue = IPR_MAX_COMMANDS,
	.this_id = -1,
	.sg_tablesize = IPR_MAX_SGLIST,
	.max_sectors = IPR_MAX_SECTORS,
	.cmd_per_lun = IPR_MAX_CMD_PER_LUN,
	.use_clustering = ENABLE_CLUSTERING,
	.shost_attrs = ipr_ioa_attrs,
	.proc_name = IPR_NAME
};

/**
 * ipr_version_show - Show the driver version
 * @dd:	device driver struct
 * @buf:	buffer
 * 
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ipr_version_show(struct device_driver *dd, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", IPR_DRIVER_VERSION);
}

static DRIVER_ATTR(version, S_IRUGO, ipr_version_show, NULL);

/* This table describes the differences between DMA controller chips */
static const struct ipr_chip_cfg_t ipr_chip_cfg[] = { 
	{ /* Gemstone */
		.mailbox = 0x0042C,
		.cache_line_size = 0x20,
		{
			.set_interrupt_mask_reg = 0x0022C,
			.clr_interrupt_mask_reg = 0x00230,
			.sense_interrupt_mask_reg = 0x0022C,
			.clr_interrupt_reg = 0x00228,
			.sense_interrupt_reg = 0x00224,
			.ioarrin_reg = 0x00404,
			.sense_uproc_interrupt_reg = 0x00214,
			.set_uproc_interrupt_reg = 0x00214,
			.clr_uproc_interrupt_reg = 0x00218
		}
	},
	{ /* Snipe */
		.mailbox = 0x0052C,
		.cache_line_size = 0x20,
		{
			.set_interrupt_mask_reg = 0x00288,
			.clr_interrupt_mask_reg = 0x0028C,
			.sense_interrupt_mask_reg = 0x00288,
			.clr_interrupt_reg = 0x00284,
			.sense_interrupt_reg = 0x00280,
			.ioarrin_reg = 0x00504,
			.sense_uproc_interrupt_reg = 0x00290,
			.set_uproc_interrupt_reg = 0x00290,
			.clr_uproc_interrupt_reg = 0x00294
		}
	},
};

static const struct pci_device_id ipr_pci_table[] __devinitdata = {
	{ PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_IBM_GEMSTONE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_5702,
		0, 0, (kernel_ulong_t)&ipr_chip_cfg[0] },
	{ PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_IBM_GEMSTONE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_5703,
	      0, 0, (kernel_ulong_t)&ipr_chip_cfg[0] },
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_SNIPE,
		PCI_VENDOR_ID_IBM, IPR_SUBS_DEV_ID_2780,
		0, 0, (kernel_ulong_t)&ipr_chip_cfg[1] },
	{ }
};
MODULE_DEVICE_TABLE(pci, ipr_pci_table);

static struct pci_driver ipr_driver = {
	.name = IPR_NAME,
	.id_table = ipr_pci_table,
	.probe = ipr_probe,
	.remove = ipr_remove,
	.driver = {
		.shutdown = ipr_shutdown,
	},
};

MODULE_AUTHOR("Brian King <brking@us.ibm.com>");
module_param_named(disable_tcq, ipr_disable_tcq, int, 0);
MODULE_PARM_DESC(disable_tcq, "Set to 1 to disable tagged command queuing");
module_param_named(safe, ipr_safe_settings, int, 0);
MODULE_PARM_DESC(safe, "Use safe settings (slow)");
module_param_named(verbose, ipr_verbose, int, 0);
MODULE_PARM_DESC(verbose, "Set to 0 - 4 for increasing verbosity of device driver");
module_param_named(unsafe, ipr_unsafe, int, 0);
MODULE_PARM_DESC(unsafe, "Do not use!");
MODULE_LICENSE("GPL");

static const char ipr_version[] = {"ipr version=" IPR_DRIVER_VERSION};

static struct file_operations ipr_fops = {
	.ioctl = ipr_ioctl,
	.open = ipr_open,
	.release = ipr_close,
};

unsigned int ipr_ioctls[] =
{
	IPR_IOCTL_PASSTHRU,
	IPR_IOCTL_RUN_DIAGNOSTICS,
	IPR_IOCTL_DUMP_IOA,
	IPR_IOCTL_RESET_IOA,
	IPR_IOCTL_READ_DRIVER_CFG,
	IPR_IOCTL_WRITE_DRIVER_CFG,
	IPR_IOCTL_GET_BUS_CAPABILTIES,
	IPR_IOCTL_SET_BUS_ATTRIBUTES,
	IPR_IOCTL_GET_TRACE,
	IPR_IOCTL_RECLAIM_CACHE,
	IPR_IOCTL_QUERY_CONFIGURATION,
	IPR_IOCTL_UCODE_DOWNLOAD,
	IPR_IOCTL_CHANGE_ADAPTER_ASSIGNMENT
};

static const char *ipr_gpdd_dev_end_states[] = {
	"Command complete",
	"Terminated by host",
	"Terminated by device reset",
	"Terminated by bus reset",
	"Unknown",
	"Command not started"
};

static const char *ipr_gpdd_dev_bus_phases[] = {
	"Bus free",
	"Arbitration",
	"Selection",
	"Message out",
	"Command",
	"Message in",
	"Data out",
	"Data in",
	"Status",
	"Reselection",
	"Unknown"
};

/*  A constant array of IOASCs/URCs/Error Messages */
static const
struct ipr_error_table_t ipr_error_table[] = {
	{0x00000000, 0x8155, 0x8155, "Permanent", 1,
	"An unknown error was received"},
	{0x00330000, 0x0000, 0x0000, "None", 1,
	"Soft underlength error"},
	{0x005A0000, 0x0000, 0x0000, "None", 1,
	"Command to be cancelled not found"},
	{0x00808000, 0x0000, 0x0000, "None", 1,
	"Qualified success"},
	{0x01080000, 0xFFFE, 0x8140, "Statistical", 1,
	"Soft device bus error recovered by the IOA."},
	{0x01170600, 0xFFF9, 0x8141, "Temporary", 1,
	"Device sector reassign successful"},
	{0x01170900, 0xFFF7, 0x8141, "Temporary", 1,
	"Media error recovered by device rewrite procedures."},
	{0x01180200, 0x7001, 0x8141, "Statistical", 1,
	"IOA sector reassignment successful."},
	{0x01180500, 0xFFF9, 0x8141, "Statistical", 1,
	"Soft media error. Sector reassignment recommended."},
	{0x01180600, 0xFFF7, 0x8141, "Temporary", 1,
	"Media error recovered by IOA rewrite procedures."},
	{0x01418000, 0x0000, 0xFF3D, "Statistical", 1,
	"Soft PCI bus error recovered by the IOA."},
	{0x01430000, 0x0000, 0x0000, "None", 1,
	"Unsolicited device bus message received."},
	{0x01440000, 0xFFF6, 0x8141, "Statistical", 1,
	"Device hardware error recovered by the IOA."},
	{0x01448100, 0xFFF6, 0x8141, "Statistical", 1,
	"Device hardware error recovered by the device."},
	{0x01448200, 0x0000, 0xFF3D, "Statistical", 1,
	"Soft IOA error recovered by the IOA."},
	{0x01448300, 0xFFFA, 0x8141, "Statistical", 1,
	"Undefined device response recovered by the IOA."},
	{0x014A0000, 0xFFF6, 0x8141, "Statistical", 1,
	"Device bus error, message or command phase."},
	{0x015D0000, 0xFFF6, 0x8145, "Threshold", 1,
	"Failure prediction threshold exceeded."},
	{0x015D9200, 0x0000, 0x8009, "Threshold", 1,
	"Impending cache battery pack failure."},
	{0x02040400, 0x0000, 0x34FF, "Informational", 1,
	"Disk device format in progress."},
	{0x023F0000, 0x0000, 0x0000, "None", 0,
	"Synchronization required."},
	{0x02448530, 0x0000, 0x0000, "None", 0,
	"ACA active"},
	{0x02670100, 0x3020, 0x3400, "Permanent", 1,
	"Storage subsystem configuration error."},
	{0x03110B00, 0xFFF5, 0x3400, "Permanent", 1,
	"Medium error, data unreadable, recommend reassign."},
	{0x03110C00, 0x0000, 0x0000, "Permanent", 1,
	"Medium error, data unreadable, do not reassign."},	/* 0x7000, 0x3400 */
	{0x03310000, 0xFFF3, 0x3400, "Permanent", 1,
	"Disk media format bad."},
	{0x04050000, 0x3002, 0x3400, "Recoverable", 0,
	"Addressed device failed to respond to selection."},
	{0x04080000, 0x3100, 0x3100, "Permanent", 1,
	"Device bus error."},
	{0x04080100, 0x3109, 0x3400, "Recoverable", 1,
	"IOA timed out a device command."},
	{0x04088000, 0x0000, 0x3120, "Permanent", 1,
	"SCSI bus is not operational."},
	{0x04118000, 0x0000, 0x9000, "Permanent", 1,
	"IOA reserved area data check"},
	{0x04118100, 0x0000, 0x9001, "Permanent", 1,
	"IOA reserved area invalid data pattern."},
	{0x04118200, 0x0000, 0x9002, "Permanent", 1,
	"IOA reserved area LRC error."},
	{0x04320000, 0x102E, 0x3400, "Permanent", 1,
	"Out of alternate sectors for disk storage."},
	{0x04330000, 0xFFF4, 0x3400, "Permanent", 1,
	"Data transfer underlength error."},
	{0x04338000, 0xFFF4, 0x3400, "Permanent", 1,
	"Data transfer overlength error."},
	{0x043E0100, 0x0000, 0x3400, "Permanent", 1,
	"Logical unit failure."},
	{0x04408500, 0xFFF4, 0x3400, "Permanent", 1,
	"Device microcode is corrupt."},
	{0x04430000, 0x0000, 0x0000, "None", 0,
	"Unsupported device bus message received."},
	{0x04418000, 0x0000, 0x8150, "Permanent", 1,
	"PCI bus error."},
	{0x04440000, 0xFFF4, 0x3400, "Permanent", 1,
	"Disk device problem."},
	{0x04448200, 0x0000, 0x8150, "Permanent", 1,
	"Permanent IOA failure."},
	{0x04448300, 0x3010, 0x3400, "Permanent", 1,
	"Disk device returned wrong response to IOA."},
	{0x04448400, 0x0000, 0x8151, "Permanent", 1,
	"IOA microcode error"},
	{0x04448500, 0x0000, 0x0000, "None", 0,
	"Device bus status error"},
	{0x04448600, 0x0000, 0x8157, "Permanent", 1,
	"IOA error requiring IOA reset to recover"},
	{0x04490000, 0x0000, 0x0000, "None", 0,
	"Message reject received from the device."},
	{0x04449200, 0x0000, 0x8008, "Permanent", 1,
	"A permanent cache battery pack failure occurred"},
	{0x0444A000, 0x0000, 0x9090, "Permanent", 1,
	"Disk unit has been modified after the last known status"},
	{0x0444A200, 0x0000, 0x9081, "Permanent", 1,
	"IOA detected device error"},
	{0x0444A300, 0x0000, 0x9082, "Permanent", 1,
	"IOA detected device error"},
	{0x044A0000, 0x3110, 0x3400, "Permanent", 1,
	"Device bus error, message or command phase."},
	{0x04670400, 0x0000, 0x9091, "Permanent", 1,
	"Incorrect hardware configuration change has been detected."},
	{0x046E0000, 0xFFF4, 0x3400, "Permanent", 1,
	"Command to logical unit failed."},
	{0x05240000, 0x0000, 0x0000, "None", 1,
	"Illegal request, invalid request type or request packet."},
	{0x05250000, 0x0000, 0x0000, "None", 0,
	"Illegal request, invalid resource handle."},
	{0x06040500, 0x0000, 0x9031, "Temporary", 1,
	"Array protection temporarily suspended, protection resuming."},
	{0x06040600, 0x0000, 0x9040, "Temporary", 1,
	"Array protection temporarily suspended, protection resuming."},
	{0x060A8000, 0x0000, 0x0000, "Permanent", 1,
	"Not applicable."},
	{0x06288000, 0x0000, 0x3140, "Informational", 1,
	"SCSI bus is not operational."},
	{0x06290000, 0xFFFB, 0x3400, "Informational", 1,
	"SCSI bus was reset."},
	{0x06290500, 0xFFFE, 0x8140, "Informational", 1,
	"SCSI bus transition to single ended."},
	{0x06290600, 0xFFFE, 0x8140, "Informational", 1,
	"SCSI bus transition to LVD."},
	{0x06298000, 0xFFFB, 0x3400, "Informational", 1,
	"SCSI bus was reset by another initiator."},
	{0x063F0300, 0x3029, 0x3400, "Informational", 1,
	"A device replacement has occurred"},
	{0x064C8000, 0x0000, 0x9051, "Permanent", 1,
	"IOA cache data exists for a missing or failed device"},
	{0x06670100, 0x0000, 0x9025, "Permanent", 1,
	"Disk unit is not supported at its physical location"},
	{0x06670600, 0x0000, 0x3020, "Permanent", 1,
	"IOA detected a SCSI bus configuration error"},
	{0x06678000, 0x0000, 0x3150, "Permanent", 1,
	"SCSI bus configuration error"},
	{0x06690200, 0x0000, 0x9041, "Temporary", 1,
	"Array protection temporarily suspended"},
	{0x066B0200, 0x0000, 0x9030, "Permanent", 1,
	"Array no longer protected due to missing or failed disk unit"},
	{0x06808000, 0x0000, 0x8012, "Permanent", 1,
	"Attached read cache devices exceed capacity supported by IOA"},
	{0x07278000, 0x0000, 0x9008, "Permanent", 1,
	"IOA does not support functions expected by devices"},
	{0x07278100, 0x0000, 0x9010, "Permanent", 1,
	"Cache data associated with attached devices cannot be found"},
	{0x07278200, 0x0000, 0x9011, "Permanent", 1,
	"Cache data belongs to devices other than those attached"},
	{0x07278400, 0x0000, 0x9020, "Permanent", 1,
	"Array not functional due to present hardware configuration."},
	{0x07278500, 0x0000, 0x9021, "Permanent", 1,
	"Array not functional due to present hardware configuration"},
	{0x07278600, 0x0000, 0x9022, "Permanent", 1,
	"Array not functional due to present hardware configuration"},
	{0x07278700, 0x0000, 0x9023, "Permanent", 1,
	"Array member(s) not at required physical locations"},
	{0x07278800, 0x0000, 0x9024, "Permanent", 1,
	"Array not functional due to present hardware configuration"},
	{0x07278900, 0x0000, 0x9026, "Permanent", 1,
	"Array not functional due to present hardware configuration"},
	{0x07278A00, 0x0000, 0x9027, "Permanent", 1,
	"Array not functional due to present hardware configuration"},
	{0x07278B00, 0x0000, 0x9028, "Permanent", 1,
	"Maximum number of arrays already exist."},
	{0x07278C00, 0x0000, 0x9050, "Permanent", 1,
	"Required cache data cannot be located for a disk unit"},
	{0x07278D00, 0x0000, 0x9052, "Permanent", 1,
	"Cache data exists for a device that has been modified"},
	{0x07278E00, 0x0000, 0x9053, "Permanent", 1,
	"IOA resources not available due to previous problems"},
	{0x07278F00, 0x0000, 0x9054, "Permanent", 1,
	"IOA resources not available due to previous problems"},
	{0x07279100, 0x0000, 0x9092, "Permanent", 1,
	"Disk unit requires initialization before use"},
	{0x07279200, 0x0000, 0x9029, "Permanent", 1,
	"Incorrect hardware configuration change has been detected"},
	{0x07279500, 0x0000, 0x9009, "Permanent", 1,
	"Data Protect, device configuration sector is not convertible"},
	{0x07279600, 0x0000, 0x9060, "Permanent", 1,
	"One or more disk pairs are missing from an array"},
	{0x07279700, 0x0000, 0x9061, "Permanent", 1,
	"One or more disks are missing from an array"},
	{0x07279800, 0x0000, 0x9062, "Permanent", 1,
	"One or more disks are missing from an array"},
	{0x07279900, 0x0000, 0x9063, "Permanent", 1,
	"Maximum number of functional arrays has been exceeded"},
	{0x0B260000, 0x0000, 0x0000, "None", 1,
	"Aborted command, invalid descriptor."},
	{0x0B5A0000, 0x0000, 0x0000, "Informational", 1,
	"Command terminated by host"}
};

static const struct ipr_ses_table_entry ipr_ses_table[] = {
	{ "2104-DL1        ", "XXXXXXXXXXXXXXXX", 80 },
	{ "2104-TL1        ", "XXXXXXXXXXXXXXXX", 80 },
	{ "HSBP07M P U2SCSI", "XXXXXXXXXXXXXXXX", 80 }, /* Hidive 7 slot */
	{ "HSBP05M P U2SCSI", "XXXXXXXXXXXXXXXX", 80 }, /* Hidive 5 slot */
	{ "HSBP05M S U2SCSI", "XXXXXXXXXXXXXXXX", 80 }, /* Bowtie */
	{ "HSBP06E ASU2SCSI", "XXXXXXXXXXXXXXXX", 80 }, /* MartinFenning */
	{ "2104-DU3        ", "XXXXXXXXXXXXXXXX", 160 },
	{ "2104-TU3        ", "XXXXXXXXXXXXXXXX", 160 },
	{ "HSBP04C RSU2SCSI", "XXXXXXX*XXXXXXXX", 160 },
	{ "HSBP06E RSU2SCSI", "XXXXXXX*XXXXXXXX", 160 },
	{ "St  V1S2        ", "XXXXXXXXXXXXXXXX", 160 },
	{ "HSBPD4M  PU3SCSI", "XXXXXXX*XXXXXXXX", 160 },
	{ "VSBPD1H   U3SCSI", "XXXXXXX*XXXXXXXX", 160 }
};

/**
 * ipr_sleep_no_lock - Sleep for the specified amount of time
 * @delay:		time to sleep
 *
 * Sleep for the specified amount of time
 * 
 * Return value:
 * 	none
 **/
static void ipr_sleep_no_lock(signed long delay)
{
	DECLARE_WAIT_QUEUE_HEAD(internal_wait);

	sleep_on_timeout(&internal_wait, (delay * HZ) / 1000);
	return;
}

/**
 * ipr_sleep - Sleep for the specified amount of time
 * @ioa_cfg:	ioa config struct
 * @delay:		time to sleep in msecs
 *
 * Release the host lock and sleep for the specified amount of time
 * 
 * Return value:
 * 	none
 **/
static void ipr_sleep(struct ipr_ioa_cfg *ioa_cfg, signed long delay)
{
	spin_unlock_irq(ioa_cfg->host->host_lock);
	ipr_sleep_no_lock(delay);
	spin_lock_irq(ioa_cfg->host->host_lock);
	return;
}

/**
 * ipr_interruptible_sleep_on - Sleep on the wait queue
 * @ioa_cfg:	ioa config struct
 * @wait_head:	wait queue to sleep on
 *
 * Sleep interruptibly on the wait queue.
 * 
 * Return value:
 * 	0 on success / -EINTR if woken due to a signal
 **/
static int ipr_interruptible_sleep_on(struct ipr_ioa_cfg *ioa_cfg,
				      wait_queue_head_t *wait_head)
{
	DECLARE_WAITQUEUE(wait_q_entry, current);

	set_current_state(TASK_INTERRUPTIBLE);

	add_wait_queue(wait_head, &wait_q_entry);

	spin_unlock_irq(ioa_cfg->host->host_lock);
	schedule();
	spin_lock_irq(ioa_cfg->host->host_lock);

	remove_wait_queue(wait_head, &wait_q_entry);

	if (signal_pending(current)) {
		flush_signals(current);
		return -EINTR;
	}

	return 0;
}

/**
 * ipr_sleep_on - Sleep on the wait queue
 * @ioa_cfg:	ioa config struct
 * @wait_head:	wait queue to sleep on
 *
 * Sleep uninterruptibly on the wait queue.
 * 
 * Return value:
 * 	none
 **/
static void ipr_sleep_on(struct ipr_ioa_cfg *ioa_cfg,
			 wait_queue_head_t *wait_head)
{
	wait_queue_t wait_q_entry;

	init_waitqueue_entry(&wait_q_entry, current);

	set_current_state(TASK_UNINTERRUPTIBLE);

	add_wait_queue(wait_head, &wait_q_entry);

	spin_unlock_irq(ioa_cfg->host->host_lock);

	schedule();

	spin_lock_irq(ioa_cfg->host->host_lock);

	remove_wait_queue(wait_head, &wait_q_entry);

	return;
}

/**
 * ipr_block_requests -  Stop the host from issuing new requests.
 * @ioa_cfg:	ioa config struct
 *
 * This function blocks host requests.
 *
 * Return value:
 * 	none
 **/
static void ipr_block_requests(struct ipr_ioa_cfg *ioa_cfg)
{
	ENTER;

	if (0 == ioa_cfg->block_host_ops++) {
		ioa_cfg->allow_cmds = 0;

		/* Stop new requests from coming in */
		scsi_block_requests(ioa_cfg->host);
	}

	LEAVE;
}

/**
 * ipr_unblock_requests - Allow the host to send requests again.
 * @ioa_cfg:	ioa config struct
 *
 * This function unblocks requests.
 *
 * Return value:
 * 	none
 **/
static void ipr_unblock_requests(struct ipr_ioa_cfg *ioa_cfg)
{
	ENTER;

	if (0 == --ioa_cfg->block_host_ops) {
		ioa_cfg->allow_cmds = 1;
		spin_unlock_irq(ioa_cfg->host->host_lock);
		scsi_unblock_requests(ioa_cfg->host);
		spin_lock_irq(ioa_cfg->host->host_lock);
	}

	LEAVE;
}

/**
 * ipr_get_mode_page - Locate specified mode page
 * @mode_pages:	mode page buffer
 * @page_code:	page code to find
 * @len:		minimum required length for mode page
 *
 * Return value:
 * 	pointer to mode page / NULL on failure
 **/
static void *ipr_get_mode_page(struct ipr_mode_pages *mode_pages,
			       u32 page_code, u32 len)
{
	struct ipr_mode_page_hdr *mode_hdr;
	u32 page_length;
	u32 length;

	if (!mode_pages || (mode_pages->hdr.length == 0))
		return NULL;

	length = (mode_pages->hdr.length + 1) - 4 - mode_pages->hdr.block_desc_len;
	mode_hdr = (struct ipr_mode_page_hdr *)
		(mode_pages->data + mode_pages->hdr.block_desc_len);

	while (length) {
		if (mode_hdr->page_code == page_code) {
			if (mode_hdr->page_length >= (len - sizeof(struct ipr_mode_page_hdr)))
				return mode_hdr;
			break;
		} else {
			page_length = (sizeof(struct ipr_mode_page_hdr) +
				       mode_hdr->page_length);
			length -= page_length;
			mode_hdr = (struct ipr_mode_page_hdr *)
				((unsigned long)mode_hdr + page_length);
		}
	}
	return NULL;
}

#ifdef CONFIG_PPC_PSERIES
static const u16 ipr_blocked_processors[] = {
	PV_NORTHSTAR,
	PV_PULSAR,
	PV_POWER4,
	PV_ICESTAR,
	PV_SSTAR,
	PV_POWER4p,
	PV_630,
	PV_630p
};

/**
 * ipr_invalid_adapter - Determine if this adapter is supported on this hardware
 * @ioa_cfg:	ioa cfg struct
 *
 * Adapters that use Gemstone revision < 3.1 do not work reliably on
 * certain pSeries hardware. This function determines if the given
 * adapter is in one of these confgurations or not.
 * 
 * Return value:
 * 	1 if adapter is not supported / 0 if adapter is supported
 **/
static int ipr_invalid_adapter(struct ipr_ioa_cfg *ioa_cfg)
{
	u8 rev_id;
	int i;

	if (ioa_cfg->type == 0x5702) {
		if (pci_read_config_byte(ioa_cfg->pdev, PCI_REVISION_ID,
					 &rev_id) == PCIBIOS_SUCCESSFUL) {
			if (rev_id < 4) {
				for (i = 0; i < ARRAY_SIZE(ipr_blocked_processors); i++){
					if (__is_processor(ipr_blocked_processors[i]))
						return 1;
				}
			}
		}
	}
	return 0;
}
#else
#define ipr_invalid_adapter(ioa_cfg) 0
#endif

/**
 * ipr_trc_hook - Add a trace entry to the driver trace
 * @ipr_cmd:	ipr command struct
 * @type:		trace type
 * @add_data:	additional data
 *
 * Return value:
 * 	none
 **/
static void ipr_trc_hook(struct ipr_cmnd *ipr_cmd,
			 u8 type, u32 add_data)
{
	struct ipr_trace_entry *trace_entry;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	trace_entry = &ioa_cfg->trace[ioa_cfg->trace_index++];
	trace_entry->time = jiffies;
	trace_entry->op_code = ipr_cmd->ioarcb.cmd_pkt.cdb[0];
	trace_entry->type = type;
	trace_entry->cmd_index = ipr_cmd->cmd_index;
	trace_entry->res_handle = ipr_cmd->ioarcb.res_handle;
	trace_entry->add_data = add_data;
}

/**
 * ipr_reinit_ipr_cmnd - Re-initialize an IPR Cmnd block for reuse
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	none
 **/
static void ipr_reinit_ipr_cmnd(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioasa *ioasa = &ipr_cmd->ioasa;

	memset(&ioarcb->cmd_pkt, 0, sizeof(struct ipr_cmd_pkt));
	ioarcb->write_data_transfer_length = 0;
	ioarcb->read_data_transfer_length = 0;
	ioarcb->write_ioadl_len = 0;
	ioarcb->read_ioadl_len = 0;
	ioasa->ioasc = 0;
	ioasa->residual_data_len = 0;

	ipr_cmd->scsi_cmd = NULL;
	ipr_cmd->flags = 0;
	ipr_cmd->sense_buffer[0] = 0;
	ipr_cmd->dma_use_sg = 0;
}

/**
 * ipr_init_ipr_cmnd - Initialize an IPR Cmnd block
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	none
 **/
static void ipr_init_ipr_cmnd(struct ipr_cmnd *ipr_cmd)
{
	ipr_reinit_ipr_cmnd(ipr_cmd);

	init_timer(&ipr_cmd->timer);
	ipr_cmd->parent = NULL;
}

/**
 * ipr_reinit_ipr_cmnd_for_erp - Re-initialize a cmnd block to be used for ERP
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	none
 **/
static void ipr_reinit_ipr_cmnd_for_erp(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioarcb *ioarcb;
	struct ipr_ioasa *ioasa;

	ioarcb = &ipr_cmd->ioarcb;
	ioasa = &ipr_cmd->ioasa;

	memset(&ioarcb->cmd_pkt, 0, sizeof(struct ipr_cmd_pkt));
	ioarcb->write_data_transfer_length = 0;
	ioarcb->read_data_transfer_length = 0;
	ioarcb->write_ioadl_len = 0;
	ioarcb->read_ioadl_len = 0;
	ioasa->ioasc = 0;
	ioasa->residual_data_len = 0;
	ipr_cmd->parent = NULL;
}

/**
 * ipr_get_free_ipr_cmnd - Get a free IPR Cmnd block
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	pointer to ipr command struct
 **/
static
struct ipr_cmnd *ipr_get_free_ipr_cmnd(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_cmnd *ipr_cmd;

	ipr_cmd = list_entry(ioa_cfg->free_q.next, struct ipr_cmnd, queue);
	list_del(&ipr_cmd->queue);
	ipr_init_ipr_cmnd(ipr_cmd);

	return ipr_cmd;
}

/**
 * ipr_unmap_sglist - Unmap scatterlist if mapped
 * @ioa_cfg:	ioa config struct
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_unmap_sglist(struct ipr_ioa_cfg *ioa_cfg,
			     struct ipr_cmnd *ipr_cmd)
{
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;

	if (ipr_cmd->dma_use_sg) {
		if (scsi_cmd->use_sg > 0) {
			pci_unmap_sg(ioa_cfg->pdev, scsi_cmd->request_buffer,
				     scsi_cmd->use_sg,
				     scsi_cmd->sc_data_direction);
		} else {
			pci_unmap_single(ioa_cfg->pdev, ipr_cmd->dma_handle,
					 scsi_cmd->bufflen,
					 scsi_cmd->sc_data_direction);
		}
	}
}

/**
 * ipr_get_task_attributes - Translate SPI Q-Tag to task attributes
 * @scsi_cmd:	scsi command struct
 *
 * Return value:
 * 	task attributes
 **/
static u8 ipr_get_task_attributes(struct scsi_cmnd *scsi_cmd)
{
	u8 tag[2];
	u8 rc = IPR_UNTAGGED_TASK;

	if (scsi_populate_tag_msg(scsi_cmd, tag)) {
		switch (tag[0]) {
		case MSG_SIMPLE_TAG:
			rc = IPR_SIMPLE_TASK;
			break;
		case MSG_HEAD_TAG:
			rc = IPR_HEAD_OF_Q_TASK;
			break;
		case MSG_ORDERED_TAG:
			rc = IPR_ORDERED_TASK;
			break;
		};
	}

	return rc;
}

/**
 * ipr_mask_and_clear_all_interrupts - Mask and clears all interrupts on the adapter
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	none
 **/
static void ipr_mask_and_clear_all_interrupts(struct ipr_ioa_cfg *ioa_cfg)
{
	volatile u32 int_reg;

	/* Stop new interrupts */
	ioa_cfg->allow_interrupts = 0;

	/* Set interrupt mask to stop all new interrupts */
	writel(~0, ioa_cfg->regs.set_interrupt_mask_reg);
	int_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);

	/* Clear any pending interrupts */
	writel(~0, ioa_cfg->regs.clr_interrupt_reg);
	int_reg = readl(ioa_cfg->regs.sense_interrupt_reg);
}

/**
 * ipr_init - Module entry point
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static int __init ipr_init(void)
{
	int i, rc = 0;

	spin_lock_init(&ipr_driver_lock);
	INIT_LIST_HEAD(&ipr_ioa_head);

	if (ipr_safe_settings)
		ipr_disable_tcq = 1;

	for (i = 0; !rc && (i < ARRAY_SIZE(ipr_ioctls)); i++)
		rc = register_ioctl32_conversion(ipr_ioctls[i], NULL);

	if (rc) {
		ipr_err("Couldn't register ioctl32 conversion for %d! rc=%d\n",
			ipr_ioctls[i], rc);

		for (; i >= 0; i--)
			unregister_ioctl32_conversion(ipr_ioctls[i]);

		return rc;
	}

	/* Register our character devices. Use a dynamic major number */
	rc = register_chrdev(0, IPR_NAME, &ipr_fops);

	if (rc < 0) {
		ipr_err("Couldn't register ipr char device! rc=%d\n", rc);
	} else {
		ipr_major = rc;
		pci_register_driver(&ipr_driver);
		rc = driver_create_file(&ipr_driver.driver, &driver_attr_version);
	}

	if (rc) {
		for (i = 0; i < ARRAY_SIZE(ipr_ioctls); i++)
			unregister_ioctl32_conversion(ipr_ioctls[i]);
	}

	return rc;
}

/**
 * ipr_save_pcix_cmd_reg - Save PCI-X command register
 * @ioa_cfg:	ioa config struct
 * 
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_save_pcix_cmd_reg(struct ipr_ioa_cfg *ioa_cfg)
{
	int pcix_cmd_reg;

	pcix_cmd_reg = pci_find_capability(ioa_cfg->pdev, PCI_CAP_ID_PCIX);

	if (pcix_cmd_reg == 0) {
		dev_err(&ioa_cfg->pdev->dev,
			"Failed to save PCI-X command register\n");
		return -EIO;
	}

	if (pci_read_config_word(ioa_cfg->pdev, pcix_cmd_reg,
				 &ioa_cfg->saved_pcix_cmd_reg) != PCIBIOS_SUCCESSFUL) {
		dev_err(&ioa_cfg->pdev->dev, "Read of config space failed\n");
		return -EIO;
	}

	ioa_cfg->saved_pcix_cmd_reg |= PCI_X_CMD_DPERR_E | PCI_X_CMD_ERO;

	if (pci_write_config_word(ioa_cfg->pdev, pcix_cmd_reg,
				  ioa_cfg->saved_pcix_cmd_reg) != PCIBIOS_SUCCESSFUL) {
		dev_err(&ioa_cfg->pdev->dev, "Read of config space failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * ipr_set_pcix_cmd_reg - Setup PCI-X command register
 * @ioa_cfg:	ioa config struct
 * 
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_set_pcix_cmd_reg(struct ipr_ioa_cfg *ioa_cfg)
{
	int pcix_cmd_reg;

	pcix_cmd_reg = pci_find_capability(ioa_cfg->pdev, PCI_CAP_ID_PCIX);

	if (pcix_cmd_reg) {
		if (pci_write_config_word(ioa_cfg->pdev, pcix_cmd_reg,
					  ioa_cfg->saved_pcix_cmd_reg) != PCIBIOS_SUCCESSFUL) {
			dev_err(&ioa_cfg->pdev->dev, "Read of config space failed\n");
			return -EIO;
		}
	} else {
		dev_err(&ioa_cfg->pdev->dev,
			"Failed to setup PCI-X command register\n");
		return -EIO;
	}

	return 0;
}

/**
 * ipr_initialize_bus_attr - Initialize SCSI bus attributes to default values
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	none
 **/
static void __devinit ipr_initialize_bus_attr(struct ipr_ioa_cfg *ioa_cfg)
{
	int i;

	for (i = 0; i < IPR_MAX_NUM_BUSES; i++) {
		ioa_cfg->bus_attr[i].bus = i;
		ioa_cfg->bus_attr[i].qas_enabled = 0;
		ioa_cfg->bus_attr[i].bus_width = IPR_DEFAULT_BUS_WIDTH;
		if (ipr_safe_settings)
			ioa_cfg->bus_attr[i].max_xfer_rate = IPR_SAFE_SCSI_RATE;
		else
			ioa_cfg->bus_attr[i].max_xfer_rate = IPR_DEFAULT_SCSI_RATE;
	}
}

/**
 * ipr_init_ioa_cfg - Initialize IOA config struct
 * @ioa_cfg:	ioa config struct
 * @host:		scsi host struct
 * @pdev:		PCI dev struct
 *
 * Return value:
 * 	none
 **/
static void __devinit ipr_init_ioa_cfg(struct ipr_ioa_cfg *ioa_cfg,
				       struct Scsi_Host *host, struct pci_dev *pdev)
{
	ioa_cfg->host = host;
	ioa_cfg->pdev = pdev;
	ioa_cfg->minor_num = IPR_NUM_MINORS;
	ioa_cfg->debug_level = ipr_verbose;
	sprintf(ioa_cfg->eye_catcher, IPR_EYECATCHER);
	sprintf(ioa_cfg->trace_start, IPR_TRACE_START_LABEL);
	sprintf(ioa_cfg->ipr_free_label, IPR_FREEQ_LABEL);
	sprintf(ioa_cfg->ipr_pending_label, IPR_PENDQ_LABEL);
	sprintf(ioa_cfg->cfg_table_start, IPR_CFG_TBL_START);
	sprintf(ioa_cfg->resource_table_label, IPR_RES_TABLE_LABEL);
	sprintf(ioa_cfg->ipr_hcam_label, IPR_HCAM_LABEL);
	sprintf(ioa_cfg->ipr_cmd_label, IPR_CMD_LABEL);

	INIT_LIST_HEAD(&ioa_cfg->free_q);
	INIT_LIST_HEAD(&ioa_cfg->pending_q);
	INIT_LIST_HEAD(&ioa_cfg->hostrcb_free_q);
	INIT_LIST_HEAD(&ioa_cfg->hostrcb_pending_q);
	INIT_LIST_HEAD(&ioa_cfg->free_res_q);
	INIT_LIST_HEAD(&ioa_cfg->used_res_q);
	INIT_WORK(&ioa_cfg->low_pri_work, ipr_worker_thread, ioa_cfg);
	init_waitqueue_head(&ioa_cfg->reset_wait_q);
	init_waitqueue_head(&ioa_cfg->sdt_wait_q);
	ioa_cfg->sdt_state = INACTIVE;

	ipr_initialize_bus_attr(ioa_cfg);

	host->max_id = IPR_MAX_NUM_TARGETS_PER_BUS;
	host->max_lun = IPR_MAX_NUM_LUNS_PER_TARGET;
	host->max_channel = IPR_MAX_BUS_TO_SCAN;
	host->unique_id = host->host_no;
	host->max_cmd_len = IPR_MAX_CDB_LEN;
	pci_set_drvdata(pdev, ioa_cfg);

	/* Initialize our semaphore for IOCTLs */
	sema_init(&ioa_cfg->ioctl_semaphore, IPR_NUM_IOCTL_CMD_BLKS);

	memcpy(&ioa_cfg->regs, &ioa_cfg->chip_cfg->regs, sizeof(ioa_cfg->regs));

	ioa_cfg->regs.set_interrupt_mask_reg += ioa_cfg->hdw_dma_regs;
	ioa_cfg->regs.clr_interrupt_mask_reg += ioa_cfg->hdw_dma_regs;
	ioa_cfg->regs.sense_interrupt_mask_reg += ioa_cfg->hdw_dma_regs;
	ioa_cfg->regs.clr_interrupt_reg += ioa_cfg->hdw_dma_regs;
	ioa_cfg->regs.sense_interrupt_reg += ioa_cfg->hdw_dma_regs;
	ioa_cfg->regs.ioarrin_reg += ioa_cfg->hdw_dma_regs;
	ioa_cfg->regs.sense_uproc_interrupt_reg += ioa_cfg->hdw_dma_regs;
	ioa_cfg->regs.set_uproc_interrupt_reg += ioa_cfg->hdw_dma_regs;
	ioa_cfg->regs.clr_uproc_interrupt_reg += ioa_cfg->hdw_dma_regs;
}

/**
 * ipr_free_mem - Frees memory allocated for an adapter
 * @ioa_cfg:	ioa cfg struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_free_mem(struct ipr_ioa_cfg *ioa_cfg)
{
	int i;

	kfree(ioa_cfg->res_entries);

	pci_free_consistent(ioa_cfg->pdev, sizeof(struct ipr_vpd_cbs),
			    ioa_cfg->vpd_cbs, ioa_cfg->vpd_cbs_dma);

	for (i = 0; i < IPR_NUM_CMD_BLKS; i++) {
		if (ioa_cfg->ipr_cmnd_list[i])
			pci_pool_free(ioa_cfg->ipr_cmd_pool, ioa_cfg->ipr_cmnd_list[i],
				      ioa_cfg->ipr_cmnd_list_dma[i]);
	}

	if (ioa_cfg->ipr_cmd_pool)
		pci_pool_destroy (ioa_cfg->ipr_cmd_pool);

	pci_free_consistent(ioa_cfg->pdev, sizeof(u32) * IPR_NUM_CMD_BLKS,
			    ioa_cfg->host_rrq, ioa_cfg->host_rrq_dma);

	pci_free_consistent(ioa_cfg->pdev, sizeof(struct ipr_config_table),
			    ioa_cfg->cfg_table,
			    ioa_cfg->cfg_table_dma);

	for (i = 0; i < IPR_NUM_HCAMS; i++) {
		pci_free_consistent(ioa_cfg->pdev,
				    sizeof(struct ipr_hostrcb),
				    ioa_cfg->hostrcb[i],
				    ioa_cfg->hostrcb_dma[i]);
	}

	kfree(ioa_cfg->trace);
}

/**
 * ipr_probe_ioa - Allocates memory and does first stage of initialization
 * @pdev:		PCI device struct
 * @dev_id:		PCI device id struct
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static int __devinit ipr_probe_ioa(struct pci_dev *pdev,
				   const struct pci_device_id *dev_id)
{
	struct ipr_ioa_cfg *ioa_cfg;
	struct Scsi_Host *host;
	unsigned long ipr_regs, ipr_regs_pci;
	u32 rc = PCIBIOS_SUCCESSFUL;

	ENTER;

	if ((rc = pci_enable_device(pdev))) {
		dev_err(&pdev->dev, "Cannot enable adapter\n");
		return rc;
	}

	dev_info(&pdev->dev, "Found IOA with IRQ: %d\n", pdev->irq);

	/* Initialize SCSI Host structure */
	host = scsi_host_alloc(&driver_template, sizeof(*ioa_cfg));

	if (!host) {
		dev_err(&pdev->dev, "call to scsi_host_alloc failed!\n");
		return -ENOMEM;
	}

	ioa_cfg = (struct ipr_ioa_cfg *)host->hostdata;

	memset(ioa_cfg, 0, sizeof(struct ipr_ioa_cfg));

	ioa_cfg->chip_cfg = (const struct ipr_chip_cfg_t *)dev_id->driver_data;

	ipr_regs_pci = pci_resource_start(pdev, 0);

	if (!request_mem_region(ipr_regs_pci,
				pci_resource_len(pdev, 0), IPR_NAME)) {
		dev_err(&pdev->dev,
			"Couldn't register memory range of registers\n");
		scsi_host_put(host);
		return -ENOMEM;
	}

	ipr_regs = (unsigned long)ioremap(ipr_regs_pci,
					  pci_resource_len(pdev, 0));

	ioa_cfg->hdw_dma_regs = ipr_regs;
	ioa_cfg->hdw_dma_regs_pci = ipr_regs_pci;
	ioa_cfg->ioa_mailbox = ioa_cfg->chip_cfg->mailbox + ipr_regs;

	ipr_init_ioa_cfg(ioa_cfg, host, pdev);

	pci_set_master(pdev);

	rc = pci_set_dma_mask(pdev, 0xffffffff);

	if (rc != PCIBIOS_SUCCESSFUL) {
		dev_err(&pdev->dev, "Failed to set PCI DMA mask\n");
		rc = -EIO;
		goto cleanup_nomem;
	}

	rc = pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE,
				   ioa_cfg->chip_cfg->cache_line_size);

	if (rc != PCIBIOS_SUCCESSFUL) {
		dev_err(&pdev->dev, "Read of config space failed\n");
		rc = -EIO;
		goto cleanup_nomem;
	}

	/* Save away PCI config space for use following IOA reset */
	rc = pci_save_state(pdev, ioa_cfg->pci_cfg_buf);

	if (rc != PCIBIOS_SUCCESSFUL) {
		dev_err(&pdev->dev, "Read of config space failed\n");
		rc = -EIO;
		goto cleanup_nomem;
	}

	if ((rc = ipr_save_pcix_cmd_reg(ioa_cfg)))
		goto cleanup_nomem;

	if ((rc = ipr_set_pcix_cmd_reg(ioa_cfg)))
		goto cleanup_nomem;

	if ((rc = ipr_alloc_mem(ioa_cfg)))
		goto cleanup;

	ipr_mask_and_clear_all_interrupts(ioa_cfg);

	/* Request our IRQ */
	rc = request_irq(pdev->irq, ipr_isr, SA_SHIRQ, IPR_NAME, ioa_cfg);

	if (rc) {
		dev_err(&pdev->dev, "Couldn't register IRQ %d! rc=%d\n",
			pdev->irq, rc);
		goto cleanup_nolog;
	}

	spin_lock(&ipr_driver_lock);

	/* Add this controller to the linked list of controllers */
	list_add_tail(&ioa_cfg->queue, &ipr_ioa_head);

	spin_unlock(&ipr_driver_lock);

	LEAVE;

	return 0;

cleanup:
	dev_err(&pdev->dev, "Couldn't allocate enough memory for device driver! \n");
cleanup_nolog:
	ipr_free_mem(ioa_cfg);
cleanup_nomem:
	iounmap((void *) ipr_regs);
	release_mem_region(ipr_regs_pci, pci_resource_len(pdev, 0));
	scsi_host_put(host);

	return rc;
}

/**
 * ipr_scan_vsets - Scans for VSET devices
 * @ioa_cfg:	ioa config struct
 *
 * Description: Since the VSET resources do not follow SAM in that we can have
 * sparse LUNs with no LUN 0, we have to scan for these ourselves.
 *
 * Return value:
 * 	none
 **/
static void ipr_scan_vsets(struct ipr_ioa_cfg *ioa_cfg)
{
	int target, lun;

	for (target = 0; target < IPR_MAX_NUM_TARGETS_PER_BUS; target++)
		for (lun = 0; lun < IPR_MAX_NUM_VSET_LUNS_PER_TARGET; lun++ )
			scsi_add_device(ioa_cfg->host, IPR_VSET_BUS, target, lun);
}

/**
 * ipr_probe - Adapter hot plug add entry point
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static int __devinit ipr_probe(struct pci_dev *pdev,
			       const struct pci_device_id *dev_id)
{
	struct ipr_ioa_cfg *ioa_cfg;
	int rc;

	ENTER;

	rc = ipr_probe_ioa(pdev, dev_id);

	if (!rc) {
		ioa_cfg = pci_get_drvdata(pdev);
		rc = ipr_probe_ioa_part2(ioa_cfg);
		rc |= scsi_add_host(ioa_cfg->host, &pdev->dev);

		if (rc) {
			ipr_remove(pdev);
		} else {
			ioa_cfg->allow_ml_add_del = 1;
			scsi_scan_host(ioa_cfg->host);
			ipr_scan_vsets(ioa_cfg);
		}
	}

	LEAVE;

	return rc;
}

/**
 * ipr_free_cmd_blks - Frees command blocks allocated for an adapter
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	none
 **/
static void ipr_free_cmd_blks(struct ipr_ioa_cfg *ioa_cfg)
{
	int i;

	for (i = 0; i < IPR_NUM_CMD_BLKS; i++) {
		if (ioa_cfg->ipr_cmnd_list[i])
			pci_pool_free(ioa_cfg->ipr_cmd_pool,
				      ioa_cfg->ipr_cmnd_list[i],
				      ioa_cfg->ipr_cmnd_list_dma[i]);

		ioa_cfg->ipr_cmnd_list[i] = NULL;
	}

	if (ioa_cfg->ipr_cmd_pool)
		pci_pool_destroy (ioa_cfg->ipr_cmd_pool);

	ioa_cfg->ipr_cmd_pool = NULL;
}

/**
 * ipr_alloc_cmd_blks - Allocate command blocks for an adapter
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	0 on success / -ENOMEM on allocation failure
 **/
static int __devinit ipr_alloc_cmd_blks(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioarcb *ioarcb;
	u32 dma_addr;
	int i;

	ioa_cfg->ipr_cmd_pool = pci_pool_create (IPR_NAME, ioa_cfg->pdev,
						 sizeof(struct ipr_cmnd), 8, 0);

	if (!ioa_cfg->ipr_cmd_pool)
		return -ENOMEM;

	for (i = 0; i < IPR_NUM_CMD_BLKS; i++) {
		ipr_cmd = pci_pool_alloc (ioa_cfg->ipr_cmd_pool, SLAB_KERNEL, &dma_addr);

		if (!ipr_cmd) {
			ipr_free_cmd_blks(ioa_cfg);
			return -ENOMEM;
		}

		ioa_cfg->ipr_cmnd_list[i] = ipr_cmd;
		ioa_cfg->ipr_cmnd_list_dma[i] = dma_addr;

		ioarcb = &ipr_cmd->ioarcb;
		ioarcb->ioarcb_host_pci_addr = cpu_to_be32(dma_addr);
		ioarcb->host_response_handle = cpu_to_be32(i << 2);
		ioarcb->write_ioadl_addr =
			cpu_to_be32(dma_addr + offsetof(struct ipr_cmnd, ioadl));
		ioarcb->read_ioadl_addr = ioarcb->write_ioadl_addr;
		ioarcb->ioasa_host_pci_addr =
			cpu_to_be32(dma_addr + offsetof(struct ipr_cmnd, ioasa));
		ioarcb->ioasa_len = cpu_to_be16(sizeof(struct ipr_ioasa));
		ipr_cmd->cmd_index = i;
		ipr_cmd->ioa_cfg = ioa_cfg;
		ipr_cmd->sense_buffer_dma = dma_addr +
			offsetof(struct ipr_cmnd, sense_buffer);

		list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);
	}

	return 0;
}

/**
 * ipr_alloc_mem - Allocate memory for an adapter
 * @ioa_cfg:	ioa config struct
 *
 * Return value:
 * 	0 on success / non-zero for error
 **/
static int __devinit ipr_alloc_mem(struct ipr_ioa_cfg *ioa_cfg)
{
	int i;

	ENTER;

	ioa_cfg->res_entries = kmalloc(sizeof(struct ipr_resource_entry) *
				       IPR_MAX_PHYSICAL_DEVS, GFP_KERNEL);

	if (!ioa_cfg->res_entries)
		goto cleanup;

	memset(ioa_cfg->res_entries, 0,
	       sizeof(struct ipr_resource_entry) * IPR_MAX_PHYSICAL_DEVS);

	for (i = 0; i < IPR_MAX_PHYSICAL_DEVS; i++)
		list_add_tail(&ioa_cfg->res_entries[i].queue, &ioa_cfg->free_res_q);

	ioa_cfg->vpd_cbs = pci_alloc_consistent(ioa_cfg->pdev,
						sizeof(struct ipr_vpd_cbs),
						&ioa_cfg->vpd_cbs_dma);

	if (!ioa_cfg->vpd_cbs)
		goto cleanup;

	if (ipr_alloc_cmd_blks(ioa_cfg))
		goto cleanup;

	ioa_cfg->host_rrq = pci_alloc_consistent(ioa_cfg->pdev,
						 sizeof(u32) * IPR_NUM_CMD_BLKS,
						 &ioa_cfg->host_rrq_dma);

	if (!ioa_cfg->host_rrq)
		goto cleanup;

	ioa_cfg->cfg_table = pci_alloc_consistent(ioa_cfg->pdev,
						  sizeof(struct ipr_config_table),
						  &ioa_cfg->cfg_table_dma);

	if (!ioa_cfg->cfg_table)
		goto cleanup;

	for (i = 0; i < IPR_NUM_HCAMS; i++) {
		ioa_cfg->hostrcb[i] = pci_alloc_consistent(ioa_cfg->pdev,
							   sizeof(struct ipr_hostrcb),
							   &ioa_cfg->hostrcb_dma[i]);

		if (!ioa_cfg->hostrcb[i])
			goto cleanup;

		memset(ioa_cfg->hostrcb[i], 0, sizeof(struct ipr_hostrcb));
		ioa_cfg->hostrcb[i]->hostrcb_dma = ioa_cfg->hostrcb_dma[i];

		if (i < IPR_NUM_LOG_HCAMS)
			ioa_cfg->hostrcb[i]->op_code = IPR_HOST_RCB_OP_CODE_LOG_DATA;
		else
			ioa_cfg->hostrcb[i]->op_code = IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE;

		memcpy(&ioa_cfg->hostrcb[i]->dasd_timeouts.record,
		       ipr_dasd_timeouts,
		       sizeof(ipr_dasd_timeouts));

		ioa_cfg->hostrcb[i]->dasd_timeouts.length =
			cpu_to_be32(sizeof(struct ipr_dasd_timeouts));

		list_add_tail(&ioa_cfg->hostrcb[i]->queue, &ioa_cfg->hostrcb_free_q);
	}

	ioa_cfg->trace = kmalloc(sizeof(struct ipr_trace_entry) *
				 IPR_NUM_TRACE_ENTRIES, GFP_KERNEL);

	if (!ioa_cfg->trace)
		goto cleanup;

	memset(ioa_cfg->trace, 0,
	       sizeof(struct ipr_trace_entry) * IPR_NUM_TRACE_ENTRIES);

	ioa_cfg->trace_index = 0;

	LEAVE;

	return 0;

cleanup:
	ipr_free_mem(ioa_cfg);

	LEAVE;

	return -ENOMEM;
}


/**
 * ipr_probe_ioa_part2 - Initializes IOAs found in ipr_probe_ioa(..)
 * @ioa_cfg:	ioa cfg struct
 *
 * Description: This is the second phase of adapter intialization
 * This function takes care of initilizing the adapter to the point
 * where it can accept new commands.

 * Return value:
 * 	0 on sucess / -EIO on failure
 **/
static int __devinit ipr_probe_ioa_part2(struct ipr_ioa_cfg *ioa_cfg)
{
	int rc = 0;
	int minor_num;
	unsigned long host_lock_flags = 0;

	ENTER;

	spin_lock_irqsave(ioa_cfg->host->host_lock, host_lock_flags);

	dev_dbg(&ioa_cfg->pdev->dev, "ioa_cfg adx: 0x%p\n", ioa_cfg);

	ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);

	ipr_sleep_on(ioa_cfg, &ioa_cfg->reset_wait_q);

	if (ioa_cfg->ioa_is_dead) {
		rc = -EIO;
	} else if (ipr_invalid_adapter(ioa_cfg)) {
		if (!ipr_unsafe)
			rc = -EIO;

		dev_err(&ioa_cfg->pdev->dev,
			"Adapter not supported in this hardware configuration.\n");
	} else {
		spin_lock(&ipr_driver_lock);

		minor_num = find_first_zero_bit(ipr_minors, IPR_NUM_MINORS);

		if (minor_num == IPR_NUM_MINORS) {
			dev_err(&ioa_cfg->pdev->dev,
				"Could not allocate minor number for adapter.\n");

			rc = -ENOMEM;
		} else {
			set_bit(minor_num, ipr_minors);
			ioa_cfg->minor_num = minor_num;
		}

		spin_unlock(&ipr_driver_lock);
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, host_lock_flags);

	LEAVE;

	return rc;
}

/**
 * ipr_init_ioa_mem - Initialize ioa_cfg control block
 * @ioa_cfg:	ioa cfg struct
 *
 * Return value:
 * 	nothing
 **/
static void ipr_init_ioa_mem(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_hostrcb *hostrcb, *temp;

	memset(ioa_cfg->host_rrq, 0, sizeof(u32) * IPR_NUM_CMD_BLKS);

	/* Initialize Host RRQ pointers */
	ioa_cfg->hrrq_start = ioa_cfg->host_rrq;
	ioa_cfg->hrrq_end = &ioa_cfg->host_rrq[IPR_NUM_CMD_BLKS - 1];
	ioa_cfg->hrrq_curr = ioa_cfg->hrrq_start;
	ioa_cfg->toggle_bit = 1;

	/* Zero out config table */
	memset(ioa_cfg->cfg_table, 0, sizeof(struct ipr_config_table));

	/*
	 * Hostrcbs outstanding to the adapter never come back, so
	 * we need to place them back on the free queue
	 */
	list_for_each_entry_safe(hostrcb, temp, &ioa_cfg->hostrcb_pending_q, queue)
		list_move_tail(&hostrcb->queue, &ioa_cfg->hostrcb_free_q);
}

/**
 * ipr_scsi_eh_done - mid-layer done function for aborted ops
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked by the interrupt handler for
 * ops generated by the SCSI mid-layer which are being aborted.
 *
 * Return value:
 * 	none
 **/
static void ipr_scsi_eh_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;

	scsi_cmd->result |= (DID_ERROR << 16);

	ipr_unmap_sglist(ioa_cfg, ipr_cmd);
	scsi_cmd->scsi_done(scsi_cmd);
	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);
}

/**
 * ipr_fail_all_ops - Fails all outstanding ops.
 * @ioa_cfg:	ioa config struct
 *
 * This function fails all outstanding ops.
 *
 * Return value:
 * 	none
 **/
static void ipr_fail_all_ops(struct ipr_ioa_cfg *ioa_cfg)
{
	struct ipr_cmnd *ipr_cmd, *temp;

	ENTER;

	list_for_each_entry_safe(ipr_cmd, temp, &ioa_cfg->pending_q, queue) {
		list_del(&ipr_cmd->queue);

		ipr_cmd->ioasa.ioasc = cpu_to_be32(IPR_IOASC_IOA_WAS_RESET);
		ipr_cmd->ioasa.ilid = cpu_to_be32(IPR_DRIVER_ILID);

		if (ipr_cmd->scsi_cmd)
			ipr_cmd->done = ipr_scsi_eh_done;

		ipr_trc_hook(ipr_cmd, IPR_TRACE_FINISH, IPR_IOASC_IOA_WAS_RESET);

		del_timer(&ipr_cmd->timer);

		ipr_cmd->done(ipr_cmd);
	}

	LEAVE;

	return;
}

/**
 * ipr_initiate_ioa_reset - Initiate an adapter reset
 * @ioa_cfg:		ioa config struct
 * @shutdown_type:	shutdown type
 *
 * Description: This function will initiate the reset of the given adapter.
 * If the caller needs to wait on the completion of the reset,
 * the caller must sleep on the reset_wait_q.
 *
 * Return value:
 * 	none
 **/
static void ipr_initiate_ioa_reset(struct ipr_ioa_cfg *ioa_cfg,
				   enum ipr_shutdown_type shutdown_type)
{
	struct ipr_cmnd *ipr_cmd;

	if (ioa_cfg->sdt_state == GET_DUMP)
		ioa_cfg->sdt_state = ABORT_DUMP;

	if (ioa_cfg->reset_retries++ > IPR_NUM_RESET_RELOAD_RETRIES) {
		dev_err(&ioa_cfg->pdev->dev,
			"IOA taken offline - error recovery failed.\n");

		ioa_cfg->reset_retries = 0;
		ioa_cfg->ioa_is_dead = 1;

		if (ioa_cfg->in_ioa_bringdown) {
			ioa_cfg->reset_cmd = NULL;
			ioa_cfg->in_reset_reload = 0;
			ipr_fail_all_ops(ioa_cfg);
			wake_up_all(&ioa_cfg->reset_wait_q);
			return;
		} else {
			ioa_cfg->in_ioa_bringdown = 1;
			shutdown_type = IPR_SHUTDOWN_NONE;
		}
	}

	ioa_cfg->in_reset_reload = 1;
	ioa_cfg->allow_cmds = 0;

	list_for_each_entry(ipr_cmd, &ioa_cfg->pending_q, queue)
		ipr_cmd->parent = NULL;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ioa_cfg->reset_cmd = ipr_cmd;
	ipr_cmd->job_step = ipr_reset_shutdown_ioa;
	ipr_cmd->shutdown_type = shutdown_type;

	ipr_reset_ioa_job(ipr_cmd);
}

/**
 * ipr_initiate_ioa_bringdown - Bring down an adapter
 * @ioa_cfg:		ioa config struct
 * @shutdown_type:	shutdown type
 *
 * Description: This function will initiate bringing down the adapter.
 * This consists of issuing an IOA shutdown to the adapter
 * to flush the cache, and running BIST.
 * If the caller needs to wait on the completion of the reset,
 * the caller must sleep on the reset_wait_q.
 *
 * Return value:
 * 	none
 **/
static void ipr_initiate_ioa_bringdown(struct ipr_ioa_cfg *ioa_cfg,
				       enum ipr_shutdown_type shutdown_type)
{
	ENTER;

	ioa_cfg->reset_retries = 0;
	ioa_cfg->in_ioa_bringdown = 1;
	ipr_initiate_ioa_reset(ioa_cfg, shutdown_type);

	LEAVE;
}


/**
 * ipr_reset_ioa_job - Adapter reset job
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function is the job router for the adapter reset job.
 *
 * Return value:
 * 	none
 **/
static void ipr_reset_ioa_job(struct ipr_cmnd *ipr_cmd)
{
	u32 rc, ioasc;
	unsigned long scratch = ipr_cmd->scratch;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	do {
		ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

		if (ioa_cfg->reset_cmd != ipr_cmd) {
			/*
			 * We are doing nested adapter resets and this is
			 * not the current reset job.
			 */
			ipr_trace;
			list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);
			return;
		}

		if (ioasc) {
			dev_dbg(&ioa_cfg->pdev->dev,
				"0x%02X failed with IOASC: 0x%08X\n",
				ipr_cmd->ioarcb.cmd_pkt.cdb[0], ioasc);

			ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
			list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);
			return;
		}

		ipr_reinit_ipr_cmnd(ipr_cmd);
		ipr_cmd->scratch = scratch;
		rc = ipr_cmd->job_step(ipr_cmd);
	} while(rc == IPR_RC_JOB_CONTINUE);
}


/**
 * ipr_reset_start_timer - Start a timer for adapter reset job
 * @ipr_cmd:	ipr command struct
 * @timeout:	timeout value
 *
 * Description: This function is used in adapter reset processing
 * for timing events. If the reset_cmd pointer in the IOA
 * config struct is not this adapter's we are doing nested
 * resets and fail_all_ops will take care of freeing the
 * command block.
 *
 * Return value:
 * 	none
 **/
static void ipr_reset_start_timer(struct ipr_cmnd *ipr_cmd,
				  unsigned long timeout)
{
	list_add_tail(&ipr_cmd->queue, &ipr_cmd->ioa_cfg->pending_q);
	ipr_cmd->done = ipr_reset_ioa_job;

	ipr_cmd->timer.data = (unsigned long) ipr_cmd;
	ipr_cmd->timer.expires = jiffies + timeout;
	ipr_cmd->timer.function = (void (*)(unsigned long))ipr_reset_timer_done;
	add_timer(&ipr_cmd->timer);
}

/**
 * ipr_reset_timer_done - Adapter reset timer function
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function is used in adapter reset processing
 * for timing events. If the reset_cmd pointer in the IOA
 * config struct is not this adapter's we are doing nested
 * resets and fail_all_ops will take care of freeing the
 * command block.
 *
 * Return value:
 * 	none
 **/
static void ipr_reset_timer_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if (ioa_cfg->sdt_state == GET_DUMP)
		ioa_cfg->sdt_state = ABORT_DUMP;

	if (ioa_cfg->reset_cmd == ipr_cmd) {
		list_del(&ipr_cmd->queue);
		ipr_cmd->done(ipr_cmd);
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
}


/**
 * ipr_reset_shutdown_ioa - Shutdown the adapter
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function issues an adapter shutdown of the
 * specified type to the specified adapter as part of the
 * adapter reset job.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_shutdown_ioa(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	enum ipr_shutdown_type shutdown_type = ipr_cmd->shutdown_type;
	unsigned long timeout;
	int rc;

	ENTER;

	if (shutdown_type != IPR_SHUTDOWN_NONE && !ioa_cfg->ioa_is_dead) {
		ipr_cmd->ioarcb.res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
		ipr_cmd->ioarcb.cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
		ipr_cmd->ioarcb.cmd_pkt.cdb[0] = IPR_IOA_SHUTDOWN;
		ipr_cmd->ioarcb.cmd_pkt.cdb[1] = shutdown_type;

		if (shutdown_type == IPR_SHUTDOWN_ABBREV)
			timeout = IPR_ABBREV_SHUTDOWN_TIMEOUT;
		else if (shutdown_type == IPR_SHUTDOWN_PREPARE_FOR_NORMAL)
			timeout = IPR_SHUTDOWN_TIMEOUT;
		else
			timeout = IPR_SHUTDOWN_TIMEOUT;

		ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, timeout);

		rc = IPR_RC_JOB_RETURN;
	} else {
		rc = IPR_RC_JOB_CONTINUE;
	}

	ipr_cmd->job_step = ipr_reset_alert;

	LEAVE;

	return rc;
}

/**
 * ipr_reset_alert - Alert the adapter of a pending reset
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function alerts the adapter that it will be reset.
 * If memory space is not currently enabled, proceed directly
 * to running BIST on the adapter.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_alert(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	u16 cmd_reg;
	int rc;

	ENTER;

	rc = pci_read_config_word(ioa_cfg->pdev, PCI_COMMAND, &cmd_reg);

	if ((rc == PCIBIOS_SUCCESSFUL) && (cmd_reg & PCI_COMMAND_MEMORY)) {
		ipr_mask_and_clear_all_interrupts(ioa_cfg);
		writel(IPR_UPROCI_RESET_ALERT, ioa_cfg->regs.set_uproc_interrupt_reg);
		ipr_cmd->time_left = IPR_WAIT_FOR_RESET_TIMEOUT;
		ipr_cmd->job_step = ipr_reset_wait_to_start_bist;
	} else {
		ipr_cmd->job_step = ipr_reset_start_bist;
	}

	LEAVE;

	return IPR_RC_JOB_CONTINUE;	
}

/**
 * ipr_reset_allowed - Query whether or not IOA can be reset
 * @ioa_cfg:	ioa config struct
 * 
 * Return value:
 * 	0 if reset not allowed / non-zero if reset is allowed
 **/
static int ipr_reset_allowed(struct ipr_ioa_cfg *ioa_cfg)
{
	volatile u32 temp_reg;

	temp_reg = readl(ioa_cfg->regs.sense_interrupt_reg);

	return ((temp_reg & IPR_PCII_CRITICAL_OPERATION) == 0);
}

/**
 * ipr_reset_wait_to_start_bist - Wait for permission to reset IOA.
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function waits for adapter permission to run BIST,
 * then runs BIST. If the adapter does not give permission after a
 * reasonable time, we will reset the adapter anyway. The impact of
 * resetting the adapter without warning the adapter is the risk of
 * losing the persistent error log on the adapter. If the adapter is
 * reset while it is writing to the flash on the adapter, the flash
 * segment will have bad ECC and be zeroed.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_wait_to_start_bist(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int rc = IPR_RC_JOB_RETURN;

	if (!ipr_reset_allowed(ioa_cfg) && ipr_cmd->time_left) {
		ipr_cmd->time_left -= IPR_CHECK_FOR_RESET_TIMEOUT;
		ipr_reset_start_timer(ipr_cmd, IPR_CHECK_FOR_RESET_TIMEOUT);
	} else {
		ipr_cmd->job_step = ipr_reset_start_bist;
		rc = IPR_RC_JOB_CONTINUE;
	}

	return rc;
}

/**
 * ipr_reset_start_bist - Run BIST on the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function runs BIST on the adapter, then delays 2 seconds.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_start_bist(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int rc;

	ENTER;

	rc = pci_write_config_byte(ioa_cfg->pdev, PCI_BIST, PCI_BIST_START);

	if (rc != PCIBIOS_SUCCESSFUL) {
		ipr_cmd->ioasa.ioasc = cpu_to_be32(IPR_IOASC_PCI_ACCESS_ERROR);
		rc = IPR_RC_JOB_CONTINUE;
	} else {
		ipr_cmd->job_step = ipr_reset_restore_cfg_space;
		ipr_reset_start_timer(ipr_cmd, IPR_WAIT_FOR_BIST_TIMEOUT);
		rc = IPR_RC_JOB_RETURN;
	}

	LEAVE;

	return rc;
}

/**
 * ipr_reset_restore_cfg_space - Restore PCI config space.
 * @ipr_cmd:	ipr command struct
 *
 * Description: This function restores the saved PCI config space of
 * the adapter, fails all outstanding ops back to the callers, and
 * fetches the dump/unit check if applicable to this reset.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_reset_restore_cfg_space(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	int rc;

	ENTER;

	rc = pci_restore_state(ioa_cfg->pdev, ioa_cfg->pci_cfg_buf);

	if (rc != PCIBIOS_SUCCESSFUL) {
		ipr_cmd->ioasa.ioasc = cpu_to_be32(IPR_IOASC_PCI_ACCESS_ERROR);
		return IPR_RC_JOB_CONTINUE;
	}

	if (ipr_set_pcix_cmd_reg(ioa_cfg)) {
		ipr_cmd->ioasa.ioasc = cpu_to_be32(IPR_IOASC_PCI_ACCESS_ERROR);
		return IPR_RC_JOB_CONTINUE;
	}

	ipr_fail_all_ops(ioa_cfg);

	if (ioa_cfg->ioa_unit_checked) {
		ioa_cfg->ioa_unit_checked = 0;
		ipr_get_unit_check_buffer(ioa_cfg);
		ipr_cmd->job_step = ipr_reset_alert;
		ipr_reset_start_timer(ipr_cmd, 0);
		return IPR_RC_JOB_RETURN;
	}

	if (ioa_cfg->in_ioa_bringdown) {
		ipr_cmd->job_step = ipr_ioa_bringdown_done;
	} else {
		ipr_cmd->job_step = ipr_reset_enable_ioa;

		if (GET_DUMP == ioa_cfg->sdt_state) {
			ipr_reset_start_timer(ipr_cmd, IPR_DUMP_TIMEOUT);
			ipr_cmd->job_step = ipr_reset_wait_for_dump;
			wake_up_interruptible(&ioa_cfg->sdt_wait_q);
			return IPR_RC_JOB_RETURN;
		}
	}

	ENTER;

	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_reset_wait_for_dump - Wait for a dump to timeout.
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked when an adapter dump has run out
 * of processing time.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE
 **/
static int ipr_reset_wait_for_dump(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	if (ioa_cfg->sdt_state == GET_DUMP)
		ioa_cfg->sdt_state = ABORT_DUMP;

	ipr_cmd->job_step = ipr_reset_alert;

	return IPR_RC_JOB_CONTINUE;
}


/**
 * ipr_reset_enable_ioa - Enable the IOA following a reset.
 * @ipr_cmd:	ipr command struct
 *
 * This function reinitializes some control blocks and
 * enables destructive diagnostics on the adapter. 
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_reset_enable_ioa(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	volatile u32 temp_reg;

	ENTER;

	ipr_init_ioa_mem(ioa_cfg);

	/* Enable interrupts */
	ioa_cfg->allow_interrupts = 1;
	writel(IPR_PCII_OPER_INTERRUPTS, ioa_cfg->regs.clr_interrupt_mask_reg);
	temp_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);

	/* Enable destructive diagnostics on IOA */
	writel(IPR_DOORBELL, ioa_cfg->regs.set_uproc_interrupt_reg);

	dev_info(&ioa_cfg->pdev->dev, "Initializing IOA.\n");

	ipr_cmd->timer.data = (unsigned long) ipr_cmd;
	ipr_cmd->timer.expires = jiffies + IPR_OPERATIONAL_TIMEOUT;
	ipr_cmd->timer.function = (void (*)(unsigned long))ipr_timeout;
	add_timer(&ipr_cmd->timer);
	ipr_cmd->job_step = ipr_ioafp_indentify_hrrq;

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioafp_indentify_hrrq - Send Identify Host RRQ.
 * @ipr_cmd:	ipr command struct
 *
 * This function send an Identify Host Request Response Queue
 * command to establish the HRRQ with the adapter. 
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_indentify_hrrq(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ENTER;

	dev_info(&ioa_cfg->pdev->dev, "Starting IOA initialization sequence.\n");

	ioarcb->cmd_pkt.cdb[0] = IPR_ID_HOST_RR_Q;
	ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);

	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
	ioarcb->cmd_pkt.cdb[2] =
		((u32) ioa_cfg->host_rrq_dma >> 24) & 0xff;
	ioarcb->cmd_pkt.cdb[3] =
		((u32) ioa_cfg->host_rrq_dma >> 16) & 0xff;
	ioarcb->cmd_pkt.cdb[4] =
		((u32) ioa_cfg->host_rrq_dma >> 8) & 0xff;
	ioarcb->cmd_pkt.cdb[5] =
		((u32) ioa_cfg->host_rrq_dma) & 0xff;
	ioarcb->cmd_pkt.cdb[7] =
		((sizeof(u32) * IPR_NUM_CMD_BLKS) >> 8) & 0xff;
	ioarcb->cmd_pkt.cdb[8] =
		(sizeof(u32) * IPR_NUM_CMD_BLKS) & 0xff;

	ipr_cmd->job_step = ipr_ioafp_std_inquiry;

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;

	return IPR_RC_JOB_RETURN;
}


/**
 * ipr_inquiry - Send an Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This utility function sends an inquiry to the adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_inquiry(struct ipr_cmnd *ipr_cmd, u8 flags, u8 page,
			u32 dma_addr, u8 xfer_len)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;

	ENTER;

	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;
	ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);

	ioarcb->cmd_pkt.cdb[0] = INQUIRY;
	ioarcb->cmd_pkt.cdb[1] = flags;
	ioarcb->cmd_pkt.cdb[2] = page;
	ioarcb->cmd_pkt.cdb[4] = xfer_len;

	ioarcb->read_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ioarcb->read_data_transfer_length = cpu_to_be32(xfer_len);

	ioadl->address = cpu_to_be32(dma_addr);
	ioadl->flags_and_data_len =
		cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | xfer_len);

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;
}

/**
 * ipr_ioafp_std_inquiry - Send a Standard Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a standard inquiry to the adapter.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_std_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;

	ipr_cmd->job_step = ipr_ioafp_page0_inquiry;

	ipr_inquiry(ipr_cmd, 0, 0,
		    ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_vpd_cbs, ioa_vpd),
		    sizeof(struct ipr_ioa_vpd));

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioafp_page0_inquiry - Send a Page 0 Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Page 0 inquiry to the adapter
 * to retrieve the list of supported pages.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_page0_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	char type[5];

	ENTER;

	/* Grab the type out of the VPD and store it away */
	memcpy(type, ioa_cfg->vpd_cbs->ioa_vpd.std_inq_data.vpids.product_id, 4);
	type[4] = '\0';
	ioa_cfg->type = simple_strtoul((char *)type, NULL, 16);

	ipr_cmd->job_step = ipr_ioafp_page3_inquiry;

	ipr_inquiry(ipr_cmd, 1, 0,
		    ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_vpd_cbs, page0_data),
		    sizeof(struct ipr_inquiry_page0));

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioafp_inq_page_supp - Check if an inquiry page is supported.
 * @ioa_cfg:	ioa config struct
 * @page:		inquiry page code
 *
 * This utility function checks Page 0 inquiry to see if the given
 * inquiry page is supported by the IOA..
 *
 * Return value:
 * 	1 if page is supported / 0 if not supported
 **/
static int ipr_ioafp_inq_page_supp(struct ipr_ioa_cfg *ioa_cfg, u8 page)
{
	struct ipr_inquiry_page0 *supp_vpd = &ioa_cfg->vpd_cbs->page0_data;
	int i;

	for (i = 0;
	     (i < supp_vpd->page_length) && (i < IPR_MAX_NUM_SUPP_INQ_PAGES);
	     i++) {
		if (supp_vpd->supported_page_codes[i] == page)
			return 1;
	}

	return 0;
}

/**
 * ipr_ioafp_page3_inquiry - Send a Page 3 Inquiry to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Page 3 inquiry to the adapter
 * to retrieve software VPD information.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_page3_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;

	ipr_cmd->job_step = ipr_ioafp_query_ioa_cfg;

	if (!ipr_ioafp_inq_page_supp(ioa_cfg, 3))
		return IPR_RC_JOB_CONTINUE;

	ipr_inquiry(ipr_cmd, 1, 3,
		    ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_vpd_cbs, page3_data),
		    sizeof(struct ipr_inquiry_page3));

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioafp_query_ioa_cfg - Send a Query IOA Config to the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a Query IOA Configuration command
 * to the adapter to retrieve the IOA configuration table.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_query_ioa_cfg(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;

	ENTER;

	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
	ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);

	ioarcb->cmd_pkt.cdb[0] = IPR_QUERY_IOA_CONFIG;
	ioarcb->cmd_pkt.cdb[7] = (sizeof(struct ipr_config_table) >> 8) & 0xff;
	ioarcb->cmd_pkt.cdb[8] = sizeof(struct ipr_config_table) & 0xff;

	ioarcb->read_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ioarcb->read_data_transfer_length =
		cpu_to_be32(sizeof(struct ipr_config_table));

	ioadl->address = cpu_to_be32(ioa_cfg->cfg_table_dma);
	ioadl->flags_and_data_len =
		cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | sizeof(struct ipr_config_table));

	ipr_cmd->job_step = ipr_init_res_table;

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_init_res_entry - Initialize a resource entry struct.
 * @res:	resource entry struct
 *
 * Return value:
 * 	none
 **/
static void ipr_init_res_entry(struct ipr_resource_entry *res)
{
	res->in_init = 0;
	res->redo_init = 0;
	res->needs_sync_complete = 0;
	res->in_erp = 0;
	res->add_to_ml = 0;
	res->del_from_ml = 0;
	res->resetting_device = 0;
	res->scsi_device = NULL;
}

/**
 * ipr_init_res_table - Initialize the resource table
 * @ipr_cmd:	ipr command struct
 *
 * This function looks through the existing resource table, comparing
 * it with the config table. This function will take care of old/new
 * devices and schedule adding/removing them from the mid-layer
 * as appropriate.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE
 **/
static int ipr_init_res_table(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_resource_entry *res, *temp;
	struct ipr_config_table_entry *cfgte;
	int found, i;
	LIST_HEAD(old_res);

	ENTER;

	if (ioa_cfg->cfg_table->ucode_download_req)
		dev_err(&ioa_cfg->pdev->dev, "Microcode download required\n");

	list_for_each_entry_safe(res, temp, &ioa_cfg->used_res_q, queue)
		list_move_tail(&res->queue, &old_res);

	for (i = 0; i < ioa_cfg->cfg_table->num_entries; i++) {
		cfgte = &ioa_cfg->cfg_table->dev[i];
		found = 0;

		list_for_each_entry_safe(res, temp, &old_res, queue) {
			if (!memcmp(&res->cfgte.res_addr,
				    &cfgte->res_addr, sizeof(cfgte->res_addr))) {
				list_move_tail(&res->queue, &ioa_cfg->used_res_q);
				found = 1;
				break;
			}
		}

		if (!found) {
			if (list_empty(&ioa_cfg->free_res_q)) {
				dev_err(&ioa_cfg->pdev->dev, "Too many devices attached\n");
				break;
			}

			res = list_entry(ioa_cfg->free_res_q.next,
					 struct ipr_resource_entry, queue);
			list_move_tail(&res->queue, &ioa_cfg->used_res_q);
			ipr_init_res_entry(res);
			if (ioa_cfg->allow_ml_add_del) {
				ipr_trace;
				res->add_to_ml = 1;
			}
		}

		memcpy(&res->cfgte, cfgte, sizeof(struct ipr_config_table_entry));
	}

	list_for_each_entry_safe(res, temp, &old_res, queue) {
		if (ioa_cfg->allow_ml_add_del) {
			ipr_trace;
			res->del_from_ml = 1;
			list_move_tail(&res->queue, &ioa_cfg->used_res_q);
		} else {
			ipr_trace;
		}
	}

	ipr_cmd->job_step = ipr_ioafp_mode_sense_page28;

	LEAVE;

	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_ioafp_mode_sense_page28 - Issue Mode Sense Page 28 to IOA
 * @ipr_cmd:	ipr command struct
 *
 * This function send a Page 28 mode sense to the IOA to
 * retrieve SCSI bus attributes.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_mode_sense_page28(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;

	ipr_build_mode_sense(ipr_cmd, cpu_to_be32(IPR_IOA_RES_HANDLE),
			     0x28, ioa_cfg->vpd_cbs_dma +
			     offsetof(struct ipr_vpd_cbs, mode_pages),
			     sizeof(struct ipr_mode_pages));

	ipr_cmd->job_step = ipr_ioafp_mode_select_page28;

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_check_term_power - Check for term power errors
 * @ioa_cfg:	ioa config struct
 * @mode_pages:	IOAFP mode pages buffer
 *
 * Check the IOAFP's mode page 28 for term power errors
 * 
 * Return value:
 * 	nothing
 **/
static void ipr_check_term_power(struct ipr_ioa_cfg *ioa_cfg,
				 struct ipr_mode_pages *mode_pages)
{
	int i;
	int entry_length;
	struct ipr_dev_bus_entry *bus;
	struct ipr_mode_page28 *mode_page;

	mode_page = ipr_get_mode_page(mode_pages, 0x28,
				      sizeof(struct ipr_mode_page28));

	entry_length = mode_page->entry_length;

	bus = mode_page->bus;

	for (i = 0; i < mode_page->num_entries; i++) {
		if (bus->term_power_absent) {
			dev_err(&ioa_cfg->pdev->dev,
				"Term power is absent on scsi bus %d\n",
				bus->res_addr.bus);
		}

		bus = (struct ipr_dev_bus_entry *)((char *)bus + entry_length);
	}
}

/**
 * ipr_find_ses_entry - Find matching SES in SES table
 * @res:	resource entry struct of SES
 * 
 * Return value:
 * 	pointer to SES table entry / NULL on failure
 **/
static const struct ipr_ses_table_entry *
ipr_find_ses_entry(struct ipr_resource_entry *res)
{
	int i, j, matches;
	const struct ipr_ses_table_entry *ste;

	ste = ipr_ses_table;

	for (i = 0; i < ARRAY_SIZE(ipr_ses_table); i++, ste++) {
		for (j = 0, matches = 0; j < IPR_PROD_ID_LEN; j++) {
			if (ste->compare_product_id_byte[j] == 'X') {
				if (res->cfgte.std_inq_data.vpids.product_id[j] == ste->product_id[j])
					matches++;
				else
					break;
			} else
				matches++;
		}

		if (matches == IPR_PROD_ID_LEN)
			return ste;
	}

	return NULL;
}

/**
 * ipr_get_max_scsi_speed - Determine max SCSI speed for a given bus
 * @ioa_cfg:	ioa config struct
 * @bus:		SCSI bus
 * @bus_width:	bus width
 * 
 * Return value:
 *	SCSI bus speed in units of 100KHz, 1600 is 160 MHz
 *	For a 2-byte wide SCSI bus, the maximum transfer speed is
 *	twice the maximum transfer rate (e.g. for a wide enabled bus,
 *	max 160MHz = max 320MB/sec).
 **/
static u32 ipr_get_max_scsi_speed(struct ipr_ioa_cfg *ioa_cfg, u8 bus, u8 bus_width)
{
	struct ipr_resource_entry *res;
	const struct ipr_ses_table_entry *ste;
	u32 max_xfer_rate = IPR_DEFAULT_SCSI_RATE;

	/* Loop through each config table entry in the config table buffer */
	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if (!(IPR_IS_SES_DEVICE(res->cfgte.std_inq_data)))
			continue;

		if (bus != res->cfgte.res_addr.bus)
			continue;

		if (!(ste = ipr_find_ses_entry(res)))
			continue;

		max_xfer_rate = (ste->max_bus_speed_limit * 10) / (bus_width / 8);
	}

	return max_xfer_rate;
}

/**
 * ipr_scsi_bus_speed_limit - Limit the SCSI speed based on SES table
 * @ioa_cfg:	ioa config struct
 *
 * Looks through the config table checking for SES devices. If
 * the SES device is in the SES table indicating a maximum SCSI
 * bus speed, the speed is limited for the bus.
 * 
 * Return value:
 * 	none
 **/
static void ipr_scsi_bus_speed_limit(struct ipr_ioa_cfg *ioa_cfg)
{
	u32 max_xfer_rate;
	int i;

	for (i = 0; i < IPR_MAX_NUM_BUSES; i++) {
		max_xfer_rate = ipr_get_max_scsi_speed(ioa_cfg, i,
						       ioa_cfg->bus_attr[i].bus_width);

		if (max_xfer_rate < ioa_cfg->bus_attr[i].max_xfer_rate)
			ioa_cfg->bus_attr[i].max_xfer_rate = max_xfer_rate;
	}
}

/**
 * ipr_modify_ioafp_mode_page_28 - Modify IOAFP Mode Page 28
 * @ioa_cfg:	ioa config struct
 * @mode_pages:	mode page 28 buffer
 *
 * Updates mode page 28 based on driver configuration
 * 
 * Return value:
 * 	none
 **/
static void ipr_modify_ioafp_mode_page_28(struct ipr_ioa_cfg *ioa_cfg,
					  	struct ipr_mode_pages *mode_pages)
{
	int i, entry_length;
	struct ipr_dev_bus_entry *bus;
	struct ipr_bus_attributes *bus_attr;
	struct ipr_mode_page28 *mode_page;

	mode_page = ipr_get_mode_page(mode_pages, 0x28,
				      sizeof(struct ipr_mode_page28));

	entry_length = mode_page->entry_length;

	/* Loop for each device bus entry */
	for (i = 0, bus = mode_page->bus;
	     i < mode_page->num_entries;
	     i++, bus = (struct ipr_dev_bus_entry *)((u8 *)bus + entry_length)) {
		if (bus->res_addr.bus > IPR_MAX_NUM_BUSES) {
			dev_err(&ioa_cfg->pdev->dev,
				"Invalid resource address reported: 0x%08X\n",
				IPR_GET_PHYS_LOC(bus->res_addr));
			continue;
		}

		bus_attr = &ioa_cfg->bus_attr[bus->res_addr.bus];
		bus->extended_reset_delay = IPR_EXTENDED_RESET_DELAY;
		bus->bus_width = bus_attr->bus_width;
		bus->max_xfer_rate = cpu_to_be32(bus_attr->max_xfer_rate);
		if (bus_attr->qas_enabled)
			bus->qas_capability = IPR_MODEPAGE28_QAS_CAPABILITY_ENABLE_ALL;
		else
			bus->qas_capability = IPR_MODEPAGE28_QAS_CAPABILITY_DISABLE_ALL;
	}
}

/**
 * ipr_ioafp_mode_select_page28 - Issue Mode Select Page 28 to IOA
 * @ipr_cmd:	ipr command struct
 *
 * This function sets up the SCSI bus attributes and sends
 * a Mode Select for Page 28 to activate them.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioafp_mode_select_page28(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_mode_pages *mode_pages = &ioa_cfg->vpd_cbs->mode_pages;
	int length;

	ENTER;

	ipr_scsi_bus_speed_limit(ioa_cfg);
	ipr_check_term_power(ioa_cfg, mode_pages);
	ipr_modify_ioafp_mode_page_28(ioa_cfg, mode_pages);

	length = mode_pages->hdr.length + 1;
	mode_pages->hdr.length = 0;

	ipr_build_mode_select(ipr_cmd, cpu_to_be32(IPR_IOA_RES_HANDLE), 0x11,
			      ioa_cfg->vpd_cbs_dma + offsetof(struct ipr_vpd_cbs, mode_pages),
			      length);

	ipr_cmd->job_step = ipr_init_devices;
	ipr_cmd->res = list_entry(ioa_cfg->used_res_q.next,
				  struct ipr_resource_entry, queue);

	ipr_do_req(ipr_cmd, ipr_reset_ioa_job, ipr_timeout, IPR_INTERNAL_TIMEOUT);

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_init_devices - Setup attached devices for use on the adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function takes care of setting up all attached advanced
 * function devices. It takes care of making sure QERR, AWRE, etc.
 * are setup according to the adapter's expectations.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_init_devices(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_resource_entry *res = ipr_cmd->res;
	struct ipr_hostrcb *hostrcb;

	list_for_each_entry_continue(res, &ioa_cfg->used_res_q, queue) {
		res->needs_sync_complete = 1;

		if (res->del_from_ml || !ipr_is_af(res))
			continue;

		if (list_empty(&ioa_cfg->hostrcb_free_q))
			return IPR_RC_JOB_RETURN;

		ipr_cmd->res = res;

		hostrcb = list_entry(ioa_cfg->hostrcb_free_q.next,
				     struct ipr_hostrcb, queue);
		list_del(&hostrcb->queue);

		ipr_af_init(ioa_cfg, res, hostrcb, ipr_cmd);
	}

	ipr_cmd->job_step = ipr_wait_for_dev_init_done;

	return IPR_RC_JOB_CONTINUE;
}

/**
 * ipr_wait_for_dev_init_done - Wait for completion of device initialization.
 * @ipr_cmd:	ipr command struct
 *
 * This function waits for all outstanding device initialization jobs
 * to complete.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_wait_for_dev_init_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_hostrcb *hostrcb;
	int num_free_hostrcbs = 0;

	list_for_each_entry(hostrcb, &ioa_cfg->hostrcb_free_q, queue)
		num_free_hostrcbs++;

	if (num_free_hostrcbs == IPR_NUM_HCAMS) {
		ipr_cmd->job_step = ipr_ioa_reset_done;
		return IPR_RC_JOB_CONTINUE;
	}

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioa_reset_done - IOA reset completion.
 * @ipr_cmd:	ipr command struct
 *
 * This function processes the completion of an adapter reset.
 * It schedules any necessary mid-layer add/removes and
 * wakes any reset sleepers.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioa_reset_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_resource_entry *res;
	struct ipr_hostrcb *hostrcb, *temp;

	ENTER;

	ioa_cfg->in_reset_reload = 0;
	ioa_cfg->allow_cmds = 1;
	ioa_cfg->reset_cmd = NULL;

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if (res->add_to_ml || res->del_from_ml) {
			ipr_trace;
			schedule_work(&ioa_cfg->low_pri_work);
			break;
		}
	}

	list_for_each_entry_safe(hostrcb, temp, &ioa_cfg->hostrcb_free_q, queue) {
		list_del(&hostrcb->queue);
		if (hostrcb->op_code == IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE)
			ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);
		else
			ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_LOG_DATA, hostrcb);
	}

	dev_info(&ioa_cfg->pdev->dev, "IOA initialized.\n");

	ioa_cfg->reset_retries = 0;

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	wake_up_all(&ioa_cfg->reset_wait_q);

	if (ioa_cfg->sdt_state == DUMP_OBTAINED)
		wake_up_interruptible(&ioa_cfg->sdt_wait_q);

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_ioa_bringdown_done - IOA bring down completion.
 * @ipr_cmd:	ipr command struct
 *
 * This function processes the completion of an adapter bring down.
 * It wakes any reset sleepers.
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_ioa_bringdown_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;

	ioa_cfg->in_reset_reload = 0;

	ioa_cfg->reset_retries = 0;

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	wake_up_all(&ioa_cfg->reset_wait_q);

	LEAVE;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_handle_other_interrupt - Handle "other" interrupts
 * @ioa_cfg:	ioa config struct
 * @int_reg:	interrupt register
 *
 * Return value:
 * 	IRQ_NONE / IRQ_HANDLED
 **/
static irqreturn_t ipr_handle_other_interrupt(struct ipr_ioa_cfg *ioa_cfg,
					      volatile u32 int_reg)
{
	irqreturn_t rc = IRQ_HANDLED;

	if (int_reg & IPR_PCII_IOA_TRANS_TO_OPER) {
		/* Mask the interrupt */
		writel(IPR_PCII_IOA_TRANS_TO_OPER, ioa_cfg->regs.set_interrupt_mask_reg);
		int_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);

		/* Clear the interrupt */
		writel(IPR_PCII_IOA_TRANS_TO_OPER, ioa_cfg->regs.clr_interrupt_reg);
		int_reg = readl(ioa_cfg->regs.sense_interrupt_reg);

		del_timer(&ioa_cfg->reset_cmd->timer);
		ipr_reset_ioa_job(ioa_cfg->reset_cmd);
	} else if (int_reg & IPR_PCII_ERROR_INTERRUPTS) {
		if (int_reg & IPR_PCII_IOA_UNIT_CHECKED)
			ioa_cfg->ioa_unit_checked = 1;
		else
			dev_err(&ioa_cfg->pdev->dev,
				"Permanent IOA failure. 0x%08X\n", int_reg);

		if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
			ioa_cfg->sdt_state = GET_DUMP;

		ipr_block_requests(ioa_cfg);
		ipr_mask_and_clear_all_interrupts(ioa_cfg);
		ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
	} else {
		rc = IRQ_NONE;
		dev_err(&ioa_cfg->pdev->dev, "Unknown interrupt received: 0x%08X\n",
			int_reg);
	}

	return rc;
}

/**
 * ipr_isr - Interrupt service routine
 * @irq:	irq number
 * @devp:	pointer to ioa config struct
 * @regs:	pt_regs struct
 *
 * Return value:
 * 	IRQ_NONE / IRQ_HANDLED
 **/
irqreturn_t ipr_isr(int irq, void *devp, struct pt_regs *regs)
{
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *)devp;
	unsigned long lock_flags = 0;
	volatile u32 int_reg, int_mask_reg;
	u32 ioasc;
	u16 cmd_index;
	struct ipr_cmnd *ipr_cmd;
	irqreturn_t rc = IRQ_NONE;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	/* If interrupts are disabled, ignore the interrupt */
	if (!ioa_cfg->allow_interrupts) {
		ipr_trace;
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return IRQ_NONE;
	}

	int_mask_reg = readl(ioa_cfg->regs.sense_interrupt_mask_reg);
	int_reg = readl(ioa_cfg->regs.sense_interrupt_reg) & ~int_mask_reg;

	/* If an interrupt on the adapter did not occur, ignore it */
	if (unlikely((int_reg & IPR_PCII_OPER_INTERRUPTS) == 0)) {
		ipr_trace;
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return IRQ_NONE;
	}

	while (1) {
		ipr_cmd = NULL;

		while ((be32_to_cpu(*ioa_cfg->hrrq_curr) & IPR_HRRQ_TOGGLE_BIT) ==
		       ioa_cfg->toggle_bit) {

			cmd_index = (be32_to_cpu(*ioa_cfg->hrrq_curr) &
				     IPR_HRRQ_REQ_RESP_HANDLE_MASK) >> IPR_HRRQ_REQ_RESP_HANDLE_SHIFT;

			if (unlikely(cmd_index >= IPR_NUM_CMD_BLKS)) {
				ioa_cfg->errors_logged++;
				dev_err(&ioa_cfg->pdev->dev, "Invalid response handle from IOA\n");

				if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
					ioa_cfg->sdt_state = GET_DUMP;

				ipr_block_requests(ioa_cfg);
				ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);
				return IRQ_HANDLED;
			}

			ipr_cmd = ioa_cfg->ipr_cmnd_list[cmd_index];

			ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

			ipr_trc_hook(ipr_cmd, IPR_TRACE_FINISH, ioasc);

			list_del(&ipr_cmd->queue);
			del_timer(&ipr_cmd->timer);
			ipr_cmd->done(ipr_cmd);

			rc = IRQ_HANDLED;

			if (ioa_cfg->hrrq_curr < ioa_cfg->hrrq_end) {
				ioa_cfg->hrrq_curr++;
			} else {
				ioa_cfg->hrrq_curr = ioa_cfg->hrrq_start;
				ioa_cfg->toggle_bit ^= 1u;
			}
		}

		if (ipr_cmd != NULL) {
			/* Clear the PCI interrupt */
			writel(IPR_PCII_HRRQ_UPDATED, ioa_cfg->regs.clr_interrupt_reg);
			int_reg = readl(ioa_cfg->regs.sense_interrupt_reg) & ~int_mask_reg;
		} else
			break;
	}

	if (unlikely(rc == IRQ_NONE))
		rc = ipr_handle_other_interrupt(ioa_cfg, int_reg);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	return rc;
}

/**
 * ipr_do_req -  Send driver initiated requests.
 * @ipr_cmd:		ipr command struct
 * @done:			done function
 * @timeout_func:	timeout function
 * @timeout:		timeout value
 *
 * This function sends the specified command to the adapter with the
 * timeout given. The done function is invoked on command completion.
 *
 * Return value:
 * 	none
 **/
static void ipr_do_req(struct ipr_cmnd *ipr_cmd,
		       void (*done) (struct ipr_cmnd *),
		       void (*timeout_func) (struct ipr_cmnd *), u32 timeout)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->pending_q);

	ipr_cmd->done = done;

	ipr_cmd->timer.data = (unsigned long) ipr_cmd;
	ipr_cmd->timer.expires = jiffies + timeout;
	ipr_cmd->timer.function = (void (*)(unsigned long))timeout_func;

	add_timer(&ipr_cmd->timer);

	ipr_trc_hook(ipr_cmd, IPR_TRACE_START, 0);

	writel(be32_to_cpu(ipr_cmd->ioarcb.ioarcb_host_pci_addr),
	       ioa_cfg->regs.ioarrin_reg);
}

/**
 * ipr_timeout -  An internally generated op has timed out.
 * @ipr_cmd:	ipr command struct
 *
 * This function blocks host requests and initiates an
 * adapter reset.
 *
 * Return value:
 * 	none
 **/
static void ipr_timeout(struct ipr_cmnd *ipr_cmd)
{
	unsigned long lock_flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	ENTER;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	ioa_cfg->errors_logged++;
	dev_err(&ioa_cfg->pdev->dev,
		"Adapter being reset due to command timeout.\n");

	ipr_block_requests(ioa_cfg);

	if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
		ioa_cfg->sdt_state = GET_DUMP;

	ipr_initiate_ioa_reset(ioa_cfg, IPR_SHUTDOWN_NONE);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	LEAVE;
}

/**
 * ipr_internal_cmd_done - Op done function for an internally generated op.
 * @ipr_cmd:	ipr command struct
 *
 * This function is the op done function for an internally generated,
 * blocking op. It simply wakes the sleeping thread.
 *
 * Return value:
 * 	none
 **/
static void ipr_internal_cmd_done(struct ipr_cmnd *ipr_cmd)
{
	complete(&ipr_cmd->completion);
}

/**
 * ipr_send_blocking_cmd - Send command and sleep on its completion.
 * @ipr_cmd:	ipr command struct
 * @timeout_func:	function to invoke if command times out
 * @timeout:	timeout
 *
 * Return value:
 * 	none
 **/
static void ipr_send_blocking_cmd(struct ipr_cmnd *ipr_cmd,
				  void (*timeout_func) (struct ipr_cmnd *ipr_cmd),
				  u32 timeout)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;

	init_completion(&ipr_cmd->completion);
	ipr_do_req(ipr_cmd, ipr_internal_cmd_done, timeout_func, timeout);

	spin_unlock_irq(ioa_cfg->host->host_lock);
	wait_for_completion(&ipr_cmd->completion);
	spin_lock_irq(ioa_cfg->host->host_lock);
}


/**
 * ipr_slave_alloc - Prepare for commands to a device.
 * @scsi_device:	scsi device struct
 *
 * This function saves a pointer to the resource entry
 * in the scsi device struct if the device exists. We
 * can then use this pointer in ipr_queue when handling
 * new commands.
 *
 * Return value:
 * 	0 on success 
 **/
int ipr_slave_alloc(struct scsi_device *scsi_device)
{
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) scsi_device->host->hostdata;
	struct ipr_resource_entry *res;
	unsigned long lock_flags;

	scsi_device->hostdata = NULL;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if ((res->cfgte.res_addr.bus == scsi_device->channel) &&
		    (res->cfgte.res_addr.target == scsi_device->id) &&
		    (res->cfgte.res_addr.lun == scsi_device->lun) &&
		    !ipr_is_af_dasd_device(res)) {
			if (ioa_cfg->allow_ml_add_del)
				ipr_trace;
			res->scsi_device = scsi_device;
			res->add_to_ml = 0;
			scsi_device->hostdata = res;
			res->needs_sync_complete = 1;
			break;
		}
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return 0;
}

/**
 * ipr_configure_vset - Configure a volume set resource.
 * @scsi_device:	scsi device struct
 *
 * This function modifies the queue depth for a volume set
 * resource based on the number of devices in the volume set.
 *
 * Return value:
 * 	0 on success 
 **/
static int ipr_configure_vset(struct scsi_device *scsi_device)
{
	struct ipr_resource_entry *res = scsi_device->hostdata;
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioa_cfg *ioa_cfg;
	u32 dma_addr;
	struct ipr_query_res_state *res_state;
	unsigned long lock_flags = 0;
	int q_depth;
	u32 ioasc;

	ioa_cfg = (struct ipr_ioa_cfg *) scsi_device->host->hostdata;

	res_state = pci_alloc_consistent(ioa_cfg->pdev,
					 sizeof(struct ipr_query_res_state),
					 &dma_addr);

	if (!res_state)
		return -ENOMEM;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);

	ipr_build_query_res_state(res, ipr_cmd, dma_addr);

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_INTERNAL_DEV_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	if (ioasc) {
		ipr_sdev_err(scsi_device,
			     "Query resource state failed with IOASC: 0x%08X\n",
			     ioasc);
	} else {
		q_depth = res_state->vset.num_devices_in_vset *
			IPR_NUM_CMDS_PER_DEV_IN_VSET;
		scsi_adjust_queue_depth(scsi_device, 0, q_depth);
		ipr_sdev_info(scsi_device, "Setting queue depth to %d\n:", q_depth);
	}

	pci_free_consistent(ioa_cfg->pdev,
			    sizeof(struct ipr_query_res_state),
			    res_state, dma_addr);

	return (ioasc ? -EIO : 0);
}

/**
 * ipr_configure_gpdd_tcq - Configure a SCSI disk to run TCQ.
 * @scsi_device:	scsi device struct
 *
 * This function sets up the control mode page to enable
 * tagged command queueing on the adapter. The adapter requires
 * that we run with QERR set to to 1, so we set that up here.
 *
 * Return value:
 * 	0 on success 
 **/
static int ipr_configure_gpdd_tcq(struct scsi_device *scsi_device)
{
	struct ipr_ioa_cfg *ioa_cfg = (struct ipr_ioa_cfg *) scsi_device->host->hostdata;
	struct ipr_resource_entry *res = scsi_device->hostdata;
	int len = 0;
	struct ipr_mode_pages *mode_buf, *ch_mode_buf;
	u32 dma_addr, ch_dma_addr;
	unsigned long lock_flags = 0;
	u32 ioasc;

	mode_buf = pci_alloc_consistent(ioa_cfg->pdev,
					sizeof(struct ipr_mode_pages), &dma_addr);

	if (!mode_buf)
		return -ENOMEM;

	ch_mode_buf = pci_alloc_consistent(ioa_cfg->pdev,
					   sizeof(struct ipr_mode_pages), &ch_dma_addr);

	if (!ch_mode_buf) {
		pci_free_consistent(ioa_cfg->pdev,
				    sizeof(struct ipr_mode_pages), mode_buf, dma_addr);
		return -ENOMEM;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	ioasc = ipr_blocking_mode_sense(ioa_cfg, res, 0x0A, dma_addr,
					sizeof(struct ipr_mode_pages));

	if (!IPR_IOASC_SENSE_KEY(ioasc))
		ioasc = ipr_blocking_mode_sense(ioa_cfg, res, 0x4A, ch_dma_addr,
						sizeof(struct ipr_mode_pages));

	if (!IPR_IOASC_SENSE_KEY(ioasc))
		len = ipr_set_page0x0a(mode_buf, ch_mode_buf);

	if (!IPR_IOASC_SENSE_KEY(ioasc))
		ioasc = ipr_blocking_mode_select(ioa_cfg, res, 0x11, dma_addr, len);

	if (!IPR_IOASC_SENSE_KEY(ioasc)) {
		ipr_sdev_info(scsi_device, "Setting TCQ depth to %d\n",
			      IPR_MAX_TAGGED_CMD_PER_DEV);

		scsi_activate_tcq(scsi_device, IPR_MAX_TAGGED_CMD_PER_DEV);
	}

	pci_free_consistent(ioa_cfg->pdev, sizeof(struct ipr_mode_pages),
			    mode_buf, dma_addr);
	pci_free_consistent(ioa_cfg->pdev, sizeof(struct ipr_mode_pages),
			    ch_mode_buf, ch_dma_addr);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	if (IPR_IOASC_SENSE_KEY(ioasc)) {
		ipr_sdev_err(scsi_device, "Failed to configure device for TCQing. "
			     "IOASC: 0x%08X\n:", ioasc);
	}

	return 0;
}

/**
 * ipr_configure_gpdd_notcq - Configure a SCSI device to run without TCQing.
 * @scsi_device:	scsi device struct
 *
 * This function sets the queue depth for a scsi device.
 *
 * Return value:
 * 	0 on success 
 **/
static int ipr_configure_gpdd_notcq(struct scsi_device *scsi_device)
{
	scsi_adjust_queue_depth(scsi_device, 0, IPR_MAX_CMD_PER_LUN);

	return 0;
}

/**
 * ipr_slave_configure - Configure a SCSI device
 * @scsi_device:	scsi device struct
 *
 * This function configures the specified scsi device for optimal
 * performance.
 *
 * Return value:
 * 	0 on success 
 **/
int ipr_slave_configure(struct scsi_device *scsi_device)
{
	struct ipr_resource_entry *res;
	struct ipr_ioa_cfg *ioa_cfg;
	int rc;

	if (!scsi_device->hostdata)
		return 0;

	res = scsi_device->hostdata;

	ioa_cfg = (struct ipr_ioa_cfg *) scsi_device->host->hostdata;

	/* Since we may be sending a command to the resource, down the
	 IOCTL semaphore to make sure we don't run out of resources */
	if (down_interruptible(&ioa_cfg->ioctl_semaphore)) {
		ipr_trace;
		res->scsi_device = NULL;
		scsi_device->hostdata = NULL;
		return -EINTR;
	}

	if (ipr_is_af(res)) {
		rc = ipr_configure_vset(scsi_device);
	} else if (scsi_device->tagged_supported && !ipr_disable_tcq) {
		rc = ipr_configure_gpdd_tcq(scsi_device);
	} else {
		rc = ipr_configure_gpdd_notcq(scsi_device);
	}

	up(&ioa_cfg->ioctl_semaphore);

	if (rc) {
		res->scsi_device = NULL;
		scsi_device->hostdata = NULL;
	}

	return rc;
}

/**
 * ipr_vset_stop_unit - Send a STOP UNIT to a VSET resource
 * @ioa_cfg:	ioa config struct
 * @res:		resource entry struct
 *
 * This function sends a STOP UNIT to a VSET resource to
 * flush the write cache..
 *
 * Return value:
 * 	IOASC of the command
 **/
static u32 ipr_vset_stop_unit(struct ipr_ioa_cfg *ioa_cfg,
			      struct ipr_resource_entry *res)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioarcb *ioarcb;
	u32 ioasc;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ioarcb = &ipr_cmd->ioarcb;

	ioarcb->res_handle = res->cfgte.res_handle;
	ioarcb->cmd_pkt.sync_complete = 1;
	ioarcb->cmd_pkt.cdb[0] = START_STOP;
	ioarcb->cmd_pkt.cdb[4] = IPR_START_STOP_STOP;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_STOP_DEVICE_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	if (ioasc) {
		ipr_res_err(ioa_cfg, res->cfgte.res_addr,
			     "Stop unit failed with IOASC 0x%08X\n", ioasc);
	}

	return ioasc;
}

/**
 * ipr_evaluate_device - Send an Evaluate Device Capabilities
 * @ioa_cfg:	ioa config struct
 * @res:		resource entry struct
 *
 * This function sends an Evaluate Device Capabilities command
 * to the IOA. 
 *
 * Return value:
 * 	IOASC of the command
 **/
static u32 ipr_evaluate_device(struct ipr_ioa_cfg *ioa_cfg,
			       struct ipr_resource_entry *res)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioarcb *ioarcb;
	u32 ioasc;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ioarcb = &ipr_cmd->ioarcb;

	ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
	ioarcb->cmd_pkt.cdb[0] = IPR_EVAL_DEVICE_CAPABILTIES;
	ioarcb->cmd_pkt.cdb[2] = (be32_to_cpu(res->cfgte.res_handle) >> 24) & 0xff;
	ioarcb->cmd_pkt.cdb[3] = (be32_to_cpu(res->cfgte.res_handle) >> 16) & 0xff;
	ioarcb->cmd_pkt.cdb[4] = (be32_to_cpu(res->cfgte.res_handle) >> 8) & 0xff;
	ioarcb->cmd_pkt.cdb[5] = be32_to_cpu(res->cfgte.res_handle) & 0xff;

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_EVALUATE_DEVICE_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	if (ioasc) {
		ipr_res_dbg(ioa_cfg, res->cfgte.res_addr,
			    "Evaluate device failed with IOASC: 0x%08X\n",
			    ioasc);
	}

	return ioasc;
}

/**
 * ipr_slave_destroy - Unconfigure a SCSI device
 * @scsi_device:	scsi device struct
 *
 * This function unconfigures a scsi device. If the device is
 * a volume set, we send a STOP_UNIT to the device. This will
 * flush the write cache for the associated disks (VSET
 * resources do not support the flush cache command), and
 * will also finish any outstanding parity updates.
 *
 * Return value:
 * 	nothing
 **/
void ipr_slave_destroy(struct scsi_device *scsi_device)
{
	struct ipr_resource_entry *res;
	struct ipr_ioa_cfg *ioa_cfg;
	unsigned long lock_flags = 0;

	ioa_cfg = (struct ipr_ioa_cfg *) scsi_device->host->hostdata;

	/* Since we may be sending a command to the resource, down the
	 IOCTL semaphore to make sure we don't run out of resources */
	down(&ioa_cfg->ioctl_semaphore);

	if (scsi_device->hostdata) {
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

		res = (struct ipr_resource_entry *) scsi_device->hostdata;
		res->scsi_device = NULL;
		ioa_cfg = (struct ipr_ioa_cfg *) scsi_device->host->hostdata;

		if (!res->del_from_ml && ioa_cfg->allow_cmds && ipr_is_vset_device(res))
			ipr_vset_stop_unit(ioa_cfg, res);

		if (res->del_from_ml)
			list_move_tail(&res->queue, &ioa_cfg->free_res_q);
		else if (ioa_cfg->allow_cmds)
			ipr_evaluate_device(ioa_cfg, res);

		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
	}

	up(&ioa_cfg->ioctl_semaphore);

	scsi_device->hostdata = NULL;
}

/**
 * ipr_scsi_done - mid-layer done function
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked by the interrupt handler for
 * ops generated by the SCSI mid-layer 
 *
 * Return value:
 * 	none
 **/
static void ipr_scsi_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	u32 ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	scsi_cmd->resid = be32_to_cpu(ipr_cmd->ioasa.residual_data_len);

	if (likely(IPR_IOASC_SENSE_KEY(ioasc) == 0)) {
		ipr_unmap_sglist(ioa_cfg, ipr_cmd);
		list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);
		scsi_cmd->scsi_done(scsi_cmd);
	} else 
		ipr_erp_start(ioa_cfg, ipr_cmd);
}

/**
 * ipr_build_ioadl - Build a scatter/gather list and map the buffer
 * @ioa_cfg:	ioa config struct
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	0 on success / -1 on failure
 **/
static int ipr_build_ioadl(struct ipr_ioa_cfg *ioa_cfg,
			   struct ipr_cmnd *ipr_cmd)
{
	int i;
	struct scatterlist *sglist;
	u32 length;
	u32 ioadl_flags = 0;
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;

	length = scsi_cmd->bufflen;

	if (length == 0)
		return 0;

	if (scsi_cmd->use_sg) {
		ipr_cmd->dma_use_sg = pci_map_sg(ioa_cfg->pdev,
						 scsi_cmd->request_buffer,
						 scsi_cmd->use_sg,
						 scsi_cmd->sc_data_direction);

		if (scsi_cmd->sc_data_direction == DMA_TO_DEVICE) {
			ioadl_flags = IPR_IOADL_FLAGS_WRITE;
			ioarcb->cmd_pkt.write_not_read = 1;
			ioarcb->write_data_transfer_length = cpu_to_be32(length);
			ioarcb->write_ioadl_len =
				cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);
		}
		else if (scsi_cmd->sc_data_direction == DMA_FROM_DEVICE) {
			ioadl_flags = IPR_IOADL_FLAGS_READ;
			ioarcb->read_data_transfer_length = cpu_to_be32(length);
			ioarcb->read_ioadl_len =
				cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);
		}

		sglist = scsi_cmd->request_buffer;

		for (i = 0; i < ipr_cmd->dma_use_sg; i++) {
			ioadl[i].flags_and_data_len =
				cpu_to_be32(ioadl_flags | sg_dma_len(&sglist[i]));

			ioadl[i].address =
				cpu_to_be32(sg_dma_address(&sglist[i]));
		}

		if (likely(ipr_cmd->dma_use_sg)) {
			ioadl[i-1].flags_and_data_len |=
				cpu_to_be32(IPR_IOADL_FLAGS_LAST);
		}
		else
			dev_err(&ioa_cfg->pdev->dev, "pci_map_sg failed!\n");
	} else {
		if (scsi_cmd->sc_data_direction == DMA_TO_DEVICE) {
			ioadl_flags = IPR_IOADL_FLAGS_WRITE;
			ioarcb->cmd_pkt.write_not_read = 1;
			ioarcb->write_data_transfer_length = cpu_to_be32(length);
			ioarcb->write_ioadl_len =
				cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);
		}
		else if (scsi_cmd->sc_data_direction == DMA_FROM_DEVICE) {
			ioadl_flags = IPR_IOADL_FLAGS_READ;
			ioarcb->read_data_transfer_length = cpu_to_be32(length);
			ioarcb->read_ioadl_len =
				cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);
		}

		ipr_cmd->dma_handle = pci_map_single(ioa_cfg->pdev,
						     scsi_cmd->buffer, length,
						     scsi_cmd->sc_data_direction);

		if (likely(!pci_dma_error(ipr_cmd->dma_handle))) {
			ipr_cmd->dma_use_sg = 1;

			ioadl[0].flags_and_data_len =
				cpu_to_be32(ioadl_flags | length | IPR_IOADL_FLAGS_LAST);

			ioadl[0].address = cpu_to_be32(ipr_cmd->dma_handle);
		} else
			dev_err(&ioa_cfg->pdev->dev, "pci_map_single failed!\n");
	}

	if (ipr_cmd->dma_use_sg)
		return 0;

	return -1;
}

/**
 * ipr_send_cmd - Build and send a mid-layer command
 * @ioa_cfg:	ioa config struct
 * @scsi_cmd:	scsi command struct
 * @res:		ipr resource entry
 *
 * Return value:
 * 	0 on success / SCSI_MLQUEUE_HOST_BUSY if DMA mapping fails
 **/
static int ipr_send_cmd(struct ipr_ioa_cfg *ioa_cfg,
			struct scsi_cmnd *scsi_cmd,
			struct ipr_resource_entry *res)
{
	struct ipr_cmnd *ipr_cmd;
	int rc = 0;
	void (*timeout_func) (struct scsi_cmnd *);
	int timeout;
	struct ipr_ioarcb *ioarcb;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ioarcb = &ipr_cmd->ioarcb;

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->pending_q);

	memcpy(ioarcb->cmd_pkt.cdb, scsi_cmd->cmnd, scsi_cmd->cmd_len);
	ipr_cmd->scsi_cmd = scsi_cmd;
	ioarcb->res_handle = res->cfgte.res_handle;

	/*
	 * Double the timeout value to use as we will use the adapter
	 * as the primary timing mechanism
	 */
	timeout_func = (void (*)(struct scsi_cmnd *)) scsi_cmd->eh_timeout.function;
	timeout = scsi_cmd->timeout_per_command;

	scsi_add_timer(scsi_cmd, timeout * IPR_TIMEOUT_MULTIPLIER, timeout_func);

	ipr_cmd->done = ipr_scsi_done;

	ipr_trc_hook(ipr_cmd, IPR_TRACE_START, IPR_GET_PHYS_LOC(res->cfgte.res_addr));

	if ((timeout / HZ) > IPR_MAX_SECOND_RADIX_TIMEOUT) {
		ioarcb->cmd_pkt.timeout =
			cpu_to_be16(((timeout / HZ) / 60) | IPR_TIMEOUT_MINUTE_RADIX);
	} else {
		ioarcb->cmd_pkt.timeout = cpu_to_be16(timeout / HZ);
	}

	if (scsi_cmd->underflow == 0)
		ioarcb->cmd_pkt.no_underlength_checking = 1;

	if (res->needs_sync_complete) {
		ioarcb->cmd_pkt.sync_complete = 1;
		res->needs_sync_complete = 0;
	}

	ioarcb->cmd_pkt.ext_delay_after_reset = 1;
	ioarcb->cmd_pkt.aligned_client_buffer = 1;
	ioarcb->cmd_pkt.no_link_descriptors = 1;
	ioarcb->cmd_pkt.task_attributes = ipr_get_task_attributes(scsi_cmd);

	rc = ipr_build_ioadl(ioa_cfg, ipr_cmd);

	if (likely(rc == 0)) {
		writel(be32_to_cpu(ipr_cmd->ioarcb.ioarcb_host_pci_addr),
		       ioa_cfg->regs.ioarrin_reg);
	} else {
		 list_move_tail(&ipr_cmd->queue, &ioa_cfg->free_q);
		 rc = SCSI_MLQUEUE_HOST_BUSY;
	}

	return rc;
}


/**
 * ipr_queue - Queue a mid-layer request
 * @scsi_cmd:	scsi command struct
 * @done:		done function
 *
 * This function queues a request generated by the mid-layer.
 *
 * Return value:
 *	0 on success
 *	SCSI_MLQUEUE_DEVICE_BUSY if device is busy
 *	SCSI_MLQUEUE_HOST_BUSY if host is busy
 **/
int ipr_queue(struct scsi_cmnd *scsi_cmd, void (*done) (struct scsi_cmnd *))
{
	struct ipr_ioa_cfg *ioa_cfg;
	struct ipr_resource_entry *res;
	int rc = 0;

	scsi_cmd->scsi_done = done;

	ioa_cfg = (struct ipr_ioa_cfg *)scsi_cmd->device->host->hostdata;
	res = scsi_cmd->device->hostdata;

	scsi_cmd->result = (DID_OK << 16);

	if (unlikely(ioa_cfg->block_host_ops)) {
		/*
		 * We are currently blocking all devices due to a host reset 
		 * We have told the host to stop giving us new requests, but
		 * retries on failed ops don't count. xxx may be able to remove
		 */
		ipr_trace;
		return 0;
	}

	/* xxx can I remove some of these checks? */
	if (unlikely(ioa_cfg->ioa_is_dead || !res || res->del_from_ml)) {
		memset(scsi_cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		scsi_cmd->result = (DID_NO_CONNECT << 16);
		scsi_cmd->scsi_done(scsi_cmd);
		return 0;
	}

	rc = ipr_send_cmd(ioa_cfg, scsi_cmd, res);

	return rc;
}

/**
 * ipr_get_error - Find the specfied IOASC in the ipr_error_table.
 * @ioasc:	IOASC
 *
 * This function will return the index of into the ipr_error_table
 * for the specified IOASC. If the IOASC is not in the table,
 * 0 will be returned, which points to the entry used for unknown errors.
 *
 * Return value:
 * 	index into the ipr_error_table
 **/
static u32 ipr_get_error(u32 ioasc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ipr_error_table); i++)
		if (ipr_error_table[i].ioasc == ioasc)
			return i;

	return 0;
}

/**
 * ipr_dump_ioasa - Dump contents of IOASA
 * @ioa_cfg:	ioa config struct
 * @ipr_cmd:	ipr command struct
 *
 * This function is invoked by the interrupt handler when ops
 * fail. It will log the IOASA if appropriate. Only called
 * for GPDD ops.
 *
 * Return value:
 * 	none
 **/
static void ipr_dump_ioasa(struct ipr_ioa_cfg *ioa_cfg,
			   struct ipr_cmnd *ipr_cmd)
{
	int i;
	u16 data_len;
	u32 ioasc;
	struct ipr_ioasa *ioasa = &ipr_cmd->ioasa;
	u32 *ioasa_data = (u32 *)ioasa;
	int error_index;

	ioasc = be32_to_cpu(ioasa->ioasc) & IPR_IOASC_IOASC_MASK;

	if (0 == ioasc)
		return;

	if (ioa_cfg->debug_level < IPR_DEFAULT_DEBUG_LEVEL)
		return;

	if (ioa_cfg->debug_level < IPR_MAX_DEBUG_LEVEL) {
		/* Don't dump recovered errors */
		if (IPR_IOASC_SENSE_KEY(ioasc) < 2)
			return;

		/* Don't log an error if the IOA already logged one */
		if (ioasa->ilid != 0)
			return;
	}

	error_index = ipr_get_error(ioasc);

	if (ipr_error_table[error_index].log_ioasa == 0)
		return;

	if ((ioasa->gpdd.device_end_state <= ARRAY_SIZE(ipr_gpdd_dev_end_states)) &&
	    (ioasa->gpdd.device_bus_phase <= ARRAY_SIZE(ipr_gpdd_dev_bus_phases))) {
		ipr_sdev_err(ipr_cmd->scsi_cmd->device,
			     "%s End state: %s Phase: %s\n",
			     ipr_error_table[error_index].error,
			     ipr_gpdd_dev_end_states[ioasa->gpdd.device_end_state],
			     ipr_gpdd_dev_bus_phases[ioasa->gpdd.device_bus_phase]);
	} else {
		ipr_sdev_err(ipr_cmd->scsi_cmd->device, "%s\n",
			     ipr_error_table[error_index].error);
	}

	if (sizeof(struct ipr_ioasa) < be16_to_cpu(ioasa->ret_stat_len))
		data_len = sizeof(struct ipr_ioasa);
	else
		data_len = be16_to_cpu(ioasa->ret_stat_len);

	ipr_err("IOASA Dump:\n");

	for (i = 0; i < data_len / 4; i += 4) {
		ipr_err("%08X: %08X %08X %08X %08X\n", i*4,
			be32_to_cpu(ioasa_data[i]),
			be32_to_cpu(ioasa_data[i+1]),
			be32_to_cpu(ioasa_data[i+2]),
			be32_to_cpu(ioasa_data[i+3]));
	}
}

/**
 * ipr_info - Get information about the card/driver
 * @scsi_host:	scsi host struct
 *
 * Return value:
 * 	pointer to buffer with description string
 **/
const char * ipr_ioa_info(struct Scsi_Host *host)
{
	static char buffer[512];
	struct ipr_ioa_cfg *ioa_cfg;
	unsigned long lock_flags = 0;

	ioa_cfg = (struct ipr_ioa_cfg *) host->hostdata;

	spin_lock_irqsave(host->host_lock, lock_flags);
	sprintf(buffer, "IBM %X Storage Adapter", ioa_cfg->type);
	spin_unlock_irqrestore(host->host_lock, lock_flags);

	return buffer;
}

/**
 * ipr_cancel_op - Cancel specified op
 * @scsi_cmd:	scsi command struct
 *
 * This function cancels specified op.
 *
 * Return value:
 *	SUCCESS / FAILED
 **/
static int ipr_cancel_op(struct scsi_cmnd * scsi_cmd)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioa_cfg *ioa_cfg;
	struct ipr_resource_entry *res;
	struct ipr_cmd_pkt *cmd_pkt;
	u32 ioasc, ioarcb_addr;
	int op_found = 0;

	ENTER;

	ioa_cfg = (struct ipr_ioa_cfg *)scsi_cmd->device->host->hostdata;
	res = scsi_cmd->device->hostdata;

	list_for_each_entry(ipr_cmd, &ioa_cfg->pending_q, queue) {
		if (ipr_cmd->scsi_cmd == scsi_cmd) {
			ipr_cmd->done = ipr_scsi_eh_done;
			op_found = 1;
			break;
		}
	}

	if (!op_found)
		return SUCCESS;

	ioarcb_addr = be32_to_cpu(ipr_cmd->ioarcb.ioarcb_host_pci_addr);

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);

	ipr_cmd->ioarcb.res_handle = res->cfgte.res_handle;
	cmd_pkt = &ipr_cmd->ioarcb.cmd_pkt;
	cmd_pkt->request_type = IPR_RQTYPE_IOACMD;
	cmd_pkt->cdb[0] = IPR_ABORT_TASK;
	cmd_pkt->cdb[2] = (ioarcb_addr >> 24) & 0xff;
	cmd_pkt->cdb[3] = (ioarcb_addr >> 16) & 0xff;
	cmd_pkt->cdb[4] = (ioarcb_addr >> 8) & 0xff;
	cmd_pkt->cdb[5] = ioarcb_addr & 0xff;

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_ABORT_TASK_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	res->needs_sync_complete = 1;

	LEAVE;

	return (IPR_IOASC_SENSE_KEY(ioasc) ? FAILED : SUCCESS);
}

/**
 * ipr_eh_abort - Abort a single op
 * @scsi_cmd:	scsi command struct
 *
 * Return value:
 * 	SUCCESS / FAILED
 **/
int ipr_eh_abort(struct scsi_cmnd * scsi_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg;

	ENTER;

	ioa_cfg = (struct ipr_ioa_cfg *) scsi_cmd->device->host->hostdata;

	/* If we are currently going through reset/reload, return failed. This will force the
	   mid-layer to call ipr_eh_host_reset, which will then go to sleep and wait for the
	   reset to complete */
	if (ioa_cfg->in_reset_reload)
		return FAILED;

	if (ioa_cfg->ioa_is_dead)
		return FAILED;

	if (!scsi_cmd->device->hostdata)
		return FAILED;

	LEAVE;

	return ipr_cancel_op(scsi_cmd);
}

/**
 * ipr_eh_dev_reset - Reset the device
 * @scsi_cmd:	scsi command struct
 *
 * This function issues a device reset to the affected device.
 * A LUN reset will be sent to the device first. If that does
 * not work, a target reset will be sent.
 *
 * Return value:
 *	SUCCESS / FAILED
 **/
int ipr_eh_dev_reset(struct scsi_cmnd * scsi_cmd)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioa_cfg *ioa_cfg;
	struct ipr_resource_entry *res;
	struct ipr_cmd_pkt *cmd_pkt;
	u32 ioasc;

	ENTER;

	ioa_cfg = (struct ipr_ioa_cfg *) scsi_cmd->device->host->hostdata;
	res = scsi_cmd->device->hostdata;

	/*
	 * If we are currently going through reset/reload, return failed. This will force the
	 * mid-layer to call ipr_eh_host_reset, which will then go to sleep and wait for the
	 * reset to complete
	 */
	if (ioa_cfg->in_reset_reload)
		return FAILED;

	if (ioa_cfg->ioa_is_dead)
		return FAILED;

	list_for_each_entry(ipr_cmd, &ioa_cfg->pending_q, queue) {
		if (ipr_cmd->ioarcb.res_handle == res->cfgte.res_handle) {
			if (ipr_cmd->scsi_cmd)
				ipr_cmd->done = ipr_scsi_eh_done;
		}
	}

	res->resetting_device = 1;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);

	ipr_cmd->ioarcb.res_handle = res->cfgte.res_handle;
	cmd_pkt = &ipr_cmd->ioarcb.cmd_pkt;
	cmd_pkt->request_type = IPR_RQTYPE_IOACMD;
	cmd_pkt->sync_complete = 1;
	cmd_pkt->cdb[0] = IPR_RESET_DEVICE;
	cmd_pkt->cdb[2] = IPR_RESET_TYPE_SELECT | IPR_LUN_RESET | IPR_TARGET_RESET;

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_DEVICE_RESET_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	res->resetting_device = 0;

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	LEAVE;

	return (ioasc ? FAILED : SUCCESS);
}

/**
 * ipr_reset_reload - Reset/Reload the IOA
 * @ioa_cfg:		ioa config struct
 * @shutdown_type:	shutdown type
 *
 * This function resets the adapter and re-initializes it.
 * This function assumes that all new host commands have been stopped.
 * Return value:
 * 	SUCCESS / FAILED
 **/
static int ipr_reset_reload(struct ipr_ioa_cfg *ioa_cfg,
			    enum ipr_shutdown_type shutdown_type)
{
	if (ioa_cfg->ioa_is_dead)
		return FAILED;

	if (ioa_cfg->in_reset_reload) {
		ipr_sleep_on(ioa_cfg, &ioa_cfg->reset_wait_q);

		/* If we got hit with a host reset while we were already resetting
		   the adapter for some reason, and the reset failed. */
		if (ioa_cfg->ioa_is_dead) {
			ipr_trace;
			return FAILED;
		}
	} else {
		ipr_initiate_ioa_reset(ioa_cfg, shutdown_type);

		if (ioa_cfg->in_reset_reload)
			ipr_sleep_on(ioa_cfg, &ioa_cfg->reset_wait_q);

		if (ioa_cfg->ioa_is_dead) {
			ipr_trace;
			return FAILED;
		}
	}

	return SUCCESS;
}

/**
 * ipr_eh_host_reset - Reset the host adapter
 * @scsi_cmd:	scsi command struct
 *
 * Return value:
 * 	SUCCESS / FAILED
 **/
int ipr_eh_host_reset(struct scsi_cmnd * scsi_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg;
	int rc;

	ENTER;

	ioa_cfg = (struct ipr_ioa_cfg *) scsi_cmd->device->host->hostdata;

	dev_err(&ioa_cfg->pdev->dev,
		"Adapter being reset as a result of error recovery.\n");

	if (WAIT_FOR_DUMP == ioa_cfg->sdt_state)
		ioa_cfg->sdt_state = GET_DUMP;

	rc = ipr_reset_reload(ioa_cfg, IPR_SHUTDOWN_ABBREV);

	if (rc != SUCCESS)
		dev_printk(KERN_CRIT, &ioa_cfg->pdev->dev,
			   "Reset of IOA failed.\n");

	LEAVE;

	return rc;
}

/**
 * ipr_biosparam - Return the HSC mapping
 * @scsi_device:	scsi device struct
 * @block_device:	block device pointer
 * @capacity:	capacity of the device
 * @parm:		Array containing returned HSC values. 
 *
 * This function generates the HSC parms that fdisk uses.
 * We want to make sure we return something that places partitions
 * on 4k boundaries for best performance with the IOA.
 *
 * Return value:
 * 	0 on success 
 **/
int ipr_biosparam(struct scsi_device *scsi_device,
		  struct block_device *block_device,
		  sector_t capacity, int *parm)
{
	int heads, sectors, cylinders;

	heads = 128;
	sectors = 32;

	cylinders = (capacity / (128 * 32));

	/* return result */
	parm[0] = heads;
	parm[1] = sectors;
	parm[2] = cylinders;

	return 0;
}

/**
 * ipr_shutdown - Shutdown handler.
 * @dev:	device struct
 *
 * This function is invoked upon system shutdown/reboot. It will issue
 * an adapter shutdown to the adapter to flush the write cache.
 *
 * Return value:
 * 	none
 **/
static void ipr_shutdown(struct device *dev)
{
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(to_pci_dev(dev));
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ipr_initiate_ioa_bringdown(ioa_cfg, IPR_SHUTDOWN_NORMAL);
	if (ioa_cfg->in_reset_reload)
		ipr_sleep_on(ioa_cfg, &ioa_cfg->reset_wait_q);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
}

/**
 * ipr_shutdown_ioa - Shutdown the specified adapter.
 * @ioa_cfg:		ioa config struct
 *
 * This function issues an adapter shutdown to the specified adapter to
 * flush the adapter's write cache.
 *
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static u32 ipr_shutdown_ioa(struct ipr_ioa_cfg *ioa_cfg)
{
	u32 rc = 0;
	struct ipr_cmnd *ipr_cmd;
	u32 ioasc;

	ENTER;

	ioa_cfg->allow_cmds = 0;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);

	ipr_cmd->ioarcb.res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);

	ipr_cmd->ioarcb.cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
	ipr_cmd->ioarcb.cmd_pkt.cdb[0] = IPR_IOA_SHUTDOWN;
	ipr_cmd->ioarcb.cmd_pkt.cdb[1] = IPR_SHUTDOWN_NORMAL;

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_SHUTDOWN_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	if (IPR_IOASC_SENSE_KEY(ioasc) != 0) {
		dev_err(&ioa_cfg->pdev->dev,
			"Shutdown to IOA failed with IOASC: 0x%08X\n", ioasc);
		rc = -EIO;
	}

	LEAVE;
	return rc;
}

/**
 * ipr_send_hcam - Send an HCAM to the adapter.
 * @ioa_cfg:	ioa config struct
 * @type:		HCAM type
 * @hostrcb:	hostrcb struct
 *
 * This function will send a Host Controlled Async command to the adapter.
 * If HCAMs are currently not allowed to be issued to the adapter, it will
 * place the hostrcb on the free queue.
 *
 * Return value:
 * 	none
 **/
static void ipr_send_hcam(struct ipr_ioa_cfg *ioa_cfg, u8 type,
			  struct ipr_hostrcb *hostrcb)
{
	struct ipr_cmnd *ipr_cmd;
	struct ipr_ioarcb *ioarcb;

	if (ioa_cfg->allow_cmds) {
		ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
		list_add_tail(&ipr_cmd->queue, &ioa_cfg->pending_q);
		list_add_tail(&hostrcb->queue, &ioa_cfg->hostrcb_pending_q);

		ipr_cmd->hostrcb = hostrcb;
		ioarcb = &ipr_cmd->ioarcb;

		ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
		ioarcb->cmd_pkt.request_type = IPR_RQTYPE_HCAM;
		ioarcb->cmd_pkt.cdb[0] = IPR_HOST_CONTROLLED_ASYNC;
		ioarcb->cmd_pkt.cdb[1] = type;
		ioarcb->cmd_pkt.cdb[7] = (sizeof(struct ipr_hostrcb) >> 8) & 0xff;
		ioarcb->cmd_pkt.cdb[8] = sizeof(struct ipr_hostrcb) & 0xff;

		ioarcb->read_data_transfer_length =
			cpu_to_be32(sizeof(struct ipr_hostrcb));
		ioarcb->read_ioadl_len =
			cpu_to_be32(sizeof(struct ipr_ioadl_desc));
		ipr_cmd->ioadl[0].flags_and_data_len =
			cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | sizeof(struct ipr_hostrcb));
		ipr_cmd->ioadl[0].address = cpu_to_be32(hostrcb->hostrcb_dma);

		if (type == IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE)
			ipr_cmd->done = ipr_process_ccn;
		else
			ipr_cmd->done = ipr_process_error;

		writel(be32_to_cpu(ipr_cmd->ioarcb.ioarcb_host_pci_addr),
		       ioa_cfg->regs.ioarrin_reg);
	} else {
		list_add_tail(&hostrcb->queue, &ioa_cfg->hostrcb_free_q);
	}
}

/**
 * ipr_handle_config_change - Handle a config change from the adapter
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb
 * 
 * Return value:
 * 	none
 **/
static void ipr_handle_config_change(struct ipr_ioa_cfg *ioa_cfg,
			      struct ipr_hostrcb *hostrcb)
{
	struct ipr_resource_entry *res = NULL;
	struct ipr_config_table_entry *cfgte;
	u32 is_ndn = 1;
	struct scsi_device *scsi_device;

	cfgte = &hostrcb->ccn.cfgte;

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		if (res->cfgte.res_handle == cfgte->res_handle) {
			is_ndn = 0;
			break;
		}
	}

	if (is_ndn) {
		if (list_empty(&ioa_cfg->free_res_q)) {
			ipr_send_hcam(ioa_cfg,
				      IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
				      hostrcb);
			return;
		}

		res = list_entry(ioa_cfg->free_res_q.next,
				 struct ipr_resource_entry, queue);

		list_del(&res->queue);
		memset(res, 0, sizeof(struct ipr_resource_entry));
		res->add_to_ml = 1;
		list_add_tail(&res->queue, &ioa_cfg->used_res_q);
	}

	memcpy(&res->cfgte, cfgte, sizeof(struct ipr_config_table_entry));

	if (hostrcb->notify_type == IPR_HOST_RCB_NOTIF_TYPE_REM_ENTRY) {
		scsi_device = res->scsi_device;

		if (scsi_device) {
			res->del_from_ml = 1;
			schedule_work(&ioa_cfg->low_pri_work);
		} else {
			list_move_tail(&res->queue, &ioa_cfg->free_res_q);
			ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);
		}
	} else
		ipr_dev_init(ioa_cfg, res, hostrcb);
}

/**
 * ipr_process_ccn - Op done function for a CCN.
 * @ipr_cmd:	ipr command struct
 *
 * This function is the op done function for a configuration
 * change notification host controlled async from the adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_process_ccn(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	u32 ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_del(&hostrcb->queue);

	if (ioasc) {
		if (ioasc != IPR_IOASC_IOA_WAS_RESET) {
			dev_err(&ioa_cfg->pdev->dev,
				"Host RCB failed with IOASC: 0x%08X\n", ioasc);
		}

		ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);
	} else {
		ipr_handle_config_change(ioa_cfg, hostrcb);
	}
}

/**
 * ipr_dev_init - Setup a SCSI device for use on this adapter
 * @ioa_cfg:	ioa config struct
 * @res:		resource entry struct
 * @hostrcb:	hostrcb pointer
 *
 * Return value:
 * 	none
 **/
static void ipr_dev_init(struct ipr_ioa_cfg *ioa_cfg,
			 struct ipr_resource_entry *res,
			 struct ipr_hostrcb *hostrcb)
{
	if (ipr_is_af(res)) {
		if (res->in_init) {
			/* We got another config change for this device while we were in
			 our bringup job. This will simply force us to start over from
			 the beginning */
			res->redo_init = 1;
			ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);
		} else {
			ipr_af_init(ioa_cfg, res, hostrcb, NULL);
		}
	} else if (res->add_to_ml) {
		schedule_work(&ioa_cfg->low_pri_work);
		ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);
	} else {
		ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);
	}
}

/**
 * ipr_af_init - Setup an AF device for use on this adapter
 * @ioa_cfg:	ioa config struct
 * @res:		resource entry struct
 * @hostrcb:	hostrcb pointer
 * @parent_cmd: parent cmd
 *
 * Return value:
 * 	none
 **/
static void ipr_af_init(struct ipr_ioa_cfg *ioa_cfg,
			struct ipr_resource_entry *res,
			struct ipr_hostrcb *hostrcb,
			struct ipr_cmnd *parent_cmd)
{
	struct ipr_cmnd *ipr_cmd;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);

	res->in_init = 1;
	ipr_cmd->hostrcb = hostrcb;
	ipr_cmd->parent = parent_cmd;
	hostrcb->res = res;

	if (ipr_is_af_dasd_device(res)) {
		ipr_cmd->job_step = ipr_std_inquiry;
		ipr_dasd_init_job(ipr_cmd);
	} else {
		ipr_cmd->job_step = ipr_start_unit;
		ipr_vset_init_job(ipr_cmd);
	}
}


/**
 * ipr_vset_init_job - Setup a VSET device for use on this adapter
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static void ipr_vset_init_job(struct ipr_cmnd *ipr_cmd)
{
	u32 rc, ioasc;
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;

	do {
		ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

		if (ioasc) {
			ipr_res_err(ipr_cmd->ioa_cfg, res->cfgte.res_addr,
				    "0x%02X failed with IOASC: 0x%08X\n",
				    ipr_cmd->ioarcb.cmd_pkt.cdb[0], ioasc);

			ipr_cmd->job_step = ipr_vset_init_done;
		} else {
			ipr_reinit_ipr_cmnd(ipr_cmd);
			ipr_cmd->hostrcb = hostrcb;

			if (res->redo_init) {
				ipr_cmd->job_step = ipr_std_inquiry;
				res->redo_init = 0;
			}
		}

		rc = ipr_cmd->job_step(ipr_cmd);
	} while(rc == IPR_RC_JOB_CONTINUE);
}

/**
 * ipr_start_unit - Send Start Unit to a VSET resource
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_start_unit(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	int rc = IPR_RC_JOB_CONTINUE;

	/*
	 * If this device has a scsi device pointer, then the
	 * mid-layer already knows about this device and we could
	 * be going through an adapter reset, in which case we need
	 * to start the device again.
	 */
	if (res->scsi_device) {
		rc = IPR_RC_JOB_RETURN;

		ioarcb->res_handle = res->cfgte.res_handle;
		ioarcb->cmd_pkt.sync_override = 1;
		ioarcb->cmd_pkt.cdb[0] = START_STOP;
		ioarcb->cmd_pkt.cdb[4] = IPR_START_STOP_START;
		ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

		ipr_do_req(ipr_cmd, ipr_vset_init_job, ipr_timeout,
			   IPR_INTERNAL_DEV_TIMEOUT);
	}

	ipr_cmd->job_step = ipr_vset_init_done;

	return rc;
}

/**
 * ipr_vset_init_done - Handle completion of VSET init job
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_RETURN
 **/
static int ipr_vset_init_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_ioa_cfg * ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_resource_entry *res = hostrcb->res;
	struct ipr_cmnd *parent;

	parent = ipr_cmd->parent;

	ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	if (parent)
		parent->done(parent);
	else if (res->add_to_ml)
		schedule_work(&ioa_cfg->low_pri_work);

	return IPR_RC_JOB_RETURN;
}


/**
 * ipr_dasd_init_job - Setup a SCSI disk device for use on this adapter
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	none
 **/
static void ipr_dasd_init_job(struct ipr_cmnd *ipr_cmd)
{
	u32 rc, ioasc;
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;

	do {
		ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

		if (ioasc) {
			ipr_res_err(ipr_cmd->ioa_cfg, res->cfgte.res_addr,
				    "0x%02X failed with IOASC: 0x%08X\n",
				    ipr_cmd->ioarcb.cmd_pkt.cdb[0], ioasc);

			ipr_cmd->job_step = ipr_dev_init_done;
		} else {
			ipr_reinit_ipr_cmnd(ipr_cmd);
			ipr_cmd->hostrcb = hostrcb;

			if (res->redo_init) {
				ipr_cmd->job_step = ipr_std_inquiry;
				res->redo_init = 0;
			}
		}

		rc = ipr_cmd->job_step(ipr_cmd);
	} while(rc == IPR_RC_JOB_CONTINUE);
}

/**
 * ipr_std_inquiry - Send standard inquiry to a SCSI disk device
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_std_inquiry(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ioarcb->res_handle = res->cfgte.res_handle;
	ioarcb->cmd_pkt.cdb[0] = INQUIRY;
	ioarcb->cmd_pkt.cdb[4] = sizeof(struct ipr_std_inq_data);
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

	ioadl->flags_and_data_len =
		cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | sizeof(struct ipr_std_inq_data));
	ioadl->address = cpu_to_be32(hostrcb->hostrcb_dma +
				     offsetof(struct ipr_hostrcb, std_inq));
	ioarcb->read_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ioarcb->read_data_transfer_length =
		cpu_to_be32(sizeof(struct ipr_std_inq_data));

	ipr_do_req(ipr_cmd, ipr_dasd_init_job, ipr_timeout,
		   IPR_INTERNAL_DEV_TIMEOUT);

	ipr_cmd->job_step = ipr_set_supported_devs;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_set_sup_dev_dflt - Initialize a Set Supported Device buffer
 * @supported_dev:	supported device struct
 * @vpids:			vendor product id struct
 * 
 * Return value:
 * 	none
 **/
static void ipr_set_sup_dev_dflt(struct ipr_supported_device *supported_dev,
				 struct ipr_std_inq_vpids *vpids)
{
	memset(supported_dev, 0, sizeof(struct ipr_supported_device));
	memcpy(&supported_dev->vpids, vpids, sizeof(struct ipr_std_inq_vpids));
	supported_dev->num_records = 1;
	supported_dev->data_length =
		cpu_to_be16(sizeof(struct ipr_supported_device));
	supported_dev->reserved = 0;
}

/**
 * ipr_set_supported_devs - Send Set Supported Devices for a device
 * @ipr_cmd:	ipr command struct
 *
 * This function send a Set Supported Devices to the adapter
 *  
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_set_supported_devs(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_std_inq_data *std_inq = &hostrcb->std_inq;
	struct ipr_supported_device *supp_dev = &hostrcb->supp_dev;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ipr_set_sup_dev_dflt(supp_dev, &std_inq->vpids);

	ioarcb->res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
	ioarcb->cmd_pkt.write_not_read = 1;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

	ioarcb->cmd_pkt.cdb[0] = IPR_SET_SUPPORTED_DEVICES;
	ioarcb->cmd_pkt.cdb[7] = (sizeof(struct ipr_supported_device) >> 8) & 0xff;
	ioarcb->cmd_pkt.cdb[8] = sizeof(struct ipr_supported_device) & 0xff;

	ioadl->flags_and_data_len = cpu_to_be32(IPR_IOADL_FLAGS_WRITE_LAST |
						sizeof(struct ipr_supported_device));
	ioadl->address = cpu_to_be32(hostrcb->hostrcb_dma +
				     offsetof(struct ipr_hostrcb, supp_dev));
	ioarcb->write_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ioarcb->write_data_transfer_length =
		cpu_to_be32(sizeof(struct ipr_supported_device));

	ipr_do_req(ipr_cmd, ipr_dasd_init_job, ipr_timeout,
		   IPR_SET_SUP_DEVICE_TIMEOUT);

	ipr_cmd->job_step = ipr_set_dasd_timeouts;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_set_dasd_timeouts - Send a Set DASD Timeouts command
 * @ipr_cmd:	ipr command struct
 *
 * This function send a Set DASD Timeouts to tell the adapter
 * how long to time the various ops at the physical device level.
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_set_dasd_timeouts(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_resource_entry *res = hostrcb->res;

	ioarcb->res_handle = res->cfgte.res_handle;
	ioarcb->cmd_pkt.write_not_read = 1;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

	ioarcb->cmd_pkt.cdb[0] = IPR_SET_DASD_TIMEOUTS;
	ioarcb->cmd_pkt.cdb[7] = (sizeof(struct ipr_dasd_timeouts) >> 8) & 0xff;
	ioarcb->cmd_pkt.cdb[8] = sizeof(struct ipr_dasd_timeouts) & 0xff;

	ioadl->flags_and_data_len = cpu_to_be32(IPR_IOADL_FLAGS_WRITE_LAST |
						sizeof(struct ipr_dasd_timeouts));
	ioadl->address = cpu_to_be32(hostrcb->hostrcb_dma +
				     offsetof(struct ipr_hostrcb, dasd_timeouts));
	ioarcb->write_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ioarcb->write_data_transfer_length =
		cpu_to_be32(sizeof(struct ipr_dasd_timeouts));

	ipr_do_req(ipr_cmd, ipr_dasd_init_job, ipr_timeout,
		   IPR_SET_DASD_TIMEOUTS_TIMEOUT);

	ipr_cmd->job_step = ipr_query_res_state;

	return IPR_RC_JOB_RETURN;
}


/**
 * ipr_build_query_res_state - Builds a Query Resource State Command
 * @res:		resource entry struct
 * @ipr_cmd:	ipr command struct
 * @dma_addr:	DMA address for buffer
 *
 * Return value:
 * 	none
 **/
static void ipr_build_query_res_state(struct ipr_resource_entry *res,
				      struct ipr_cmnd *ipr_cmd, u32 dma_addr)
{
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;

	ioarcb->res_handle = res->cfgte.res_handle;
	ioarcb->cmd_pkt.cdb[0] = IPR_QUERY_RESOURCE_STATE;
	ioarcb->cmd_pkt.cdb[7] = (sizeof(struct ipr_query_res_state) >> 8) & 0xff;
	ioarcb->cmd_pkt.cdb[8] = sizeof(struct ipr_query_res_state) & 0xff;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

	ioadl->flags_and_data_len =
		cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | sizeof(struct ipr_query_res_state));
	ioadl->address = cpu_to_be32(dma_addr);
	ioarcb->read_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ioarcb->read_data_transfer_length =
		cpu_to_be32(sizeof(struct ipr_query_res_state));
}

/**
 * ipr_query_res_state - Sends a Query Resource State to a device
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_query_res_state(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;

	ipr_build_query_res_state(res, ipr_cmd,
				  hostrcb->hostrcb_dma +
				  offsetof(struct ipr_hostrcb, res_query));

	ipr_do_req(ipr_cmd, ipr_dasd_init_job, ipr_timeout,
		   IPR_INTERNAL_DEV_TIMEOUT);

	ipr_cmd->job_step = ipr_mode_sense_pg0x01_cur;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_build_mode_sense - Builds a mode sense command
 * @ipr_cmd:	ipr command struct
 * @res:		resource entry struct
 * @parm:		Byte 2 of mode sense command
 * @dma_addr:	DMA address of mode sense buffer
 * @xfer_len:	Size of DMA buffer
 *
 * Return value:
 * 	none
 **/
static void ipr_build_mode_sense(struct ipr_cmnd *ipr_cmd,
				 u32 res_handle,
				 u8 parm, u32 dma_addr, u8 xfer_len)
{
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ioarcb->res_handle = res_handle;
	ioarcb->cmd_pkt.cdb[0] = MODE_SENSE;
	ioarcb->cmd_pkt.cdb[2] = parm;
	ioarcb->cmd_pkt.cdb[4] = xfer_len;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

	ioadl->flags_and_data_len =
		cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | xfer_len);
	ioadl->address = cpu_to_be32(dma_addr);
	ioarcb->read_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ioarcb->read_data_transfer_length = cpu_to_be32(xfer_len);
}

/**
 * ipr_mode_sense - Sends a Mode Sense to a device
 * @ipr_cmd:	ipr command struct
 * @res:		resource entry struct
 * @parm:		Byte 2 of Mode Sense command
 * @dma_addr:	DMA buffer address
 * @xfer_len:	data transfer length
 *
 * Return value:
 * 	none
 **/
static void ipr_mode_sense(struct ipr_cmnd *ipr_cmd,
			   struct ipr_resource_entry *res,
			   u8 parm, u32 dma_addr, u8 xfer_len)
{
	ipr_build_mode_sense(ipr_cmd, res->cfgte.res_handle,
			     parm, dma_addr, xfer_len);

	ipr_do_req(ipr_cmd, ipr_dasd_init_job, ipr_timeout,
		   IPR_INTERNAL_DEV_TIMEOUT);
}

/**
 * ipr_blocking_mode_sense - Sends a blocking Mode Sense
 * @ipr_cmd:	ipr command struct
 * @res:		resource entry struct
 * @parm:		Byte 2 of Mode Sense command
 * @dma_addr:	DMA buffer address
 * @xfer_len:	data transfer length
 *
 * Return value:
 * 	IOASC
 **/
static u32 ipr_blocking_mode_sense(struct ipr_ioa_cfg *ioa_cfg,
				   struct ipr_resource_entry *res,
				   u8 parm, u32 dma_addr, u8 xfer_len)
{
	u32 ioasc;
	struct ipr_cmnd *ipr_cmd;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ipr_cmd->ioarcb.cmd_pkt.sync_complete = 1;
	ipr_cmd->ioarcb.cmd_pkt.no_underlength_checking = 1;

	ipr_build_mode_sense(ipr_cmd, res->cfgte.res_handle,
			     parm, dma_addr, xfer_len);

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_INTERNAL_DEV_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	return ioasc;
}

/**
 * ipr_mode_sense_pg0x01_cur - Get current parms for page 0x01
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_mode_sense_pg0x01_cur(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;
	struct ipr_query_res_state *res_query = &hostrcb->res_query;

	if (res_query->read_write_prot) {
		ipr_cmd->job_step = ipr_dev_init_done;
		return IPR_RC_JOB_CONTINUE;
	}

	ipr_mode_sense(ipr_cmd, res, 0x01,
		       hostrcb->hostrcb_dma +
		       offsetof(struct ipr_hostrcb, mode_pages),
		       sizeof(struct ipr_mode_pages));

	ipr_cmd->job_step = ipr_mode_sense_pg0x01_changeable;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_mode_sense_pg0x01_changeable - Get changeable parms for page 0x01
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_mode_sense_pg0x01_changeable(struct ipr_cmnd *ipr_cmd) 
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;

	ipr_mode_sense(ipr_cmd, res, 0x41,
		       hostrcb->hostrcb_dma +
		       offsetof(struct ipr_hostrcb, changeable_parms),
		       sizeof(struct ipr_mode_pages));

	ipr_cmd->job_step = ipr_mode_select_pg0x01;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_build_mode_select - Build a mode select command
 * @ipr_cmd:	ipr command struct
 * @res_handle:	resource handle to send command to
 * @parm:		Byte 2 of Mode Sense command
 * @dma_addr:	DMA buffer address
 * @xfer_len:	data transfer length
 *
 * Return value:
 * 	none
 **/
static void ipr_build_mode_select(struct ipr_cmnd *ipr_cmd,
				  u32 res_handle, u8 parm, u32 dma_addr,
				  u8 xfer_len)
{
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;

	ioarcb->res_handle = res_handle;
	ioarcb->cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;
	ioarcb->cmd_pkt.write_not_read = 1;
	ioarcb->cmd_pkt.cdb[0] = MODE_SELECT;
	ioarcb->cmd_pkt.cdb[1] = parm;
	ioarcb->cmd_pkt.cdb[4] = xfer_len;

	ioadl->flags_and_data_len =
		cpu_to_be32(IPR_IOADL_FLAGS_WRITE_LAST | xfer_len);
	ioadl->address = cpu_to_be32(dma_addr);
	ioarcb->write_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ioarcb->write_data_transfer_length = cpu_to_be32(xfer_len);
}

/**
 * ipr_mode_select - Sends a Mode Select to a device
 * @ipr_cmd:	ipr command struct
 * @res:		resource entry struct
 * @parm:		Byte 2 of Mode Sense command
 * @dma_addr:	DMA buffer address
 * @xfer_len:	data transfer length
 *
 * Return value:
 * 	none
 **/
static void ipr_mode_select(struct ipr_cmnd *ipr_cmd,
			    struct ipr_resource_entry *res,
			    u8 parm, u32 dma_addr, u8 xfer_len)
{
	ipr_build_mode_select(ipr_cmd, res->cfgte.res_handle,
			      parm, dma_addr, xfer_len);

	ipr_do_req(ipr_cmd, ipr_dasd_init_job, ipr_timeout,
		   IPR_INTERNAL_DEV_TIMEOUT);
}

/**
 * ipr_blocking_mode_select - Sends a blocking Mode Select
 * @ipr_cmd:	ipr command struct
 * @res:		resource entry struct
 * @parm:		Byte 2 of Mode Select command
 * @dma_addr:	DMA buffer address
 * @xfer_len:	data transfer length
 *
 * Return value:
 * 	IOASC
 **/
static u32 ipr_blocking_mode_select(struct ipr_ioa_cfg *ioa_cfg,
				    struct ipr_resource_entry *res,
				    u8 parm, u32 dma_addr, u8 xfer_len)
{
	u32 ioasc;
	struct ipr_cmnd *ipr_cmd;

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);
	ipr_cmd->ioarcb.cmd_pkt.sync_complete = 1;

	ipr_build_mode_select(ipr_cmd, res->cfgte.res_handle,
			      parm, dma_addr, xfer_len);

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_INTERNAL_DEV_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	return ioasc;
}

/**
 * ipr_set_mode_parm_hdr - Setup mode parm header for mode select
 * @mode_pages:	mode page buffer
 *
 * Return value:
 * 	length to use for mode select
 **/
static u8 ipr_set_mode_parm_hdr(struct ipr_mode_pages *mode_pages)
{
	u8 len;

	len = mode_pages->hdr.length + 1;

	mode_pages->hdr.length = 0;
	mode_pages->hdr.medium_type = 0;
	mode_pages->hdr.device_spec_parms = 0;

	return len;
}

/**
 * ipr_set_page0x01 - Setup mode page 0x01 for mode select
 * @mode_pages:		mode page buffer
 * @ch_mode_pages:	changeable mode page buffer
 *
 * Return value:
 * 	length to use for mode select
 **/
static u8 ipr_set_page0x01(struct ipr_mode_pages *mode_pages,
			   struct ipr_mode_pages *ch_mode_pages)
{
	struct ipr_rw_err_mode_page *rw_err_mode_pg, *ch_rw_err_mode_pg;

	rw_err_mode_pg = ipr_get_mode_page(mode_pages, 0x01,
					   sizeof(struct ipr_rw_err_mode_page));
	ch_rw_err_mode_pg = ipr_get_mode_page(ch_mode_pages, 0x01,
					      sizeof(struct ipr_rw_err_mode_page));

	if (ch_rw_err_mode_pg && rw_err_mode_pg) {
		IPR_SET_MODE(ch_rw_err_mode_pg->awre, rw_err_mode_pg->awre, 1);
		IPR_SET_MODE(ch_rw_err_mode_pg->arre, rw_err_mode_pg->arre, 1);
		rw_err_mode_pg->hdr.parms_saveable = 0;
	}

	return ipr_set_mode_parm_hdr(mode_pages);
}

/**
 * ipr_mode_select_pg0x01 - Send mode select for page 0x01
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_mode_select_pg0x01(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;
	u8 len;

	len = ipr_set_page0x01(&hostrcb->mode_pages, &hostrcb->changeable_parms);

	ipr_mode_select(ipr_cmd, res, 0x11,
			hostrcb->hostrcb_dma +
			offsetof(struct ipr_hostrcb, mode_pages),
			len);

	ipr_cmd->job_step = ipr_mode_sense_pg0x0a_cur;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_mode_sense_pg0x0a_cur - Get current parms for page 0x0A
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_mode_sense_pg0x0a_cur(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;

	ipr_mode_sense(ipr_cmd, res, 0x0a,
		       hostrcb->hostrcb_dma +
		       offsetof(struct ipr_hostrcb, mode_pages),
		       sizeof(struct ipr_mode_pages));

	ipr_cmd->job_step = ipr_mode_sense_pg0x0a_changeable;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_mode_sense_pg0x0a_cur - Get changeable parms for page 0x0A
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_mode_sense_pg0x0a_changeable(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;

	ipr_mode_sense(ipr_cmd, res, 0x4a,
		       hostrcb->hostrcb_dma +
		       offsetof(struct ipr_hostrcb, changeable_parms),
		       sizeof(struct ipr_mode_pages));

	ipr_cmd->job_step = ipr_mode_select_pg0x0a;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_mode_select_pg0x0a - Send page 0x0A mode select
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_mode_select_pg0x0a(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;
	u8 len;

	len = ipr_set_page0x0a(&hostrcb->mode_pages, &hostrcb->changeable_parms);

	ipr_mode_select(ipr_cmd, res, 0x11,
			hostrcb->hostrcb_dma +
			offsetof(struct ipr_hostrcb, mode_pages),
			len);

	ipr_cmd->job_step = ipr_mode_sense_pg0x20;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_mode_sense_pg0x20 - Send page 0x20 mode sense
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_mode_sense_pg0x20(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;

	ipr_mode_sense(ipr_cmd, res, 0x20,
		       hostrcb->hostrcb_dma +
		       offsetof(struct ipr_hostrcb, mode_pages),
		       sizeof(struct ipr_mode_pages));

	ipr_cmd->job_step = ipr_mode_select_pg0x20;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_set_page0x20 - Setup mode page 0x20 for mode select
 * @mode_pages:		mode page buffer
 *
 * Return value:
 * 	length to use for mode select
 **/
static u8 ipr_set_page0x20(struct ipr_mode_pages *mode_pages)
{
	struct ipr_ioa_dasd_page_20 *ioa_dasd_pg_20;

	ioa_dasd_pg_20 = ipr_get_mode_page(mode_pages, 0x20,
					   sizeof(struct ipr_ioa_dasd_page_20));

	if (ioa_dasd_pg_20) {
		ioa_dasd_pg_20->max_tcq_depth = IPR_MAX_TAGGED_CMD_PER_DEV;
		ioa_dasd_pg_20->hdr.parms_saveable = 0;
	}

	return ipr_set_mode_parm_hdr(mode_pages);
}

/**
 * ipr_mode_select_pg0x20 - Send page 0x20 mode select
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_mode_select_pg0x20(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_resource_entry *res = hostrcb->res;
	u8 len;

	len = ipr_set_page0x20(&hostrcb->mode_pages);

	ipr_mode_select(ipr_cmd, res, 0x11,
			hostrcb->hostrcb_dma +
			offsetof(struct ipr_hostrcb, mode_pages),
			len);

	ipr_cmd->job_step = ipr_dev_init_done;

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_dev_init_done - Completion of device init job
 * @ipr_cmd:	ipr command struct
 *
 * Return value:
 * 	IPR_RC_JOB_CONTINUE / IPR_RC_JOB_RETURN
 **/
static int ipr_dev_init_done(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	struct ipr_cmnd *parent = ipr_cmd->parent;

	ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, hostrcb);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	if (parent)
		parent->done(parent);

	return IPR_RC_JOB_RETURN;
}

/**
 * ipr_set_page0x0a - Setup mode page 0x0A for mode select
 * @mode_pages:		mode page buffer
 * @ch_mode_pages:	changeable mode page buffer
 *
 * Return value:
 * 	length to use for mode select
 **/
static u8 ipr_set_page0x0a(struct ipr_mode_pages *mode_pages,
			   struct ipr_mode_pages *ch_mode_pages)
{
	struct ipr_control_mode_page *control_pg, *ch_control_pg;

	control_pg = ipr_get_mode_page(mode_pages, 0x0a,
				       sizeof(struct ipr_control_mode_page));
	ch_control_pg = ipr_get_mode_page(ch_mode_pages, 0x0a,
					  sizeof(struct ipr_control_mode_page));

	if (control_pg && ch_control_pg) {
		IPR_SET_MODE(ch_control_pg->queue_algorithm_modifier,
			     control_pg->queue_algorithm_modifier, 1);
		IPR_SET_MODE(ch_control_pg->qerr, control_pg->qerr, 1);
		IPR_SET_MODE(ch_control_pg->dque, control_pg->dque, 0);
		control_pg->hdr.parms_saveable = 0;
	}

	return ipr_set_mode_parm_hdr(mode_pages);
}

/**
 * ipr_process_error - Op done function for an adapter error log.
 * @ipr_cmd:	ipr command struct
 *
 * This function is the op done function for an error log host
 * controlled async from the adapter. It will log the error and
 * send the HCAM back to the adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_process_error(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_hostrcb *hostrcb = ipr_cmd->hostrcb;
	u32 ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	list_del(&hostrcb->queue);

	if (!ioasc) {
		ipr_handle_log_data(ioa_cfg, hostrcb);
	} else if (ioasc != IPR_IOASC_IOA_WAS_RESET) {
		dev_err(&ioa_cfg->pdev->dev,
			"Host RCB failed with IOASC: 0x%08X\n", ioasc);
	}

	ipr_send_hcam(ioa_cfg, IPR_HCAM_CDB_OP_CODE_LOG_DATA, hostrcb);
}

/**
 * ipr_log_vpd - Log the passed VPD to the error log.
 * @vpids:			vendor/product id struct
 * @serial_num:		serial number string
 *
 * Return value:
 * 	none
 **/
static void ipr_log_vpd(struct ipr_std_inq_vpids *vpids, u8 *serial_num)
{
	char buffer[max_t(int, sizeof(struct ipr_std_inq_vpids),
			  IPR_SERIAL_NUM_LEN) + 1];

	memcpy(buffer, vpids, sizeof(struct ipr_std_inq_vpids));
	buffer[sizeof(struct ipr_std_inq_vpids)] = '\0';
	ipr_err("Vendor/Product ID: %s\n", buffer);

	memcpy(buffer, serial_num, IPR_SERIAL_NUM_LEN);
	buffer[IPR_SERIAL_NUM_LEN] = '\0';
	ipr_err("    Serial Number: %s\n", buffer);	
}

/**
 * ipr_log_cache_error - Log a cache error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_cache_error(struct ipr_ioa_cfg *ioa_cfg,
				struct ipr_hostrcb *hostrcb)
{
	struct ipr_hostrcb_type_02_error *error = &hostrcb->error.type_02_error;

	ipr_err("-----Current Configuration-----\n");
	ipr_err("I/O Processor Information:\n");
	ipr_log_vpd(&error->ioa_vpids, error->ioa_sn);
	ipr_err("Cache Adapter Card Information:\n");
	ipr_log_vpd(&error->cfc_vpids, error->cfc_sn);

	ipr_err("-----Expected Configuration-----\n");
	ipr_err("I/O Processor Information:\n");
	ipr_log_vpd(&error->ioa_last_attached_to_cfc_vpids,
		    error->ioa_last_attached_to_cfc_sn);
	ipr_err("Cache Adapter Card Information:\n");
	ipr_log_vpd(&error->cfc_last_attached_to_ioa_vpids,
		    error->cfc_last_attached_to_ioa_sn);

	ipr_err("Additional IOA Data: %08X %08X %08X\n",
		     be32_to_cpu(error->ioa_data[0]),
		     be32_to_cpu(error->ioa_data[1]),
		     be32_to_cpu(error->ioa_data[2]));
}

/**
 * ipr_log_config_error - Log a configuration error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_config_error(struct ipr_ioa_cfg *ioa_cfg,
				 struct ipr_hostrcb *hostrcb)
{
	int errors_logged, i;
	struct ipr_hostrcb_device_data_entry *dev_entry;

	errors_logged = be32_to_cpu(hostrcb->error.type_03_error.errors_logged);

	ipr_err("Device Errors Detected/Logged: %d/%d\n",
		be32_to_cpu(hostrcb->error.type_03_error.errors_detected),
		errors_logged);

	dev_entry = hostrcb->error.type_03_error.dev_entry;

	for (i = 0; i < errors_logged; i++, dev_entry++) {
		ipr_err_separator;

		if (dev_entry->dev_res_addr.bus >= IPR_MAX_NUM_BUSES) {
			ipr_err("Device %d: missing\n", i + 1);
		} else {
			ipr_err("Device %d: %s:%d:%d:%d\n", i + 1,
				ioa_cfg->pdev->dev.bus_id, dev_entry->dev_res_addr.bus,
				dev_entry->dev_res_addr.target, dev_entry->dev_res_addr.lun);
		}
		ipr_log_vpd(&dev_entry->dev_vpids, dev_entry->dev_sn);

		ipr_err("-----New Device Information-----\n");
		ipr_log_vpd(&dev_entry->new_dev_vpids, dev_entry->new_dev_sn);

		ipr_err("I/O Processor Information:\n");
		ipr_log_vpd(&dev_entry->ioa_last_with_dev_vpids,
			    dev_entry->ioa_last_with_dev_sn);

		ipr_err("Cache Adapter Card Information:\n");
		ipr_log_vpd(&dev_entry->cfc_last_with_dev_vpids,
			    dev_entry->cfc_last_with_dev_sn);

		ipr_err("Additional IOA Data: %08X %08X %08X %08X %08X\n",
			be32_to_cpu(dev_entry->ioa_data[0]),
			be32_to_cpu(dev_entry->ioa_data[1]),
			be32_to_cpu(dev_entry->ioa_data[2]),
			be32_to_cpu(dev_entry->ioa_data[3]),
			be32_to_cpu(dev_entry->ioa_data[4]));
	}
}

/**
 * ipr_log_array_error - Log an array configuration error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_array_error(struct ipr_ioa_cfg *ioa_cfg,
				struct ipr_hostrcb *hostrcb)
{
	int i;
	struct ipr_hostrcb_type_04_error *error;
	struct ipr_hostrcb_array_data_entry *array_entry;
	u8 zero_sn[IPR_SERIAL_NUM_LEN];

	memset(zero_sn, '0', IPR_SERIAL_NUM_LEN);

	error = &hostrcb->error.type_04_error;

	ipr_err_separator;

	ipr_err("RAID %s Array Configuration: %s:%d:%d:%d\n",
		error->protection_level,
		ioa_cfg->pdev->dev.bus_id,
		error->last_func_vset_res_addr.bus,
		error->last_func_vset_res_addr.target,
		error->last_func_vset_res_addr.lun);

	ipr_err_separator;

	array_entry = error->array_member;

	for (i = 0; i < 18; i++) {
		if (!memcmp(array_entry->serial_num, zero_sn, IPR_SERIAL_NUM_LEN))
			continue;

		if (error->exposed_mode_adn == i) {
			ipr_err("Exposed Array Member %d:\n", i);
		} else {
			ipr_err("Array Member %d:\n", i);
		}

		ipr_log_vpd(&array_entry->vpids, array_entry->serial_num);

		if (array_entry->dev_res_addr.bus >= IPR_MAX_NUM_BUSES) {
			ipr_err("Current Location: unknown\n");
		} else {
			ipr_err("Current Location: %s:%d:%d:%d\n",
				ioa_cfg->pdev->dev.bus_id,
				array_entry->dev_res_addr.bus,
				array_entry->dev_res_addr.target,
				array_entry->dev_res_addr.lun);
		}

		if (array_entry->dev_res_addr.bus >= IPR_MAX_NUM_BUSES) {
			ipr_err("Expected Location: unknown\n");
		} else {
			ipr_err("Expected Location: %s:%d:%d:%d\n",
				ioa_cfg->pdev->dev.bus_id,
				array_entry->expected_dev_res_addr.bus,
				array_entry->expected_dev_res_addr.target,
				array_entry->expected_dev_res_addr.lun);
		}

		ipr_err_separator;

		if (i == 9)
			array_entry = error->array_member2;
		else
			array_entry++;
	}
}

/**
 * ipr_log_generic_error - Log an adapter error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * Return value:
 * 	none
 **/
static void ipr_log_generic_error(struct ipr_ioa_cfg *ioa_cfg,
				  struct ipr_hostrcb *hostrcb)
{
	int i;
	int ioa_data_len = be32_to_cpu(hostrcb->length);

	if (ioa_data_len == 0)
		return;

	ipr_err("IOA Error Data:\n");
	ipr_err("Offset    0 1 2 3  4 5 6 7  8 9 A B  C D E F\n");

	for (i = 0; i < ioa_data_len / 4; i += 4) {
		ipr_err("%08X: %08X %08X %08X %08X\n", i*4,
			be32_to_cpu(hostrcb->raw.data[i]),
			be32_to_cpu(hostrcb->raw.data[i+1]),
			be32_to_cpu(hostrcb->raw.data[i+2]),
			be32_to_cpu(hostrcb->raw.data[i+3]));
	}
}

/**
 * ipr_handle_log_data - Log an adapter error.
 * @ioa_cfg:	ioa config struct
 * @hostrcb:	hostrcb struct
 *
 * This function logs an adapter error to the system.
 *
 * Return value:
 * 	none
 **/
static void ipr_handle_log_data(struct ipr_ioa_cfg *ioa_cfg,
				struct ipr_hostrcb *hostrcb)
{
	u16 urc;
	u32 ioasc;
	int error_index;
	char error_string[100];

	if (hostrcb->notify_type != IPR_HOST_RCB_NOTIF_TYPE_ERROR_LOG_ENTRY)
		return;

	if (hostrcb->notifications_lost == IPR_HOST_RCB_NOTIFICATIONS_LOST)
		dev_err(&ioa_cfg->pdev->dev, "Error notifications lost\n");

	ioasc = be32_to_cpu(hostrcb->error.failing_dev_ioasc);

	if ((ioasc == IPR_IOASC_BUS_WAS_RESET) ||
	    (ioasc == IPR_IOASC_BUS_WAS_RESET_BY_OTHER)) {
		/* Tell the midlayer we had a bus reset so it will handle the UA properly */
		scsi_report_bus_reset(ioa_cfg->host,
				      hostrcb->error.failing_dev_res_addr.bus);
	}

	error_index = ipr_get_error(ioasc);

	if (hostrcb->error.failing_dev_res_handle == IPR_IOA_RES_HANDLE) {
		urc = ipr_adjust_urc(error_index,
				     hostrcb->error.failing_dev_res_addr, ioasc, 0,
				     error_string);

		if (urc == 0)
			return;

		if (ipr_is_device(&hostrcb->error.failing_dev_res_addr)) {
			ipr_res_err(ioa_cfg, hostrcb->error.failing_dev_res_addr,
				    "SRC: %04X %04X, Class: %s, %s\n",
				    ioa_cfg->type, urc, ipr_error_table[error_index].class,
				    error_string);
		} else {
			dev_err(&ioa_cfg->pdev->dev, "SRC: %04X %04X, Class: %s, %s\n",
				ioa_cfg->type, urc, ipr_error_table[error_index].class,
				error_string);
		}
	} else {
		urc = ipr_adjust_urc(error_index,
				     hostrcb->error.failing_dev_res_addr, ioasc,
				     1, error_string);

		if (urc == 0)
			return;

		ipr_res_err(ioa_cfg, hostrcb->error.failing_dev_res_addr,
			    "SRC: 6600 %04X, Class: %s, %s\n",
			    urc, ipr_error_table[error_index].class,
			    error_string);
	}

	/* Set indication we have logged an error */
	ioa_cfg->errors_logged++;

	switch (hostrcb->overlay_id) {
	case IPR_HOST_RCB_OVERLAY_ID_1:
		ipr_log_generic_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_2:
		ipr_log_cache_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_3:
		ipr_log_config_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_4:
	case IPR_HOST_RCB_OVERLAY_ID_6:
		ipr_log_array_error(ioa_cfg, hostrcb);
		break;
	case IPR_HOST_RCB_OVERLAY_ID_DEFAULT:
		ipr_log_generic_error(ioa_cfg, hostrcb);
		break;
	default:
		dev_err(&ioa_cfg->pdev->dev,
			"Unknown error received. Overlay ID: %d\n",
			hostrcb->overlay_id);
		break;
	}
}

/**
 * ipr_open - Open connection ipr char device
 * @inode:		Device file inode
 * @filp:		Device file pointer
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_open(struct inode *inode, struct file *filp)
{
	struct ipr_ioa_cfg *ioa_cfg;
	int minor;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	spin_lock(&ipr_driver_lock);

	if (filp->private_data == NULL) {
		minor = MINOR(inode->i_rdev);
		list_for_each_entry(ioa_cfg, &ipr_ioa_head, queue) {
			if (ioa_cfg->minor_num == minor) {
				filp->private_data = ioa_cfg;
				break;
			}
		}
	} else
		ioa_cfg = filp->private_data;

	spin_unlock(&ipr_driver_lock);

	if (filp->private_data == NULL)
		return -ENXIO;

	return 0;
}

/**
 * ipr_close - Close connection to ipr char device
 * @inode:		Device file inode
 * @filp:		Device file pointer
 *
 * Return value:
 * 	0 on success / -ENXIO if device does not exist
 **/
static int ipr_close(struct inode *inode, struct file *filp)
{
	if (filp->private_data == NULL)
		return -ENXIO;

	return 0;
}

/**
 * ipr_wait_until_ioctl_allowed - Wait for IOCTLs to be allowed 
 * @ioa_cfg:	ioa config struct
 *
 * This function will check to see if IOCTLs are currently
 * allowed to execute. If we are currently going through a
 * reset of the adapter, it will sleep interruptibly until
 * the reset is complete.
 *
 * Return value:
 * 	0 if IOCTLs are allowed / other if IOCTLs are not allowed
 **/
static int ipr_wait_until_ioctl_allowed(struct ipr_ioa_cfg *ioa_cfg)
{
	int rc = 0;

	if (ioa_cfg->in_reset_reload)
		rc = ipr_interruptible_sleep_on(ioa_cfg, &ioa_cfg->reset_wait_q);

	if (!ioa_cfg->allow_cmds)
		rc = -EIO;

	return rc;
}

/**
 * ipr_passthru_ioctl - Passthrough IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_passthru_ioctl
 *
 * This function will process a passthrough IOCTL.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_passthru_ioctl(struct ipr_ioa_cfg *ioa_cfg,
			      struct ipr_passthru_ioctl *passthru_ioctl,
			      unsigned long arg)
{
	unsigned long lock_flags = 0;
	int result;
	u32 k_buffer_dma;
	void *k_buffer = NULL;
	struct ipr_cmnd *ipr_cmd;
	u8 *u_buffer;
	u32 ioasc;

	u_buffer = (u8 *)(arg + offsetof(struct ipr_passthru_ioctl, buffer));

	if (passthru_ioctl->buffer_len) {
		k_buffer = pci_alloc_consistent(ioa_cfg->pdev, passthru_ioctl->buffer_len,
						&k_buffer_dma);

		if (!k_buffer) {
			ipr_err("Buffer allocation for passthru IOCTL failed\n");
			return -ENOMEM;
		}

		if (passthru_ioctl->cmd_pkt.write_not_read) {
			result = copy_from_user(k_buffer, (const void *)u_buffer,
						passthru_ioctl->buffer_len);

			if (result) {
				ipr_trace;
				pci_free_consistent(ioa_cfg->pdev, passthru_ioctl->buffer_len,
						    k_buffer, k_buffer_dma);
				return result;
			}
		}
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if ((result = ipr_wait_until_ioctl_allowed(ioa_cfg))) {
		ipr_trace;
		pci_free_consistent(ioa_cfg->pdev, passthru_ioctl->buffer_len,
				    k_buffer, k_buffer_dma);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return result;
	}

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);

	memcpy(&ipr_cmd->ioarcb.cmd_pkt, &passthru_ioctl->cmd_pkt,
	       sizeof(ipr_cmd->ioarcb.cmd_pkt));

	ipr_cmd->ioarcb.res_handle = passthru_ioctl->res_handle;

	if (passthru_ioctl->buffer_len) {
		if (passthru_ioctl->cmd_pkt.write_not_read) {
			ipr_cmd->ioarcb.write_data_transfer_length = 
				cpu_to_be32(passthru_ioctl->buffer_len);
			ipr_cmd->ioarcb.write_ioadl_len =
				cpu_to_be32(sizeof(struct ipr_ioadl_desc));
			ipr_cmd->ioadl[0].flags_and_data_len =
				cpu_to_be32(IPR_IOADL_FLAGS_WRITE_LAST | passthru_ioctl->buffer_len);
		} else {
			ipr_cmd->ioarcb.read_data_transfer_length = 
				cpu_to_be32(passthru_ioctl->buffer_len);
			ipr_cmd->ioarcb.read_ioadl_len =
				cpu_to_be32(sizeof(struct ipr_ioadl_desc));
			ipr_cmd->ioadl[0].flags_and_data_len =
				cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | passthru_ioctl->buffer_len);
		}

		ipr_cmd->ioadl[0].address = cpu_to_be32(k_buffer_dma);
	}

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout,
			      passthru_ioctl->timeout_in_sec * HZ);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	/* xxx delete */
	if (ioasc) {
		dev_err(&ioa_cfg->pdev->dev,
			"Passthu IOCTL %02X failed with IOASC: 0x%08X\n",
			ipr_cmd->ioarcb.cmd_pkt.cdb[0], ioasc);
	}

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	passthru_ioctl->ioasc = ioasc;

	if (!ioasc && passthru_ioctl->buffer_len &&
	    !passthru_ioctl->cmd_pkt.write_not_read) {
		result = copy_to_user(u_buffer, k_buffer, passthru_ioctl->buffer_len);
	} else if (ioasc) {
		result = -EIO;
	}

	pci_free_consistent(ioa_cfg->pdev, passthru_ioctl->buffer_len,
			    k_buffer, k_buffer_dma);

	return result;
}

/**
 * ipr_query_config_ioctl - Query config IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_query_config_ioctl
 *
 * This function will process a Query Config IOCTL.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_query_config_ioctl(struct ipr_ioa_cfg *ioa_cfg,
				  struct ipr_query_config_ioctl *query_ioctl,
				  unsigned long arg)
{
	struct ipr_resource_entry *res;
	struct ipr_config_table *cfg_table;
	int result, i;
	unsigned long lock_flags = 0;
	u8 *u_buffer;

	if (query_ioctl->buffer_len == 0) {
		dev_err(&ioa_cfg->pdev->dev,
			"Zero buffer length on query config ioctl\n");
		return -EINVAL;
	}

	u_buffer = (u8 *)(arg + offsetof(struct ipr_query_config_ioctl, buffer));

	cfg_table = kmalloc(sizeof(struct ipr_config_table), GFP_KERNEL);

	if (!cfg_table) {
		ipr_err("Buffer allocation for query config ioctl failed\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if ((result = ipr_wait_until_ioctl_allowed(ioa_cfg))) {
		ipr_trace;
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		kfree(cfg_table);
		return result;
	}

	memset(cfg_table, 0, sizeof(struct ipr_config_table));

	i = 0;

	list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
		memcpy(&cfg_table->dev[i], &res->cfgte, sizeof(res->cfgte));
		i++;
	}

	cfg_table->num_entries = i;

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	result = copy_to_user(u_buffer, cfg_table, sizeof(struct ipr_config_table));

	kfree(cfg_table);

	return result;
}

/**
 * ipr_reclaim_cache_ioctl - Reclaim cache storage IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_reclaim_cache_ioctl
 *
 * This function will process a reclaim cache storage IOCTL.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_reclaim_cache_ioctl(struct ipr_ioa_cfg *ioa_cfg,
				   struct ipr_reclaim_cache_ioctl *reclaim_ioctl,
				   unsigned long arg)
{
	u32 k_buffer_dma;
	void *k_buffer;
	int result;
	u32 ioasc;
	u8 *u_buffer;
	unsigned long lock_flags;
	struct ipr_cmnd *ipr_cmd;

	if (reclaim_ioctl->buffer_len == 0) {
		dev_err(&ioa_cfg->pdev->dev,
			"Zero buffer length on query config ioctl\n");
		return -EINVAL;
	}

	u_buffer = (u8 *)(arg + offsetof(struct ipr_reclaim_cache_ioctl, buffer));

	k_buffer = pci_alloc_consistent(ioa_cfg->pdev, reclaim_ioctl->buffer_len,
					&k_buffer_dma);

	if (!k_buffer) {
		ipr_err("Buffer allocation for reclaim cache storage failed\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if ((result = ipr_wait_until_ioctl_allowed(ioa_cfg))) {
		pci_free_consistent(ioa_cfg->pdev, reclaim_ioctl->buffer_len,
				    k_buffer, k_buffer_dma);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return result;
	}

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);

	ipr_cmd->ioarcb.cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
	ipr_cmd->ioarcb.cmd_pkt.cdb[0] = IPR_RECLAIM_CACHE_STORE;
	ipr_cmd->ioarcb.cmd_pkt.cdb[1] = 0x11;
	ipr_cmd->ioarcb.cmd_pkt.cdb[7] = (reclaim_ioctl->buffer_len & 0xff00) >> 8;
	ipr_cmd->ioarcb.cmd_pkt.cdb[8] = reclaim_ioctl->buffer_len & 0xff;
	ipr_cmd->ioarcb.read_data_transfer_length =
		cpu_to_be32(reclaim_ioctl->buffer_len);
	ipr_cmd->ioarcb.read_ioadl_len = cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ipr_cmd->ioadl[0].flags_and_data_len =
		cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | reclaim_ioctl->buffer_len);
	ipr_cmd->ioadl[0].address = cpu_to_be32(k_buffer_dma);

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_RECLAIM_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	if (ioasc) {
		if (IPR_IOASC_SENSE_KEY(ioasc) == ILLEGAL_REQUEST)
			result = -EINVAL;
		else
			result = -EIO;
	} else {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		result = copy_to_user(u_buffer, k_buffer, reclaim_ioctl->buffer_len);
		spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	}

	pci_free_consistent(ioa_cfg->pdev, reclaim_ioctl->buffer_len,
			    k_buffer, k_buffer_dma);

	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	if (ioasc != IPR_IOASC_IOA_WAS_RESET) {
		ipr_block_requests(ioa_cfg);

		if (ipr_reset_reload(ioa_cfg, IPR_SHUTDOWN_NORMAL) != SUCCESS)
			result = -EIO;

		ipr_unblock_requests(ioa_cfg);
	}

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return result;
}

/**
 * ipr_get_trace_ioctl - Get trace IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_get_trace_ioctl
 *
 * This function will copy the driver's internal trace buffer
 * into the user's buffer.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_get_trace_ioctl(struct ipr_ioa_cfg *ioa_cfg,
			       struct ipr_get_trace_ioctl *trace_ioctl,
			       unsigned long arg)
{
	unsigned long lock_flags = 0;
	void *k_buffer;
	u8 *u_buffer;
	int result;

	if (trace_ioctl->buffer_len == 0) {
		dev_err(&ioa_cfg->pdev->dev, "Zero buffer length on get trace ioctl\n");
		return -EINVAL;
	}

	u_buffer = (u8 *)(arg + offsetof(struct ipr_get_trace_ioctl, buffer));

	k_buffer = kmalloc(IPR_TRACE_SIZE, GFP_KERNEL);

	if (!k_buffer) {
		ipr_err("Buffer allocation on get trace failed\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	memcpy(k_buffer, ioa_cfg->trace, IPR_TRACE_SIZE);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	result = copy_to_user((void *)u_buffer, k_buffer,
			     min(trace_ioctl->buffer_len, IPR_TRACE_SIZE));

	kfree(k_buffer);

	return result;
}

/**
 * ipr_write_cfg_ioctl - Write adapter configuration IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_driver_cfg
 *
 * This function will write the user driver configuration
 * to the driver.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_write_cfg_ioctl(struct ipr_ioa_cfg *ioa_cfg,
			       struct ipr_driver_cfg *driver_cfg)
{
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	ioa_cfg->debug_level = driver_cfg->debug_level;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return 0;
}

/**
 * ipr_read_cfg_ioctl - Read adapter configuration IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_driver_cfg
 *
 * This function will write the driver configuration
 * to the user's buffer.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_read_cfg_ioctl(struct ipr_ioa_cfg *ioa_cfg,
			      struct ipr_driver_cfg *driver_cfg)
{
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	driver_cfg->debug_level = ioa_cfg->debug_level;
	driver_cfg->debug_level_max = IPR_MAX_DEBUG_LEVEL;
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return 0;
}

/**
 * ipr_get_bus_capabilities_ioctl - Get Bus Capabilities IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_bus_attributes
 *
 * This function will return the bus capabilities for the specified
 * SCSI bus in the user buffer.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_get_bus_capabilities_ioctl(struct ipr_ioa_cfg *ioa_cfg,
				  struct ipr_bus_attributes *bus_attributes)
{
	u8 bus;
	unsigned long lock_flags = 0;

	bus = bus_attributes->bus;

	if (bus > IPR_MAX_NUM_BUSES) {
		dev_err(&ioa_cfg->pdev->dev,
			"Invalid bus on get bus attributes IOCTL\n");
		return -EINVAL;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	bus_attributes->qas_enabled = 0;
	bus_attributes->bus_width = IPR_DEFAULT_BUS_WIDTH;
	bus_attributes->max_xfer_rate =
		ipr_get_max_scsi_speed(ioa_cfg, bus, IPR_DEFAULT_BUS_WIDTH);
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return 0;
}

/**
 * ipr_set_bus_attributes_ioctl - Set Bus Attributes IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_bus_attributes
 *
 * This function will save the bus attributes for the specified
 * SCSI bus.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_set_bus_attributes_ioctl(struct ipr_ioa_cfg *ioa_cfg,
				  struct ipr_bus_attributes *bus_attributes)
{
	unsigned long lock_flags = 0;
	u8 bus;

	bus = bus_attributes->bus;

	if (bus > IPR_MAX_NUM_BUSES) {
		dev_err(&ioa_cfg->pdev->dev,
			"Invalid bus on set bus attributes IOCTL\n");
		return -EINVAL;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
	memcpy(&ioa_cfg->bus_attr[bus], &bus_attributes,
	       sizeof(struct ipr_bus_attributes));
	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return 0;
}

/**
 * ipr_diagnostic_ioctl - IOA Diagnostics IOCTL 
 * @ioa_cfg:	ioa config struct
 *
 * This function will reset the adapter and wait a reasonable
 * amount of time for any errors that the adapter might log.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_diagnostic_ioctl(struct ipr_ioa_cfg *ioa_cfg)
{
	int rc = 0;
	unsigned long lock_flags = 0;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if ((rc = ipr_wait_until_ioctl_allowed(ioa_cfg))) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return rc;
	}

	ipr_block_requests(ioa_cfg);

	ioa_cfg->errors_logged = 0;

	if (ipr_reset_reload(ioa_cfg, IPR_SHUTDOWN_NORMAL) != SUCCESS)
		rc = -EIO;

	ipr_unblock_requests(ioa_cfg);

	/* Wait for a second for any errors to be logged */
	ipr_sleep(ioa_cfg, 1000);

	if (ioa_cfg->errors_logged)
		rc = -EIO;

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return rc;
}

/**
 * ipr_copy_sdt_to_user - Copy SDT to user buffer.
 * @ioa_cfg:	ioa config struct
 * @dest:		user buffer pointer
 * @length:		buffer length
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_copy_sdt_to_user(struct ipr_ioa_cfg *ioa_cfg,
				u8 *dest, u32 length)
{
	u32 *src;
	int page_index = 0;
	int bytes_to_copy = PAGE_SIZE;
	int result = 0;

	result = copy_to_user(dest, ioa_cfg->dump,
			      sizeof(struct ipr_dump_driver_header));

	dest += sizeof(struct ipr_dump_driver_header);
	length -= sizeof(struct ipr_dump_driver_header);

	if (!result) {
		result = copy_to_user(dest, &ioa_cfg->ioa_dump->hdr,
				      sizeof(struct ipr_dump_entry_header));

		dest += sizeof(struct ipr_dump_entry_header);
		length -= sizeof(struct ipr_dump_entry_header);
	}

	if (!result) {
		result = copy_to_user(dest, &ioa_cfg->ioa_dump->sdt,
				      sizeof(struct ipr_sdt));

		dest += sizeof(struct ipr_sdt);
		length -= sizeof(struct ipr_sdt);
	}

	while ((src = ioa_cfg->ioa_dump->ioa_data[page_index])) {
		if (length) {
			if (length > PAGE_SIZE)
				length -= PAGE_SIZE;
			else {
				bytes_to_copy = length;
				length = 0;
			}

			if (!result) {
				result = copy_to_user(dest, src,
						      bytes_to_copy);
				dest += bytes_to_copy;
			}
		}

		free_page((unsigned long)src);
		ioa_cfg->ioa_dump->ioa_data[page_index] = NULL;
		page_index++;
	}

	return result;
}

/**
 * ipr_dump_ioctl - IOA Dump IOCTL 
 * @ioa_cfg:	ioa config struct
 * @buffer:		user buffer for IOA dump
 *
 * This function will sleep waiting for a severe adapter error
 * which requires an adapter reset to recover from. When this
 * occurs, this function will copy the contents of the adapter's
 * memory to the user provided buffer for failure analysis.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_dump_ioctl(struct ipr_ioa_cfg *ioa_cfg,
			  struct ipr_dump_ioctl *dump_ioctl, unsigned long arg)
{
	struct ipr_dump_ioa_entry *dump_ioa_entry;
	struct ipr_dump_driver_header *dump_driver_header;
	unsigned long lock_flags = 0;
	int dump_len, result;
	u8 *u_buffer;

	u_buffer = (u8 *)(arg + offsetof(struct ipr_dump_ioctl, buffer));

	if (dump_ioctl->buffer_len < IPR_MIN_DUMP_SIZE) {
		ipr_err("Invalid buffer length on dump ioa %d\n", dump_ioctl->buffer_len);
		return -EINVAL;
	}

	dump_ioa_entry = kmalloc(sizeof(struct ipr_dump_ioa_entry), GFP_KERNEL);

	if (dump_ioa_entry == NULL) {
		ipr_err("Dump memory allocation failed\n");
		return -ENOMEM;
	}

	memset(dump_ioa_entry, 0, sizeof(struct ipr_dump_ioa_entry));

	dump_driver_header = kmalloc(sizeof(struct ipr_dump_driver_header), GFP_KERNEL);

	if (dump_driver_header == NULL) {
		ipr_err("Dump memory allocation failed\n");
		kfree(dump_ioa_entry);
		return -ENOMEM;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if (INACTIVE != ioa_cfg->sdt_state) {
		ipr_err("Invalid request, dump ioa already active\n");
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		kfree(dump_ioa_entry);
		kfree(dump_driver_header);
		return -EIO;
	}

	ioa_cfg->ioa_dump = dump_ioa_entry;
	ioa_cfg->dump = dump_driver_header;

	if (ioa_cfg->ioa_is_dead && !ioa_cfg->dump_taken) {
		ipr_get_ioa_smart_dump(ioa_cfg);
		ioa_cfg->dump_taken = 1;
	} else {
		ioa_cfg->sdt_state = WAIT_FOR_DUMP;
		ipr_interruptible_sleep_on(ioa_cfg, &ioa_cfg->sdt_wait_q);

		if (ioa_cfg->sdt_state == GET_DUMP)
			ipr_get_ioa_smart_dump(ioa_cfg);
	}

	if (ioa_cfg->sdt_state == DUMP_OBTAINED) {
		if (ipr_reset_reload(ioa_cfg, IPR_SHUTDOWN_NONE) == SUCCESS) {
			spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
			dump_len = min(dump_ioctl->buffer_len, ioa_cfg->dump->hdr.len);
			result = ipr_copy_sdt_to_user(ioa_cfg, u_buffer, dump_len);
			spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
		}
	}

	ioa_cfg->ioa_dump = NULL;
	ioa_cfg->dump = NULL;
	ioa_cfg->sdt_state = INACTIVE;

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	kfree(dump_ioa_entry);
	kfree(dump_driver_header);

	return 0;
}

/**
 * ipr_reset_adapter_ioctl - IOA Reset IOCTL 
 * @ioa_cfg:	ioa config struct
 *
 * This function will reset the adapter.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_reset_adapter_ioctl(struct ipr_ioa_cfg *ioa_cfg)
{
	int result = 0;
	unsigned long lock_flags;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if ((result = ipr_wait_until_ioctl_allowed(ioa_cfg))) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		return result;
	}

	ipr_block_requests(ioa_cfg);

	if (ipr_reset_reload(ioa_cfg, IPR_SHUTDOWN_NORMAL) != SUCCESS)
		result = -EIO;

	ipr_unblock_requests(ioa_cfg);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return result;
}

/**
 * ipr_alloc_ucode_buffer - Allocates a microcode download buffer
 * @buf_len:		buffer length
 *
 * Allocates a DMA'able buffer in chunks and assembles a scatter/gather
 * list to use for microcode download
 *
 * Return value:
 * 	pointer to sglist / NULL on failure
 **/
static struct ipr_sglist *ipr_alloc_ucode_buffer(int buf_len)
{
	int sg_size, order, bsize_elem, num_elem, i, j;
	struct ipr_sglist *sglist;
	struct scatterlist *scatterlist;
	void *kaddr;
	struct page *page;

	/* Get the minimum size per scatter/gather element */
	sg_size = buf_len / (IPR_MAX_SGLIST - 1);

	/* Get the actual size per element */
	order = get_order(sg_size);

	/* Determine the actual number of bytes per element */
	bsize_elem = PAGE_SIZE * (1 << order);

	/* Determine the actual number of sg entries needed */
	if (buf_len % bsize_elem)
		num_elem = (buf_len / bsize_elem) + 1;
	else
		num_elem = buf_len / bsize_elem;

	/* Allocate a scatter/gather list for the DMA */
	sglist = kmalloc(sizeof(struct ipr_sglist) +
			 (sizeof(struct scatterlist) * (num_elem - 1)),
			 GFP_KERNEL);

	if (sglist == NULL) {
		ipr_trace;
		return NULL;
	}

	memset(sglist, 0, sizeof(struct ipr_sglist) +
	       (sizeof(struct scatterlist) * (num_elem - 1)));

	scatterlist = sglist->scatterlist;

	sglist->order = order;
	sglist->num_sg = num_elem;

	/* Allocate a bunch of sg elements */
	for (i = 0; i < num_elem; i++) {
		page = alloc_pages(GFP_KERNEL, order);
		if (!page) {
			ipr_trace;

			/* Free up what we already allocated */
			for (j = i - 1; j >= 0; j--)
				__free_pages(scatterlist[j].page, order);
			kfree(sglist);
			sglist = NULL;
			break;
		}

		scatterlist[i].page = page;
		kaddr = kmap(scatterlist[i].page);
		memset(kaddr, 0, bsize_elem);
		kunmap(scatterlist[i].page);
	}

	return sglist;
}

/**
 * ipr_free_ucode_buffer - Frees a microcode download buffer
 * @p_dnld:		scatter/gather list pointer
 *
 * Free a DMA'able ucode download buffer previously allocated with
 * ipr_alloc_ucode_buffer
 *
 * Return value:
 * 	nothing
 **/
static void ipr_free_ucode_buffer(struct ipr_sglist *sglist)
{
	int i;

	for (i = 0; i < sglist->num_sg; i++)
		__free_pages(sglist->scatterlist[i].page, sglist->order);

	kfree(sglist);
}

/**
 * ipr_copy_ucode_buffer - Copy user buffer to kernel buffer
 * @sglist:		scatter/gather list pointer
 * @buffer:		user buffer pointer
 * @len:		buffer length
 *
 * Copy a microcode image from a user buffer into a buffer allocated by
 * ipr_alloc_ucode_buffer
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_copy_ucode_buffer(struct ipr_sglist *sglist,
				 u8 *buffer, u32 len)
{
	int bsize_elem, i, result = 0;
	struct scatterlist *scatterlist;
	void *kaddr;

	/* Determine the actual number of bytes per element */
	bsize_elem = PAGE_SIZE * (1 << sglist->order);

	scatterlist = sglist->scatterlist;

	for (i = 0; i < (len / bsize_elem); i++, buffer += bsize_elem) {
		kaddr = kmap(scatterlist[i].page);
		result = copy_from_user(kaddr, buffer, bsize_elem);
		kunmap(scatterlist[i].page);

		scatterlist[i].length = bsize_elem;

		if (result != 0) {
			ipr_trace;
			return result;
		}
	}

	if (len % bsize_elem) {
		kaddr = kmap(scatterlist[i].page);
		result = copy_from_user(kaddr, buffer, len % bsize_elem);
		kunmap(scatterlist[i].page);

		scatterlist[i].length = len % bsize_elem;
	}

	return result;
}

/**
 * ipr_map_ucode_buffer - Map a microcode download buffer
 * @ipr_cmd:	ipr command struct
 * @sglist:		scatter/gather list
 * @len:		total length of download buffer
 *
 * Maps a microcode download scatter/gather list for DMA and
 * builds the IOADL.
 *
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_map_ucode_buffer(struct ipr_cmnd *ipr_cmd,
				struct ipr_sglist *sglist, int len)
{
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	struct ipr_ioarcb *ioarcb = &ipr_cmd->ioarcb;
	struct ipr_ioadl_desc *ioadl = ipr_cmd->ioadl;
	struct scatterlist *scatterlist = sglist->scatterlist;
	int i;

	ipr_cmd->dma_use_sg = pci_map_sg(ioa_cfg->pdev, scatterlist,
					 sglist->num_sg, DMA_TO_DEVICE);

	ioarcb->cmd_pkt.write_not_read = 1;
	ioarcb->write_data_transfer_length = cpu_to_be32(len);
	ioarcb->write_ioadl_len =
		cpu_to_be32(sizeof(struct ipr_ioadl_desc) * ipr_cmd->dma_use_sg);

	for (i = 0; i < ipr_cmd->dma_use_sg; i++) {
		ioadl[i].flags_and_data_len =
			cpu_to_be32(IPR_IOADL_FLAGS_WRITE | sg_dma_len(&scatterlist[i]));
		ioadl[i].address =
			cpu_to_be32(sg_dma_address(&scatterlist[i]));
	}

	if (likely(ipr_cmd->dma_use_sg)) {
		ioadl[i-1].flags_and_data_len |=
			cpu_to_be32(IPR_IOADL_FLAGS_LAST);
	}
	else {
		dev_err(&ioa_cfg->pdev->dev, "pci_map_sg failed!\n");
		return -EIO;
	}

	return 0;
}

/**
 * ipr_ucode_dnld_ioctl - IOA microcode download IOCTL 
 * @ioa_cfg:	ioa config struct
 * @arg:		user space pointer to struct ipr_ucode_download_ioctl
 *
 * This function will download microcode to the adapter.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_ucode_dnld_ioctl(struct ipr_ioa_cfg *ioa_cfg,
				struct ipr_ucode_download_ioctl *dnld_ioctl,
				unsigned long arg)
{
	int result;
	unsigned long lock_flags = 0;
	struct ipr_sglist *sglist;
	u8 *u_buffer;
	u32 ioasc;
	struct ipr_cmnd *ipr_cmd;

	if ((dnld_ioctl->buffer_len == 0) ||
	    (dnld_ioctl->buffer_len > IPR_MAX_WRITE_BUFFER_SIZE)) {
		dev_err(&ioa_cfg->pdev->dev,
			"Invalid buffer length on ucode download ioctl\n");
		return -EINVAL;
	}

	u_buffer = (u8 *)(arg + offsetof(struct ipr_ucode_download_ioctl, buffer));

	sglist = ipr_alloc_ucode_buffer(dnld_ioctl->buffer_len);

	if (!sglist) {
		dev_err(&ioa_cfg->pdev->dev, "Microcode buffer allocation failed\n");
		return -ENOMEM;
	}

	result = ipr_copy_ucode_buffer(sglist, u_buffer, dnld_ioctl->buffer_len);

	if (result) {
		dev_err(&ioa_cfg->pdev->dev,
			"Microcode buffer copy to kernel memory failed\n");
		ipr_free_ucode_buffer(sglist);
		return result;
	}

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	if ((result = ipr_wait_until_ioctl_allowed(ioa_cfg))) {
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		ipr_free_ucode_buffer(sglist);
		return result;
	}

	ipr_block_requests(ioa_cfg);

	if ((result = ipr_shutdown_ioa(ioa_cfg))) {
		ipr_unblock_requests(ioa_cfg);
		spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
		ipr_free_ucode_buffer(sglist);
		return result;
	}

	ipr_cmd = ipr_get_free_ipr_cmnd(ioa_cfg);

	ipr_cmd->ioarcb.res_handle = cpu_to_be32(IPR_IOA_RES_HANDLE);
	ipr_cmd->ioarcb.cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;
	ipr_cmd->ioarcb.cmd_pkt.cdb[0] = WRITE_BUFFER;
	ipr_cmd->ioarcb.cmd_pkt.cdb[1] = 5;
	ipr_cmd->ioarcb.cmd_pkt.cdb[6] = (dnld_ioctl->buffer_len & 0xff0000) >> 16;
	ipr_cmd->ioarcb.cmd_pkt.cdb[7] = (dnld_ioctl->buffer_len & 0x00ff00) >> 8;
	ipr_cmd->ioarcb.cmd_pkt.cdb[8] = dnld_ioctl->buffer_len & 0x0000ff;
	ipr_cmd->ioarcb.cmd_pkt.write_not_read = 1;

	if ((result = ipr_map_ucode_buffer(ipr_cmd, sglist, dnld_ioctl->buffer_len)))
		goto leave;

	ipr_send_blocking_cmd(ipr_cmd, ipr_timeout, IPR_WRITE_BUFFER_TIMEOUT);

	ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	if (ioasc) {
		dnld_ioctl->ioasc = ioasc;
		result = -EIO;
	}

	pci_unmap_sg(ioa_cfg->pdev, sglist->scatterlist,
		     sglist->num_sg, DMA_TO_DEVICE);

leave:
	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);

	if (ipr_reset_reload(ioa_cfg, IPR_SHUTDOWN_NONE) != SUCCESS)
		result = -EIO;

	ipr_unblock_requests(ioa_cfg);

	ipr_free_ucode_buffer(sglist);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	return result;

}

/**
 * ipr_ioctl - IOA IOCTL interface
 * @inode:		character device inode
 * @filp:		character device file pointer
 * @cmd:		IOCTL command
 * @arg:		IOCTL argument
 *
 * This function handles IOCTLs to the adapter character device.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_ioctl(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	struct ipr_ioa_cfg *ioa_cfg;
	void *k_buffer = NULL;
	int result = 0;

	/* Have we been opened? */
	if (filp->private_data == NULL) {
		ipr_trace;
		return -ENXIO;
	}

	if (_IOC_TYPE(cmd) != IPR_IOCTL_CODE)
		return -EINVAL;

	ioa_cfg = filp->private_data;

	if (_IOC_SIZE(cmd)) {
		k_buffer = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL);
		if (!k_buffer) {
			ipr_trace;
			return -ENOMEM;
		}

		result = copy_from_user(k_buffer, (const void *)arg, _IOC_SIZE(cmd));

		if (result) {
			ipr_trace;
			kfree(k_buffer);
			return result;
		}
	}

	if (down_interruptible(&ioa_cfg->ioctl_semaphore)) {
		ipr_trace;
		kfree(k_buffer);
		return -EINTR;
	}

	if (ioa_cfg->ioa_is_dead) {
		ipr_trace;
		up(&ioa_cfg->ioctl_semaphore);
		kfree(k_buffer);
		return -EIO;
	}

	switch (cmd) {
	case IPR_IOCTL_PASSTHRU:
		result = ipr_passthru_ioctl(ioa_cfg, k_buffer, arg);
		break;
	case IPR_IOCTL_RUN_DIAGNOSTICS:
		result = ipr_diagnostic_ioctl(ioa_cfg);
		break;
	case IPR_IOCTL_DUMP_IOA:
		result = ipr_dump_ioctl(ioa_cfg, k_buffer, arg);
		break;
	case IPR_IOCTL_RESET_IOA:
		result = ipr_reset_adapter_ioctl(ioa_cfg);
		break;
	case IPR_IOCTL_READ_DRIVER_CFG:
		result = ipr_read_cfg_ioctl(ioa_cfg, k_buffer);
		break;
	case IPR_IOCTL_WRITE_DRIVER_CFG:
		result = ipr_write_cfg_ioctl(ioa_cfg, k_buffer);
		break;
	case IPR_IOCTL_GET_BUS_CAPABILTIES:
		result = ipr_get_bus_capabilities_ioctl(ioa_cfg, k_buffer);
		break;
	case IPR_IOCTL_SET_BUS_ATTRIBUTES:
		result = ipr_set_bus_attributes_ioctl(ioa_cfg, k_buffer);
		break;
	case IPR_IOCTL_GET_TRACE:
		result = ipr_get_trace_ioctl(ioa_cfg, k_buffer, arg);
		break;
	case IPR_IOCTL_RECLAIM_CACHE:
		result = ipr_reclaim_cache_ioctl(ioa_cfg, k_buffer, arg);
		break;
	case IPR_IOCTL_QUERY_CONFIGURATION:
		result = ipr_query_config_ioctl(ioa_cfg, k_buffer, arg);
		break;
	case IPR_IOCTL_UCODE_DOWNLOAD:
		result = ipr_ucode_dnld_ioctl(ioa_cfg, k_buffer, arg);
		break;
	default:
		result = -EINVAL;
	};

	if (_IOC_SIZE(cmd) && (_IOC_DIR(cmd) & _IOC_READ)) 
		result |= copy_to_user((void *)arg, k_buffer, _IOC_SIZE(cmd));

	up(&ioa_cfg->ioctl_semaphore);

	kfree(k_buffer);

	return result;
}

/**
 * ipr_wait_iodbg_ack - Wait for an IODEBUG ACK from the IOA
 * @ioa_cfg:		ioa config struct
 * @max_delay:		max delay in micro-seconds to wait
 *
 * Waits for an IODEBUG ACK from the IOA, doing busy looping.
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_wait_iodbg_ack(struct ipr_ioa_cfg *ioa_cfg, int max_delay)
{
	volatile u32 pcii_reg;
	int delay = 1;
	int rc = -EIO;

	/* Read interrupt reg until IOA signals IO Debug Acknowledge */
	while (delay < max_delay) {
		pcii_reg = readl(ioa_cfg->regs.sense_interrupt_reg);

		if (pcii_reg & IPR_PCII_IO_DEBUG_ACKNOWLEDGE) {
			rc = 0;
			break;
		}

		/* udelay cannot be used if delay is more than a few milliseconds */
		if ((delay / 1000) > MAX_UDELAY_MS)
			mdelay(delay / 1000);
		else
			udelay(delay);

		delay += delay;
	}
	return rc;
}

/**
 * ipr_get_ldump_data_section - Dump IOA memory
 * @ioa_cfg:			ioa config struct
 * @start_addr:			adapter address to dump
 * @dest:				destination kernel buffer
 * @length_in_words:	length to dump in 4 byte words
 *
 * Return value:
 * 	0 on success / -EIO on failure
 **/
static int ipr_get_ldump_data_section(struct ipr_ioa_cfg *ioa_cfg,
				      u32 start_addr,
				      u32 *dest, u32 length_in_words)
{
	u32 temp_pcii_reg;
	int i, delay = 0;

	/* Write IOA interrupt reg starting LDUMP state  */
	writel((IPR_UPROCI_RESET_ALERT | IPR_UPROCI_IO_DEBUG_ALERT),
	       ioa_cfg->regs.set_uproc_interrupt_reg);

	/* Wait for IO debug acknowledge */
	if (ipr_wait_iodbg_ack(ioa_cfg,
			       IPR_LDUMP_MAX_LONG_ACK_DELAY_IN_USEC)) {
		dev_err(&ioa_cfg->pdev->dev,
			"IOA long data transfer timeout\n");
		return -EIO;
	}

	/* Signal LDUMP interlocked - clear IO debug ack */
	writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
	       ioa_cfg->regs.clr_interrupt_reg);

	/* Write Mailbox with starting address */
	writel(start_addr, ioa_cfg->ioa_mailbox);

	/* Signal address valid - clear IOA Reset alert */
	writel(IPR_UPROCI_RESET_ALERT,
	       ioa_cfg->regs.clr_uproc_interrupt_reg);

	for (i = 0; i < length_in_words; i++) {
		/* Wait for IO debug acknowledge */
		if (ipr_wait_iodbg_ack(ioa_cfg,
				       IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC))
		{
			dev_err(&ioa_cfg->pdev->dev,
				"IOA short data transfer timeout\n");
			return -EIO;
		}

		/* Read data from mailbox and increment destination pointer */
		*dest = cpu_to_be32(readl(ioa_cfg->ioa_mailbox));
		dest++;

		/* For all but the last word of data, signal data received */
		if (i < (length_in_words - 1))
			/* Signal dump data received - Clear IO debug Ack */
			writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
			       ioa_cfg->regs.clr_interrupt_reg);
	}

	/* Signal end of block transfer. Set reset alert then clear IO debug ack */
	writel(IPR_UPROCI_RESET_ALERT,
	       ioa_cfg->regs.set_uproc_interrupt_reg);

	writel(IPR_UPROCI_IO_DEBUG_ALERT,
	       ioa_cfg->regs.clr_uproc_interrupt_reg);

	/* Signal dump data received - Clear IO debug Ack */
	writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
	       ioa_cfg->regs.clr_interrupt_reg);

	/* Wait for IOA to signal LDUMP exit - IOA reset alert will be cleared */
	while (delay < IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC) {
		temp_pcii_reg =
		    readl(ioa_cfg->regs.sense_uproc_interrupt_reg);

		if (!(temp_pcii_reg & IPR_UPROCI_RESET_ALERT))
			break;

		/* Delay 10 usecs. */
		udelay(10);
		delay += (10);
	}

	return 0;
}


/**
 * ipr_get_unit_check_buffer - Get the unit check buffer from the IOA
 * @ioa_cfg:		ioa config struct
 *
 * Fetches the unit check buffer from the adapter by clocking the data
 * through the mailbox register.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_get_unit_check_buffer(struct ipr_ioa_cfg *ioa_cfg)
{
	unsigned long mailbox;
	struct ipr_hostrcb *hostrcb;
	struct ipr_uc_sdt sdt;
	int rc, length;
	u8 op_code;

	mailbox = readl(ioa_cfg->ioa_mailbox);

	if (!ipr_sdt_is_fmt2(mailbox)) {
		ipr_unit_check_no_data(ioa_cfg);
		LEAVE;
		return;
	}

	memset(&sdt, 0, sizeof(struct ipr_uc_sdt));
	rc = ipr_get_ldump_data_section(ioa_cfg, mailbox, (u32 *) &sdt,
					(sizeof(struct ipr_uc_sdt)) / sizeof(u32));

	if (rc || (be32_to_cpu(sdt.hdr.state) != IPR_FMT2_SDT_READY_TO_USE) ||
	    (sdt.entry[0].bar_str_offset == 0) || !sdt.entry[0].valid_entry) {
		ipr_unit_check_no_data(ioa_cfg);
		LEAVE;
		return;
	}

	/* Find length of the first sdt entry (UC buffer) */
	length = (be32_to_cpu(sdt.entry[0].end_offset) -
		  be32_to_cpu(sdt.entry[0].bar_str_offset)) & IPR_FMT2_MBX_ADDR_MASK;

	/* xxx try to remove the reliance on op_code in hostrcb.
	 Possibly define a structure for the actual hostrcb */
	hostrcb = list_entry(ioa_cfg->hostrcb_free_q.next,
			     struct ipr_hostrcb, queue);
	list_del(&hostrcb->queue);
	op_code = hostrcb->op_code;
	memset(hostrcb, 0, 1024);

	rc = ipr_get_ldump_data_section(ioa_cfg,
					be32_to_cpu(sdt.entry[0].bar_str_offset),
					(u32 *)hostrcb,
					min(length, 1024) / sizeof(u32));

	if (!rc)
		ipr_handle_log_data(ioa_cfg, hostrcb);
	else
		ipr_unit_check_no_data(ioa_cfg);

	hostrcb->op_code = op_code;
	list_add_tail(&hostrcb->queue, &ioa_cfg->hostrcb_free_q);
}

/**
 * ipr_unit_check_no_data - Log a unit check/no data error log
 * @ioa_cfg:		ioa config struct
 *
 * Logs an error indicating the adapter unit checked, but for some
 * reason, we were unable to fetch the unit check buffer.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_unit_check_no_data(struct ipr_ioa_cfg *ioa_cfg)
{
	ioa_cfg->errors_logged++;
	dev_err(&ioa_cfg->pdev->dev, "IOA unit check with no data\n");
}

/**
 * ipr_worker_thread - Worker thread
 * @data:		ioa config struct
 *
 * Called at task level from a work thread. This function takes care
 * of adding and removing device from the mid-layer as configuration
 * changes are detected by the adapter.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_worker_thread(void *data)
{
	unsigned long lock_flags;
	struct ipr_resource_entry *res;
	struct scsi_device *scsi_device;
	struct ipr_ioa_cfg *ioa_cfg = data;
	u8 bus, target, lun;
	int did_work;

	ENTER;

	spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);

	do {
		did_work = 0;

		if (!ioa_cfg->allow_cmds) {
			ipr_trace;
			spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
			return;
		}

		list_for_each_entry(res, &ioa_cfg->used_res_q, queue) {
			if (res->add_to_ml) {
				did_work = 1;
				bus = res->cfgte.res_addr.bus;
				target = res->cfgte.res_addr.target;
				lun = res->cfgte.res_addr.lun;

				ipr_trace;
				spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
				scsi_add_device(ioa_cfg->host, bus, target, lun);
				spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
				break;
			} else if (res->del_from_ml && res->scsi_device) {
				did_work = 1;
				scsi_device = res->scsi_device;

				ipr_trace;
				spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);
				scsi_remove_device(scsi_device);
				spin_lock_irqsave(ioa_cfg->host->host_lock, lock_flags);
				break;
			} else if (res->del_from_ml) {
				ipr_trace;
			}
		}
	} while(did_work);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, lock_flags);

	LEAVE;

	return;
}

/**
 * ipr_gen_sense - Generate SCSI sense data from an IOASA
 * @ioasa:		IOASA
 * @sense_buf:	sense data buffer
 * 
 * Return value:
 * 	none
 **/
static void ipr_gen_sense(struct ipr_cmnd *ipr_cmd)
{
	u32 failing_lba;
	u8 *sense_buf = ipr_cmd->scsi_cmd->sense_buffer;
	struct ipr_ioasa *ioasa = &ipr_cmd->ioasa;
	u32 ioasc = be32_to_cpu(ioasa->ioasc);

	memset(sense_buf, 0, SCSI_SENSE_BUFFERSIZE);

	if (ioasc >= IPR_FIRST_DRIVER_IOASC)
		return;

	ipr_cmd->scsi_cmd->result = (CHECK_CONDITION << 1);

	if ((ioasc == IPR_IOASC_MED_DO_NOT_REALLOC) &&
	    (ioasa->vset.failing_lba_hi != 0)) {
		sense_buf[0] = 0x72;
		sense_buf[1] = IPR_IOASC_SENSE_KEY(ioasc);
		sense_buf[2] = IPR_IOASC_SENSE_CODE(ioasc);
		sense_buf[3] = IPR_IOASC_SENSE_QUAL(ioasc);

		sense_buf[7] = 12;
		sense_buf[8] = 0;
		sense_buf[9] = 0x0A;
		sense_buf[10] = 0x80;

		failing_lba = be32_to_cpu(ioasa->vset.failing_lba_hi);

		sense_buf[12] = (failing_lba & 0xff000000) >> 24;
		sense_buf[13] = (failing_lba & 0x00ff0000) >> 16;
		sense_buf[14] = (failing_lba & 0x0000ff00) >> 8;
		sense_buf[15] = failing_lba & 0x000000ff;

		failing_lba = be32_to_cpu(ioasa->vset.failing_lba_lo);

		sense_buf[16] = (failing_lba & 0xff000000) >> 24;
		sense_buf[17] = (failing_lba & 0x00ff0000) >> 16;
		sense_buf[18] = (failing_lba & 0x0000ff00) >> 8;
		sense_buf[19] = failing_lba & 0x000000ff;
	} else {
		sense_buf[0] = 0x70;
		sense_buf[2] = IPR_IOASC_SENSE_KEY(ioasc);
		sense_buf[12] = IPR_IOASC_SENSE_CODE(ioasc);
		sense_buf[13] = IPR_IOASC_SENSE_QUAL(ioasc);

		/* Illegal request */
		if ((IPR_IOASC_SENSE_KEY(ioasc) == 0x05) &&
		    (be32_to_cpu(ioasa->ioasc_specific) & IPR_FIELD_POINTER_VALID)) {
			sense_buf[7] = 10;	/* additional length */

			/* IOARCB was in error */
			if (IPR_IOASC_SENSE_CODE(ioasc) == 0x24)
				sense_buf[15] = 0xC0;
			else	/* Parameter data was invalid */
				sense_buf[15] = 0x80;

			sense_buf[16] =
			    ((IPR_FIELD_POINTER_MASK &
			      be32_to_cpu(ioasa->ioasc_specific)) >> 8) & 0xff;
			sense_buf[17] =
			    (IPR_FIELD_POINTER_MASK &
			     be32_to_cpu(ioasa->ioasc_specific)) & 0xff;
		} else {
			if (ioasc == IPR_IOASC_MED_DO_NOT_REALLOC) {
				failing_lba = be32_to_cpu(ioasa->vset.failing_lba_lo);

				sense_buf[0] |= 0x80;	/* Or in the Valid bit */
				sense_buf[3] = (failing_lba & 0xff000000) >> 24;
				sense_buf[4] = (failing_lba & 0x00ff0000) >> 16;
				sense_buf[5] = (failing_lba & 0x0000ff00) >> 8;
				sense_buf[6] = failing_lba & 0x000000ff;
			}

			sense_buf[7] = 6;	/* additional length */
		}
	}
}

/**
 * ipr_erp_start - Process an error response for a SCSI op
 * @ioa_cfg:	ioa config struct
 * @ipr_cmd:	ipr command struct
 *
 * This function determines whether or not to initiate ERP
 * on the affected device. 
 *
 * Return value:
 * 	nothing
 **/
static void ipr_erp_start(struct ipr_ioa_cfg *ioa_cfg,
			      struct ipr_cmnd *ipr_cmd)
{
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_resource_entry *res = scsi_cmd->device->hostdata;
	u32 ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	/* xxx */
	if (!scsi_cmd->device->hostdata)
		ipr_scsi_eh_done(ipr_cmd);

	if (ipr_is_vset_device(res))
		ipr_gen_sense(ipr_cmd);
	else
		ipr_dump_ioasa(ioa_cfg, ipr_cmd);

	switch (ioasc & IPR_IOASC_IOASC_MASK) {
	case IPR_IOASC_NR_ACA_ACTIVE:
	case IPR_IOASC_RCV_DEV_BUS_MSG_RECEIVED:
	case IPR_IOASC_ABORTED_CMD_TERM_BY_HOST:
		scsi_cmd->result |= (DID_ERROR << 16);
		break;
	case IPR_IOASC_IR_RESOURCE_HANDLE:
		scsi_cmd->result |= (DID_NO_CONNECT << 16);
		break;
	case IPR_IOASC_HW_SEL_TIMEOUT:
		scsi_cmd->result |= (DID_NO_CONNECT << 16);
		res->needs_sync_complete = 1;
		break;
	case IPR_IOASC_SYNC_REQUIRED:
		if (!res->in_erp) {
			res->needs_sync_complete = 1;
			scsi_cmd->result |= (BUSY << 1);
		} else {
			scsi_cmd->result |= (DID_ERROR << 16);
		}
		break;
	case IPR_IOASC_MED_DO_NOT_REALLOC: /* prevent retries */
		scsi_cmd->result |= (DID_PASSTHROUGH << 16);
		break;
	case IPR_IOASC_BUS_WAS_RESET:
	case IPR_IOASC_BUS_WAS_RESET_BY_OTHER:
		/*
		 * Report the bus reset and ask for a retry. The device
		 * will give CC/UA the next command.
		 */
		if (!res->resetting_device)
			scsi_report_bus_reset(ioa_cfg->host, scsi_cmd->device->channel);
		scsi_cmd->result |= (DID_ERROR << 16);
		res->needs_sync_complete = 1;
		break;
	case IPR_IOASC_HW_DEV_BUS_STATUS:
		scsi_cmd->result |= IPR_IOASC_SENSE_STATUS(ioasc);
		ipr_erp_cancel_all(ipr_cmd);
		return;
	default:
		scsi_cmd->result |= (DID_ERROR << 16);
		if (!ipr_is_vset_device(res))
			res->needs_sync_complete = 1;
		break;
	}

	ipr_unmap_sglist(ioa_cfg, ipr_cmd);
	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);
	scsi_cmd->scsi_done(scsi_cmd);
}

/**
 * ipr_erp_cancel_all - Send cancel all to a device
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a cancel all to a device to clear the
 * queue. If we are running TCQ on the device, QERR is set to 1,
 * which means all outstanding ops have been dropped on the floor.
 * Cancel all will return them to us.
 *
 * Return value:
 * 	nothing
 **/
static void ipr_erp_cancel_all(struct ipr_cmnd *ipr_cmd)
{
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_resource_entry *res = scsi_cmd->device->hostdata;
	struct ipr_cmd_pkt *cmd_pkt;

	res->in_erp = 1;

	ipr_reinit_ipr_cmnd_for_erp(ipr_cmd);

	cmd_pkt = &ipr_cmd->ioarcb.cmd_pkt;
	cmd_pkt->request_type = IPR_RQTYPE_IOACMD;
	cmd_pkt->cdb[0] = IPR_CANCEL_ALL_REQUESTS;

	ipr_do_req(ipr_cmd, ipr_erp_request_sense, ipr_timeout,
		   IPR_CANCEL_ALL_TIMEOUT);
}

/**
 * ipr_erp_request_sense - Send request sense to a device
 * @ipr_cmd:	ipr command struct
 *
 * This function sends a request sense to a device as a result
 * of a check condition. 
 *
 * Return value:
 * 	nothing
 **/
static void ipr_erp_request_sense(struct ipr_cmnd *ipr_cmd)
{
	struct ipr_cmd_pkt *cmd_pkt = &ipr_cmd->ioarcb.cmd_pkt;
	u32 ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	/* xxx remove after testing this. should never happen */
	if (IPR_IOASC_SENSE_KEY(ioasc) > 0)
		ipr_trace;

	ipr_reinit_ipr_cmnd_for_erp(ipr_cmd);

	cmd_pkt->request_type = IPR_RQTYPE_SCSICDB;
	cmd_pkt->cdb[0] = REQUEST_SENSE;
	cmd_pkt->cdb[4] = SCSI_SENSE_BUFFERSIZE;
	cmd_pkt->sync_override = 1;
	cmd_pkt->no_underlength_checking = 1;
	cmd_pkt->timeout = cpu_to_be16(IPR_REQUEST_SENSE_TIMEOUT / HZ);

	ipr_cmd->ioadl[0].flags_and_data_len =
		cpu_to_be32(IPR_IOADL_FLAGS_READ_LAST | SCSI_SENSE_BUFFERSIZE);
	ipr_cmd->ioadl[0].address =
		cpu_to_be32(ipr_cmd->sense_buffer_dma);

	ipr_cmd->ioarcb.read_ioadl_len =
		cpu_to_be32(sizeof(struct ipr_ioadl_desc));
	ipr_cmd->ioarcb.read_data_transfer_length =
		cpu_to_be32(SCSI_SENSE_BUFFERSIZE);

	ipr_do_req(ipr_cmd, ipr_erp_done, ipr_timeout,
		   IPR_REQUEST_SENSE_TIMEOUT * IPR_TIMEOUT_MULTIPLIER);
}

/**
 * ipr_erp_done - Process completion of ERP for a device
 * @ipr_cmd:		ipr command struct
 *
 * This function copies the sense buffer into the scsi_cmd
 * struct and pushes the scsi_done function. 
 *
 * Return value:
 * 	nothing
 **/
static void ipr_erp_done(struct ipr_cmnd *ipr_cmd)
{
	struct scsi_cmnd *scsi_cmd = ipr_cmd->scsi_cmd;
	struct ipr_resource_entry *res = scsi_cmd->device->hostdata;
	struct ipr_ioa_cfg *ioa_cfg = ipr_cmd->ioa_cfg;
	u32 ioasc = be32_to_cpu(ipr_cmd->ioasa.ioasc);

	if (IPR_IOASC_SENSE_KEY(ioasc) > 0) {
		scsi_cmd->result |= (DID_ERROR << 16);
		ipr_sdev_err(scsi_cmd->device,
			     "Request Sense failed with IOASC: 0x%08X\n", ioasc);
	} else {
		memcpy(scsi_cmd->sense_buffer, ipr_cmd->sense_buffer,
		       SCSI_SENSE_BUFFERSIZE);
	}

	res->needs_sync_complete = 1;
	ipr_unmap_sglist(ioa_cfg, ipr_cmd);
	list_add_tail(&ipr_cmd->queue, &ioa_cfg->free_q);
	scsi_cmd->scsi_done(scsi_cmd);
}

/**
 * ipr_adjust_urc - Munges the URC
 * @error_index:		index into IOASC table
 * @res_addr:			failing device resource address
 * @ioasc:				IOASC
 * @dev_urc:			device URC
 * #err_string:			error string describing the error
 *
 * Return value:
 * 	URC to use
 **/
static u16 ipr_adjust_urc(u32 error_index,
			  struct ipr_res_addr res_addr,
			  u32 ioasc, int dev_urc, char *err_string)
{
	u16 urc;

	if (dev_urc)
		urc = ipr_error_table[error_index].dev_urc;
	else
		urc = ipr_error_table[error_index].iop_urc;

	strcpy(err_string, ipr_error_table[error_index].error);

	switch (ioasc) {
	case 0x01080000:
		if (!dev_urc)	/* 8140 and 813x */
			strcpy(err_string, "IOA detected recoverable device bus error");

		if (IPR_GET_PHYS_LOC(res_addr) != IPR_IOA_RES_ADDR) {
			if (dev_urc)
				urc = 0xfffe;
			else
				urc = 0x8130 | (res_addr.bus & 0xf);
		}
		break;
	case 0x015D0000:
		if (!dev_urc) {
			if (IPR_GET_PHYS_LOC(res_addr) != IPR_IOA_RES_ADDR)
				urc = 0x8146;
			else	/* 8145 */
				strcpy(err_string, "A recoverable IOA error occurred");
		}
		break;
	case 0x04080000:
	case 0x04088000:
	case 0x06288000:
	case 0x06678000:
		urc |= (res_addr.bus & 0xf);
		break;
	case 0x06670600:
		if (!dev_urc)
			urc |= (res_addr.bus & 0xf);
		break;
	case 0x04080100:
		if (IPR_GET_PHYS_LOC(res_addr) == IPR_IOA_RES_ADDR) {
			if (dev_urc) {
				urc = 0;
			} else {
				urc = 0x8150;
				strcpy(err_string, "A permanent IOA failure occurred");
			}
		}
		break;
	default:
		break;
	}

	if (urc == 0x8141)
		strcpy(err_string, "IOA detected recoverable device error");
	else if (urc == 0x3400)
		strcpy(err_string, "IOA detected device error");
	else if (urc == 0xFFFB)
		strcpy(err_string, "SCSI bus reset occurred");

	return urc;
}

/**
 * ipr_sdt_copy - Copy Smart Dump Table to kernel buffer
 * @ioa_cfg:		ioa config struct
 * @pci_address:	adapter address
 * @length:			length of data to copy
 *
 * Copy data from PCI adapter to kernel buffer.
 * Note: length MUST be a 4 byte multiple
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_sdt_copy(struct ipr_ioa_cfg *ioa_cfg,
			unsigned long pci_address, u32 length)
{
	int bytes_copied = 0;
	int current_length, rc, rem_len, rem_page_len;
	u32 *page = NULL;

	while ((bytes_copied < length) && 
	       ((ioa_cfg->ioa_dump->hdr.length + bytes_copied) < IPR_MAX_IOA_DUMP_SIZE)) {
		if ((ioa_cfg->ioa_dump->page_offset >= PAGE_SIZE) ||
		    (ioa_cfg->ioa_dump->page_offset == 0)) {
			page = (u32 *)__get_free_page(GFP_ATOMIC);

			if (!page) {
				ipr_trace;
				return bytes_copied;
			}

			ioa_cfg->ioa_dump->page_offset = 0;
			ioa_cfg->ioa_dump->ioa_data[ioa_cfg->ioa_dump->next_page_index] = page;
			ioa_cfg->ioa_dump->next_page_index++;
		} else
			page = ioa_cfg->ioa_dump->ioa_data[ioa_cfg->ioa_dump->next_page_index - 1];

		rem_len = length - bytes_copied;
		rem_page_len = PAGE_SIZE - ioa_cfg->ioa_dump->page_offset;

		/* Copy the min of remaining length to copy and the remaining space in this page */
		current_length = min(rem_len, rem_page_len);

		spin_lock_irq(ioa_cfg->host->host_lock);

		if (ioa_cfg->sdt_state == ABORT_DUMP) {
			rc = -EIO;
		} else {
			rc = ipr_get_ldump_data_section(ioa_cfg,
							pci_address + bytes_copied,
							&page[ioa_cfg->ioa_dump->page_offset / 4],
							(current_length / sizeof(u32)));
		}

		spin_unlock_irq(ioa_cfg->host->host_lock);

		if (!rc) {
			ioa_cfg->ioa_dump->page_offset += current_length;
			bytes_copied += current_length;
		} else {
			ipr_trace;
			break;
		}

		/* xxx Since our dump could take a while, we want to let other people
		   have some processor time while we dump */
		/* ipr_sleep_no_lock(1); */
	}

	return bytes_copied;
}

/**
 * ipr_get_ioa_smart_dump - Fetch IOA smart dump.
 * @ioa_cfg:		ioa config struct
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ipr_get_ioa_smart_dump(struct ipr_ioa_cfg *ioa_cfg)
{
	unsigned long mailbox, sdt_start_addr;
	unsigned long sdt_entry_word, dump_data_buffer;
	u32 num_entries, num_entries_used, start_offset, end_offset;
	u32 bytes_to_copy, bytes_copied, rc;
	u32 *buffer;
	struct ipr_sdt *sdt;
	int sdt_entry_index;

	ENTER;

	mailbox = readl(ioa_cfg->ioa_mailbox);
	start_offset = mailbox & IPR_FMT2_MBX_ADDR_MASK;

	if (!ipr_sdt_is_fmt2(mailbox)) {
		dev_err(&ioa_cfg->pdev->dev, "Invalid SDT format: %lx\n", mailbox);
		return -ENXIO;
	}

	ioa_cfg->ioa_dump->format = IPR_SDT_FMT2;
	sdt_start_addr = mailbox;

	dev_err(&ioa_cfg->pdev->dev, "Dump of IOA initiated\n");

	ioa_cfg->dump->hdr.eye_catcher = IPR_DUMP_EYE_CATCHER;

	/* Initialize the overall dump header */
	ioa_cfg->dump->hdr.len = sizeof(struct ipr_dump_driver_header);
	ioa_cfg->dump->hdr.num_elems = 1;
	ioa_cfg->dump->hdr.first_entry_offset = sizeof(struct ipr_dump_header);
	ioa_cfg->dump->hdr.status = IPR_DUMP_STATUS_SUCCESS;

	/* IOA location data */
	ioa_cfg->dump->location_entry.hdr.length =
		sizeof(struct ipr_dump_location_entry) -
		sizeof(struct ipr_dump_entry_header);
	ioa_cfg->dump->location_entry.hdr.id = IPR_DUMP_TEXT_ID;

	strcpy(ioa_cfg->dump->location_entry.location, ioa_cfg->pdev->dev.bus_id);

	/* Trace table entry */
	memcpy(ioa_cfg->dump->trace_entry.trace, ioa_cfg->trace, IPR_TRACE_SIZE);

	ioa_cfg->dump->hdr.num_elems++;

	ioa_cfg->dump->trace_entry.hdr.length =
		sizeof(struct ipr_dump_trace_entry) -
		sizeof(struct ipr_dump_entry_header);
	ioa_cfg->dump->trace_entry.hdr.id = IPR_DUMP_TRACE_ID;

	/* IOA Dump entry */
	ioa_cfg->ioa_dump->hdr.length = 0;
	ioa_cfg->ioa_dump->hdr.id = IPR_DUMP_IOA_DUMP_ID;

	/* Update dump_header */
	ioa_cfg->dump->hdr.len += sizeof(struct ipr_dump_entry_header);
	ioa_cfg->dump->hdr.num_elems++;

	buffer = (u32 *)&ioa_cfg->ioa_dump->sdt;

	rc = ipr_get_ldump_data_section(ioa_cfg,
					sdt_start_addr, buffer,
					sizeof(struct ipr_sdt) / sizeof(u32));

	/* First entries in sdt are actually a list of dump addresses and
	 lengths to gather the real dump data.  sdt represents the pointer
	 to the ioa generated dump table.  Dump data will be extracted based
	 on entries in this table */
	sdt = &ioa_cfg->ioa_dump->sdt;

	/* Smart Dump table is ready to use and the first entry is valid */
	if (!rc || (be32_to_cpu(sdt->hdr.state) != IPR_FMT2_SDT_READY_TO_USE)) {
		dev_err(&ioa_cfg->pdev->dev,
			"Dump of IOA failed. Dump table not valid.\n");
		ioa_cfg->dump->hdr.status = IPR_DUMP_STATUS_FAILED;
		ioa_cfg->sdt_state = DUMP_OBTAINED;
		return 0;
	}

	num_entries = be32_to_cpu(sdt->hdr.num_entries);
	num_entries_used = be32_to_cpu(sdt->hdr.num_entries_used);

	spin_unlock_irq(ioa_cfg->host->host_lock);

	for (sdt_entry_index = 0;
	     (sdt_entry_index < num_entries_used) &&
		     (sdt_entry_index < num_entries) &&
		     (sdt_entry_index < IPR_NUM_SDT_ENTRIES) &&
		     (be32_to_cpu(ioa_cfg->ioa_dump->hdr.length) <
		      IPR_MAX_IOA_DUMP_SIZE); sdt_entry_index++) {
		if (sdt->entry[sdt_entry_index].valid_entry) {
			sdt_entry_word = be32_to_cpu(sdt->entry[sdt_entry_index].bar_str_offset);
			start_offset = sdt_entry_word & IPR_FMT2_MBX_ADDR_MASK;
			end_offset = be32_to_cpu(sdt->entry[sdt_entry_index].end_offset);

			if (ipr_sdt_is_fmt2(sdt_entry_word))
				dump_data_buffer = sdt_entry_word;
			else
				dump_data_buffer = 0;

			if (dump_data_buffer != 0) {
				/* Dump_header will be updated after all ioa sdt
				 dump entries have been obtained. */
				/* Copy data from adapter to driver buffers */
				bytes_to_copy = (end_offset - start_offset);
				bytes_copied = ipr_sdt_copy(ioa_cfg, dump_data_buffer,
							    bytes_to_copy);

				/* Update dump_entry_header length */
				ioa_cfg->ioa_dump->hdr.length += bytes_copied;

				if (bytes_copied != bytes_to_copy) {
					dev_err(&ioa_cfg->pdev->dev, "Dump of IOA completed.\n");

					ioa_cfg->dump->hdr.status = IPR_DUMP_STATUS_QUAL_SUCCESS;
					ioa_cfg->dump->hdr.len += ioa_cfg->ioa_dump->hdr.length;
					spin_lock_irq(ioa_cfg->host->host_lock);
					ioa_cfg->sdt_state = DUMP_OBTAINED;
					return 0;
				}
			}
		}
	}

	spin_lock_irq(ioa_cfg->host->host_lock);

	dev_err(&ioa_cfg->pdev->dev, "Dump of IOA completed.\n");

	/* Update dump_header */
	ioa_cfg->dump->hdr.len += ioa_cfg->ioa_dump->hdr.length;

	ioa_cfg->sdt_state = DUMP_OBTAINED;

	LEAVE;

	return 0;
}

/**
 * ipr_free_all_resources - Free all allocated resources for an adapter.
 * @ipr_cmd:	ipr command struct
 *
 * This function frees all allocated resources for the
 * specified adapter.
 *
 * Return value:
 * 	none
 **/
static void ipr_free_all_resources(struct ipr_ioa_cfg *ioa_cfg)
{
	ENTER;

	free_irq(ioa_cfg->pdev->irq, ioa_cfg);

	iounmap((void *) ioa_cfg->hdw_dma_regs);
	release_mem_region(ioa_cfg->hdw_dma_regs_pci,
			   pci_resource_len(ioa_cfg->pdev, 0));

	ipr_free_mem(ioa_cfg);

	scsi_host_put(ioa_cfg->host);

	LEAVE;
}

/**
 * ipr_remove - IOA hot plug remove entry point
 * @pdev:	pci device struct
 *
 * Adapter hot plug remove entry point.
 * 
 * Return value:
 * 	none
 **/
static void ipr_remove(struct pci_dev *pdev)
{
	unsigned long host_lock_flags = 0;
	struct ipr_ioa_cfg *ioa_cfg = pci_get_drvdata(pdev);

	ENTER;

	ioa_cfg->allow_cmds = 0;

	flush_scheduled_work();

	scsi_remove_host(ioa_cfg->host);

	spin_lock_irqsave(ioa_cfg->host->host_lock, host_lock_flags);

	ipr_initiate_ioa_bringdown(ioa_cfg, IPR_SHUTDOWN_NORMAL);

	if (ioa_cfg->in_reset_reload)
		ipr_sleep_on(ioa_cfg, &ioa_cfg->reset_wait_q);

	wake_up_interruptible(&ioa_cfg->sdt_wait_q);

	spin_lock(&ipr_driver_lock);
	list_del(&ioa_cfg->queue);
	if (ioa_cfg->minor_num != IPR_NUM_MINORS)
		clear_bit(ioa_cfg->minor_num, ipr_minors);
	spin_unlock(&ipr_driver_lock);

	spin_unlock_irqrestore(ioa_cfg->host->host_lock, host_lock_flags);

	ipr_free_all_resources(ioa_cfg);

	LEAVE;

	return;
}

/**
 * ipr_exit - Module unload
 *
 * Module unload entry point.
 * 
 * Return value:
 * 	none
 **/
static void __exit ipr_exit(void)
{
	int i;

	ENTER;

	driver_remove_file(&ipr_driver.driver, &driver_attr_version);
	pci_unregister_driver(&ipr_driver);
	for (i = 0; i < ARRAY_SIZE(ipr_ioctls); i++)
		unregister_ioctl32_conversion(ipr_ioctls[i]);
	unregister_chrdev(ipr_major, IPR_NAME);

	LEAVE;
}

module_init(ipr_init);
module_exit(ipr_exit);
