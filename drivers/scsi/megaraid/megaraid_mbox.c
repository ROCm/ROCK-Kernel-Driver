/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: megaraid.c
 * Version	: v2.20.0.rc1 (May 17 2004)
 *
 * Authors:
 * 	Atul Mukker		<Atul.Mukker@lsil.com>
 * 	Sreenivas Bagalkote	<Sreenivas.Bagalkote@lsil.com>
 * 	Manoj Jose		<Manoj.Jose@lsil.com>
 *
 * List of supported controllers
 *
 * OEM	Product Name	NickName	Ser	VID	DID	SSVID	SSID
 * ---	------------	-------		---	---	---	----	----
 * Dell	PERC3/Di 	Discovery	N/A	1028	000E	1028	0123
 * Dell	PERC4/SC			520	1000	1960	1028	0520
 * Dell	PERC4/DC			518	1000	1960	1028	0518
 * Dell	PERC4/QC			531	1000	0407	1028	0531
 * Dell	PERC4/Di	Everglades	N/A	1028	000F	1028	014A
 * Dell	PERC 4e/Si	Big Bend	N/A	1028	0013	1028	016c
 * Dell	PERC 4e/Di	Kobuk		N/A	1028	0013	1028	016d
 * Dell	PERC 4e/Di	Corvette	N/A	1028	0013	1028	016e
 * Dell	PERC 4e/Di	Expedition	N/A	1028	0013	1028	016f
 * Dell	PERC 4e/Di	Guadalupe	N/A	1028	0013	1028	0170
 * Dell	PERC 4e/DC	320-2E		N/A	1000	0408	1028	0002
 * Dell	PERC 4e/SC	320-1E		N/A	1000	0408	1028	0001
 *
 *
 * LSI	MegaRAID SCSI 320-2XR		EP033	1000	0040	1000	0033
 * 	Baracuda
 * LSI	MegaRAID SCSI 320-2XRWS		EP066	1000	0040	1000	0066
 * 	Low Profile
 * LSI	MegaRAID SCSI 320-1XR
 * 	Trinidad
 * LSI	MegaRAID SCSI 320-0		520-0	1000	1960	1000	A520
 * LSI	MegaRAID SCSI 320-1		520	1000	1960	1000	0520
 * LSI	MegaRAID SCSI 320-2		518	1000	1960	1000	0518
 * LSI	MegaRAID SCSI 320-0X	ZCR	EP055	1000	0407	1000	0530
 * LSI	MegaRAID SCSI 320-2X		532	1000	0407	1000	0532
 * LSI	MegaRAID SCSI 320-4X		531	1000	0407	1000	0531
 *
 *
 * LSI	MegaRAID SCSI 320-1E	Aruba	N/A	1000	0408	1000	0001
 * LSI	MegaRAID SCSI 320-2E	Cayman	EP078	1000	0408	1000	0002
 * LSI	MegaRAID SCSI 320-4E	Bermuda
 *
 *
 * LSI	MegaRAID SATA 150-2		534	1095	3112	1000	0534
 * LSI	MegaRAID SATA 150-4		523	1000	1960	1000	4523
 * LSI	MegaRAID SATA 150-6		523	1000	1960	1000	0523
 * LSI	MegaRAID SATA 150-4X			1000	0409	1000	1504
 * LSI	MegaRAID SATA 150-8X			1000	0409	1000	1508
 *
 * For history of changes, see changelog.megaraid
 */

#include "megaraid_mbox.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static int megaraid_detect(Scsi_Host_Template *);
static int megaraid_release(struct Scsi_Host *);
static int megaraid_shutdown_notify(struct notifier_block *, ulong, void*);
#endif

static int megaraid_init(void);
static void megaraid_exit(void);

static int megaraid_probe_one(struct pci_dev*, const struct pci_device_id*);
static void megaraid_detach_one(struct pci_dev *);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void megaraid_mbox_shutdown(struct device *);
#endif

static status_t megaraid_io_attach(adapter_t *);
static void megaraid_io_detach(adapter_t *);

static status_t megaraid_alloc_cmd_packets(adapter_t *);
static void megaraid_free_cmd_packets(adapter_t *);

static status_t megaraid_init_mbox(adapter_t *);
static void megaraid_fini_mbox(adapter_t *);

static int megaraid_abort_handler(struct scsi_cmnd *);
static int megaraid_reset_handler(struct scsi_cmnd *);
static int mbox_post_sync_cmd(adapter_t *, uint8_t []);
static int mbox_post_sync_cmd_fast(adapter_t *, uint8_t []);

static status_t megaraid_mbox_product_info(adapter_t *);
static status_t megaraid_mbox_extended_cdb(adapter_t *);
static int megaraid_mbox_support_random_del(adapter_t *);
static status_t megaraid_mbox_support_ha(adapter_t *, uint16_t *);
static int megaraid_mbox_get_max_sg(adapter_t *);
static void megaraid_mbox_enum_raid_scsi(adapter_t *);
static void megaraid_mbox_flush_cache(adapter_t *);

static void megaraid_mbox_display_scb(adapter_t *, scb_t *, int);

static int megaraid_queue_command(struct scsi_cmnd *,
		void (*)(struct scsi_cmnd *));

static const char *megaraid_info(struct Scsi_Host *);

static inline scb_t *megaraid_mbox_build_cmd(adapter_t *, Scsi_Cmnd *, int *);
static inline scb_t *megaraid_alloc_scb(adapter_t *, struct scsi_cmnd *);
static inline void megaraid_dealloc_scb(adapter_t *, scb_t *);
static inline void megaraid_mbox_prepare_pthru(adapter_t *, scb_t *,
		Scsi_Cmnd *);
static inline void megaraid_mbox_prepare_epthru(adapter_t *, scb_t *,
		Scsi_Cmnd *);
static inline int megaraid_mbox_mksgl(adapter_t *, scb_t *, uint32_t *,
		uint32_t *);

static inline void megaraid_mbox_runpendq(adapter_t *);
static inline status_t mbox_post_cmd(adapter_t *, scb_t *);

static void megaraid_mbox_dpc(unsigned long);
static inline void megaraid_mbox_sync_scb(adapter_t *, scb_t *);

static irqreturn_t megaraid_isr(int, void *, struct pt_regs *);
static inline int megaraid_ack_sequence(adapter_t *);

static inline status_t megaraid_busywait_mbox(mraid_device_t *);
static inline status_t __megaraid_busywait_mbox(mraid_device_t *);

static status_t megaraid_cmm_register(adapter_t *);
static status_t megaraid_cmm_unregister(adapter_t *);
static int megaraid_mbox_mm_cmd(unsigned long, uioc_t*, uint32_t);
static int megaraid_mbox_internal_command(adapter_t *, uioc_t *);
static void megaraid_mbox_internal_done(Scsi_Cmnd *);
static int gather_hbainfo(adapter_t *, mraid_hba_info_t *);
static int wait_till_fw_empty(adapter_t*);



MODULE_AUTHOR("LSI Logic Corporation");
MODULE_DESCRIPTION("LSI Logic MegaRAID unified driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(MEGARAID_VERSION);

/*
 * ### modules parameters for driver ###
 */

/*
 * Set to enable driver to expose unconfigured disk to kernel
 */
static int megaraid_expose_unconf_disks = 0;
module_param(megaraid_expose_unconf_disks, int, 0);
MODULE_PARM_DESC(megaraid_expose_unconf_disks,
	"Set to expose unconfigured disks to kernel (default=0)");

/**
 * driver wait time if the adapter's mailbox is busy
 */
static unsigned int max_mbox_busy_wait = MBOX_BUSY_WAIT;
module_param(max_mbox_busy_wait, int, 0);
MODULE_PARM_DESC(max_mbox_busy_wait,
	"Max wait for mailbox in microseconds if busy (default=10)");

/**
 * number of sectors per IO command
 */
static unsigned int megaraid_max_sectors = MBOX_MAX_SECTORS;
module_param(megaraid_max_sectors, int, 0);
MODULE_PARM_DESC(megaraid_max_sectors,
	"Maximum number of sectors per IO command (default=128)");

/**
 * number of commands supported by the driver
 */
static unsigned int megaraid_driver_cmds = MBOX_MAX_DRIVER_CMDS;
module_param(megaraid_driver_cmds, int, 0);
MODULE_PARM_DESC(megaraid_driver_cmds,
	"Maximum number of commands supported by the driver (default=256)");


/**
 * number of commands per logical unit
 */
static unsigned int megaraid_cmd_per_lun = MBOX_MAX_DRIVER_CMDS;
module_param(megaraid_cmd_per_lun, int, 0);
MODULE_PARM_DESC(megaraid_cmd_per_lun,
	"Maximum number of commands per logical unit (default=256)");


/**
 * Fast driver load option, skip scanning for physical devices during load.
 * This would result in non-disk devices being skipped during driver load
 * time. These can be later added though, using /proc/scsi/scsi
 */
static unsigned int megaraid_fast_load = 0;
module_param(megaraid_fast_load, int, 0);
MODULE_PARM_DESC(megaraid_fast_load,
	"Faster loading of the driver, skips physical devices! (default=0)");


/*
 * ### global data ###
 */
mraid_driver_t mraid_driver_g = {
	.driver_version	= { 0x02, 0x20, 0x00, 0xB3, 5, 12, 20, 4},
};


/*
 * PCI table for all supported controllers.
 */
static struct pci_device_id pci_id_table_g[] =  {
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4_DI_DISCOVERY,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4_DI_DISCOVERY,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_PERC4_SC,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4_SC,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_PERC4_DC,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4_DC,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_PERC4_QC,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4_QC,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4_DI_EVERGLADES,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4_DI_EVERGLADES,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4E_SI_BIGBEND,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4E_SI_BIGBEND,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4E_DI_KOBUK,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4E_DI_KOBUK,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4E_DI_CORVETTE,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4E_DI_CORVETTE,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4E_DI_EXPEDITION,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4E_DI_EXPEDITION,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4E_DI_GUADALUPE,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4E_DI_GUADALUPE,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_PERC4E_DC_320_2E,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4E_DC_320_2E,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_PERC4E_SC_320_1E,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC4E_SC_320_1E,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_AMI,
		.device		= PCI_DEVICE_ID_AMI_MEGARAID3,
		.subvendor	= PCI_VENDOR_ID_DELL,
		.subdevice	= PCI_SUBSYS_ID_PERC3_DC,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SCSI_320_0,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SCSI_320_0,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SCSI_320_1,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SCSI_320_1,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SCSI_320_2,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SCSI_320_2,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SCSI_320_0x,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SCSI_320_0x,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SCSI_320_2x,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SCSI_320_2x,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SCSI_320_4x,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SCSI_320_4x,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SCSI_320_1E,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SCSI_320_1E,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SCSI_320_2E,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SCSI_320_2E,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SATA_150_4,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SATA_150_4,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SATA_150_6,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SATA_150_6,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SATA_300_4x,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SATA_300_4x,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SATA_300_8x,
		.subvendor	= PCI_VENDOR_ID_LSI_LOGIC,
		.subdevice	= PCI_SUBSYS_ID_MEGARAID_SATA_300_8x,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_INTEL_RAID_SRCU42X,
		.subvendor	= PCI_VENDOR_ID_INTEL,
		.subdevice	= PCI_SUBSYS_ID_INTEL_RAID_SRCU42X,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_INTEL_RAID_SRCS16,
		.subvendor	= PCI_VENDOR_ID_INTEL,
		.subdevice	= PCI_SUBSYS_ID_INTEL_RAID_SRCS16,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_INTEL_RAID_SRCU42E,
		.subvendor	= PCI_VENDOR_ID_INTEL,
		.subdevice	= PCI_SUBSYS_ID_INTEL_RAID_SRCU42E,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_INTEL_RAID_SRCZCRX,
		.subvendor	= PCI_VENDOR_ID_INTEL,
		.subdevice	= PCI_SUBSYS_ID_INTEL_RAID_SRCZCRX,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_INTEL_RAID_SRCS28X,
		.subvendor	= PCI_VENDOR_ID_INTEL,
		.subdevice	= PCI_SUBSYS_ID_INTEL_RAID_SRCS28X,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_INTEL_RAID_SROMBU42E_ALIEF,
		.subvendor	= PCI_VENDOR_ID_INTEL,
		.subdevice	= PCI_SUBSYS_ID_INTEL_RAID_SROMBU42E_ALIEF,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_INTEL_RAID_SROMBU42E_HARWICH,
		.subvendor	= PCI_VENDOR_ID_INTEL,
		.subdevice	= PCI_SUBSYS_ID_INTEL_RAID_SROMBU42E_HARWICH,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_FSC_MEGARAID_PCI_EXPRESS_ROMB,
		.subvendor	= PCI_SUBSYS_ID_FSC,
		.subdevice	= PCI_SUBSYS_ID_FSC_MEGARAID_PCI_EXPRESS_ROMB,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, pci_id_table_g);


// TODO: Add the shutdown routine
static struct pci_driver megaraid_pci_driver_g = {
	.name		= "megaraid",
	.id_table	= pci_id_table_g,
	.probe		= megaraid_probe_one,
	.remove		= __devexit_p(megaraid_detach_one),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	.driver		= {
		.shutdown	= megaraid_mbox_shutdown,
	}
#endif
};

/*
 * ### START: LK 2.4 compatibility layer ###
 *
 * This layer only accounts for lk 2.4 and lk 2.6, not for, e.g. lk 2.5
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

/*
 * Scsi host template for megaraid unified driver
 */
#define MRAID_TEMPLATE							\
{									\
	.module				= THIS_MODULE,			\
	.name				= "MegaRAID",			\
	.proc_name			= "megaraid",			\
	.info				= megaraid_info,		\
	.queuecommand			= megaraid_queue_command,	\
	.eh_abort_handler		= megaraid_abort_handler,	\
	.eh_device_reset_handler	= megaraid_reset_handler,	\
	.eh_bus_reset_handler		= megaraid_reset_handler,	\
	.eh_host_reset_handler		= megaraid_reset_handler,	\
	.use_clustering			= ENABLE_CLUSTERING,		\
}

#else	// lk 2.4

#define MRAID_TEMPLATE							\
{									\
	.name				= "MegaRAID",			\
	.proc_name			= "megaraid",			\
	.info				= megaraid_info,		\
	.queuecommand			= megaraid_queue_command,	\
	.eh_abort_handler		= megaraid_abort_handler,	\
	.eh_device_reset_handler	= megaraid_reset_handler,	\
	.eh_bus_reset_handler		= megaraid_reset_handler,	\
	.eh_host_reset_handler		= megaraid_reset_handler,	\
	.use_clustering			= ENABLE_CLUSTERING,		\
	.use_new_eh_code		= 1,				\
	.highmem_io			= 1,				\
	.vary_io			= 1,				\
	.detect				= megaraid_detect,		\
	.release			= megaraid_release,		\
}

static struct notifier_block megaraid_shutdown_notifier = {
	.notifier_call = megaraid_shutdown_notify
};
#endif

static Scsi_Host_Template megaraid_template_g = MRAID_TEMPLATE;
static Scsi_Host_Template *megaraid_template_gp = &megaraid_template_g;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
/**
 * megaraid_detect - old-style detect
 * @param host_template	: megaraid host template
 *
 * This is called with the old-sytle driver load interface.  We simply call
 * the new-style driver load entry point and return number of active
 * controllers to mid-layer.
 * We will need to override the location of the template since the old-style
 * drivers rely of "driver_template" to be defined by the drivers.
 */
static int __init
megaraid_detect(Scsi_Host_Template *host_template)
{
	// override the template for old-style interface
	megaraid_template_gp = host_template;

	megaraid_init();	// module entry point for new drivers

	/*
	 * Register the Shutdown Notification hook in kernel
	 */
	if (register_reboot_notifier(&megaraid_shutdown_notifier)) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: shutdown notification not registered.\n"));
	}

	return mraid_driver_g.attach_count;
}


/**
 * megaraid_release - old-style release
 * @param host	: host to be released
 *
 * This is called with the old-sytle driver unload interface. We call the
 * corresponding detach routine. When the attach counter goes down to 0, call
 * the new-style module exit point. While calling this exit routine, we rely
 * on the fact that the mraid_device_t device list has been initialized to
 * NULL and traversing this list in the new-style exit point will have no
 * effect.
 */
static int
megaraid_release(struct Scsi_Host *host)
{
	adapter_t		*adapter;

	con_log(CL_DLEVEL1, (KERN_NOTICE "megaraid: release.\n"));

	adapter = (adapter_t *)SCSIHOST2ADAP(host);

	megaraid_detach_one(adapter->pdev);

	if (mraid_driver_g.attach_count == 0) {
		unregister_reboot_notifier(&megaraid_shutdown_notifier);
		megaraid_exit();
	}

	return 0;
}


/**
 * megaraid_shutdown_notify - shutdown notification hook
 * @param this		: unused
 * @param code		: shutdown code
 * @param unused	: unused
 *
 * This routine will be called when the use has done a forced shutdown on the
 * system. Invoke the appropriate LLD's shutdown notification entry point
 */
static int
megaraid_shutdown_notify (struct notifier_block *this, unsigned long code,
		void *unused)
{
	adapter_t		*adapter;
	struct list_head	*pos, *next;
	int			i;

	/*
	 * Invoke the LLD's shutdown notification hook for all possible
	 * shutdown codes: SYS_DOWN, SYS_HALT, SYS_RESTART, SYS_POWER_OFF
	 */
	list_for_each_safe(pos, next, &mraid_driver_g.device_list) {

		adapter = list_entry(pos, adapter_t, list);

		// FIXME: Add shutdown notification
	}

	/*
	 * Have a delibrate delay to make sure all the caches are actually
	 * flushed.
	 */
	con_log(CL_ANN, (KERN_INFO "megaraid: cache flush delay:   "));
	for (i = 9; i >= 0; i--) {
		con_log(CL_ANN, ("\b\b\b[%d]", i));
		mdelay(1000);
	}
	con_log(CL_ANN, ("\b\b\b[done]\n"));
	mdelay(1000);

	return NOTIFY_OK;
}
#endif

/**
 * megaraid_init - module load hook
 *
 * We register ourselves as hotplug enabled module and let PCI subsystem
 * discover our adaters
 **/
static int __init
megaraid_init(void)
{
	int			rval;

	// Announce the driver version
	con_log(CL_ANN, (KERN_INFO "megaraid: %s %s\n", MEGARAID_VERSION,
		MEGARAID_EXT_VERSION));

	// check validity of module parameters
	if (megaraid_driver_cmds > MBOX_MAX_DRIVER_CMDS) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid mailbox: max commands reset to %d\n",
			MBOX_MAX_DRIVER_CMDS));

		megaraid_driver_cmds = MBOX_MAX_DRIVER_CMDS;
	}

	if (megaraid_cmd_per_lun > MBOX_MAX_DRIVER_CMDS) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid mailbox: max commands per lun reset to %d\n",
			MBOX_MAX_DRIVER_CMDS));

		megaraid_cmd_per_lun = MBOX_MAX_DRIVER_CMDS;
	}


	/*
	 * Setup the driver global data structures
	 */
	INIT_LIST_HEAD(&mraid_driver_g.device_list);

	// register as a PCI hot-plug driver module
	if ((rval = pci_module_init(&megaraid_pci_driver_g))) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: could not register hotplug support.\n"));
	}

	return rval;
}


/**
 * megaraid_exit - driver unload entry point
 *
 * We simply unwrap the megaraid_init routine here
 */
static void __exit
megaraid_exit(void)
{
	con_log(CL_DLEVEL1, (KERN_NOTICE "megaraid: unloading framework\n"));

	// unregister as PCI hotplug driver
	pci_unregister_driver(&megaraid_pci_driver_g);

	// All adapters must be detached by now
	ASSERT(list_empty(&mraid_driver_g.device_list));

	return;
}


/**
 * megaraid_probe_one - PCI hotplug entry point
 * @param pdev	: handle to this controller's PCI configuration space
 * @param id	: pci device id of the class of controllers
 *
 * This routine should be called whenever a new adapter is detected by the
 * PCI hotplug susbsytem.
 **/
static int __devinit
megaraid_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	adapter_t		*adapter;
	uint32_t		unique_id;
	struct list_head	*next;
	int			alloc_adapter_f	= 0;
	int			init_mbox_f	= 0;


	// Make sure this adapter is not already setup
	unique_id	= pdev->bus->number << 8 | pdev->devfn;

	list_for_each(next, &mraid_driver_g.device_list) {
		adapter = list_entry(next, adapter_t, list);

		if (adapter->unique_id == unique_id) {

			con_log(CL_ANN, (KERN_WARNING
				"megaraid: reject re-init request for dev: "));

			con_log(CL_ANN, ("%#4.04x:%#4.04x:%#4.04x:%#4.04x:",
				pdev->vendor, pdev->device,
				pdev->subsystem_vendor,
				pdev->subsystem_device));

			con_log(CL_ANN, ("bus %d:slot %d:func %d\n",
				pdev->bus->number, PCI_SLOT(pdev->devfn),
				PCI_FUNC(pdev->devfn)));

			return -ENODEV;	// already initialized
		}
	}


	// detected a new controller
	con_log(CL_ANN, (KERN_INFO
		"megaraid: probe new device %#4.04x:%#4.04x:%#4.04x:%#4.04x: ",
		pdev->vendor, pdev->device, pdev->subsystem_vendor,
		pdev->subsystem_device));

	con_log(CL_ANN, ("bus %d:slot %d:func %d\n", pdev->bus->number,
		PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn)));

	if (pci_enable_device(pdev)) {
		con_log(CL_ANN, (KERN_WARNING
				"megaraid: pci_enable_device failed\n"));
		return -ENODEV;
	}

	// Enable bus-mastering on this controller
	pci_set_master(pdev);

	// Allocate the per driver initialization structure
	adapter = kmalloc(sizeof(adapter_t), GFP_KERNEL);

	if (adapter == NULL) {
		con_log(CL_ANN, (KERN_WARNING
		"megaraid: out of memory, %s %d.\n", __FUNCTION__, __LINE__));

		goto fail_probe;
	}
	alloc_adapter_f = 1;
	memset(adapter, 0, sizeof(adapter_t));


	// set up PCI related soft state and other pre-known parameters
	adapter->unique_id	= unique_id;
	adapter->irq		= pdev->irq;
	adapter->pci_id		= id;
	adapter->pdev		= pdev;

	atomic_set(&adapter->being_detached, 0);

	// Setup the default DMA mask. This would be changed later on
	// depending on hardware capabilities
	if (pci_set_dma_mask(adapter->pdev, 0xFFFFFFFF) != 0) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: pci_set_dma_mask failed:%d\n", __LINE__));

		goto fail_probe;
	}


	// Initialize the synchronization lock for kernel and LLD
	spin_lock_init(&adapter->lock);
	adapter->host_lock = &adapter->lock;


	// Setup resources to issue internal commands with interrupts availble
	init_MUTEX(&adapter->imtx);


	// Initialize the command queues: the list of free SCBs and the list
	// of pending SCBs.
	INIT_LIST_HEAD(&adapter->scb_pool);
	spin_lock_init(&adapter->scb_pool_lock);

	INIT_LIST_HEAD(&adapter->pend_list);
	spin_lock_init(&adapter->pend_list_lock);

	INIT_LIST_HEAD(&adapter->completed_list);
	spin_lock_init(&adapter->completed_list_lock);


	// Start the mailbox based controller
	if (megaraid_init_mbox(adapter) != MRAID_SUCCESS) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: maibox adapter did not initialize\n"));

		goto fail_probe;
	}
	init_mbox_f = 1;

	// attach with scsi mid-layer
	if (megaraid_io_attach(adapter) != MRAID_SUCCESS) {
		con_log(CL_ANN, (KERN_WARNING "megaraid: io attach failed\n"));
		goto fail_probe;
	}

	/*
	 * Register with LSI Common Management Module
	 */
	if (megaraid_cmm_register(adapter) != MRAID_SUCCESS) {
		con_log(CL_ANN, (KERN_WARNING
		"megaraid: could not register with management module\n"));
		// continue loading
	}

	/*
	 * successfully attached this driver. Increment the device counter and
	 * display interesting facts about the driver.
	 */
	mraid_driver_g.attach_count++;

	// setup adapter handle in PCI soft state
	pci_set_drvdata(pdev, adapter);

	// put the adapter in the global list of our controllers
	list_add_tail(&adapter->list, &mraid_driver_g.device_list);

	con_log(CL_ANN, (KERN_NOTICE
		"megaraid: fw version:[%s] bios version:[%s]\n",
		adapter->fw_version, adapter->bios_version));

	return 0;

fail_probe:
	if (init_mbox_f) {
		megaraid_fini_mbox(adapter);
	}

	if (alloc_adapter_f) {
		kfree(adapter);
	}

	pci_disable_device(pdev);

	return -ENODEV;
}


/**
 * megaraid_detach_one - release the framework resources and call LLD release
 * routine
 * @param pdev	: handle for our PCI cofiguration space
 *
 * This routine is called during driver unload. We free all the allocated
 * resources and call the corresponding LLD so that it can also release all
 * its resources.
 *
 * This routine is also called from the PCI hotplug system
 **/
static void
megaraid_detach_one(struct pci_dev *pdev)
{
	adapter_t	*adapter;


	// Start a rollback on this adapter
	adapter = pci_get_drvdata(pdev);


	// Make sure we don't break for multiple detach(?) for a controller
	if (!adapter) {
		con_log(CL_DLEVEL1, (KERN_CRIT
		"megaraid: Invalid detach on %#4.04x:%#4.04x:%#4.04x:%#4.04x\n",
			pdev->vendor, pdev->device, pdev->subsystem_vendor,
			pdev->subsystem_device));

		return;
	}
	else {
		con_log(CL_ANN, (KERN_NOTICE
		"megaraid: detaching device %#4.04x:%#4.04x:%#4.04x:%#4.04x\n",
			pdev->vendor, pdev->device, pdev->subsystem_vendor,
			pdev->subsystem_device));
	}


	// do not allow any more requests from the management module for this
	// adapter.
	// FIXME: How do we account for the request which might still be
	// pending with us?
	atomic_set(&adapter->being_detached, 1);

	// remove the adapter from the global list of our controllers
	list_del_init(&adapter->list);

	// reset the device state in the PCI structure. We check this
	// condition when we enter here. If the device state is NULL,
	// that would mean the device has already been removed
	pci_set_drvdata(pdev, NULL);

	mraid_driver_g.attach_count--;

	/*
	 * Unregister from common management module
	 *
	 * FIXME: this must return success or failure for conditions if there
	 * is a command pending with LLD or not.
	 */
	megaraid_cmm_unregister(adapter);

	megaraid_io_detach(adapter);

	// finalize the mailbox based controller
	megaraid_fini_mbox(adapter);

	kfree(adapter);

	pci_disable_device(pdev);

	return;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
/**
 * megaraid_mbox_shutdown - PCI shutdown for megaraid HBA
 * @param device	: generice driver model device
 *
 * Find out if this device is still attached. If found, perform flush cache
 */
static void
megaraid_mbox_shutdown(struct device *device)
{
	adapter_t		*adapter = pci_get_drvdata(to_pci_dev(device));
	adapter_t		*ta;
	struct list_head	*next;
	static int		counter;

	if (!adapter) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: null device in shutdown\n"));
		return;
	}

	// make sure this adapter is still attached
	ta = NULL;
	list_for_each(next, &mraid_driver_g.device_list) {
		ta = list_entry(next, adapter_t, list);
		if (ta == adapter) break;
		ta = NULL;
	}

	if (!ta) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: invalid device in shutdown\n"));
		return;
	}

	// flush caches now
	con_log(CL_ANN, (KERN_INFO "megaraid: flushing adapter %d...",
		counter++));

	megaraid_mbox_flush_cache(adapter);

	con_log(CL_ANN, ("done\n"));
}
#endif


/**
 * megaraid_io_attach - attach a device with the IO subsystem
 * @param adapter	: controller's soft state
 *
 * Attach this device with the IO subsystem
 **/
static status_t
megaraid_io_attach(adapter_t *adapter)
{
	struct Scsi_Host	*host;

	// Initialize SCSI Host structure
	host = scsi_host_alloc(megaraid_template_gp, 8);
	if (!host) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: scsi_register failed.\n"));

		return MRAID_FAILURE;
	}

	SCSIHOST2ADAP(host)	= (caddr_t )adapter;
	adapter->host		= host;

	// export the parameters required by the mid-layer
	scsi_assign_lock(host, adapter->host_lock);
	scsi_set_device(host, &adapter->pdev->dev);

	host->irq		= adapter->irq;
	host->unique_id		= adapter->unique_id;
	host->can_queue		= adapter->max_cmds;
	host->this_id		= adapter->init_id;
	host->sg_tablesize	= adapter->sglen;
	host->max_sectors	= adapter->max_sectors;
	host->cmd_per_lun	= adapter->cmd_per_lun;
	host->max_channel	= adapter->max_channel;
	host->max_id		= adapter->max_target;
	host->max_lun		= adapter->max_lun;


	// notify mid-layer about the new controller
	if (scsi_add_host(host, &adapter->pdev->dev)) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: scsi_add_host failed.\n"));

		scsi_host_put(host);

		return MRAID_FAILURE;
	}

	scsi_scan_host(host);

	return MRAID_SUCCESS;
}


/**
 * megaraid_io_detach - detach a device from the IO subsystem
 * @param adapter	: controller's soft state
 *
 * Detach this device from the IO subsystem
 **/
static void
megaraid_io_detach(adapter_t *adapter)
{
	struct Scsi_Host	*host;

	con_log(CL_DLEVEL1, (KERN_INFO "megaraid: io detach\n"));

	host = adapter->host;

	scsi_remove_host(host);

	scsi_host_put(host);

	return;
}


/**
 * megaraid_info - information string about the driver
 * @host	: megaraid host
 *
 * returns a descriptive string about the driver
 **/
static const char *
megaraid_info(struct Scsi_Host *host)
{
	return "LSI Logic Corporation MegaRAID driver";
}


/*
 * START: Mailbox Low Level Driver
 *
 * This is section specific to the single mailbox based controllers
 */

/**
 * megaraid_init_mbox - initialize controller
 * @param adapter	- our soft state
 *
 * . Allocate 16-byte aligned mailbox memory for firmware handshake
 * . Allocate controller's memory resources
 * . Find out all initialization data
 * . Allocate memory required for all the commands
 * . Use internal library of FW routines, build up complete soft state
 */
static status_t __init
megaraid_init_mbox(adapter_t *adapter)
{
	struct pci_dev		*pdev;
	mraid_device_t		*raid_dev;
	bool_t			mem_region_f	= MRAID_FALSE;
	bool_t			alloc_cmds_f	= MRAID_FALSE;
	bool_t			irq_f		= MRAID_FALSE;
	int			i;


	adapter->quiescent	= MRAID_FALSE;
	adapter->ito		= MBOX_TIMEOUT;
	pdev			= adapter->pdev;

	/*
	 * Allocate and initialize the init data structure for mailbox
	 * controllers
	 */
	raid_dev = kmalloc(sizeof(mraid_device_t), GFP_KERNEL);
	if (raid_dev == NULL) return MRAID_FAILURE;

	memset(raid_dev, 0, sizeof(mraid_device_t));

	/*
	 * Attach the adapter soft state is raid device soft state
	 */
	adapter->raid_device	= (caddr_t)raid_dev;
	raid_dev->fast_load	= megaraid_fast_load;


	// our baseport
	raid_dev->baseport = pci_resource_start(pdev, 0);

	if (pci_request_regions(pdev, "MegaRAID: LSI Logic Corporation") != 0) {

		con_log(CL_ANN, (KERN_WARNING
				"megaraid: mem region busy\n"));

		goto fail_init;
	}

	raid_dev->baseaddr = (unsigned long)
			ioremap_nocache(raid_dev->baseport, 128);

	if (!raid_dev->baseaddr) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: could not map hba memory\n") );

		pci_release_regions(pdev);

		goto fail_init;
	}
	mem_region_f = MRAID_TRUE;

	//
	// Setup the rest of the soft state using the library of FW routines
	//

	// request IRQ and register the interrupt service routine
	if (request_irq(adapter->irq, megaraid_isr, SA_SHIRQ, "megaraid",
		adapter)) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: Couldn't register IRQ %d!\n", adapter->irq));

		goto fail_init;
	}

	irq_f = MRAID_TRUE;


	// initialize the mutual exclusion lock for the mailbox
	spin_lock_init(&raid_dev->mailbox_lock);

	// allocate memory required for commands
	if (megaraid_alloc_cmd_packets(adapter) != MRAID_SUCCESS) {
		goto fail_init;
	}
	alloc_cmds_f = MRAID_TRUE;

	// Product info
	if (megaraid_mbox_product_info(adapter) != MRAID_SUCCESS) {
		goto fail_init;
	}

	// Do we support extended CDBs
	adapter->max_cdb_sz = 10;
	if (megaraid_mbox_extended_cdb(adapter) == MRAID_SUCCESS) {
		adapter->max_cdb_sz = 16;
	}

	/*
	 * Do we support cluster environment, if we do, what is the initiator
	 * id.
	 * NOTE: In a non-cluster aware firmware environment, the LLD should
	 * return 7 as initiator id.
	 */
	adapter->ha		= MRAID_FALSE;
	adapter->init_id	= -1;
	if (megaraid_mbox_support_ha(adapter, &adapter->init_id) ==
			MRAID_SUCCESS) {
		adapter->ha = MRAID_TRUE;
	}

	/*
	 * Prepare the device ids array to have the mapping between the kernel
	 * device address and megaraid device address.
	 * We export the physical devices on their actual addresses. The
	 * logical drives are exported on a virtual SCSI channel
	 */
	adapter->virtual_ch = adapter->max_channel;

	mraid_setup_device_map(adapter);

	// If the firmware supports random deletion, update the device id map
	if (megaraid_mbox_support_random_del(adapter)) {

		// Change the logical drives numbers in device_ids array one
		// slot in device_ids is reserved for target id, that's why
		// "<=" below
		for (i = 0; i <= MAX_LOGICAL_DRIVES_40LD; i++) {
			adapter->device_ids[adapter->virtual_ch][i] += 0x80;
		}
		adapter->device_ids[adapter->virtual_ch][adapter->init_id] =
			0xFF;
	}

	/*
	 * find out the maximum number of scatter-gather elements supported by
	 * this firmware
	 */
	adapter->sglen = megaraid_mbox_get_max_sg(adapter);

	// enumerate RAID and SCSI channels so that all devices on SCSI
	// channels can later be exported, including disk devices
	megaraid_mbox_enum_raid_scsi(adapter);

	/*
	 * Other parameters required by upper layer
	 *
	 * maximum number of sectors per IO command
	 */
	adapter->max_sectors = megaraid_max_sectors;

	/*
	 * number of queued commands per LUN.
	 */
	adapter->cmd_per_lun = megaraid_cmd_per_lun;

	// Set the DMA mask to 64-bit. All supported controllers as capable of
	// DMA in this range
	if (pci_set_dma_mask(adapter->pdev, 0xFFFFFFFFFFFFFFFFULL) != 0) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: could not set DMA mask for 64-bit.\n"));

		goto fail_init;
	}

	// setup tasklet for DPC
	tasklet_init(&adapter->dpc_h, megaraid_mbox_dpc,
			(unsigned long)adapter);

	con_log(CL_DLEVEL1, (KERN_INFO
		"megaraid mbox hba successfully initialized\n"));

	return MRAID_SUCCESS;

fail_init:
	if (alloc_cmds_f) {
		megaraid_free_cmd_packets(adapter);
	}
	if (irq_f == MRAID_TRUE) {
		free_irq(adapter->irq, adapter);
	}
	if (mem_region_f) {
		iounmap((caddr_t)raid_dev->baseaddr);
		pci_release_regions(adapter->pdev);
	}

	kfree(raid_dev);

	return MRAID_FAILURE;
}


/**
 * megaraid_fini_mbox - undo controller initialization
 * @param adapter	- our soft state
 */
static void
megaraid_fini_mbox(adapter_t *adapter)
{
	mraid_device_t *raid_dev = ADAP2RAIDDEV(adapter);

	// flush all caches
	megaraid_mbox_flush_cache(adapter);

	megaraid_free_cmd_packets(adapter);

	free_irq(adapter->irq, adapter);

	iounmap((caddr_t)raid_dev->baseaddr);

	pci_release_regions(adapter->pdev);

	kfree(raid_dev);

	return;
}


/**
 * megaraid_alloc_cmd_packets - allocate shared mailbox
 * @param adapter	: soft state of the raid controller
 *
 * Allocate and align the shared mailbox. This maibox is used to issue
 * all the commands. For IO based controllers, the mailbox is also regsitered
 * with the FW. Allocate memory for all commands as well.
 * This is our big allocator
 */
static status_t
megaraid_alloc_cmd_packets(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	struct pci_dev		*pdev;
	unsigned long		align;
	scb_t			*scb;
	mbox_ccb_t		*ccb;
	mbox_t			*una_mbox;
	dma_addr_t		una_mbox_dma_h;
	struct mraid_pci_blk	*epthru_pci_blk;
	struct mraid_pci_blk	*sg_pci_blk;
	struct mraid_pci_blk	*mbox_pci_blk;
	bool_t			alloc_ibuf_f 		= MRAID_FALSE;
	bool_t			alloc_common_mbox_f 	= MRAID_FALSE;
	bool_t			alloc_int_ccb_f 	= MRAID_FALSE;
	bool_t			alloc_int_ccb_pthru_f	= MRAID_FALSE;
	bool_t			alloc_scb_f		= MRAID_FALSE;
	bool_t			alloc_mbox_f		= MRAID_FALSE;
	bool_t			alloc_epthru_f		= MRAID_FALSE;
	bool_t			alloc_sg_pool_f		= MRAID_FALSE;
	int			i;

	pdev = adapter->pdev;

	/*
	 * Setup the mailbox
	 * Allocate the common 16-byte aligned memory for the handshake
	 * mailbox.
	 */
	raid_dev->una_mbox64 = pci_alloc_consistent(adapter->pdev,
			sizeof(mbox64_t), &raid_dev->una_mbox64_dma);

	if (!raid_dev->una_mbox64) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		return MRAID_FAILURE;
	}
	memset(raid_dev->una_mbox64, 0, sizeof(mbox64_t));
	alloc_common_mbox_f = MRAID_TRUE;

	/*
	 * Align the mailbox at 16-byte boundary
	 */
	raid_dev->mbox	= &raid_dev->una_mbox64->mbox32;

	raid_dev->mbox	= (mbox_t *)((((unsigned long)raid_dev->mbox) + 15) &
				(~0UL ^ 0xFUL));

	raid_dev->mbox64 = (mbox64_t *)(((unsigned long)raid_dev->mbox) - 8);

	align = ((void *)raid_dev->mbox -
			((void *)&raid_dev->una_mbox64->mbox32));

	raid_dev->mbox_dma = (unsigned long)raid_dev->una_mbox64_dma + 8 +
			align;

	/*
	 * Allocate memory for commands issued internally, through ioctl and
	 * /proc
	 */
	adapter->ibuf = pci_alloc_consistent(pdev, MBOX_IBUF_SIZE,
				&adapter->ibuf_dma_h);
	if (!adapter->ibuf) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));

		goto fail_alloc_cmds;
	}
	alloc_ibuf_f = MRAID_TRUE;
	memset(adapter->ibuf, 0, MBOX_IBUF_SIZE);

	/*
	 * Attach mailbox drivers specific data structure with the scb for
	 * internal commands.
	 * NOTE: for internal command, we use the direct mailbox commands and
	 * passthru command without scatter-gather list. Therefore, memory
	 * allocation for scatter-gather list is not required. The data
	 * transfer address would be the internal buffer and it's dma address.
	 */
	ccb = kmalloc(sizeof(mbox_ccb_t), GFP_KERNEL);
	if (ccb == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));

		goto fail_alloc_cmds;
	}
	memset(ccb, 0, sizeof(mbox_ccb_t));
	alloc_int_ccb_f = MRAID_TRUE;


	/*
	 * Allocate passthru structure for internal commands.
	 * NOTE: for internal commands, we never use extended and 64-bit
	 * passthru mailbox commands.
	 *
	 * HACK: piggyback the mailbox on the 'extra' memory allocated below
	 * so that we do not need to track additional memory handles for
	 * mailbox for internal commands. Put a buffer of about 1k bytes
	 * between these two structures, just to separate them.
	 */
	ccb->pthru = pci_alloc_consistent(adapter->pdev, 2048,
		&ccb->pthru_dma_h);

	if (ccb->pthru == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));

		goto fail_alloc_cmds;
	}
	memset(ccb->pthru, 0, sizeof(mraid_passthru_t));
	alloc_int_ccb_pthru_f = MRAID_TRUE;

	una_mbox	= (mbox_t *)((unsigned long)ccb->pthru + 1024);
	una_mbox_dma_h	= (dma_addr_t)((unsigned long)ccb->pthru_dma_h + 1024);

	ccb->mbox	= (mbox_t *)((((unsigned long)una_mbox) + 15) &
				(~0UL ^ 0xFUL));

	align		= ((caddr_t)ccb->mbox) - ((caddr_t)una_mbox);
	ccb->mbox_dma_h	= (dma_addr_t)((unsigned long)una_mbox_dma_h + align);
	ccb->mbox64	= (mbox64_t *)((unsigned long)ccb->mbox - 8);
	ccb->raw_mbox	= (uint8_t *)ccb->mbox;

	// Attach with framework
	adapter->iscb.ccb = (caddr_t)ccb;


	// Allocate memory for our SCSI Command Blocks and their associated
	// memory

	/*
	 * Allocate memory for the base list of scb. Later allocate memory for
	 * CCBs and embedded components of each CCB and point the pointers in
	 * scb to the allocated components
	 * NOTE: The code to allocate SCB will be duplicated in all the LLD
	 * since the calling routine does not yet know the number of available
	 * commands.
	 */
	adapter->scb_list = kmalloc(sizeof(scb_t) * megaraid_driver_cmds,
			GFP_KERNEL);

	if (adapter->scb_list == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		goto fail_alloc_cmds;
	}
	alloc_scb_f = MRAID_TRUE;
	memset(adapter->scb_list, 0, sizeof(scb_t) * megaraid_driver_cmds);

	// Allocate memory for 16-bytes aligned mailboxes
	raid_dev->mbox_pool_handle = mraid_pci_blk_pool_create(adapter->pdev,
			megaraid_driver_cmds, sizeof(mbox64_t) + 16, 16,
			raid_dev->mbox_pool);

	if (raid_dev->mbox_pool_handle == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		goto fail_alloc_cmds;
	}
	alloc_mbox_f = MRAID_TRUE;

	/*
	 * Allocate memory for each embedded passthru strucuture pointer
	 * Request for a 128 bytes aligned structure for each passthru command
	 * structure
	 * Since passthru and extended passthru commands are exclusive, they
	 * share common memory pool. Passthru structures piggyback on memory
	 * allocted to extended passthru since passthru is smaller of the two
	 */
	raid_dev->epthru_pool_handle = mraid_pci_blk_pool_create(adapter->pdev,
			megaraid_driver_cmds, sizeof(mraid_epassthru_t), 128,
			raid_dev->epthru_pool);

	if (raid_dev->epthru_pool_handle == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		goto fail_alloc_cmds;
	}
	alloc_epthru_f = MRAID_TRUE;

	// Allocate memory for each scatter-gather list. Request for 512 bytes
	// alignment for each sg list
	raid_dev->sg_pool_handle = mraid_pci_blk_pool_create(adapter->pdev,
		megaraid_driver_cmds, sizeof(mbox_sgl64) * MBOX_MAX_SG_SIZE,
		512, raid_dev->sg_pool);

	if (raid_dev->sg_pool_handle == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		goto fail_alloc_cmds;
	}
	alloc_sg_pool_f = MRAID_TRUE;

	// Adjust the scb pointers and link in the free pool
	epthru_pci_blk	= raid_dev->epthru_pool;
	sg_pci_blk	= raid_dev->sg_pool;
	mbox_pci_blk	= raid_dev->mbox_pool;

	for (i = 0; i < megaraid_driver_cmds; i++) {
		scb			= adapter->scb_list + i;
		ccb			= raid_dev->ccb_list + i;

		ccb->mbox	= (mbox_t *)(mbox_pci_blk[i].vaddr + 16);
		ccb->raw_mbox	= (uint8_t *)ccb->mbox;
		ccb->mbox64	= (mbox64_t *)(mbox_pci_blk[i].vaddr + 8);
		ccb->mbox_dma_h	= mbox_pci_blk[i].dma_addr + 16;

		// make sure the mailbox is aligned properly
		if (ccb->mbox_dma_h & 0x0F) {
			con_log(CL_ANN, (KERN_CRIT
				"megaraid mbox: not aligned on 16-bytes\n"));

			goto fail_alloc_cmds;
		}

		ccb->epthru	= (mraid_epassthru_t *)epthru_pci_blk[i].vaddr;
		ccb->epthru_dma_h	= epthru_pci_blk[i].dma_addr;
		ccb->pthru		= (mraid_passthru_t *)ccb->epthru;
		ccb->pthru_dma_h	= ccb->epthru_dma_h;


		ccb->sgl64		= (mbox_sgl64 *)sg_pci_blk[i].vaddr;
		ccb->sgl_dma_h		= sg_pci_blk[i].dma_addr;
		ccb->sgl32		= (mbox_sgl32 *)ccb->sgl64;

		scb->ccb		= (caddr_t)ccb;
		scb->gp			= 0;

		// cmdid will be allocated later
		scb->sno		= -1;

		scb->scp		= NULL;
		scb->state		= SCB_FREE;
		scb->dma_direction	= PCI_DMA_NONE;
		scb->dma_type		= MRAID_DMA_NONE;
		scb->dev_channel	= -1;
		scb->dev_target		= -1;

		// put scb in the free pool
		list_add(&scb->list, &adapter->scb_pool);
	}

	return MRAID_SUCCESS;

fail_alloc_cmds:
	if (alloc_sg_pool_f == MRAID_TRUE) {
		mraid_pci_blk_pool_destroy(raid_dev->sg_pool_handle);
	}
	if (alloc_epthru_f == MRAID_TRUE) {
		mraid_pci_blk_pool_destroy(raid_dev->epthru_pool_handle);
	}
	if (alloc_mbox_f == MRAID_TRUE) {
		mraid_pci_blk_pool_destroy(raid_dev->mbox_pool_handle);
	}
	if (alloc_scb_f == MRAID_TRUE ) kfree(adapter->scb_list);

	if (alloc_int_ccb_pthru_f) {
		pci_free_consistent(pdev, 2048,
			((mbox_ccb_t *)(adapter->iscb.ccb))->pthru,
			((mbox_ccb_t *)(adapter->iscb.ccb))->pthru_dma_h);
	}
	if (alloc_int_ccb_f) {
		kfree(adapter->iscb.ccb);
	}
	if (alloc_ibuf_f) {
		pci_free_consistent(pdev, MBOX_IBUF_SIZE,
			(void *)adapter->ibuf, adapter->ibuf_dma_h);
	}
	if (alloc_common_mbox_f == MRAID_TRUE) {
		pci_free_consistent(adapter->pdev, sizeof(mbox64_t),
			(caddr_t)raid_dev->una_mbox64,
			raid_dev->una_mbox64_dma);
	}

	return MRAID_FAILURE;
}


/**
 * megaraid_free_common_mailbox - free shared mailbox
 * @param adapter	: soft state of the raid controller
 *
 * Release memory resources allocated for commands
 */
static void
megaraid_free_cmd_packets(adapter_t *adapter)
{
	mraid_device_t *raid_dev = ADAP2RAIDDEV(adapter);

	mraid_pci_blk_pool_destroy(raid_dev->sg_pool_handle);
	mraid_pci_blk_pool_destroy(raid_dev->epthru_pool_handle);
	mraid_pci_blk_pool_destroy(raid_dev->mbox_pool_handle);
	kfree(adapter->scb_list);

	pci_free_consistent(adapter->pdev, 2048,
		((mbox_ccb_t *)(adapter->iscb.ccb))->pthru,
		((mbox_ccb_t *)(adapter->iscb.ccb))->pthru_dma_h);

	kfree(adapter->iscb.ccb);

	pci_free_consistent(adapter->pdev, MBOX_IBUF_SIZE,
		(void *)adapter->ibuf, adapter->ibuf_dma_h);

	pci_free_consistent(adapter->pdev, sizeof(mbox64_t),
		(caddr_t)raid_dev->una_mbox64, raid_dev->una_mbox64_dma);
	return;
}


/**
 * megaraid_queue_command - generic queue entry point for all LLDs
 * @scp		: pointer to the scsi command to be executed
 * @done	: callback routine to be called after the cmd has be completed
 *
 * Queue entry point for mailbox based controllers. This entry point is common
 * for memory and IO based controllers
 */
static int
megaraid_queue_command(struct scsi_cmnd *scp, void (* done)(struct scsi_cmnd *))
{
	adapter_t	*adapter;
	scb_t		*scb;
	int		if_busy;
	unsigned long	flags;

	adapter		= SCP2ADAPTER(scp);
	scp->scsi_done	= done;
	scp->result	= 0;

	ASSERT(spin_is_locked(adapter->host_lock));

	spin_unlock(adapter->host_lock);

	/*
	 * Allocate and build a SCB request
	 * if_busy flag will be set if megaraid_mbox_build_cmd() command could
	 * not allocate scb. We will return non-zero status in that case.
	 * NOTE: scb can be null even though certain commands completed
	 * successfully, e.g., MODE_SENSE and TEST_UNIT_READY, it would
	 * return 0 in that case, and we would do the callback right away.
	 */
	if_busy	= 0;
	scb	= megaraid_mbox_build_cmd(adapter, scp, &if_busy);

	if (scb) {
		scb->state = SCB_PENDQ;

		spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);

		list_add_tail(&scb->list, &adapter->pend_list);

		spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);

		/*
		 * Check if the HBA is in quiescent state, e.g., during a
		 * delete logical drive opertion. If it is, don't run the
		 * pending list.
		 */
		if (adapter->quiescent == MRAID_FALSE) {
			megaraid_mbox_runpendq(adapter);
		}

		spin_lock(adapter->host_lock);

		return 0;
	}

	spin_lock(adapter->host_lock);

	done(scp);

	return if_busy;
}


/**
 * megaraid_mbox_build_cmd - transform the mid-layer scsi command to megaraid
 * firmware lingua
 * @adapter	- controller's soft state
 * @scp		- mid-layer scsi command pointer
 * @busy	- set if request could not be completed because of lack of
 *		resources
 *
 * convert the command issued by mid-layer to format understood by megaraid
 * firmware. We also complete certain command without sending them to firmware
 */
static inline scb_t *
megaraid_mbox_build_cmd(adapter_t *adapter, Scsi_Cmnd *scp, int *busy)
{
	mraid_device_t		*rdev = ADAP2RAIDDEV(adapter);
	int			channel;
	int			target;
	int			islogical;
	mbox_ccb_t		*ccb;
	mraid_passthru_t	*pthru;
	mbox64_t		*mbox64;
	mbox_t			*mbox;
	scb_t			*scb;
	uint32_t		xferlen;
	char			skip[] = "skipping";
	char			scan[] = "scanning";
	char			*ss;


	/*
	 * If the command is already prepared, i.e., internal commands, return
	 * immediately.
	 */
	if (scp->cmnd[0] == MRAID_INTERNAL_COMMAND) {
		return &adapter->iscb;
	}

	/*
	 * Get the appropriate device map for the device this command is
	 * intended for
	 */
	MRAID_GET_DEVICE_MAP(adapter, scp, channel, target, islogical);

	/*
	 * Logical drive commands
	 */
	if (islogical) {
		switch (scp->cmnd[0]) {
		case TEST_UNIT_READY:
			/*
			 * Do we support clustering and is the support enabled
			 * If no, return success always
			 */
			if (adapter->ha != MRAID_TRUE) {
				scp->result = (DID_OK << 16);
				return NULL;
			}

			if (!(scb = megaraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				*busy = 1;
				return NULL;
			}

			scb->dma_direction	= PCI_DIR(scp);
			scb->dev_target		= 0xFF;
			scb->dev_target		= target;
			ccb			= (mbox_ccb_t *)scb->ccb;

			/*
			 * The command id will be provided by the command
			 * issuance routine
			 */
			ccb->raw_mbox[0]	= CLUSTER_CMD;
			ccb->raw_mbox[2]	= RESERVATION_STATUS;
			ccb->raw_mbox[3]	= target;

			return scb;

		case MODE_SENSE:
			if (scp->use_sg) {
				struct scatterlist	*sgl;
				caddr_t			vaddr;

				sgl = (struct scatterlist *)scp->request_buffer;
				if (sgl->page) {
					vaddr = (caddr_t)
						(page_address((&sgl[0])->page)
						+ (&sgl[0])->offset);

					memset(vaddr, 0, scp->cmnd[4]);
				}
				else {
					con_log(CL_ANN, (KERN_WARNING
					"megaraid mailbox: invalid sg:%d\n",
					__LINE__));
				}
			}
			else {
				memset(scp->request_buffer, 0, scp->cmnd[4]);
			}
			scp->result = (DID_OK << 16);
			return NULL;

		case INQUIRY:
			/*
			 * Display the channel scan for logical drives
			 * Do not display scan for a channel if already done.
			 */
			if (!(rdev->last_disp & (1L << SCP2CHANNEL(scp)))) {

				con_log(CL_ANN, (KERN_INFO
					"scsi[%d]: scanning scsi channel %d",
					adapter->host->host_no,
					SCP2CHANNEL(scp)));

				con_log(CL_ANN, (
					" [virtual] for logical drives\n"));

				rdev->last_disp |= (1L << SCP2CHANNEL(scp));
			}

			/* Fall through */

		case READ_CAPACITY:
			/*
			 * Do not allow LUN > 0 for logical drives and
			 * requests for more than 40 logical drives
			 */
			if (SCP2LUN(scp)) {
				scp->result = (DID_BAD_TARGET << 16);
				return NULL;
			}
			if ((target % 0x80) >= MAX_LOGICAL_DRIVES_40LD) {
				scp->result = (DID_BAD_TARGET << 16);
				return NULL;
			}


			/* Allocate a SCB and initialize passthru */
			if (!(scb = megaraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				*busy = 1;
				return NULL;
			}

			ccb			= (mbox_ccb_t *)scb->ccb;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			pthru			= ccb->pthru;
			mbox			= ccb->mbox;
			mbox64			= ccb->mbox64;

			pthru->timeout		= 0;
			pthru->ars		= 1;
			pthru->reqsenselen	= 14;
			pthru->islogical	= 1;
			pthru->logdrv		= target;
			pthru->cdblen		= scp->cmd_len;
			memcpy(pthru->cdb, scp->cmnd, scp->cmd_len);

			mbox->cmd		= MBOXCMD_PASSTHRU64;
			scb->dma_direction	= PCI_DIR(scp);

			pthru->numsge = megaraid_mbox_mksgl(adapter, scb,
				&pthru->dataxferaddr, &pthru->dataxferlen);

			mbox->xferaddr		= 0xFFFFFFFF;
			mbox64->xferaddr_lo	= ccb->pthru_dma_h;
			mbox64->xferaddr_hi	= 0;

			return scb;

		case READ_6:
		case WRITE_6:
		case READ_10:
		case WRITE_10:
		case READ_12:
		case WRITE_12:

			/*
			 * Allocate a SCB and initialize mailbox
			 */
			if (!(scb = megaraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				*busy = 1;
				return NULL;
			}
			ccb			= (mbox_ccb_t *)scb->ccb;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			mbox			= ccb->mbox;
			mbox64			= ccb->mbox64;
			mbox->logdrv		= target;

			/*
			 * A little HACK: 2nd bit is zero for all scsi read
			 * commands and is set for all scsi write commands
			 */
			mbox->cmd = (scp->cmnd[0] & 0x02) ?  MBOXCMD_LWRITE64:
					MBOXCMD_LREAD64 ;

			/*
			 * 6-byte READ(0x08) or WRITE(0x0A) cdb
			 */
			if (scp->cmd_len == 6) {
				mbox->numsectors = (uint32_t)scp->cmnd[4];
				mbox->lba =
					((uint32_t)scp->cmnd[1] << 16)	|
					((uint32_t)scp->cmnd[2] << 8)	|
					(uint32_t)scp->cmnd[3];

				mbox->lba &= 0x1FFFFF;
			}

			/*
			 * 10-byte READ(0x28) or WRITE(0x2A) cdb
			 */
			else if (scp->cmd_len == 10) {
				mbox->numsectors =
					(uint32_t)scp->cmnd[8] |
					((uint32_t)scp->cmnd[7] << 8);
				mbox->lba =
					((uint32_t)scp->cmnd[2] << 24) |
					((uint32_t)scp->cmnd[3] << 16) |
					((uint32_t)scp->cmnd[4] << 8) |
					(uint32_t)scp->cmnd[5];
			}

			/*
			 * 12-byte READ(0xA8) or WRITE(0xAA) cdb
			 */
			else if (scp->cmd_len == 12) {
				mbox->lba =
					((uint32_t)scp->cmnd[2] << 24) |
					((uint32_t)scp->cmnd[3] << 16) |
					((uint32_t)scp->cmnd[4] << 8) |
					(uint32_t)scp->cmnd[5];

				mbox->numsectors =
					((uint32_t)scp->cmnd[6] << 24) |
					((uint32_t)scp->cmnd[7] << 16) |
					((uint32_t)scp->cmnd[8] << 8) |
					(uint32_t)scp->cmnd[9];
			}
			else {
				con_log(CL_ANN, (KERN_WARNING
					"megaraid: unsupported CDB length\n"));

				megaraid_dealloc_scb(adapter, scb);

				scp->result = (DID_ERROR << 16);
				return NULL;
			}



			scb->dma_direction = PCI_DIR(scp);

			/*
			 * Calculate Scatter-Gather info
			 */
			mbox->numsge = megaraid_mbox_mksgl(adapter, scb,
					(uint32_t *)&mbox64->xferaddr_lo,
					&xferlen);
			mbox->xferaddr		= 0xFFFFFFFF;
			mbox64->xferaddr_hi	= 0;

			return scb;

		case RESERVE:
		case RELEASE:
			/*
			 * Do we support clustering and is the support enabled
			 */
			if (!adapter->ha) {
				scp->result = (DID_BAD_TARGET << 16);
				return NULL;
			}

			/*
			 * Allocate a SCB and initialize mailbox
			 */
			if (!(scb = megaraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				*busy = 1;
				return NULL;
			}

			ccb			= (mbox_ccb_t *)scb->ccb;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			ccb->raw_mbox[0]	= CLUSTER_CMD;
			/*
			 * The command id will be provided by the command
			 * issuance routine
			 */
			ccb->raw_mbox[2]	=  (scp->cmnd[0] == RESERVE) ?
						RESERVE_LD : RELEASE_LD;

			ccb->raw_mbox[3]	= target;
			scb->dma_direction	= PCI_DIR(scp);

			return scb;

		default:
			scp->result = (DID_BAD_TARGET << 16);
			return NULL;
		}
	}
	else { // Passthru device commands

		// Do not allow access to target id > 15 or LUN > 7
		if (target > 15 || SCP2LUN(scp) > 7) {
			scp->result = (DID_BAD_TARGET << 16);
			return NULL;
		}

		// if fast load option was set and scan for last device is
		// over, reset the fast_load flag so that during a possible
		// next scan, devices can be made available
		if (rdev->fast_load && (target == 15) &&
			(SCP2CHANNEL(scp) == adapter->max_channel -1)) {

			con_log(CL_ANN, (KERN_INFO
			"megaraid[%d]: physical device scan re-enabled\n",
				adapter->host->host_no));
			rdev->fast_load = 0;
		}

		/*
		 * Display the channel scan for physical devices
		 */
		if (!(rdev->last_disp & (1L << SCP2CHANNEL(scp)))) {

			ss = rdev->fast_load ? skip : scan;

			con_log(CL_ANN, (KERN_INFO
				"scsi[%d]: %s scsi channel %d [Phy %d]",
				adapter->host->host_no, ss, SCP2CHANNEL(scp),
				channel));

			con_log(CL_ANN, (
				" for non-raid devices\n"));

			rdev->last_disp |= (1L << SCP2CHANNEL(scp));
		}

		// disable channel sweep if fast load option given
		if (rdev->fast_load) {
			scp->result = (DID_BAD_TARGET << 16);
			return NULL;
		}

		/*
		 * Allocate a SCB and initialize passthru
		 */
		if (!(scb = megaraid_alloc_scb(adapter, scp))) {
			scp->result = (DID_ERROR << 16);
			*busy = 1;
			return NULL;
		}

		ccb			= (mbox_ccb_t *)scb->ccb;
		scb->dev_channel	= channel;
		scb->dev_target		= target;
		scb->dma_direction	= PCI_DIR(scp);
		mbox			= ccb->mbox;
		mbox64			= ccb->mbox64;

		// Does this firmware support extended CDBs
		if (adapter->max_cdb_sz == 16) {
			mbox->cmd		= MBOXCMD_EXTPTHRU;

			megaraid_mbox_prepare_epthru(adapter, scb, scp);

			mbox64->xferaddr_lo	= (uint32_t)ccb->epthru_dma_h;
			mbox64->xferaddr_hi	= 0;
			mbox->xferaddr		= 0xFFFFFFFF;
		}
		else {
			mbox->cmd = MBOXCMD_PASSTHRU64;

			megaraid_mbox_prepare_pthru(adapter, scb, scp);

			mbox64->xferaddr_lo	= (uint32_t)ccb->pthru_dma_h;
			mbox64->xferaddr_hi	= 0;
			mbox->xferaddr		= 0xFFFFFFFF;
		}
		return scb;
	}
}


/**
 * megaraid_alloc_scb - detach and return a scb from the free list
 * @adapter	: controller's soft state
 *
 * return the scb from the head of the free list. NULL if there are none
 * available
 **/
static inline scb_t *
megaraid_alloc_scb(adapter_t *adapter, struct scsi_cmnd *scp)
{
	struct list_head	*head = &adapter->scb_pool;
	scb_t			*scb = NULL;
	unsigned long		flags;

	// detach scb from free pool
	spin_lock_irqsave(FREE_LIST_LOCK(adapter), flags);

	if (list_empty(head)) {
		spin_unlock_irqrestore(FREE_LIST_LOCK(adapter), flags);
		return NULL;
	}

	scb = list_entry(head->next, scb_t, list);
	list_del_init(&scb->list);

	spin_unlock_irqrestore(FREE_LIST_LOCK(adapter), flags);

	scb->state	= SCB_ACTIVE;
	scb->scp	= scp;
	scb->dma_type	= MRAID_DMA_NONE;

	return scb;
}


/**
 * megaraid_dealloc_scb - return the scb to the free pool
 * @adapter	: controller's soft state
 * @scb		: scb to be freed
 *
 * return the scb back to the free list of scbs. The caller must 'flush' the
 * SCB before calling us. E.g., performing pci_unamp and/or pci_sync etc.
 * NOTE NOTE: Make sure the scb is not on any list before calling this
 * routine.
 **/
static inline void
megaraid_dealloc_scb(adapter_t *adapter, scb_t *scb)
{
	unsigned long		flags;

	// put scb in the free pool
	scb->state	= SCB_FREE;
	scb->scp	= NULL;
	spin_lock_irqsave(FREE_LIST_LOCK(adapter), flags);

	list_add(&scb->list, &adapter->scb_pool);

	spin_unlock_irqrestore(FREE_LIST_LOCK(adapter), flags);

	return;
}


/**
 * megaraid_mbox_prepare_pthru - prepare a command for physical devices
 * @adapter	- pointer to controller's soft state
 * @scb		- scsi control block
 * @scp		- scsi command from the mid-layer
 *
 * prepare a command for the scsi physical devices
 */
static inline void
megaraid_mbox_prepare_pthru(adapter_t *adapter, scb_t *scb, Scsi_Cmnd *scp)
{
	mbox_ccb_t		*ccb;
	mraid_passthru_t	*pthru;
	uint8_t			channel;
	uint8_t			target;

	ccb	= (mbox_ccb_t *)scb->ccb;
	pthru	= ccb->pthru;
	channel	= scb->dev_channel;
	target	= scb->dev_target;

	pthru->timeout		= 2;	/* 0=6sec,1=60sec,2=10min,3=3hrs */
	pthru->ars		= 1;
	pthru->islogical	= 0;
	pthru->channel		= 0;
	pthru->target		= (channel << 4) | target;
	pthru->logdrv		= SCP2LUN(scp);
	pthru->reqsenselen	= 14;
	pthru->cdblen		= scp->cmd_len;

	memcpy(pthru->cdb, scp->cmnd, scp->cmd_len);

	if (scp->request_bufflen) {
		pthru->numsge = megaraid_mbox_mksgl(adapter, scb,
				&pthru->dataxferaddr, &pthru->dataxferlen);
	}
	else {
		pthru->dataxferaddr	= 0;
		pthru->dataxferlen	= 0;
	}
	return;
}


/**
 * megaraid_mbox_prepare_epthru - prepare a command for physical devices
 * @adapter	- pointer to controller's soft state
 * @scb		- scsi control block
 * @scp		- scsi command from the mid-layer
 *
 * prepare a command for the scsi physical devices. This rountine prepares
 * commands for devices which can take extended CDBs (>10 bytes)
 */
static inline void
megaraid_mbox_prepare_epthru(adapter_t *adapter, scb_t *scb, Scsi_Cmnd *scp)
{
	mbox_ccb_t		*ccb;
	mraid_epassthru_t	*epthru;
	uint8_t			channel;
	uint8_t			target;

	ccb	= (mbox_ccb_t *)scb->ccb;
	epthru	= ccb->epthru;
	channel	= scb->dev_channel;
	target	= scb->dev_target;

	epthru->timeout		= 2;	/* 0=6sec,1=60sec,2=10min,3=3hrs */
	epthru->ars		= 1;
	epthru->islogical	= 0;
	epthru->channel		= 0;
	epthru->target		= (channel << 4) | target;
	epthru->logdrv		= SCP2LUN(scp);
	epthru->reqsenselen	= 14;
	epthru->cdblen		= scp->cmd_len;

	memcpy(epthru->cdb, scp->cmnd, scp->cmd_len);

	if (scp->request_bufflen) {
		epthru->numsge = megaraid_mbox_mksgl(adapter, scb,
				&epthru->dataxferaddr, &epthru->dataxferlen);
	}
	else {
		epthru->dataxferaddr	= 0;
		epthru->dataxferlen	= 0;
	}
	return;
}


/**
 * megaraid_mbox_mksgl - make the scatter-gather list
 * @adapter	- controller's soft state
 * @scb		- scsi control block
 * @xferaddr	- start of data transfer address (block/sg list)
 * @xferlen	- total amount of data to be transferred
 *
 * prepare the scatter-gather list
 */
static inline int
megaraid_mbox_mksgl(adapter_t *adapter, scb_t *scb, uint32_t *xferaddr,
		uint32_t *xferlen)
{
	struct scatterlist	*sgl;
	mbox_ccb_t		*ccb;
	struct page		*page;
	unsigned long		offset;
	Scsi_Cmnd		*scp;
	int			sgcnt;
	int			i;


	scp	= scb->scp;
	ccb	= (mbox_ccb_t *)scb->ccb;

	if (!scp->use_sg) {	/* scatter-gather list not used */

		page = virt_to_page(scp->request_buffer);

		offset = ((unsigned long)scp->request_buffer & ~PAGE_MASK);

		ccb->buf_dma_h = pci_map_page(adapter->pdev, page, offset,
						  scp->request_bufflen,
						  scb->dma_direction);
		scb->dma_type = MRAID_DMA_WBUF;

		/*
		 * We need to handle special 64-bit commands that need a
		 * minimum of 1 SG
		 */
		sgcnt = 1;
		ccb->sgl64[0].address	= ccb->buf_dma_h;
		ccb->sgl64[0].length	= scp->request_bufflen;
		*xferaddr		= (uint32_t)ccb->sgl_dma_h;
		*xferlen		= (uint32_t)scp->request_bufflen;

		return sgcnt;
	}

	sgl = (struct scatterlist *)scp->request_buffer;

	// The number of sg elements returned must not exceed our limit
	sgcnt = pci_map_sg(adapter->pdev, sgl, scp->use_sg,
			scb->dma_direction);

	if (sgcnt > adapter->sglen) {
		con_log(CL_ANN, (KERN_CRIT
			"megaraid critical: too many sg elements:%d\n",
			sgcnt));
		BUG();
	}

	scb->dma_type = MRAID_DMA_WSG;

	for (i = 0; i < sgcnt; i++, sgl++) {
		ccb->sgl64[i].address	= sg_dma_address(sgl);
		ccb->sgl64[i].length	= sg_dma_len(sgl);
	}

	// dataxferlen must be set, even for commands with a sg list
	*xferaddr = ccb->sgl_dma_h;
	*xferlen = (uint32_t)scp->request_bufflen;

	// Return count of SG nodes
	return sgcnt;
}


/**
 * megaraid_mbox_runpendq - execute commands queued in the pending queue
 * @adapter	- controller's soft state
 *
 * scan the pending list for commands which are not yet issued and try to
 * post to the controller.
 *
 * NOTE: We do not actually traverse the pending list. The SCBs are plucked
 * out from the head of the pending list. If it is successfully issued, the
 * next SCB is at the head now.
 */
static inline void
megaraid_mbox_runpendq(adapter_t *adapter)
{
	scb_t			*scb;
	unsigned long		flags;

	spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);

	while (!list_empty(&adapter->pend_list)) {

		ASSERT(spin_is_locked(PENDING_LIST_LOCK(adapter)));

		scb = list_entry(adapter->pend_list.next, scb_t, list);

		// remove the scb from the pending list and try to
		// issue. If we are unable to issue it, put back in
		// the pending list and return

		list_del_init(&scb->list);

		spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);

		// if mailbox was busy, return SCB back to pending
		// list. Make sure to add at the head, since that's
		// where it would have been removed from

		scb->state = SCB_ISSUED;

		if (mbox_post_cmd(adapter, scb) != MRAID_SUCCESS) {

			spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);

			scb->state = SCB_PENDQ;

			list_add(&scb->list, &adapter->pend_list);

			spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter),
				flags);

			return;
		}

		spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);
	}

	spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);


	return;
}


/**
 * mbox_post_cmd - issue a mailbox command
 * @adapter	- controller's soft state
 * @scb		- command to be issued
 *
 * post the command to the controller if mailbox is availble.
 */
static inline status_t
mbox_post_cmd(adapter_t *adapter, scb_t *scb)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox64_t	*mbox64;
	mbox_t		*mbox;
	mbox_ccb_t	*ccb;
	unsigned long	flags;
	unsigned int	i = 0;


	ccb	= (mbox_ccb_t *)scb->ccb;
	mbox	= raid_dev->mbox;
	mbox64	= raid_dev->mbox64;

	/*
	 * Check for busy mailbox. If it is, return failure - the caller
	 * should retry later.
	 */
	spin_lock_irqsave(MAILBOX_LOCK(raid_dev), flags);

	if (unlikely(mbox->busy)) {
		do {
			udelay(1);
			i++;
			rmb();
		} while(mbox->busy && (i < max_mbox_busy_wait));

		if (mbox->busy) {

			spin_unlock_irqrestore(MAILBOX_LOCK(raid_dev), flags);

			return MRAID_FAILURE;
		}
	}

	// Do we have an available command id. If not return failure
	MBOX_GET_CMDID(raid_dev, scb);

	if (scb->sno == -1) {
		spin_unlock_irqrestore(MAILBOX_LOCK(raid_dev), flags);
		return MRAID_FAILURE;
	}

	// Copy this command's mailbox data into "adapter's" mailbox
	memcpy((caddr_t)mbox64, (caddr_t)ccb->mbox64, 24);
	mbox->cmdid = scb->sno;

	adapter->outstanding_cmds++;

	if (scb->dma_direction == PCI_DMA_TODEVICE) {
		if (!scb->scp->use_sg) {	// sg list not used
			pci_dma_sync_single(adapter->pdev, ccb->buf_dma_h,
					scb->scp->request_bufflen,
					PCI_DMA_TODEVICE);
		}
		else {
			pci_dma_sync_sg(adapter->pdev, scb->scp->request_buffer,
				scb->scp->use_sg, PCI_DMA_TODEVICE);
		}
	}

	mbox->busy	= 1;	// Set busy
	mbox->poll	= 0;
	mbox->ack	= 0;
	wmb();

	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x1);

	spin_unlock_irqrestore(MAILBOX_LOCK(raid_dev), flags);

	return MRAID_SUCCESS;
}


/**
 * megaraid_isr - isr for memory based mailbox based controllers
 * @irq		- irq
 * @devp	- pointer to our soft state
 * @regs	- unused
 *
 * Interrupt service routine for memory-mapped mailbox controllers.
 */
static irqreturn_t
megaraid_isr(int irq, void *devp, struct pt_regs *regs)
{
	adapter_t	*adapter = devp;
	int		handled;


	handled = megaraid_ack_sequence(adapter);

	/* Loop through any pending requests */
	if (adapter->quiescent == MRAID_FALSE) {
		megaraid_mbox_runpendq(adapter);
	}

	return IRQ_RETVAL(handled);
}


/**
 * megaraid_ack_sequence - interrupt ack sequence for memory mapped HBAs
 * @adapter	- controller's soft state
 *
 * Interrupt ackrowledgement sequence for memory mapped HBAs. Find out the
 * completed command and put them on the completed list for later processing.
 *
 * Returns:	1 if the interrupt is valid, 0 otherwise
 */
static inline int
megaraid_ack_sequence(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_t			*mbox;
	scb_t			*scb;
	uint8_t			nstatus;
	uint8_t			completed[MBOX_MAX_FIRMWARE_STATUS];
	struct list_head	clist;
	int			handled;
	uint32_t		dword;
	unsigned long		flags;
	int			i, j;


	mbox	= raid_dev->mbox;

	// we move the SCBs from the firmware completed array to our local list
	INIT_LIST_HEAD(&clist);

	// loop till F/W has more commands for us to complete
	handled = 0;
	spin_lock_irqsave(MAILBOX_LOCK(raid_dev), flags);
	do {
		/*
		 * Check if a valid interrupt is pending. If found, force the
		 * interrupt line low.
		 */
		dword = RDOUTDOOR(raid_dev);
		if (dword != 0x10001234) break;

		handled = 1;

		WROUTDOOR(raid_dev, 0x10001234);

		nstatus = 0;
		// wait for valid numstatus to post
		for (i = 0; i < 0xFFFFF; i++) {
			if (mbox->numstatus != 0xFF) {
				nstatus = mbox->numstatus;
				break;
			}
			rmb();
		}
		mbox->numstatus = 0xFF;

		adapter->outstanding_cmds -= nstatus;

		for (i = 0; i < nstatus; i++) {

			// wait for valid command index to post
			for (j = 0; j < 0xFFFFF; j++) {
				if (mbox->completed[i] != 0xFF) break;
				rmb();
			}
			completed[i]		= mbox->completed[i];
			mbox->completed[i]	= 0xFF;

			if (completed[i] == 0xFF) {
				con_log(CL_ANN, (KERN_CRIT
				"megaraid: command posting timed out\n"));

				BUG();
				continue;
			}

			// Free the command id if this was not an internal
			// command
			if (completed[i] == 0) {
				scb = &adapter->iscb;
			}
			else {
				// Get SCB associated with this command id
				scb = MBOX_GET_CMDID_SCB(raid_dev,
					completed[i]);

				ASSERT(scb->sno == completed[i]);

				scb->sno = -1;

				MBOX_FREE_CMDID(raid_dev, completed[i]);
			}

			scb->status = mbox->status;
			list_add_tail(&scb->list, &clist);
		}

		// Acknowledge interrupt
		WRINDOOR(raid_dev, 0x02);

	} while(1);

	spin_unlock_irqrestore(MAILBOX_LOCK(raid_dev), flags);


	// put the completed commands in the completed list. DPC would
	// complete these commands later
	spin_lock_irqsave(COMPLETED_LIST_LOCK(adapter), flags);

	list_splice(&clist, &adapter->completed_list);

	spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter), flags);


	// schedule the DPC if there is some work for it
	if (handled)
		tasklet_schedule(&adapter->dpc_h);

	return handled;
}


static void
megaraid_mbox_dpc(unsigned long devp)
{
	adapter_t		*adapter = (adapter_t *)devp;
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	struct list_head	*pos, *next;
	struct list_head	clist;
	struct scatterlist	*sgl;
	scb_t			*scb;
	Scsi_Cmnd		*scp;
	mraid_passthru_t	*pthru;
	mraid_epassthru_t	*epthru;
	mbox_ccb_t		*ccb;
	int			islogical;
	int			pdev_index;
	int			pdev_state;
	mbox_t			*mbox;
	unsigned long		flags;
	uint8_t			c;
	int			status;

	// move the SCBs from the completed list to our local list
	INIT_LIST_HEAD(&clist);

	spin_lock_irqsave(COMPLETED_LIST_LOCK(adapter), flags);

	list_splice_init(&adapter->completed_list, &clist);

	spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter), flags);

	list_for_each_safe(pos, next, &clist) {
		scb = list_entry(pos, scb_t, list);

		status		= scb->status;
		scp		= scb->scp;
		ccb		= (mbox_ccb_t *)scb->ccb;
		pthru		= ccb->pthru;
		epthru		= ccb->epthru;
		mbox		= ccb->mbox;
		islogical	= MRAID_IS_LOGICAL(adapter, scp);

		/*
		 * Make sure f/w has completed a valid command
		 */
		if (!(scb->state & SCB_ISSUED)) {
			con_log(CL_ANN, (KERN_CRIT
			"megaraid critical err: invalid command %d:%d:%p\n",
				scb->sno, scb->state, scp));
			BUG();
			continue;	// Must never happen!
		}

		// check for the internal command and complete it right away
		if (scb->sno == 0) {
			scb->state	= SCB_FREE;
			scp->result	= status;

			spin_lock(adapter->host_lock);

			scp->scsi_done(scp);

			spin_unlock(adapter->host_lock);

			continue;
		}

		// Was an abort issued for this command earlier
		if (scb->state & SCB_ABORT) {
			con_log(CL_ANN, (KERN_NOTICE
			"megaraid: aborted cmd %lx[%x] completed\n",
				scp->serial_number, scb->sno));
		}

		/*
		 * If the inquiry came of a disk drive which is not part of
		 * any RAID array, expose it to the kernel. For this to be
		 * enabled, user must set the "megaraid_expose_unconf_disks"
		 * flag to 1 by specifying it on module parameter list.
		 * This would enable data migration off drives from other
		 * configurations.
		 */
		if (scp->cmnd[0] == INQUIRY && status == 0 && islogical == 0
				&& IS_RAID_CH(raid_dev, scb->dev_channel)) {

			if (scp->use_sg) {
				sgl = (struct scatterlist *)
					scp->request_buffer;

				if (sgl->page) {
					c = *(unsigned char *)
					(page_address((&sgl[0])->page) +
						(&sgl[0])->offset);
				}
				else {
					con_log(CL_ANN, (KERN_WARNING
					"megaraid mailbox: invalid sg:%d\n",
					__LINE__));
					c = 0;
				}
			}
			else {
				c = *(uint8_t *)scp->request_buffer;
			}

			if ((c & 0x1F ) == TYPE_DISK) {
				pdev_index = (scb->dev_channel * 16) +
					scb->dev_target;
				pdev_state =
					raid_dev->pdrv_state[pdev_index] & 0x0F;

				if (pdev_state == PDRV_ONLINE		||
					pdev_state == PDRV_FAILED	||
					pdev_state == PDRV_RBLD		||
					pdev_state == PDRV_HOTSPARE	||
					megaraid_expose_unconf_disks == 0) {

					status = 0xF0;
				}
			}
		}

		// Convert MegaRAID status to Linux error code
		switch (status) {

		case 0x00:

			scp->result = (DID_OK << 16);
			break;

		case 0x02:

			/* set sense_buffer and result fields */
			if (mbox->cmd == MBOXCMD_PASSTHRU ||
				mbox->cmd == MBOXCMD_PASSTHRU64) {

				memcpy(scp->sense_buffer, pthru->reqsensearea,
						14);

				scp->result = DRIVER_SENSE << 24 |
					DID_OK << 16 | CHECK_CONDITION << 1;
			}
			else {
				if (mbox->cmd == MBOXCMD_EXTPTHRU) {

					memcpy(scp->sense_buffer,
						epthru->reqsensearea, 14);

					scp->result = DRIVER_SENSE << 24 |
						DID_OK << 16 |
						CHECK_CONDITION << 1;
				} else {
					scp->sense_buffer[0] = 0x70;
					scp->sense_buffer[2] = ABORTED_COMMAND;
					scp->result = CHECK_CONDITION << 1;
				}
			}
			break;

		case 0x08:

			scp->result = DID_BUS_BUSY << 16 | status;
			break;

		default:

			/*
			 * If TEST_UNIT_READY fails, we know RESERVATION_STATUS
			 * failed
			 */
			if (scp->cmnd[0] == TEST_UNIT_READY) {
				scp->result = DID_ERROR << 16 |
					RESERVATION_CONFLICT << 1;
			}
			else
			/*
			 * Error code returned is 1 if Reserve or Release
			 * failed or the input parameter is invalid
			 */
			if (status == 1 && (scp->cmnd[0] == RESERVE ||
					 scp->cmnd[0] == RELEASE)) {

				scp->result = DID_ERROR << 16 |
					RESERVATION_CONFLICT << 1;
			}
			else {
				scp->result = DID_BAD_TARGET << 16 | status;
			}
		}

		// print a level 3 debug message for all failed commands
		if (status) {
			megaraid_mbox_display_scb(adapter, scb, CL_DLEVEL3);
		}

		/*
		 * Free our internal resources and call the mid-layer callback
		 * routine
		 */
		megaraid_mbox_sync_scb(adapter, scb);

		// remove from clist
		list_del_init(&scb->list);

		// put back in free list
		megaraid_dealloc_scb(adapter, scb);

		// send the scsi packet back to kernel
		spin_lock(adapter->host_lock);
		scp->scsi_done(scp);
		spin_unlock(adapter->host_lock);
	}

	return;
}


/**
 * megaraid_mbox_sync_scb - sync kernel buffers
 * @adapter	- controller's soft state
 * @scb		- pointer to the resource packet
 *
 * DMA sync if required.
 */
static inline void
megaraid_mbox_sync_scb(adapter_t *adapter, scb_t *scb)
{
	mbox_ccb_t	*ccb;

	ccb	= (mbox_ccb_t *)scb->ccb;

	switch (scb->dma_type) {

	case MRAID_DMA_WBUF:
		if (scb->dma_direction == PCI_DMA_FROMDEVICE) {
			pci_dma_sync_single(adapter->pdev,
					ccb->buf_dma_h,
					scb->scp->request_bufflen,
					PCI_DMA_FROMDEVICE);
		}

		pci_unmap_page(adapter->pdev, ccb->buf_dma_h,
			scb->scp->request_bufflen, scb->dma_direction);

		break;

	case MRAID_DMA_WSG:
		if (scb->dma_direction == PCI_DMA_FROMDEVICE) {
			pci_dma_sync_sg(adapter->pdev,
					scb->scp->request_buffer,
					scb->scp->use_sg, PCI_DMA_FROMDEVICE);
		}

		pci_unmap_sg(adapter->pdev, scb->scp->request_buffer,
			scb->scp->use_sg, scb->dma_direction);

		break;

	default:
		break;
	}

	return;
}


/**
 * megaraid_abort_handler - abort the scsi command
 * @scp		: command to be aborted
 *
 * Abort a previous SCSI request. Only commands on the pending list can be
 * aborted. All the commands issued to the F/W must complete.
 **/
static int
megaraid_abort_handler(struct scsi_cmnd *scp)
{
	adapter_t		*adapter;
	mraid_device_t		*raid_dev;
	scb_t			*scb;
	struct list_head	*pos, *next;
	int			found;
	unsigned long		flags;
	int			i;


	adapter		= SCP2ADAPTER(scp);
	raid_dev	= ADAP2RAIDDEV(adapter);

	ASSERT(spin_is_locked(adapter->host_lock));

	con_log(CL_ANN, (KERN_WARNING
		"megaraid: aborting-%ld cmd=%x <c=%d t=%d l=%d>\n",
		scp->serial_number, scp->cmnd[0], SCP2CHANNEL(scp),
		SCP2TARGET(scp), SCP2LUN(scp)));

	// If FW has stopped responding, simply return failure
	if (raid_dev->hw_error) {
		con_log(CL_ANN, (KERN_NOTICE
			"megaraid: hw error, not aborting\n"));
		return FAILED;
	}

	// There might a race here, where the command was completed by the
	// firmware and now it is on the completed list. Before we could
	// complete the command to the kernel in dpc, the abort came.
	// Find out if this is the case to avoid the race.
	scb = NULL;
	spin_lock_irqsave(COMPLETED_LIST_LOCK(adapter), flags);
	list_for_each_safe(pos, next, &adapter->completed_list) {

		scb = list_entry(pos, scb_t, list);

		if (scb->scp == scp) {	// Found command

			list_del_init(&scb->list);	// from completed list

			con_log(CL_ANN, (KERN_WARNING
			"megaraid: %ld:%d[%d:%d], abort from completed list\n",
				scp->serial_number, scb->sno,
				scb->dev_channel, scb->dev_target));

			scp->result = (DID_ABORT << 16);
			scp->scsi_done(scp);

			megaraid_dealloc_scb(adapter, scb);

			spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter),
				flags);

			return SUCCESS;
		}
	}
	spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter), flags);


	// Find out if this command is still on the pending list. If it is and
	// was never issued, abort and return success. If the command is owned
	// by the firmware, we must wait for it to complete by the FW.
	spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);
	list_for_each_safe(pos, next, &adapter->pend_list) {

		scb = list_entry(pos, scb_t, list);

		if (scb->scp == scp) {	// Found command

			list_del_init(&scb->list);	// from pending list

			ASSERT(!(scb->state & SCB_ISSUED));

			con_log(CL_ANN, (KERN_WARNING
				"megaraid abort: %ld[%d:%d], driver owner\n",
				scp->serial_number, scb->dev_channel,
				scb->dev_target));

			scp->result = (DID_ABORT << 16);
			scp->scsi_done(scp);

			megaraid_dealloc_scb(adapter, scb);

			spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter),
				flags);

			return SUCCESS;
		}
	}
	spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);


	// Check do we even own this command, in which case this would be
	// owned by the firmware. The only way to locate the FW scb is to
	// traverse through the list of all SCB, since driver does not
	// maintain these SCBs on any list
	found = 0;
	for (i = 0; i < megaraid_driver_cmds; i++) {
		scb = adapter->scb_list + i;

		if (scb->scp == scp) {

			found = 1;

			if (!(scb->state & SCB_ISSUED)) {
				con_log(CL_ANN, (KERN_WARNING
				"megaraid abort: %ld%d[%d:%d], invalid state\n",
				scp->serial_number, scb->sno, scb->dev_channel,
				scb->dev_target));
				BUG();
			}
			else {
				con_log(CL_ANN, (KERN_WARNING
				"megaraid abort: %ld:%d[%d:%d], fw owner\n",
				scp->serial_number, scb->sno, scb->dev_channel,
				scb->dev_target));
			}
		}
	}

	if (!found) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid abort: %ld:%d[%d:%d], do now own\n",
			scp->serial_number, scb->sno, scb->dev_channel,
			scb->dev_target));

		return SUCCESS;
	}

	// We cannot actually abort a command owned by firmware, return
	// failure and wait for reset. In host reset handler, we will find out
	// if the HBA is still live
	return FAILED;
}


/**
 * megaraid_reset_handler - device reset hadler for mailbox based driver
 * @scp		: reference command
 *
 * Reset handler for the mailbox based controller. First try to find out if
 * the FW is still live, in which case the outstanding commands counter mut go
 * down to 0. If that happens, also issue the reservation reset command to
 * relinquish (possible) reservations on the logical drives connected to this
 * host
 **/
static int
megaraid_reset_handler(struct scsi_cmnd *scp)
{
	adapter_t		*adapter;
	scb_t			*scb;
	mbox_ccb_t		*ccb;
	mraid_device_t		*raid_dev;
	struct list_head	*pos, *next;
	unsigned long		flags;
	int			rval;
	int			i;

	adapter		= SCP2ADAPTER(scp);
	raid_dev	= ADAP2RAIDDEV(adapter);

	ASSERT(spin_is_locked(adapter->host_lock));

	con_log(CL_ANN, (KERN_WARNING "megaraid: reseting the host...\n"));

	// return failure if adapter is not responding
	if (raid_dev->hw_error) {
		con_log(CL_ANN, (KERN_NOTICE
			"megaraid: hw error, cannot reset\n"));
		return FAILED;
	}


	// Under exceptional conditions, FW can take up to 3 minutes to
	// complete command processing. Wait for additional 3 minutes for the
	// pending commands counter to go down to 0. If it doesn't, mark the
	// controller offline
	// Also, reset all the commands currently owned by the driver
	spin_lock_irqsave(PENDING_LIST_LOCK(adapter), flags);
	list_for_each_safe(pos, next, &adapter->pend_list) {

		scb = list_entry(pos, scb_t, list);

		list_del_init(&scb->list);	// from pending list

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: %ld:%d[%d:%d], reset from pending list\n",
				scp->serial_number, scb->sno,
				scb->dev_channel, scb->dev_target));

		scp->result = (DID_RESET << 16);
		scp->scsi_done(scp);

		megaraid_dealloc_scb(adapter, scb);
	}
	spin_unlock_irqrestore(PENDING_LIST_LOCK(adapter), flags);

	if (adapter->outstanding_cmds) {
		con_log(CL_ANN, (KERN_NOTICE
			"megaraid: %d outstanding commands. Max wait %d sec\n",
			adapter->outstanding_cmds, MBOX_RESET_WAIT));
	}

	for (i = 0; i < MBOX_RESET_WAIT && adapter->outstanding_cmds; i++) {

		ASSERT(spin_is_locked(adapter->host_lock));

		megaraid_ack_sequence(adapter);

		spin_lock_irqsave(COMPLETED_LIST_LOCK(adapter), flags);
		while (!list_empty(&adapter->completed_list)) {

			scb = list_entry(adapter->completed_list.next, scb_t,
				list);

			list_del_init(&scb->list);	// from completed list

			con_log(CL_ANN, (KERN_WARNING
			"megaraid: %ld:%d[%d:%d], reset from completed list\n",
				scp->serial_number, scb->sno,
				scb->dev_channel, scb->dev_target));

			scp->result = (DID_RESET << 16);
			scp->scsi_done(scp);

			megaraid_dealloc_scb(adapter, scb);
		}
		spin_unlock_irqrestore(COMPLETED_LIST_LOCK(adapter), flags);

		spin_unlock(adapter->host_lock);

		// print a message once every 5 second only
		if (!(i % 5)) {
			con_log(CL_ANN, (
			"megaraid: Wait for %d commands to complete:%d\n",
				adapter->outstanding_cmds,
				MBOX_RESET_WAIT - i));
		}

		mdelay(1000);

		spin_lock(adapter->host_lock);
	}

	// If still outstanding commands, bail out
	if (adapter->outstanding_cmds) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: critical hardware error!\n"));

		raid_dev->hw_error = 1;

		return FAILED;
	}


	// If the controller supports clustering, reset reservations
	if (!adapter->ha) return SUCCESS;

	// clear reservations if any
	scb	= &adapter->iscb;
	ccb	= (mbox_ccb_t *)scb->ccb;

	ccb->raw_mbox[0] = CLUSTER_CMD;
	ccb->raw_mbox[2] = RESET_RESERVATIONS;

	rval = SUCCESS;
	if (mbox_post_sync_cmd_fast(adapter, ccb->raw_mbox) == 0) {
		con_log(CL_ANN,
			(KERN_INFO "megaraid: reservation reset\n"));
	}
	else {
		rval = FAILED;
		con_log(CL_ANN, (KERN_WARNING
				"megaraid: reservation reset failed\n"));
	}

	return rval;
}


/*
 * START: internal commands library
 *
 * This section of the driver has the common routine used by the driver and
 * also has all the FW routines
 */

/**
 * mbox_post_sync_cmd() - blocking command to the mailbox based controllers
 * @adapter	- controller's soft state
 * @raw_mbox	- the mailbox
 *
 * Issue a scb in synchronous and non-interrupt mode for mailbox based
 * controllers
 */
static int
mbox_post_sync_cmd(adapter_t *adapter, uint8_t raw_mbox[])
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox64_t	*mbox64;
	mbox_t		*mbox;
	uint8_t		status;
	long		i;


	mbox64	= raid_dev->mbox64;
	mbox	= raid_dev->mbox;

	/*
	 * Wait until mailbox is free
	 */
	if (megaraid_busywait_mbox(raid_dev) == MRAID_FAILURE)
		goto blocked_mailbox;

	/*
	 * Copy mailbox data into host structure
	 */
	memcpy((caddr_t)mbox, (caddr_t)raw_mbox, 16);
	mbox->cmdid		= 0xFE;
	mbox->busy		= 1;
	mbox->poll		= 0;
	mbox->ack		= 0;
	mbox->numstatus		= 0xFF;
	mbox->status		= 0xFF;

	wmb();
	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x1);

	// wait for maximum 1 second for status to post
	for (i = 0; i < 40000; i++) {
		if (mbox->numstatus != 0xFF) break;
		udelay(25); yield();
	}
	if (i == 40000) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid mailbox: status not available\n"));
		return -1;
	}

	// wait for maximum 1 second for poll semaphore
	for (i = 0; i < 40000; i++) {
		if (mbox->poll == 0x77) break;
		udelay(25); yield();
	}
	if (i == 40000) {
		con_log(CL_ANN, (KERN_WARNING
		"megaraid mailbox: could not get poll semaphore\n"));
		return -1;
	}

	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x2);
	wmb();

	// wait for maximum 1 second for acknowledgement
	for (i = 0; i < 40000; i++) {
		if ((RDINDOOR(raid_dev) & 0x2) == 0) {
			mbox->poll	= 0;
			mbox->ack	= 0x77;
			break;
		}
		udelay(25); yield();
	}
	if (i == 40000) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid mailbox: could not acknowledge\n"));
		return -1;
	}

	status = mbox->status;

	// invalidate the completed command id array. After command
	// completion, firmware would write the valid id.
	mbox->numstatus	= 0xFF;
	mbox->status	= 0xFF;
	for (i = 0; i < MBOX_MAX_FIRMWARE_STATUS; i++) {
		mbox->completed[i] = 0xFF;
	}

	return status;

blocked_mailbox:

	con_log(CL_ANN, (KERN_WARNING "megaraid: blocked mailbox\n") );
	return -1;
}


/**
 * mbox_post_sync_cmd_fast - blocking command to the mailbox based controllers
 * @adapter	- controller's soft state
 * @raw_mbox	- the mailbox
 *
 * Issue a scb in synchronous and non-interrupt mode for mailbox based
 * controllers. This is a faster version of the synchronous command and
 * therefore can be called in interrupt-context as well
 */
static int
mbox_post_sync_cmd_fast(adapter_t *adapter, uint8_t raw_mbox[])
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_t		*mbox;
	long		i;


	mbox	= raid_dev->mbox;

	// return immediately if the mailbox is busy
	if (mbox->busy) return MRAID_FAILURE;

	// Copy mailbox data into host structure
	memcpy((caddr_t)mbox, (caddr_t)raw_mbox, 16);
	mbox->cmdid		= 0xFE;
	mbox->busy		= 1;
	mbox->poll		= 0;
	mbox->ack		= 0;
	mbox->numstatus		= 0xFF;
	mbox->status		= 0xFF;

	wmb();
	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x1);

	for (i = 0; i < 0xFFFFF; i++) {
		if (mbox->numstatus != 0xFF) break;
	}

	if (i == 0xFFFFF) {
		// We may need to re-calibrate the counter
		con_log(CL_ANN, (KERN_CRIT
			"megaraid: fast sync command timed out\n"));
	}

	WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x2);
	wmb();

	return mbox->status;
}


/**
 * megaraid_busywait_mbox() - Wait until the controller's mailbox is available
 * @raid_dev	- RAID device (HBA) soft state
 *
 * wait until the controller's mailbox is available to accept more commands.
 */
static inline status_t
megaraid_busywait_mbox(mraid_device_t *raid_dev)
{
	mbox_t	*mbox = raid_dev->mbox;

	if (mbox->busy)
		return __megaraid_busywait_mbox(raid_dev);

	return MRAID_SUCCESS;
}


/**
 * __megaraid_busywait_mbox() - Wait until controller's mailbox is available
 * @raid_dev	- mailbox data structures
 *
 * wait until the controller's mailbox is available to accept more commands.
 */
static inline status_t
__megaraid_busywait_mbox(mraid_device_t *raid_dev)
{
	mbox_t		*mbox = raid_dev->mbox;
	unsigned long	counter;


	for (counter = 0; counter < 10000; counter++) {

		if (!mbox->busy) return MRAID_SUCCESS;

		udelay(100); yield();
	}
	return MRAID_FAILURE;	/* give up after 1 second */
}



/**
 * megaraid_mbox_product_info - some static information about the controller
 * @adapter	- our soft state
 *
 * issue commands to the controller to grab some parameters required by our
 * caller.
 */
static status_t
megaraid_mbox_product_info(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_ccb_t		*ccb;
	mbox_t			*mbox;
	mraid_pinfo_t		*pinfo;
	dma_addr_t		pinfo_dma_h;
	mraid_inquiry3_t	*mraid_inq3;
	scb_t			*scb;
	int			adapter_max_cmds;
	int			i;


	scb	= &adapter->iscb;
	ccb	= (mbox_ccb_t *)scb->ccb;

	/*
	 * Issue an ENQUIRY3 command to find out certain adapter parameters,
	 * e.g., max channels, max commands etc.
	 */
	pinfo = pci_alloc_consistent(adapter->pdev, sizeof(mraid_pinfo_t),
			&pinfo_dma_h);

	if (pinfo == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));

		return MRAID_FAILURE;
	}
	memset(pinfo, 0, sizeof(mraid_pinfo_t));

	mbox = ccb->mbox;

	mbox->xferaddr = (uint32_t)adapter->ibuf_dma_h;
	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	// NOTE: ccb->raw_mbox and ccb->mbox are same memory location
	ccb->raw_mbox[0] = FC_NEW_CONFIG;
	ccb->raw_mbox[2] = NC_SUBOP_ENQUIRY3;
	ccb->raw_mbox[3] = ENQ3_GET_SOLICITED_FULL;

	// Issue the command
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) != 0) {

		con_log(CL_ANN, (KERN_WARNING "megaraid: Inquiry3 failed\n"));

		pci_free_consistent(adapter->pdev, sizeof(mraid_pinfo_t),
			pinfo, pinfo_dma_h);

		return MRAID_FAILURE;
	}

	/*
	 * Collect information about state of each physical drive
	 * attached to the controller. We will expose all the disks
	 * which are not part of RAID
	 */
	mraid_inq3 = (mraid_inquiry3_t *)adapter->ibuf;
	for (i = 0; i < MBOX_MAX_PHYSICAL_DRIVES; i++) {
		raid_dev->pdrv_state[i] = mraid_inq3->pdrv_state[i];
	}

	/*
	 * Get product info for information like number of channels,
	 * maximum commands supported.
	 */
	memset((caddr_t)ccb->raw_mbox, 0, sizeof(ccb->raw_mbox));
	mbox->xferaddr = (uint32_t)pinfo_dma_h;

	ccb->raw_mbox[0] = FC_NEW_CONFIG;
	ccb->raw_mbox[2] = NC_SUBOP_PRODUCT_INFO;

	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) != 0) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: product info failed\n"));

		pci_free_consistent(adapter->pdev, sizeof(mraid_pinfo_t),
			pinfo, pinfo_dma_h);

		return MRAID_FAILURE;
	}

	/*
	 * Setup some parameters for host, as required by our caller
	 */
	adapter->max_channel = pinfo->nchannels;

	/*
	 * we will export all the logical drives on a single channel.
	 * Add 1 since inquires do not come for inititor ID
	 */
	adapter->max_target	= MAX_LOGICAL_DRIVES_40LD + 1;
	adapter->max_lun	= 8;	// up to 8 LUNs for non-disk devices

	/*
	 * Reserve one ID for internal commands. For commands issued
	 * internally, always use command ID 0.
	 */
	raid_dev->int_cmdid	= 0;
	adapter->iscb.sno	= 0;

	// maximum commands supported by the firmware
	if (pinfo->max_commands > MBOX_MAX_COMMANDS) {
		adapter_max_cmds = MBOX_MAX_COMMANDS;
	}
	else {
		adapter_max_cmds = pinfo->max_commands;
	}

	/*
	 * Populate the commands id pool with available command ids and
	 * invalidate rest of the entries.
	 * ID 0 is reserved for driver internal commands
	 */
	for (i = 0; i < adapter_max_cmds - 1; i++) {
		raid_dev->cmdid_instance[i].cmdid	= i + 1;
		raid_dev->cmdid_instance[i].scb		= NULL;
	}
	raid_dev->cmdid_index = i - 1;	// index of last available cmd id

	for ( ; i < MBOX_MAX_COMMANDS; i++) {	// invalidate rest of the slots
		raid_dev->cmdid_instance[i].cmdid	= -1;
		raid_dev->cmdid_instance[i].scb		= NULL;
	}

	/*
	 * Set the number of supported commands equal to that supported by the
	 * _driver_
	 */
	adapter->max_cmds	= megaraid_driver_cmds;

	memset(adapter->fw_version, 0, VERSION_SIZE);
	memset(adapter->bios_version, 0, VERSION_SIZE);

	memcpy(adapter->fw_version, pinfo->fw_version, 4);
	adapter->fw_version[4] = 0;

	memcpy(adapter->bios_version, pinfo->bios_version, 4);
	adapter->bios_version[4] = 0;

	pci_free_consistent(adapter->pdev, sizeof(mraid_pinfo_t), pinfo,
			pinfo_dma_h);

	return MRAID_SUCCESS;
}



/**
 * megaraid_mbox_extended_cdb - check for support for extended CDBs
 * @adapter	- soft state for the controller
 *
 * this routine check whether the controller in question supports extended
 * ( > 10 bytes ) CDBs
 */
static status_t
megaraid_mbox_extended_cdb(adapter_t *adapter)
{
	mbox_ccb_t	*ccb;
	mbox_t		*mbox;
	scb_t		*scb;
	status_t	rval;

	scb		= &adapter->iscb;
	ccb		= (mbox_ccb_t *)scb->ccb;
	mbox		= ccb->mbox;

	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));
	mbox->xferaddr	= (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	ccb->raw_mbox[0] = MAIN_MISC_OPCODE;
	ccb->raw_mbox[2] = SUPPORT_EXT_CDB;

	/*
	 * Issue the command
	 */
	rval = MRAID_SUCCESS;
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) != 0) {
		rval = MRAID_FAILURE;
	}

	return rval;
}


/**
 * megaraid_mbox_support_ha - Do we support clustering
 * @adapter	- soft state for the controller
 * @init_id	- ID of the initiator
 *
 * Determine if the firmware supports clustering and the ID of the initiator.
 */
static status_t
megaraid_mbox_support_ha(adapter_t *adapter, uint16_t *init_id)
{
	mbox_ccb_t	*ccb;
	mbox_t		*mbox;
	scb_t		*scb;
	status_t	rval;


	scb	= &adapter->iscb;
	ccb	= (mbox_ccb_t *)scb->ccb;
	mbox	= ccb->mbox;
	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));
	mbox->xferaddr = (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	ccb->raw_mbox[0] = GET_TARGET_ID;

	// Issue the command
	*init_id = 7;
	rval =  MRAID_FAILURE;
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {

		*init_id = *(uint8_t *)adapter->ibuf;

		con_log(CL_ANN, (KERN_INFO
			"megaraid: cluster firmware, initiator ID: %d\n",
			*init_id));

		rval =  MRAID_SUCCESS;
	}

	return rval;
}


/**
 * megaraid_mbox_support_random_del - Do we support random deletion
 * @adapter	- soft state for the controller
 *
 * Determine if the firmware supports random deletion
 * Return:	1 is operation supported, 0 otherwise
 */
static int
megaraid_mbox_support_random_del(adapter_t *adapter)
{
	mbox_ccb_t	*ccb;
	mbox_t		*mbox;
	scb_t		*scb;
	int		rval;


	scb	= &adapter->iscb;
	ccb	= (mbox_ccb_t *)scb->ccb;
	mbox	= ccb->mbox;
	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));

	ccb->raw_mbox[0] = FC_DEL_LOGDRV;
	ccb->raw_mbox[0] = OP_SUP_DEL_LOGDRV;

	// Issue the command
	rval = 0;
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {

		con_log(CL_DLEVEL1, ("megaraid: supports random deletion\n"));

		rval =  1;
	}

	return rval;
}


/**
 * megaraid_mbox_get_max_sg - maximum sg elements supported by the firmware
 * @adapter	- soft state for the controller
 *
 * Find out the maximum number of scatter-gather elements supported by the
 * firmware
 */
static int
megaraid_mbox_get_max_sg(adapter_t *adapter)
{
	mbox_ccb_t	*ccb;
	mbox_t		*mbox;
	scb_t		*scb;
	int		nsg;


	scb	= &adapter->iscb;
	ccb	= (mbox_ccb_t *)scb->ccb;
	mbox	= ccb->mbox;
	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));
	mbox->xferaddr = (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	ccb->raw_mbox[0] = MAIN_MISC_OPCODE;
	ccb->raw_mbox[2] = GET_MAX_SG_SUPPORT;

	// Issue the command
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {
		nsg =  *(uint8_t *)adapter->ibuf;
	}
	else {
		nsg =  MBOX_DEFAULT_SG_SIZE;
	}

	if (nsg > MBOX_MAX_SG_SIZE) nsg = MBOX_MAX_SG_SIZE;

	return nsg;
}


/**
 * megaraid_mbox_enum_raid_scsi - enumerate the RAID and SCSI channels
 * @adapter	- soft state for the controller
 *
 * Enumerate the RAID and SCSI channels for ROMB platoforms so that channels
 * can be exported as regular SCSI channels
 */
static void
megaraid_mbox_enum_raid_scsi(adapter_t *adapter)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_ccb_t		*ccb;
	mbox_t			*mbox;
	scb_t			*scb;


	scb	= &adapter->iscb;
	ccb	= (mbox_ccb_t *)scb->ccb;
	mbox	= ccb->mbox;
	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));
	mbox->xferaddr = (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	ccb->raw_mbox[0] = CHNL_CLASS;
	ccb->raw_mbox[2] = GET_CHNL_CLASS;

	// Issue the command. If the command fails, all channels are RAID
	// channels
	raid_dev->channel_class = 0xFF;
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {
		raid_dev->channel_class =  *(uint8_t *)adapter->ibuf;
	}

	return;
}


/**
 * megaraid_mbox_flush_cache - flush adapter and disks cache
 * @param adapter	: soft state for the controller
 *
 * Flush adapter cache followed by disks cache
 */
static void
megaraid_mbox_flush_cache(adapter_t *adapter)
{
	mbox_ccb_t	*ccb;
	mbox_t		*mbox;
	scb_t		*scb;


	scb	= &adapter->iscb;
	ccb	= (mbox_ccb_t *)scb->ccb;
	mbox	= ccb->mbox;
	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));

	ccb->raw_mbox[0] = FLUSH_ADAPTER;

	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) != 0) {
		con_log(CL_ANN, ("megaraid: flush adapter failed\n"));
	}

	ccb->raw_mbox[0] = FLUSH_SYSTEM;

	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) != 0) {
		con_log(CL_ANN, ("megaraid: flush disks cache failed\n"));
	}

	return;
}


/**
 * megaraid_mbox_display_scb - display SCB information, mostly debug purposes
 * @param adapter	: controllers' soft state
 * @param scb		: SCB to be displayed
 * @param level	: debug level for console print
 */
static void
megaraid_mbox_display_scb(adapter_t *adapter, scb_t *scb, int level)
{
	mbox_ccb_t		*ccb;
	struct scsi_cmnd	*scp;
	mbox_t			*mbox;
	int			i;


	ccb	= (mbox_ccb_t *)scb->ccb;
	scp	= scb->scp;
	mbox	= ccb->mbox;

	con_log(level, (KERN_NOTICE
		"megaraid mailbox: status:%#x cmd:%#x id:%#x ", scb->status,
		mbox->cmd, scb->sno));

	con_log(level, ("sec:%#x lba:%#x addr:%#x ld:%d sg:%d\n",
		mbox->numsectors, mbox->lba, mbox->xferaddr, mbox->logdrv,
		mbox->numsge));

	if (!scp) return;

	con_log(level, (KERN_NOTICE "scsi cmnd: "));

	for (i = 0; i < scp->cmd_len; i++) {
		con_log(level, ("%#2.02x ", scp->cmnd[i]));
	}

	con_log(level, ("\n"));

	return;
}


/*
 * END: internal commands library
 */

/*
 * START: Interface for the common management module
 *
 * This is the module, which interfaces with the common mangement module to
 * provide support for ioctl and sysfs
 */

/**
 * megaraid_cmm_register - register with the mangement module
 * @param adapter	: HBA soft state
 *
 * Register with the management module, which allows applications to issue
 * ioctl calls to the drivers. This interface is used by the management module
 * to setup sysfs support as well.
 */
static status_t
megaraid_cmm_register(adapter_t *adapter)
{
	mraid_mmadp_t	adp;

	adp.unique_id		= adapter->unique_id;
	adp.drvr_type		= DRVRTYPE_MBOX;
	adp.drvr_data		= (unsigned long)adapter;
	adp.pdev		= adapter->pdev;
	adp.issue_uioc		= megaraid_mbox_mm_cmd;
	adp.timeout		= 30;

	mraid_mm_register_adp(&adp); //FIXME: Check for error messages

	return MRAID_SUCCESS;
}


/**
 * megaraid_cmm_unregister - un-register with the mangement module
 * @param adapter	: HBA soft state
 *
 * Un-register with the management module.
 * FIXME: mgmt module must return failure for unregister if it has pending
 * commands in LLD
 */
static status_t
megaraid_cmm_unregister(adapter_t *adapter)
{
	mraid_mm_unregister_adp(adapter->unique_id);
	return MRAID_SUCCESS;
}


/**
 * megaraid_mbox_mm_cmd - interface for CMM to issue commands to LLD
 * @param drvr_data	: LLD specific data
 * @param kioc		: CMM interface packet
 * @param action	: command action
 *
 * This routine is invoked whenever the Common Mangement Module (CMM) has a
 * command for us. The 'action' parameter specifies if this is a new command
 * or otherwise.
 */
static int
megaraid_mbox_mm_cmd(unsigned long drvr_data, uioc_t *kioc, uint32_t action)
{
	adapter_t	*adp;

	if (!kioc || !drvr_data)
		return (-EINVAL);

	if (action != IOCTL_ISSUE) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: unsupported management action:%#2x\n",
			action));
		return LC_ENOTSUPP;
	}

	/*
	 * Check if drvr_data points to a valid adapter
	 */
	adp = (adapter_t *)drvr_data;

	// make sure this adapter is not being detached right now.
	if (atomic_read(&adp->being_detached)) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: reject management request, detaching\n"));
		return (-ENODEV);
	}

	switch (kioc->opcode) {

	case GET_ADAP_INFO:

		kioc->status =  gather_hbainfo(adp, (mraid_hba_info_t *)
					(unsigned long)kioc->cmdbuf);

		kioc->done(kioc);

		return kioc->status;

	case MBOX_CMD:

		return megaraid_mbox_internal_command(adp, kioc);

	default:
		return (-EINVAL);
	}

	return 0;
}

/**
 * megaraid_mbox_internal_command - issues commands routed through CMM
 * @param adapter	: HBA soft state
 * @param kioc		: management command packet
 *
 * Issues commands, which are routed through the management module.
 */
static int
megaraid_mbox_internal_command(adapter_t *adapter, uioc_t *kioc)
{
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	mbox64_t		*mbox64;
	scb_t			*scb;
	mbox_ccb_t		*ccb;
	Scsi_Cmnd		*scmd;
	uint8_t*		raw_mbox;
	unsigned long		flags = 0;

	/*
	 * The internal commands share one command id and hence are serialized.
	 * This is so because we want to reserve maximum number of available
	 * command ids for the I/O commands.
	 * The mutex is released when the command is completed by the
	 * firmware.
	 */
	down(&adapter->imtx);

	mbox64		= (mbox64_t *)(unsigned long)kioc->cmdbuf;
	raw_mbox	= (uint8_t*) &mbox64->mbox32;
	scb		= &adapter->iscb;
	ccb		= (mbox_ccb_t *)scb->ccb;
	scmd		= &adapter->isc;

	memset(scmd, 0, sizeof(Scsi_Cmnd));

	scmd->device	= &adapter->isdev;
	SCP2HOST(scmd)	= adapter->host;
	scmd->buffer	= (void *)scb;
	scmd->cmnd[0]	= MRAID_INTERNAL_COMMAND;

	scb->state	= SCB_ACTIVE;
	scb->scp	= scmd;

	memcpy(ccb->mbox64, mbox64, sizeof(mbox64_t));

	scb->gp		= (unsigned long)kioc;
	scmd->state	= 0;
	scb->sno	= raid_dev->int_cmdid;

	/*
	 * If it is a logdrv random delete operation, we have to wait till
	 * there are no outstanding cmds at the fw and then issue it directly
	 */
	if (raw_mbox[0] == FC_DEL_LOGDRV && raw_mbox[2] == OP_DEL_LOGDRV) {

		if (wait_till_fw_empty(adapter))
			return (-ETIME);

		INIT_LIST_HEAD(&scb->list);
		scmd->scsi_done	= megaraid_mbox_internal_done;

		if (mbox_post_cmd(adapter, scb) != MRAID_SUCCESS)
			return (-EBUSY);

		return 0;
	}

	spin_lock_irqsave(adapter->host_lock, flags);
	megaraid_queue_command(scmd, megaraid_mbox_internal_done);
	spin_unlock_irqrestore(adapter->host_lock, flags);

	return 0;
}

static int
wait_till_fw_empty(adapter_t *adapter)
{
	unsigned long	flags = 0;
	int		i;

	DECLARE_WAIT_QUEUE_HEAD(wq);

	/*
	 * Set the quiescent flag to stop issuing cmds to FW.
	 */
	spin_lock_irqsave(adapter->host_lock, flags);
	adapter->quiescent = MRAID_TRUE;
	spin_unlock_irqrestore(adapter->host_lock, flags);

	/*
	 * Wait till there are no more cmds outstanding at FW. Try for at most
	 * 60 seconds
	 */
	for (i = 0; i < 60 && adapter->outstanding_cmds; i++) {
		con_log(CL_DLEVEL1, (KERN_INFO
			"megaraid: FW has %d pending commands\n",
			adapter->outstanding_cmds));
		mraid_sleep(1);
	}

	return adapter->outstanding_cmds;
}


/**
 * megaraid_mbox_internal_done()
 * @scmd - internal scsi command
 *
 * Callback routine for internal commands originated from the management
 * module. Also, release the adapter mutex used to serialize access to
 * internal command index.
 *
 * NOTES: This is called with host_lock held
 */
static void
megaraid_mbox_internal_done(Scsi_Cmnd *scmd)
{
	adapter_t		*adapter = SCP2ADAPTER(scmd);
	scb_t			*scb;
	uioc_t			*kioc;
	mbox64_t		*mbox64;
	uint8_t			*raw_mbox;

	scb			= (scb_t *)scmd->buffer;
	kioc			= (uioc_t *)scb->gp;
	kioc->status		= LC_SUCCESS;
	mbox64			= (mbox64_t *)(unsigned long)kioc->cmdbuf;
	mbox64->mbox32.status	= scmd->result;
	raw_mbox		= (uint8_t*) &mbox64->mbox32;


	if (raw_mbox[0] == FC_DEL_LOGDRV && raw_mbox[2] == OP_DEL_LOGDRV
							&& !scmd->result) {
		/*
		 * It was a del logdrv command and it succeeded
		 */
		adapter->quiescent = MRAID_FALSE;
		megaraid_mbox_runpendq( adapter );
	}

	up(&adapter->imtx);	// ready for the next command

	kioc->done(kioc);

	return;
}


/**
 * gather_hbainfo - HBA characteristics for the applications
 * @param adapter	: HBA soft state
 * @param hinfo		: pointer to the caller's host info strucuture
 */
static int
gather_hbainfo(adapter_t *adapter, mraid_hba_info_t *hinfo)
{
	hinfo->pci_vendor_id	= adapter->pdev->vendor;
	hinfo->pci_device_id	= adapter->pdev->device;
	hinfo->subsys_vendor_id	= adapter->pdev->subsystem_vendor;
	hinfo->subsys_device_id	= adapter->pdev->subsystem_device;

	hinfo->pci_bus		= adapter->pdev->bus->number;
	hinfo->pci_dev_fn	= adapter->pdev->devfn;
	hinfo->pci_slot		= PCI_SLOT(adapter->pdev->devfn);
	hinfo->irq		= adapter->host->irq;
	hinfo->baseport		= ADAP2RAIDDEV(adapter)->baseport;

	hinfo->unique_id	= (hinfo->pci_bus << 8) | adapter->pdev->devfn;
	hinfo->host_no		= adapter->host->host_no;

	return 0;
}

/*
 * END: Interface for the common management module
 */


/*
 * END: Mailbox Low Level Driver
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_init(megaraid_init);
module_exit(megaraid_exit);
#else
static Scsi_Host_Template driver_template = MRAID_TEMPLATE;
#include "../scsi_module.c"
#endif

/* vim: set ts=8 sw=8 tw=78 ai si: */
