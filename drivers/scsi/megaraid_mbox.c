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
 * Version	: v2.20.0 (Apr 14 2004)
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

static status_t megaraid_io_attach(adapter_t *);
static void megaraid_io_detach(adapter_t *);

static status_t megaraid_setup_dma(adapter_t *);

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
static status_t megaraid_mbox_support_ha(adapter_t *, uint16_t *);
static status_t megaraid_mbox_boot_enabled(adapter_t *);
static void megaraid_mbox_get_boot_dev(adapter_t *);
static int megaraid_mbox_get_max_sg(adapter_t *);

static void megaraid_mbox_display_scb(adapter_t *, scb_t *, int);

static int megaraid_queue_command(struct scsi_cmnd *,
		void (*)(struct scsi_cmnd *));

static const char *megaraid_info(struct Scsi_Host *);

static inline scb_t *megaraid_mbox_build_cmd(adapter_t *, Scsi_Cmnd *, int *);
static inline void megaraid_mbox_prepare_pthru(adapter_t *, scb_t *,
		Scsi_Cmnd *);
static inline void megaraid_mbox_prepare_epthru(adapter_t *, scb_t *,
		Scsi_Cmnd *);
static inline int megaraid_mbox_mksgl(adapter_t *, scb_t *, uint32_t *,
		uint32_t *);

static inline void megaraid_mbox_runpendq(adapter_t *);
static inline status_t mbox_post_cmd(adapter_t *, scb_t *);

static void megaraid_mbox_dpc(unsigned long);
static inline void megaraid_mbox_free_scb(adapter_t *, scb_t *);

static irqreturn_t megaraid_isr_memmapped(int, void *, struct pt_regs *);
static irqreturn_t megaraid_isr_iomapped(int, void *, struct pt_regs *);

static inline bool_t megaraid_memmbox_ack_sequence(adapter_t *);
static inline bool_t megaraid_iombox_ack_sequence(adapter_t *);

static inline status_t megaraid_busywait_mbox(mraid_device_t *);
static inline status_t __megaraid_busywait_mbox(mraid_device_t *);

static status_t megaraid_cmm_register(adapter_t *);
static status_t megaraid_cmm_unregister(adapter_t *);
static int megaraid_mbox_mm_cmd(ulong, uioc_t*, uint32_t);
static int megaraid_mbox_internal_command(adapter_t *, uioc_t *);
static void megaraid_mbox_internal_done(Scsi_Cmnd *);
static int gather_hbainfo(adapter_t *, mraid_hba_info_t *);
static void megaraid_update_logdrv_ids(adapter_t*);
static int wait_till_fw_empty(adapter_t*);



MODULE_AUTHOR("LSI Logic Corporation");
MODULE_DESCRIPTION("LSI Logic MegaRAID unified driver");
MODULE_LICENSE("GPL");

/*
 * ### modules parameters for driver ###
 */

/*
 * Set to enable driver to expose unconfigured disk to kernel
 */
static int expose_unconf_disks = 0;
MODULE_PARM(expose_unconf_disks, "i");
MODULE_PARM_DESC(expose_unconf_disks,
	"Set to expose unconfigured disks to kernel (default=0)");

/**
 * driver wait time if the adapter's mailbox is busy
 */
static unsigned int max_mbox_busy_wait = MBOX_BUSY_WAIT;
MODULE_PARM(max_mbox_busy_wait, "i");
MODULE_PARM_DESC(max_mbox_busy_wait,
	"Max wait for mailbox in microseconds if busy (default=10)");

/**
 * number of sectors per IO command
 */
static unsigned int megaraid_max_sectors = MBOX_MAX_SECTORS;
MODULE_PARM(megaraid_max_sectors, "i");
MODULE_PARM_DESC(megaraid_max_sectors,
	"Maximum number of sectors per IO command (default=128)");

/**
 * number of commands supported by the driver
 */
static unsigned int megaraid_driver_cmds = MBOX_MAX_DRIVER_CMDS;
MODULE_PARM(megaraid_driver_cmds, "i");
MODULE_PARM_DESC(megaraid_driver_cmds,
	"Maximum number of commands supported by the driver (default=190)");


/**
 * number of commands per logical unit
 */
static unsigned int megaraid_cmd_per_lun = MBOX_MAX_DRIVER_CMDS;
MODULE_PARM(megaraid_cmd_per_lun, "i");
MODULE_PARM_DESC(megaraid_cmd_per_lun,
	"Maximum number of commands per logical unit (default=190)");


/**
 * Fast driver load option, skip scanning for physical devices during load.
 * This would result in non-disk devices being skipped during driver load
 * time. These can be later added though, using /proc/scsi/scsi
 */
static unsigned int megaraid_fast_load = 0;
MODULE_PARM(megaraid_fast_load, "i");
MODULE_PARM_DESC(megaraid_fast_load,
	"Faster loading of the driver, skips physical devices! (default=0)");


/*
 * ### global data ###
 */
mraid_driver_t mraid_driver_g = {
	.driver_version	= { 0x20, 0x01, 0x02, 0x00, 1, 19, 20, 4},
};


/*
 * PCI table for all supported controllers.
 */
static struct pci_device_id pci_id_table_g[] =  {
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_MEGARAID_SATA_PCIX,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= MBOX_FLAGS_64BIT
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4E_SI_DI,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= MBOX_FLAGS_64BIT
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_PERC4E_DC_SC,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= MBOX_FLAGS_64BIT
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_DISCOVERY,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_DELL,
		.device		= PCI_DEVICE_ID_PERC4_DI,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= MBOX_FLAGS_64BIT
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_PERC4_QC_VERDE,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= MBOX_FLAGS_64BIT
	},
	{
		.vendor		= PCI_VENDOR_ID_AMI,
		.device		= PCI_DEVICE_ID_AMI_MEGARAID,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_AMI,
		.device		= PCI_DEVICE_ID_AMI_MEGARAID2,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_AMI,
		.device		= PCI_DEVICE_ID_AMI_MEGARAID3,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_INTEL,
		.device		= PCI_DEVICE_ID_AMI_MEGARAID3,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{
		.vendor		= PCI_VENDOR_ID_LSI_LOGIC,
		.device		= PCI_DEVICE_ID_AMI_MEGARAID3,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_STORAGE_RAID << 8,
		.class_mask	= 0xFFFF00,
		.driver_data	= 0
	},
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, pci_id_table_g);


// lk 2.6 TODO: Add the shutdown routine
static struct pci_driver megaraid_pci_driver_g = {
	.name		= "megaraid",
	.id_table	= pci_id_table_g,
	.probe		= megaraid_probe_one,
	.remove		= __devexit_p(megaraid_detach_one),
#if 0
	.driver		= {
		.shutdown = mraid_shutdown
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

	adapter = SCSIHOST2ADAP(host);

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
/*
 * ### END: LK 2.4 compatibility layer ###
 */

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
	con_log(CL_ANN, (KERN_INFO "megaraid: %s\n", MEGARAID_VERSION));

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

	// FIXME
	SET_PRV_INTF_AVAILABLE();

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

	SET_PRV_INTF_UNAVAILABLE();

	// unregister as PCI hotplug driver
	pci_unregister_driver(&megaraid_pci_driver_g);

	// All adapters must be detached by now
	try_assertion {
		ASSERT(list_empty(&mraid_driver_g.device_list));
	}
	catch_assertion {
		con_log(CL_ANN, (KERN_CRIT
		"megaraid panic: not all adapters are released\n"));

		BUG();
	}
	end_assertion

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
	adapter_t	*adapter;
	status_t	rval;
	uint16_t	subsysvid;
	uint32_t	unique_id;
	int		i;


	// Make sure this adapter is not already setup
	unique_id	= pdev->bus->number << 8 | pdev->devfn;

	for (i = 0; i < mraid_driver_g.attach_count; i++) {
		if (mraid_driver_g.adapter[i]->unique_id == unique_id) {

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

	/*
	 * If we do not find a valid subsys vendor id, refuse to load the
	 * driver.
	 */
	subsysvid = pdev->subsystem_vendor;

	if (subsysvid && (subsysvid != PCI_VENDOR_ID_AMI) &&
			(subsysvid != PCI_VENDOR_ID_DELL) &&
			(subsysvid != PCI_VENDOR_ID_HP) &&
			(subsysvid != PCI_VENDOR_ID_INTEL) &&
			(subsysvid != PCI_SUBSYS_ID_FSC) &&
			(subsysvid != PCI_VENDOR_ID_LSI_LOGIC)) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: not loading for subsysvid:%#4.04x\n",
			subsysvid));

		return -ENODEV;
	}

	if (pci_enable_device(pdev)) {
		con_log(CL_ANN, (KERN_WARNING
				"megaraid: pci_enable_device failed.\n"));
		return -ENODEV;
	}

	// Enable bus-mastering on this controller
	// FIXME: should this be done in adapter specific code?
	pci_set_master(pdev);

	/*
	 * Allocate the per driver initialization structure
	 */
	adapter = kmalloc(sizeof(adapter_t), GFP_KERNEL);

	if (adapter == NULL) {
		con_log(CL_ANN, (KERN_WARNING
		"megaraid: out of memory, %s %d.\n", __FUNCTION__, __LINE__));

		pci_disable_device(pdev);

		return -ENODEV;
	}
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

		kfree(adapter);

		pci_disable_device(pdev);

		return -ENODEV;
	}


	// Initialize the synchronization lock for kernel and LLD
	spin_lock_init(&adapter->lock);
	adapter->host_lock = &adapter->lock;


	// Setup resources to issue internal commands with interrupts availble
	init_MUTEX(&adapter->imtx);
	init_waitqueue_head(&adapter->iwq);


	// Initialize the command queues: the list of free SCBs and the list
	// of pending SCBs.
	INIT_LIST_HEAD(&adapter->scb_pool);
	INIT_LIST_HEAD(&adapter->pend_list);
	INIT_LIST_HEAD(&adapter->completed_list);


	// Start the mailbox based controller
	if (megaraid_init_mbox(adapter) != MRAID_SUCCESS) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: maibox adapter did not initialize\n"));

		kfree(adapter);

		pci_disable_device(pdev);

		return -ENODEV;
	}

	// attach with scsi mid-layer
	rval = megaraid_io_attach(adapter);

	if (rval != MRAID_SUCCESS) {

		megaraid_fini_mbox(adapter);

		kfree(adapter);

		pci_disable_device(pdev);

		return -ENODEV;
	}

	/*
	 * We maintain an index of each adapter's soft state for application
	 * access path
	 */
	for (i = 0; i < MAX_CONTROLLERS; i++) {
		if (mraid_driver_g.adapter[i] == NULL) {
			mraid_driver_g.adapter[i] = adapter;
			adapter->slot = i;
			break;
		}
	}
	if (i == MAX_CONTROLLERS) {	// paranoia :-)
		con_log(CL_ANN, (KERN_WARNING "megaraid: too many HBAs.\n"));

		megaraid_fini_mbox(adapter);

		kfree(adapter);

		pci_disable_device(pdev);

		return -ENODEV;
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


	// Make sure we are unloading the right adapter
	if (mraid_driver_g.adapter[adapter->slot] != adapter) {
		con_log(CL_ANN, (KERN_CRIT
			"megaraid: invalid device handle:%s:%d.\n",
			__FILE__, __LINE__));
		BUG();
	}
	mraid_driver_g.adapter[adapter->slot] = NULL;

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
	host = mraid_scsi_host_alloc(megaraid_template_gp, 8);
	if (!host) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: scsi_register failed.\n"));

		return MRAID_FAILURE;
	}

	SCSIHOST2ADAP(host)	= adapter;
	adapter->host		= host;

	// export the parameters required by the mid-layer
	mraid_set_host_lock(host, adapter->host_lock);
	mraid_scsi_set_pdev(host, adapter->pdev);

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

		mraid_scsi_host_dealloc(host);

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

	mraid_scsi_host_dealloc(host);

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


/**
 * megaraid_setup_dma - setup appropriate DMA masks
 * @param adapter	: controller's soft state
 *
 * This routine would setup the appropriate DMA masks. This would be called
 * after the controller's capabilities have been determined
 *
 * Retrun:	MRAID_SUCCESS, if some DMA mask was set
 *		MRAID_FAILURE, if DMA mask could not be set
 */
static status_t
megaraid_setup_dma(adapter_t *adapter)
{
	/*
	 * Set the mask to do DMA in 64-bit range if supported by firmware and
	 * kernel
	 */
	adapter->highmem_dma = MRAID_FALSE;
	if ((adapter->flags & MRAID_DMA_64) && (sizeof(dma_addr_t) == 8)) {

		if (pci_set_dma_mask(adapter->pdev, 0xFFFFFFFFFFFFFFFFULL)
				!= 0) {

			con_log(CL_ANN, (KERN_WARNING
			"megaraid: could not set DMA mask for 64-bit.\n"));

			adapter->highmem_dma = MRAID_FALSE;
		}
		else {
			adapter->highmem_dma = MRAID_TRUE;
		}
	}
	if (adapter->highmem_dma == MRAID_FALSE) {
		if (pci_set_dma_mask(adapter->pdev, 0xFFFFFFFF) != 0) {

			con_log(CL_ANN, (KERN_WARNING
				"megaraid: pci_set_dma_mask failed:%d\n",
				__LINE__));

			return MRAID_FAILURE;
		}
		con_log(CL_DLEVEL1, (KERN_NOTICE
			"megaraid: not supporting 64-bit addressing [%d:%d]\n",
			adapter->highmem_dma, (int)sizeof(dma_addr_t)));
	}
	else {
		con_log(CL_DLEVEL1, (KERN_NOTICE
			"megaraid: supporting 64-bit addressing [%d:%d]\n",
			adapter->highmem_dma, (int)sizeof(dma_addr_t)));
	}

	return MRAID_SUCCESS;
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
	uint32_t		magic64;
	bool_t			mem_region_f	= MRAID_FALSE;
	bool_t			alloc_cmds_f	= MRAID_FALSE;
	bool_t			irq_f		= MRAID_FALSE;


	adapter->quiescent	= MRAID_FALSE;
	adapter->ito		= MBOX_TIMEOUT;

	pdev = adapter->pdev;

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
	adapter->raid_device = (caddr_t)raid_dev;


	// our baseport
	raid_dev->baseport = pci_resource_start(pdev, 0);

	/*
	 * Check for IO mapped controller
	 */
	if (pci_resource_flags(pdev, 0) & IORESOURCE_IO) {

		adapter->flags |= MRAID_BOARD_IOMAP;

		raid_dev->baseaddr = raid_dev->baseport + 0x10;

		if (!request_region(raid_dev->baseaddr, 16, "megaraid")) {
			con_log(CL_ANN, (KERN_WARNING
				"megaraid: could not claim IO region\n") );
			goto fail_init;
		}

		outb_p(ENABLE_MBOX_BYTE, raid_dev->baseport +
				ENABLE_MBOX_REGION);

		iombox_irq_ack(raid_dev);

		iombox_irq_enable(raid_dev);

	}
	else {

		/*
		 * Memory mapped controllers.
		 */
		adapter->flags |= MRAID_BOARD_MEMMAP;

		if (!request_mem_region(raid_dev->baseport, 128,
				"MegaRAID: LSI Logic Corporation")) {

			con_log(CL_ANN, (KERN_WARNING
					"megaraid: mem region busy\n"));

			goto fail_init;
		}

		raid_dev->baseaddr = (unsigned long)
				ioremap_nocache(raid_dev->baseport, 128);

		if (!raid_dev->baseaddr) {

			con_log(CL_ANN, (KERN_WARNING
				"megaraid: could not map hba memory\n") );

			release_mem_region(raid_dev->baseport, 128);

			goto fail_init;
		}
	}
	mem_region_f = MRAID_TRUE;

	/*
	 * Does this firmware support DMA in high memory
	 *
	 * For these vendor and device ids, signature offsets are not valid
	 * and 64-bit is implicit
	 */
	if (adapter->pci_id->driver_data & MBOX_FLAGS_64BIT) {
		adapter->flags |= MRAID_DMA_64;
	}
	else {
		pci_read_config_dword(pdev, SIGN_OFFSET_64BIT, &magic64);

		if (magic64 == HBA_SIGNATURE_64BIT)
			adapter->flags |= MRAID_DMA_64;
	}

	//
	// Setup the rest of the soft state using the library of FW routines
	//

	// request IRQ and register the interrupt service routine
	if (request_irq(adapter->irq, (adapter->flags & MRAID_BOARD_MEMMAP) ?
		megaraid_isr_memmapped : megaraid_isr_iomapped, SA_SHIRQ,
		"megaraid", adapter)) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid: Couldn't register IRQ %d!\n", adapter->irq));

		goto fail_init;
	}

	irq_f = MRAID_TRUE;


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
	 * Check if the user has enabled BIOS on this controller.
	 */
	if (megaraid_mbox_boot_enabled(adapter) == MRAID_SUCCESS) {
		/*
		 * Find out our boot device. This routine will also setup the
		 * appropriate meta data for us.
		 */
		megaraid_mbox_get_boot_dev(adapter);
	}

	/*
	 * find out the maximum number of scatter-gather elements supported by
	 * this firmware
	 */
	adapter->sglen = megaraid_mbox_get_max_sg(adapter);

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

	if (megaraid_setup_dma(adapter) != MRAID_SUCCESS) {
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
		if (adapter->flags & MRAID_BOARD_MEMMAP) {
			iounmap((caddr_t)raid_dev->baseaddr);
			release_mem_region(raid_dev->baseport, 128);
		}
		else {
			release_region(raid_dev->baseaddr, 16);
		}
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

	megaraid_free_cmd_packets(adapter);

	free_irq(adapter->irq, adapter);

	if (adapter->flags & MRAID_BOARD_MEMMAP) {

		iounmap((caddr_t)raid_dev->baseaddr);

		release_mem_region(raid_dev->baseport, 128);
	}
	else {
		release_region(raid_dev->baseaddr, 16);
	}

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
	struct mraid_pci_blk	*pthru_pci_blk;
	struct mraid_pci_blk	*epthru_pci_blk;
	struct mraid_pci_blk	*sg_pci_blk;
	struct mraid_pci_blk	*mbox_pci_blk;
	bool_t			alloc_ibuf_f 		= MRAID_FALSE;
	bool_t			alloc_common_mbox_f 	= MRAID_FALSE;
	bool_t			alloc_int_ccb_f 	= MRAID_FALSE;
	bool_t			alloc_int_ccb_pthru_f	= MRAID_FALSE;
	bool_t			alloc_scb_f		= MRAID_FALSE;
	bool_t			alloc_mbox_f		= MRAID_FALSE;
	bool_t			alloc_pthru_f		= MRAID_FALSE;
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
	 * Check for IO mapped controller and register mailbox
	 */
	if (pci_resource_flags(adapter->pdev, 0) & IORESOURCE_IO) {
		/*
		 * Let firmware know the mailbox address
		 */
		outb_p(raid_dev->mbox_dma & 0xFF, raid_dev->baseport +
				MBOX_PORT0);

		outb_p((raid_dev->mbox_dma >> 8) & 0xFF,
				raid_dev->baseport + MBOX_PORT1);

		outb_p((raid_dev->mbox_dma >> 16) & 0xFF,
				raid_dev->baseport + MBOX_PORT2);

		outb_p((raid_dev->mbox_dma >> 24) & 0xFF,
				raid_dev->baseport + MBOX_PORT3);
	}

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
	 */
	raid_dev->pthru_pool_handle = mraid_pci_blk_pool_create(adapter->pdev,
			megaraid_driver_cmds, sizeof(mraid_passthru_t), 128,
			raid_dev->pthru_pool);

	if (raid_dev->pthru_pool_handle == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		goto fail_alloc_cmds;
	}
	alloc_pthru_f = MRAID_TRUE;

	/*
	 * Allocate memory for each embedded extended passthru strucuture
	 * pointer. Request for a 128 bytes aligned structure for each
	 * extended passthru command structure
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
	pthru_pci_blk	= raid_dev->pthru_pool;
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

		ccb->pthru	= (mraid_passthru_t *)pthru_pci_blk[i].vaddr;
		ccb->pthru_dma_h	= pthru_pci_blk[i].dma_addr;

		ccb->epthru	= (mraid_epassthru_t *)epthru_pci_blk[i].vaddr;
		ccb->epthru_dma_h	= epthru_pci_blk[i].dma_addr;

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

		/*
		 * put scb in the free pool.
		 */
		mraid_add_scb_to_pool(adapter, scb);
	}

	return MRAID_SUCCESS;

fail_alloc_cmds:
	if (alloc_sg_pool_f == MRAID_TRUE) {
		mraid_pci_blk_pool_destroy(raid_dev->sg_pool_handle);
	}
	if (alloc_epthru_f == MRAID_TRUE) {
		mraid_pci_blk_pool_destroy(raid_dev->epthru_pool_handle);
	}
	if (alloc_pthru_f == MRAID_TRUE) {
		mraid_pci_blk_pool_destroy(raid_dev->pthru_pool_handle);
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
	mraid_pci_blk_pool_destroy(raid_dev->pthru_pool_handle);
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

	adapter		= SCP2ADAPTER(scp);
	scp->scsi_done	= done;
	scp->result	= 0;

	/*
	 * Allocate and build a SCB request
	 * if_busy flag will be set if megaraid_mbox_build_cmd() command could
	 * not allocate scb. We will return non-zero status in that case.
	 * NOTE: scb can be null even though certain commands completed
	 * successfully, e.g., MODE_SENSE and TEST_UNIT_READY, it would
	 * return 0 in that case.
	 */
	if_busy	= 0;
	scb	= megaraid_mbox_build_cmd(adapter, scp, &if_busy);

	if (scb) {
		scb->state |= SCB_PENDQ;
		list_add_tail(&scb->list, &adapter->pend_list);

		/*
		 * Check if the HBA is in quiescent state, e.g., during a
		 * delete logical drive opertion. If it is, don't run the
		 * pending list.
		 */
		if (adapter->quiescent == MRAID_FALSE) {
			megaraid_mbox_runpendq(adapter);
		}
#if 0
		/*
		 * Perform a preemptive interrupt acknowledgement sequence.
		 * This should reduce the number of interrupts generated to
		 * the OS and boosting performance.
		 * TODO: compare results w/ and w/o this.
		 */
		if (likely(adapter->flags & MRAID_BOARD_MEMMAP)) {
			megaraid_memmbox_ack_sequence(adapter);
		}
		else {
			megaraid_iombox_ack_sequence(adapter);
		}

		tasklet_schedule(&adapter->dpc_h);	// schedule the DPC
#endif
		return 0;
	}

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
	bool_t			islogical;
	mbox_ccb_t		*ccb;
	mraid_passthru_t	*pthru;
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
	if (islogical == MRAID_TRUE) {
		switch (scp->cmnd[0]) {
		case TEST_UNIT_READY:
			/*
			 * Do we support clustering and is the support enabled
			 * If no, return success always
			 */
			if (adapter->ha != MRAID_TRUE) {
				scp->result = (DID_OK << 16);
				scp->scsi_done(scp);
				return NULL;
			}

			if (!(scb = mraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				scp->scsi_done(scp);
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
			scp->scsi_done(scp);
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
				scp->scsi_done(scp);
				return NULL;
			}
			if ((target % 0x80) >= MAX_LOGICAL_DRIVES_40LD) {
				scp->result = (DID_BAD_TARGET << 16);
				scp->scsi_done(scp);
				return NULL;
			}


			/* Allocate a SCB and initialize passthru */
			if (!(scb = mraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				scp->scsi_done(scp);
				*busy = 1;
				return NULL;
			}

			ccb			= (mbox_ccb_t *)scb->ccb;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			pthru			= ccb->pthru;
			mbox			= ccb->mbox;

			pthru->timeout		= 0;
			pthru->ars		= 1;
			pthru->reqsenselen	= 14;
			pthru->islogical	= 1;
			pthru->logdrv		= target;
			pthru->cdblen		= scp->cmd_len;
			memcpy(pthru->cdb, scp->cmnd, scp->cmd_len);

			/*
			 * If 64-bit dma is supported, issue the appropriate
			 * mailbox command.
			 */
			if (adapter->highmem_dma == MRAID_TRUE) {
				mbox->cmd = MBOXCMD_PASSTHRU64;
			}
			else {
				mbox->cmd = MBOXCMD_PASSTHRU;
			}
			scb->dma_direction = PCI_DIR(scp);

			pthru->numsge = megaraid_mbox_mksgl(adapter, scb,
				&pthru->dataxferaddr, &pthru->dataxferlen);

			mbox->xferaddr = ccb->pthru_dma_h;

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
			if (!(scb = mraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				scp->scsi_done(scp);
				*busy = 1;
				return NULL;
			}
			ccb			= (mbox_ccb_t *)scb->ccb;
			scb->dev_channel	= 0xFF;
			scb->dev_target		= target;
			mbox			= ccb->mbox;
			mbox->logdrv		= target;

			/*
			 * A little hack: 2nd bit is zero for all scsi read
			 * commands and is set for all scsi write commands
			 */
			if (adapter->highmem_dma == MRAID_TRUE) {
				mbox->cmd = (scp->cmnd[0] & 0x02) ?
					MBOXCMD_LWRITE64:
					MBOXCMD_LREAD64 ;
			}
			else {
				mbox->cmd = (scp->cmnd[0] & 0x02) ?
					MBOXCMD_LWRITE:
					MBOXCMD_LREAD ;
			}

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

				scp->result = (DID_ERROR << 16);
				scp->scsi_done(scp);
				return NULL;
			}



			scb->dma_direction = PCI_DIR(scp);

			/*
			 * Calculate Scatter-Gather info
			 */
			mbox->numsge = megaraid_mbox_mksgl(adapter, scb,
					(uint32_t *)&mbox->xferaddr, &xferlen);

#ifdef MRAID_HAVE_STATS
			/*
			 * keep counter of number of IO commands coming down.
			 * Some applications are interested in this data
			 */
			if (scp->cmnd[0] & 0x02) {	/* read commands */
				adapter->nreads[target % 0x80]++;
				adapter->nreadblocks[target % 0x80] +=
					mbox->numsectors;
			}
			else {	/* write commands */
				adapter->nwrites[target % 0x80]++;
				adapter->nwriteblocks[target % 0x80] +=
					mbox->numsectors;
			}
#endif

			return scb;

		case RESERVE:
		case RELEASE:
			/*
			 * Do we support clustering and is the support enabled
			 */
			if (!adapter->ha) {
				scp->result = (DID_BAD_TARGET << 16);
				scp->scsi_done(scp);
				return NULL;
			}

			/*
			 * Allocate a SCB and initialize mailbox
			 */
			if (!(scb = mraid_alloc_scb(adapter, scp))) {
				scp->result = (DID_ERROR << 16);
				scp->scsi_done(scp);
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
			scp->scsi_done(scp);
			return NULL;
		}
	}
	else { // Passthru device commands

		// Do not allow access to target id > 15 or LUN > 7
		if (target > 15 || SCP2LUN(scp) > 7) {
			scp->result = (DID_BAD_TARGET << 16);
			scp->scsi_done(scp);
			return NULL;
		}

		// if fast load option was set and scan for last device is
		// over, reset the fast_load flag so that during a possible
		// next scan, devices can be made available
		// TODO: check if this works ok!
		if (megaraid_fast_load && (target == 15) &&
			(SCP2CHANNEL(scp) == adapter->max_channel -1)) {

			con_log(CL_ANN, (KERN_INFO
				"megaraid mailbox: physical device scan "
				"re-enabled\n"));
			megaraid_fast_load = 0;
		}

		/*
		 * Display the channel scan for physical devices
		 */
		if (!(rdev->last_disp & (1L << SCP2CHANNEL(scp)))) {

			ss = megaraid_fast_load ? skip : scan;

			con_log(CL_ANN, (KERN_INFO
				"scsi[%d]: %s scsi channel %d [Phy %d]",
				adapter->host->host_no, ss, SCP2CHANNEL(scp),
				channel));

			con_log(CL_ANN, (
				" for non-raid devices\n"));

			rdev->last_disp |= (1L << SCP2CHANNEL(scp));
		}

		// disable channel sweep if fast load option given
		if (megaraid_fast_load) {
			scp->result = (DID_BAD_TARGET << 16);
			scp->scsi_done(scp);
			return NULL;
		}

		/*
		 * Allocate a SCB and initialize passthru
		 */
		if (!(scb = mraid_alloc_scb(adapter, scp))) {
			scp->result = (DID_ERROR << 16);
			scp->scsi_done(scp);
			*busy = 1;
			return NULL;
		}

		ccb			= (mbox_ccb_t *)scb->ccb;
		scb->dev_channel	= channel;
		scb->dev_target		= target;
		scb->dma_direction	= PCI_DIR(scp);
		mbox			= ccb->mbox;

		// Does this firmware support extended CDBs
		if (adapter->max_cdb_sz == 16) {
			mbox->cmd	= MBOXCMD_EXTPTHRU;
			megaraid_mbox_prepare_epthru(adapter, scb, scp);
			mbox->xferaddr	= (uint32_t)ccb->epthru_dma_h;
		}
		else {
			if (adapter->highmem_dma == MRAID_TRUE) {
				mbox->cmd = MBOXCMD_PASSTHRU64;
			}
			else {
				mbox->cmd = MBOXCMD_PASSTHRU;
			}
			megaraid_mbox_prepare_pthru(adapter, scb, scp);
			mbox->xferaddr = (uint32_t)ccb->pthru_dma_h;
		}
		return scb;
	}
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
	mraid_device_t		*rdev = ADAP2RAIDDEV(adapter);
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
	pthru->channel		= (rdev->flags & MBOX_BOARD_40LD) ? 0 : channel;
	pthru->target		= (rdev->flags & MBOX_BOARD_40LD) ?
					(channel << 4) | target : target;
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
	mraid_device_t		*rdev = ADAP2RAIDDEV(adapter);
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
	epthru->channel		= (rdev->flags & MBOX_BOARD_40LD) ? 0 : channel;
	epthru->target		= (rdev->flags & MBOX_BOARD_40LD) ?
					(channel << 4) | target : target;
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
		if (adapter->highmem_dma == MRAID_TRUE) {
			ccb->sgl64[0].address	= ccb->buf_dma_h;
			ccb->sgl64[0].length	= scp->request_bufflen;
			*xferaddr	= (uint32_t)ccb->sgl_dma_h;
			*xferlen	= (uint32_t)scp->request_bufflen;
		}
		else {
			*xferaddr	= (uint32_t)ccb->buf_dma_h;
			*xferlen	= (uint32_t)scp->request_bufflen;
			sgcnt		= 0;
		}

		if (scb->dma_direction == PCI_DMA_TODEVICE) {
			pci_dma_sync_single(adapter->pdev, ccb->buf_dma_h,
					scp->request_bufflen,
					PCI_DMA_TODEVICE);
		}

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
		if (adapter->highmem_dma == MRAID_TRUE) {
			ccb->sgl64[i].address = sg_dma_address(sgl);
			ccb->sgl64[i].length = sg_dma_len(sgl);
		}
		else {
			ccb->sgl32[i].address = sg_dma_address(sgl);
			ccb->sgl32[i].length = sg_dma_len(sgl);
		}
	}

	// dataxferlen must be set, even for commands with a sg list
	*xferaddr = ccb->sgl_dma_h;
	*xferlen = (uint32_t)scp->request_bufflen;

	if (scb->dma_direction == PCI_DMA_TODEVICE) {
		pci_dma_sync_sg(adapter->pdev, scp->request_buffer,
				scp->use_sg, PCI_DMA_TODEVICE);
	}

	// Return count of SG nodes
	return sgcnt;
}


/**
 * megaraid_mbox_runpendq - execute commands queued in the pending queue
 * @adapter	- controller's soft state
 *
 * scan the pending list for commands which are not yet issued and try to
 * post to the controller.
 */
static inline void
megaraid_mbox_runpendq(adapter_t *adapter)
{
	struct list_head *pos, *next;
	scb_t	*scb;

	if (!list_empty(&adapter->pend_list)) {

		// Issue all pending commands to the card
		list_for_each_safe (pos, next, &adapter->pend_list) {

			scb = list_entry(pos, scb_t, list);

			if (!(scb->state & SCB_ISSUED)) {

				if (mbox_post_cmd(adapter, scb) !=
						MRAID_SUCCESS) {
					return;
				}
			}
		}
	}
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
	unsigned int	i = 0;


	ccb	= (mbox_ccb_t *)scb->ccb;
	mbox	= raid_dev->mbox;
	mbox64	= raid_dev->mbox64;

	/*
	 * Check for busy mailbox. If it is, return failure - the caller
	 * should retry later.
	 */
	if (unlikely(mbox->busy)) {
		do {
			udelay(1);
			i++;
		} while(mbox->busy && (i < max_mbox_busy_wait));

		if (mbox->busy) return MRAID_FAILURE;
	}

	// Do we have an available command id. If not return failure
	MBOX_GET_CMDID(raid_dev, scb);

	if (scb->sno == -1) return MRAID_FAILURE;

	// Copy this command's mailbox data into "adapter's" mailbox
	memcpy((caddr_t)mbox64, (caddr_t)ccb->mbox64, 24);
	mbox->cmdid = scb->sno;

	// only modify the transfer addresses if mbox64 is not already setup
	if (mbox->xferaddr != 0xFFFFFFFF) {
		switch (mbox->cmd) {
		case MBOXCMD_EXTPTHRU:
			if (adapter->highmem_dma != MRAID_TRUE) break;
			// else fall-through
		case MBOXCMD_LREAD64:
		case MBOXCMD_LWRITE64:
		case MBOXCMD_PASSTHRU64:
			mbox64->xferaddr_lo = mbox->xferaddr;
			mbox64->xferaddr_hi = 0;
			mbox->xferaddr = 0xFFFFFFFF;
			break;
		default:
			mbox64->xferaddr_lo = 0;
			mbox64->xferaddr_hi = 0;
		}
	}

	// post the command and increment the oustanding commands counter/
	scb->state |= SCB_ISSUED;

	adapter->outstanding_cmds++;

	mbox->busy = 1;		// Set busy
	if (likely(adapter->flags & MRAID_BOARD_MEMMAP)) {
		mbox->poll = 0;
		mbox->ack = 0;
		wmb();
		WRINDOOR(raid_dev, raid_dev->mbox_dma | 0x1);
	}
	else {
		iombox_irq_enable(raid_dev);
		iombox_issue_command(raid_dev);
	}

	return MRAID_SUCCESS;
}


/**
 * megaraid_isr_memmapped - isr for memory based mailbox based controllers
 * @irq		- irq
 * @devp	- pointer to our soft state
 * @regs	- unused
 *
 * Interrupt service routine for memory-mapped mailbox controllers.
 */
static irqreturn_t
megaraid_isr_memmapped(int irq, void *devp, struct pt_regs *regs)
{
	adapter_t	*adapter = devp;
	unsigned long	flags;
	bool_t		handled;


	spin_lock_irqsave(adapter->host_lock, flags);

	handled = megaraid_memmbox_ack_sequence(adapter);

	/* Loop through any pending requests */
	if (adapter->quiescent == MRAID_FALSE) {
		megaraid_mbox_runpendq(adapter);
	}

	spin_unlock_irqrestore(adapter->host_lock, flags);

	// TODO: do we need to schedule tasklet everytime
	tasklet_schedule(&adapter->dpc_h);	// schedule the DPC

	if (handled == MRAID_TRUE) {
		return IRQ_RETVAL(1);
	}
	else {
		return IRQ_RETVAL(0);
	}
}


/**
 * megaraid_memmbox_ack_sequence - interrupt ack sequence for memory mapped HBAs
 * @adapter	- controller's soft state
 *
 * Interrupt ackrowledgement sequence for memory mapped HBAs
 */
static inline bool_t
megaraid_memmbox_ack_sequence(adapter_t *adapter)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	mbox_t		*mbox;
	scb_t		*scb;
	Scsi_Cmnd	*scp;
	uint8_t		nstatus;
	uint8_t		cmdid;
	bool_t		handled;
	uint32_t	dword;
	int		i;

	mbox	= raid_dev->mbox;

	// loop till F/W has more commands for us to complete
	handled = MRAID_FALSE;
	do {
		/*
		 * Check if a valid interrupt is pending. If found, force the
		 * interrupt line low.
		 */
		dword = RDOUTDOOR(raid_dev);
		if (dword != 0x10001234) {
			// No more pending commands

			return handled;
		}
		handled = MRAID_TRUE;
		WROUTDOOR(raid_dev, 0x10001234);

		while ((nstatus = mbox->numstatus) == 0xFF)
			cpu_relax();
		mbox->numstatus = 0xFF;

		adapter->outstanding_cmds -= nstatus;

		for (i = 0; i < nstatus; i++) {
			while ((cmdid = mbox->completed[i]) == 0xFF)
				cpu_relax();

			mbox->completed[i] = 0xFF;

			// handle internal commands right away
			if (unlikely(cmdid == raid_dev->int_cmdid)) {

				scb		= &adapter->iscb;
				scp		= scb->scp;
				scp->result	= mbox->status;
				scb->status	= mbox->status;

				/*
				 * Remove the command from the pending list
				 * and call the internal callback routine
				 */
				list_del_init(&scb->list);
				scb->state = SCB_FREE;
				scp->scsi_done(scp);
			}
			else {
				/*
				 * SCB associated with this command id and put
				 * in the completed list. Move the scb for
				 * existing list to the completed list
				 */
				scb = MBOX_GET_CMDID_SCB(raid_dev, cmdid);
				scb->status = mbox->status;
				list_move_tail(&scb->list,
						&adapter->completed_list);
			}
		}

		// Acknowledge interrupt
		WRINDOOR(raid_dev, 0x02);

		// FIXME: this may not be required
		while (RDINDOOR(raid_dev) & 0x02) cpu_relax();

	} while(1);
}


static void
megaraid_mbox_dpc(unsigned long devp)
{
	adapter_t		*adapter = (adapter_t *)devp;
	mraid_device_t		*raid_dev = ADAP2RAIDDEV(adapter);
	struct list_head	*pos, *next;
	struct scatterlist	*sgl;
	scb_t			*scb;
	Scsi_Cmnd		*scp;
	mraid_passthru_t	*pthru;
	mraid_epassthru_t	*epthru;
	mbox_ccb_t		*ccb;
	bool_t			islogical;
	int			pdev_index;
	int			pdev_state;
	mbox_t			*mbox;
	unsigned long		flags;
	uint8_t			c;
	int			status;
#ifdef MRAID_HAVE_STATS
	int			ldrv_num = 0;
#endif

	spin_lock_irqsave(adapter->host_lock, flags);

	list_for_each_safe(pos, next, &adapter->completed_list) {
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
			continue;	// Must never happen!
		}

		// Was an abort issued for this command earlier
		if (scb->state & SCB_ABORT) {

			con_log(CL_ANN, (KERN_NOTICE
			"megaraid: aborted cmd %lx[%x] completed\n",
				scp->serial_number, scb->sno));

			scp->result = (DID_ABORT << 16);
			scp->scsi_done(scb->scp);
			megaraid_mbox_free_scb(adapter, scb);

			continue;
		}

		// Was a reset issued for this command earlier
		if (scb->state & SCB_RESET) {

			con_log(CL_ANN, (KERN_WARNING
			"megaraid: reset cmd %lx[%x] completed\n",
				scp->serial_number, scb->sno));

			scb->scp->result = (DID_RESET << 16);
			scp->scsi_done(scb->scp);
			megaraid_mbox_free_scb(adapter, scb);

			continue;
		}

#ifdef MRAID_HAVE_STATS
		ldrv_num = mbox->logdrv;

		/*
		 * Maintain an error counter for the logical drive.
		 * Some application like SNMP agent need such statistics
		 */
		if (status && (islogical == MRAID_TRUE) &&
				(scp->cmnd[0] == READ_6 ||
				scp->cmnd[0] == READ_10 ||
				scp->cmnd[0] == READ_12)) {
			/*
			 * Logical drive number increases by 0x80 when a
			 * logical drive is deleted
			 */
			adapter->rd_errors[ldrv_num % 0x80]++;
		}

		if (status && (islogical == MRAID_TRUE) &&
				(scp->cmnd[0] == WRITE_6 ||
				scp->cmnd[0] == WRITE_10 ||
				scp->cmnd[0] == WRITE_12)) {
			/*
			 * Logical drive number increases by 0x80 when
			 * a logical drive is deleted
			 */
			adapter->wr_errors[ldrv_num % 0x80]++;
		}
#endif

		/*
		 * If the inquiry came of a disk drive which is not part of
		 * any RAID array, expose it to the kernel. For this to be
		 * enabled, user must set the "expose_unconf_disks" flag to 1
		 * by specifying it on module parameter list.
		 * This would enable data migration off drives from other
		 * configurations.
		 * TODO: Add code for ROMB configurations, where the disks on
		 * the SCSI channel must be exposed unconditionally
		 */
		if (scp->cmnd[0] == INQUIRY && status == 0 &&
				islogical == MRAID_FALSE) {

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
					expose_unconf_disks == 0) {

					status = 0xF0;
				}
			}
		}

		// clear result; otherwise, success returns corrupt value
		scp->result = 0;

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
			 * If TEST_UNIT_READY fails, we know
			 * RESERVATION_STATUS failed
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

		// print a level 1 debug message for all failed commands
		if (status) {
			megaraid_mbox_display_scb(adapter, scb, CL_DLEVEL1);
		}

		/*
		 * Free our internal resources and call the mid-layer callback
		 * routine
		 */
		megaraid_mbox_free_scb(adapter, scb);
		scp->scsi_done(scp);
	}

	spin_unlock_irqrestore(adapter->host_lock, flags);

	return;
}


/**
 * megaraid_mbox_free_scb - free the per-command internal resources
 * @adapter	- controller's soft state
 * @scb		- pointer to the resource packet to be freed
 *
 * Put the scb back in the available pool and call DMA sync whereever
 * required.
 */
static inline void
megaraid_mbox_free_scb(adapter_t *adapter, scb_t *scb)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
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

	/*
	 * Free the command id associated with this SCB so that it can
	 * be re-used
	 */
	MBOX_FREE_CMDID(raid_dev, scb->sno);

	/*
	 * Remove the command from the pending list and return scb to the
	 * pool of free SCBs
	 */
	scb->sno = -1;
	list_del_init(&scb->list);
	mraid_dealloc_scb(adapter, scb);

	return;
}


/**
 * megaraid_isr_iomapped - isr for IO based mailbox based controllers
 * @irq		- irq
 * @devp	- pointer to our soft state
 * @regs	- unused
 *
 * Interrupt service routine for IO-mapped mailbox controllers.
 */
static irqreturn_t
megaraid_isr_iomapped(int irq, void *devp, struct pt_regs *regs)
{
	adapter_t	*adapter = (adapter_t *)devp;
	unsigned long	flags;
	bool_t		handled;


	spin_lock_irqsave(adapter->host_lock, flags);

	handled = megaraid_iombox_ack_sequence(adapter);

	/* Loop through any pending requests */
	if (adapter->quiescent == MRAID_FALSE) {
		megaraid_mbox_runpendq(adapter);
	}

	spin_unlock_irqrestore(adapter->host_lock, flags);

	tasklet_schedule(&adapter->dpc_h);	// schedule the DPC

	if (handled == MRAID_TRUE) {
		return IRQ_RETVAL(1);
	}
	else {
		return IRQ_RETVAL(0);
	}
}


/**
 * megaraid_iombox_ack_sequence - interrupt ack sequence for IO mapped HBAs
 * @adapter	- controller's soft state
 *
 * Interrupt ackrowledgement sequence for memory mapped HBAs
 */
static inline bool_t
megaraid_iombox_ack_sequence(adapter_t *adapter)
{
	mraid_device_t	*raid_dev = ADAP2RAIDDEV(adapter);
	volatile mbox_t	*mbox;
	scb_t		*scb;
	Scsi_Cmnd	*scp;
	uint8_t		nstatus;
	uint8_t		cmdid;
	bool_t		handled;
	uint8_t		byte;
	int		i;


	try_assertion {
		ASSERT(spin_is_locked(adapter->host_lock));
	}
	catch_assertion {
		con_log(CL_ANN, (KERN_CRIT "megaraid: host lock not locked\n"));
	}
	end_assertion



	mbox	= raid_dev->mbox;

	/*
	 * loop till F/W has more commands for us to complete.
	 */
	handled = MRAID_FALSE;
	do {
		/* Check if a valid interrupt is pending */
		byte = iombox_irq_state(raid_dev);

		if ((byte & MBOX_VALID_INTR_BYTE) == 0) {
			/* No more pending commands */
			return handled;
		}
		handled = MRAID_TRUE;
		iombox_set_irq_state(raid_dev, byte);

		while ((nstatus = mbox->numstatus) == 0xFF)
			cpu_relax();
		mbox->numstatus = 0xFF;

		adapter->outstanding_cmds -= nstatus;

		for (i = 0; i < nstatus; i++) {
			while ((cmdid = mbox->completed[i]) == 0xFF)
				cpu_relax();

			mbox->completed[i] = 0xFF;

			/* handle internal commands right away */
			if (unlikely(cmdid == raid_dev->int_cmdid)) {

				scb		= &adapter->iscb;
				scp		= scb->scp;
				scp->result	= mbox->status;
				scb->status	= mbox->status;

				/*
				 * Remove the command from the pending list
				 * and call the internal callback routine
				 */
				list_del_init(&scb->list);
				scb->state = SCB_FREE;
				scp->scsi_done(scp);
			}
			else {
				/*
				 * SCB associated with this command id and put
				 * in the completed list. Move the scb for
				 * existing list to the completed list
				 */
				scb = MBOX_GET_CMDID_SCB(raid_dev, cmdid);
				scb->status = mbox->status;
				list_move_tail(&scb->list,
						&adapter->completed_list);
			}
		}

		/* Acknowledge interrupt */
		iombox_irq_ack(raid_dev);

	} while(1);
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
	unsigned long		iter;


	adapter		= SCP2ADAPTER(scp);
	raid_dev	= ADAP2RAIDDEV(adapter);

	try_assertion {
		ASSERT(spin_is_locked(adapter->host_lock));
	}
	catch_assertion {
		return FAILED;
	}
	end_assertion

	con_log(CL_ANN, (KERN_WARNING
		"megaraid: aborting-%ld cmd=%x <c=%d t=%d l=%d>\n",
		scp->serial_number, scp->cmnd[0], SCP2CHANNEL(scp),
		SCP2TARGET(scp), SCP2LUN(scp)));

	// If FW has stopped responding, simply return failure
	if (raid_dev->hw_error) {
		con_log(CL_DLEVEL1, (KERN_NOTICE
			"megaraid: hw error, not aborting\n"));
		return FAILED;
	}


	// Find out if this command is still on the pending list. If it is and
	// was never issued, abort and return success. If the command is owned
	// by the firmware, we must wait for it to complete by the FW.
	// Change the state of the SCB to 'aborted' so that appropriate status
	// can be returned from the dpc if this command ever completes.
	scb = NULL;
	list_for_each_safe(pos, next, &adapter->pend_list) {

		scb = list_entry(pos, scb_t, list);

		if (scb->scp == scp) { // Found command

			scb->state |= SCB_ABORT;

			/*
			 * Check if this command was never issued. If this is
			 * the case, take it off from the pending list and
			 * complete.
			 */
			if (!(scb->state & SCB_ISSUED)) {

				con_log(CL_ANN, (KERN_WARNING
				"megaraid abort: %ld:%d[%d:%d], driver owner\n",
					scp->serial_number, scb->sno,
					scb->dev_channel, scb->dev_target));

				scp->result = (DID_ABORT << 16);
				scp->scsi_done(scp);

				megaraid_mbox_free_scb(adapter, scb);

				return SUCCESS;
			}
		}
	}


	// There might a race here, where the command was completed by the
	// firmware and now it is on the completed list. Before we could
	// complete the command to the kernel in dpc, the abort came.
	// Find out if this is the case to avoid the race.
	list_for_each_safe(pos, next, &adapter->completed_list) {

		scb = list_entry(pos, scb_t, list);

		if (scb->scp == scp) { // Found command

			con_log(CL_ANN, (KERN_WARNING
			"megaraid: %ld:%d[%d:%d], abort from completed list\n",
				scp->serial_number, scb->sno,
				scb->dev_channel, scb->dev_target));

			scp->result = (DID_ABORT << 16);
			scp->scsi_done(scp);

			megaraid_mbox_free_scb(adapter, scb);

			return SUCCESS;
		}
	}

	// Check do we even own this command, in which case this would be on
	// the pending list. If we do now own this command, return success
	// because otherwise we would end up having the controller marked
	// offline.
	if ((scb) && !(scb->state & SCB_ABORT)) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid abort: %ld:%d[%d:%d], do now own\n",
			scp->serial_number, scb->sno, scb->dev_channel,
			scb->dev_target));


		// FIXME: is it ok to do a callback for this command?
		scp->result = (DID_ABORT << 16);
		scp->scsi_done(scp);

		megaraid_mbox_free_scb(adapter, scb);

		return SUCCESS;
	}


	// We cannot actually abort a command owned by firmware, wait some
	// more time and hope it completes
	iter = 0;
	do {
		if (adapter->flags & MRAID_BOARD_MEMMAP) {
			megaraid_memmbox_ack_sequence(adapter);
		}
		else {
			megaraid_iombox_ack_sequence(adapter);
		}

		list_for_each_safe(pos, next, &adapter->completed_list) {

			scb = list_entry(pos, scb_t, list);

			if (scb->scp == scp) { // Found command

				con_log(CL_ANN, (KERN_WARNING
				"megaraid: %ld:%d[%d:%d], abort complete\n",
					scp->serial_number, scb->sno,
					scb->dev_channel, scb->dev_target));

				scp->result = (DID_ABORT << 16);
				scp->scsi_done(scp);

				megaraid_mbox_free_scb(adapter, scb);

				return SUCCESS;
			}
		}

		/*
		 * print a message once every second only
		 */
		if (!(iter % 1000)) {
			con_log(CL_ANN, (
			"megaraid: Wait for command to complete: iter:%ld\n",
				iter));
		}

		if (iter++ < MBOX_ABORT_SLEEP * 1000) {
			mdelay(1);
		}
		else {
			con_log(CL_ANN, (KERN_WARNING
				"megaraid: critical hardware error!\n"));

			raid_dev->hw_error = 1;

			return FAILED;
		}

	} while (1);

	return FAILED;
}


/**
 * megaraid_reset_handler - device reset hadler for mailbox based driver
 * @scp		: reference command
 *
 * Reset handler for the mailbox based controller. The commands completion is
 * already tried by the abort handler. We try to reset SCSI reservations if
 * any exists.
 **/
static int
megaraid_reset_handler(struct scsi_cmnd *scp)
{
	adapter_t		*adapter;
	scb_t			*iscb;
	mbox_ccb_t		*ccb;
	mraid_device_t		*raid_dev;
	int			rval;

	adapter		= SCP2ADAPTER(scp);
	raid_dev	= ADAP2RAIDDEV(adapter);

	// return failure if adapter is not responding
	if (raid_dev->hw_error) return FAILED;

	// FIXME: remove mraid_get_icmd from the code
	iscb		= mraid_get_icmd(adapter);
	ccb		= (mbox_ccb_t *)iscb->ccb;

	// clear reservations if any
	ccb->raw_mbox[0] = CLUSTER_CMD;
	ccb->raw_mbox[2] = RESET_RESERVATIONS;

	if (mbox_post_sync_cmd_fast(adapter, ccb->raw_mbox) == 0) {
		con_log(CL_ANN,
			(KERN_INFO "megaraid: reservation reset\n"));
	}
	else {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: reservation reset failed\n"));
	}

	mraid_free_icmd(adapter);

	rval = SUCCESS;

	// If there are outstanding commands, return failure
	if (adapter->outstanding_cmds) {
		con_log(CL_ANN, (KERN_INFO
			"megaraid: reset failed, %d commands outstanding\n",
			adapter->outstanding_cmds));
		rval = FAILED;
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
	uint8_t		byte;
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

	switch (raw_mbox[0]) {

	case MBOXCMD_EXTPTHRU:
		if (adapter->highmem_dma != MRAID_TRUE) break;
		// else fall-through

	case MBOXCMD_LREAD64:
	case MBOXCMD_LWRITE64:
	case MBOXCMD_PASSTHRU64:
		mbox64->xferaddr_lo	= mbox->xferaddr;
		mbox64->xferaddr_hi	= 0;
		mbox->xferaddr		= 0xFFFFFFFF;
		break;
	default:
		mbox64->xferaddr_lo	= 0;
		mbox64->xferaddr_hi	= 0;
	}

	if (likely(adapter->flags & MRAID_BOARD_MEMMAP)) {

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
	}
	else {
		iombox_irq_disable(raid_dev);
		iombox_issue_command(raid_dev);

		while (!((byte = iombox_irq_state(raid_dev)) & MBOX_INTR_VALID))
			cpu_relax();

		status = mbox->status;

		iombox_set_irq_state(raid_dev, byte);
		iombox_irq_enable(raid_dev);
		iombox_irq_ack(raid_dev);
	}

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
 * controllers
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
	volatile mbox_t	*mbox = raid_dev->mbox;

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
	volatile mbox_t	*mbox = raid_dev->mbox;
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
	scb_t			*iscb;
	int			adapter_max_cmds;
	int			i;


	// FIXME: mraid_get_icmd not required, remove.....
	iscb	= mraid_get_icmd(adapter);
	ccb	= (mbox_ccb_t *)iscb->ccb;

	/*
	 * Issue an ENQUIRY3 command to find out if this is a 40LD controller.
	 * Depending on whether we have a 40LD controller or not, we will need
	 * to issue different commands to find out certain adapter parameters,
	 * e.g., max channels, max commands etc.
	 */
	pinfo = pci_alloc_consistent(adapter->pdev, sizeof(mraid_pinfo_t),
			&pinfo_dma_h);

	if (pinfo == NULL) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));
		mraid_free_icmd(adapter);

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
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {

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
		 * 40LD controller
		 * Get product info for information like number of channels,
		 * maximum commands supported.
		 */
		raid_dev->flags = MBOX_BOARD_40LD;

		memset((caddr_t)ccb->raw_mbox, 0, sizeof(ccb->raw_mbox));
		mbox->xferaddr = (uint32_t)pinfo_dma_h;

		ccb->raw_mbox[0] = FC_NEW_CONFIG;
		ccb->raw_mbox[2] = NC_SUBOP_PRODUCT_INFO;

		if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) != 0) {

			con_log(CL_ANN, (KERN_WARNING
				"megaraid: product info failed\n"));

			pci_free_consistent(adapter->pdev,
				sizeof(mraid_pinfo_t), pinfo, pinfo_dma_h);

			mraid_free_icmd(adapter);

			return MRAID_FAILURE;
		}
	}
	else {	// 8LD controller
		mraid_extinq_t	*ext_inq;
		mraid_inquiry_t	*inq;
		dma_addr_t	dma_handle;

		raid_dev->flags = MBOX_BOARD_8LD;

		ext_inq = pci_alloc_consistent(adapter->pdev,
				sizeof(mraid_extinq_t), &dma_handle);

		if (ext_inq == NULL) {

			con_log(CL_ANN, (KERN_WARNING
				"megaraid: out of memory, %s %d.\n",
				__FUNCTION__, __LINE__));

			pci_free_consistent(adapter->pdev,
					sizeof(mraid_pinfo_t), pinfo,
					pinfo_dma_h);

			mraid_free_icmd(adapter);

			return MRAID_FAILURE;
		}

		/*
		 * issue old 0x04 command to adapter
		 */
		inq		= &ext_inq->raid_inq;
		memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));
		mbox->xferaddr	= (uint32_t)dma_handle;
		mbox->cmd	= MBOXCMD_ADPEXTINQ;

		if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {

			con_log(CL_ANN, (KERN_WARNING
			"megaraid error: adapter inquiry failed\n") );

			pci_free_consistent(adapter->pdev,
					sizeof(mraid_extinq_t), ext_inq,
					dma_handle);

			pci_free_consistent(adapter->pdev,
					sizeof(mraid_pinfo_t), pinfo,
					pinfo_dma_h);

			mraid_free_icmd(adapter);

			return MRAID_FAILURE;
		}

		/*
		 * Collect information about state of each physical drive
		 * attached to the controller. We will expose all the disks
		 * which are not part of RAID
		 */
		for (i = 0; i < MBOX_MAX_PHYSICAL_DRIVES; i++) {
			raid_dev->pdrv_state[i] = inq->pdrv_info.pdrv_state[i];
		}

		/*
		 * update the product info structure with mraid_inquiry
		 * structure
		 */
		pinfo->max_commands	= inq->adapter_info.max_commands;
		pinfo->nchannels	= inq->adapter_info.nchannels;

		for (i = 0; i < 4; i++) {
			pinfo->fw_version[i] =
				inq->adapter_info.fw_version[i];

			pinfo->bios_version[i] =
				inq->adapter_info.bios_version[i];
		}

		pci_free_consistent(adapter->pdev, sizeof(mraid_extinq_t),
				ext_inq, dma_handle);
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

	/*
	 * use HP firmware and bios version encoding for HP controllers
	 */
	if (pinfo->subsysvid == PCI_VENDOR_ID_HP) {
		sprintf(adapter->fw_version, "%c%d%d.%d%d",
			pinfo->fw_version[2],
			pinfo->fw_version[1] >> 8,
			pinfo->fw_version[1] & 0x0f,
			pinfo->fw_version[0] >> 8,
			pinfo->fw_version[0] & 0x0f);
		sprintf(adapter->bios_version, "%c%d%d.%d%d",
			pinfo->bios_version[2],
			pinfo->bios_version[1] >> 8,
			pinfo->bios_version[1] & 0x0f,
			pinfo->bios_version[0] >> 8,
			pinfo->bios_version[0] & 0x0f);
	}
	else {
		memcpy(adapter->fw_version, pinfo->fw_version, 4);
		adapter->fw_version[4] = 0;

		memcpy(adapter->bios_version, pinfo->bios_version, 4);
		adapter->bios_version[4] = 0;
	}

	/*
	 * Extra checks for some buggy firmware
	 */
	if ((pinfo->subsysid == 0x1111) && (pinfo->subsysvid == 0x1111)) {

		if (!strcmp(adapter->fw_version, "3.00") ||
				!strcmp(adapter->fw_version, "3.01")) {

			con_log (CL_ANN,  (KERN_WARNING
				"megaraid: Your  card is a Dell PERC "
				"2/SC RAID controller with  "
				"firmware\nmegaraid: 3.00 or 3.01.  "
				"This driver is known to have "
				"corruption issues\nmegaraid: with "
				"those firmware versions on this "
				"specific card.  In order\nmegaraid: "
				"to protect your data, please upgrade "
				"your firmware to version\nmegaraid: "
				"3.10 or later, available from the "
				"Dell Technical Support web\n"
				"megaraid: site at\nhttp://support."
				"dell.com/us/en/filelib/download/"
				"index.asp?fileid=2940\n"
			));
		}
	}

	/*
	 * If we have a HP 1M(0x60E7)/2M(0x60E8) controller with firmware
	 * H.01.07, H.01.08, and H.01.09 disable 64 bit support, since this
	 * firmware cannot handle 64 bit addressing
	 */
	if ((pinfo->subsysvid == PCI_VENDOR_ID_HP) &&
		((pinfo->subsysid == 0x60E7)||(pinfo->subsysid == 0x60E8))) {

		if (!strcmp(adapter->fw_version, "H01.07")		||
			!strcmp(adapter->fw_version, "H01.08")	||
			!strcmp(adapter->fw_version, "H01.09")) {

			con_log(CL_ANN, (KERN_WARNING
				"megaraid: Firmware H.01.07, H.01.08, and "
				"H.01.09 on 1M/2M controllers\n"
				"megaraid: do not support 64 bit "
				"addressing.\nmegaraid: DISABLING "
				"64 bit support.\n") );

			adapter->flags		&= ~MRAID_DMA_64;
			adapter->highmem_dma	= MRAID_FALSE;
		}
	}

	pci_free_consistent(adapter->pdev, sizeof(mraid_pinfo_t), pinfo,
			pinfo_dma_h);

	mraid_free_icmd(adapter);

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
	scb_t		*iscb;
	status_t	rval;

	iscb		= mraid_get_icmd(adapter);
	ccb		= (mbox_ccb_t *)iscb->ccb;
	mbox		= ccb->mbox;

	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));
	mbox->xferaddr	= (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	ccb->raw_mbox[0] = MAIN_MISC_OPCODE;
	ccb->raw_mbox[2] = SUPPORT_EXT_CDB;

	/*
	 * Issue the command
	 */
	rval = MRAID_FAILURE;
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {
		rval = MRAID_SUCCESS;
	}

	mraid_free_icmd(adapter);

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
	scb_t		*iscb;
	status_t	rval;


	iscb	= mraid_get_icmd(adapter);
	ccb	= (mbox_ccb_t *)iscb->ccb;
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

	mraid_free_icmd(adapter);

	return rval;
}


/**
 * megaraid_mbox_boot_enabled - check for boot support
 * @adapter	- soft state for the controller
 *
 * this routine check whether the controller in question has the BIOS enabled
 * for booting purposes
 */
static status_t
megaraid_mbox_boot_enabled(adapter_t *adapter)
{
	mbox_ccb_t	*ccb;
	mbox_t		*mbox;
	scb_t		*iscb;
	status_t	rval;


	iscb	= mraid_get_icmd(adapter);
	ccb	= (mbox_ccb_t *)iscb->ccb;
	mbox	= ccb->mbox;

	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));
	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	mbox->xferaddr		= (uint32_t)adapter->ibuf_dma_h;
	ccb->raw_mbox[0]	= IS_BIOS_ENABLED;
	ccb->raw_mbox[2]	= GET_BIOS;

	// issue the command
	rval = MRAID_FAILURE;
	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {
		if (*(char *)adapter->ibuf) {
			adapter->boot_enabled = MRAID_TRUE;
		}
		else {
			adapter->boot_enabled = MRAID_FALSE;
		}

		rval = MRAID_SUCCESS;
	}

	mraid_free_icmd(adapter);

	return rval;
}


/**
 * megaraid_mbox_get_boot_dev - find out our boot device
 * @adapter	- soft state for the controller
 *
 * Find out the booting device. All the physical devices are exported on their
 * native address (channel and target) and no re-ordering is done for them.
 * The logical drives are exported on a virtual channel. Logical drives are
 * re-ordered so that the boot logical drive is the first one to be exposed
 * before any other logical drive.
 */
static void
megaraid_mbox_get_boot_dev(adapter_t *adapter)
{
	struct private_bios_data	*bios_data;
	mbox_ccb_t			*ccb;
	mbox_t				*mbox;
	uint16_t			cksum;
	uint8_t				*cksum_p;
	uint8_t				boot_dev;
	scb_t				*iscb;
	int				i;


	iscb		= mraid_get_icmd(adapter);
	ccb		= (mbox_ccb_t *)iscb->ccb;
	mbox		= ccb->mbox;
	memset((caddr_t)ccb->raw_mbox, 0, sizeof(mbox_t));
	mbox->xferaddr	= (uint32_t)adapter->ibuf_dma_h;

	memset((void *)adapter->ibuf, 0, MBOX_IBUF_SIZE);

	ccb->raw_mbox[0] = BIOS_PVT_DATA;
	ccb->raw_mbox[2] = GET_BIOS_PVT_DATA;

	/*
	 * All logical drives appear on a virtual channel. This virtual
	 * channel is exported *after* all the physical channels. bd_channel
	 * represents the physical channel of the boot device, which is
	 * meaningless for logical drives. We invalidate (set to 0xFF) for
	 * logical drives
	 */
	adapter->virtual_ch	= adapter->max_channel;
	adapter->bd_channel	= 0xFF;

	if (mbox_post_sync_cmd(adapter, ccb->raw_mbox) == 0) {
		bios_data = (struct private_bios_data *)adapter->ibuf;

		cksum = 0;
		cksum_p = (char *)bios_data;
		for (i = 0; i < 14; i++) {
			cksum += (uint8_t)(*cksum_p++);
		}

		if (bios_data->cksum == (uint16_t)(0-cksum) ) {
			/*
			 * If MSB is set, a physical drive is set as boot
			 * device.
			 */
			if (bios_data->boot_drv & 0x80) {
				boot_dev	= bios_data->boot_drv & 0x7F;
				adapter->bd_channel	= boot_dev / 16;
				adapter->bd_target	= boot_dev % 16;
			}
			else {
				adapter->bd_target	= bios_data->boot_drv;
			}
		}
	}
	else {
		con_log(CL_ANN, (KERN_WARNING
		"megaraid: No BIOS private data, Assuming defaults\n"));
	}

	/*
	 * Prepare the device ids array and shuffle the devices around
	 * so that boot device/channel are exported first
	 */
	mraid_setup_device_map(adapter);

	mraid_free_icmd(adapter);

	return;
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
	scb_t		*iscb;
	int		nsg;


	iscb	= mraid_get_icmd(adapter);
	ccb	= (mbox_ccb_t *)iscb->ccb;
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

	mraid_free_icmd(adapter);

	return nsg;
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
	adp.drvr_data		= (ulong)adapter;
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
 */
static status_t
megaraid_cmm_unregister(adapter_t *adapter)
{
	mraid_mm_unregister_adp( adapter->unique_id );
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
 * TODO: add support for command aborting
 */
static int
megaraid_mbox_mm_cmd(ulong drvr_data, uioc_t *kioc, uint32_t action)
{
	adapter_t	*adp;
	int		i;

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

	for (i = 0; i < MAX_CONTROLLERS; i++) {
		if (mraid_driver_g.adapter[i] == adp) {
			break;
		}
	}

	if (i == MAX_CONTROLLERS) {
		con_log(CL_ANN, ("megaraid ioctl: no such adapter\n"));
		return (-ENODEV);
	}

	// make sure this adapter is not being detached right now.
	if (atomic_read(&adp->being_detached)) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid: reject management request, detaching.\n"));
		return (-ENODEV);
	}

	switch (kioc->opcode) {

	case GET_ADAP_INFO:

		kioc->status =  gather_hbainfo(adp,
				(mraid_hba_info_t *)(ulong)kioc->cmdbuf);

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

	mbox64		= (mbox64_t *)(ulong)kioc->cmdbuf;
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

	scb->gp		= (ulong)kioc;
	scmd->state	= 0;
	scb->sno	= raid_dev->int_cmdid;

	/*
	 * If it is a logdrv random delete operation, we have to wait till
	 * there are no outstanding cmds at the fw and then issue it directly
	 */
	if (raw_mbox[0] == FC_DEL_LOGDRV && raw_mbox[2] == OP_DEL_LOGDRV) {

		if (wait_till_fw_empty(adapter))
			return (-ETIME);

		INIT_LIST_HEAD( &scb->list );
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
wait_till_fw_empty( adapter_t* adp )
{
	DECLARE_WAIT_QUEUE_HEAD(wq);

	/*
	 * Set the quiescent flag to stop issuing cmds to FW.
	 */
	//TODO: spin_lock protect ?!
	adp->quiescent = MRAID_TRUE;

	/*
	 * Wait till there are no more cmds outstanding at FW
	 */
	while (adp->outstanding_cmds > 0)
		sleep_on_timeout( &wq, 1*HZ );

	//TODO: Implement a countdown mechanism where if fw doesn't
	//complete its cmds in a certain period, we have to return
	//error

	return 0;
}


/**
 * megaraid_mbox_internal_done()
 * @scmd - internal scsi command
 *
 * Callback routine for internal commands originated from the management
 * module. Also, release the adapter mutex used to serialize access to
 * internal command index.
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
	mbox64			= (mbox64_t *)(ulong)kioc->cmdbuf;
	mbox64->mbox32.status	= scmd->result;
	raw_mbox		= (uint8_t*) &mbox64->mbox32;


	if (raw_mbox[0] == FC_DEL_LOGDRV && raw_mbox[2] == OP_DEL_LOGDRV
							&& !scmd->result) {
		/*
		 * It was a del logdrv command and it succeeded
		 */
		megaraid_update_logdrv_ids( adapter );
		adapter->quiescent = MRAID_FALSE;
		megaraid_mbox_runpendq( adapter );
	}

	up(&adapter->imtx);	// ready for the next command

	kioc->done(kioc);

	return;
}


/**
 * megaraid_update_logdrv_ids - update logical drive ids after drive deletion
 * @param adp	: HBA soft state
 *
 * Update the logical drives ids in the device_ids array after a random drive
 * deletion operation. The new ids would then be automatically picked up by the
 * queue routines.
 * We would need to explicitly update the drive ids for the commands on the
 * pending lists though.
 */
static void
megaraid_update_logdrv_ids( adapter_t* adp )
{
	int			i;
	uint8_t			*rm;
	scb_t			*scb;
	mbox_ccb_t		*ccb;
	struct list_head 	*pos;
	struct list_head	*next;

	// Change the logical drives numbers in device_ids array one
	// slot in device_ids is reserved for target id, that's why
	// "<=" below
	for (i = 0; i <= MAX_LOGICAL_DRIVES_40LD; i++) {

		if (adp->device_ids[adp->virtual_ch][i] < 0x80) {
			adp->device_ids[adp->virtual_ch][i] += 0x80;
		}
	}

	if (list_empty(&adp->pend_list))
		return;

	list_for_each_safe (pos, next, &adp->pend_list) {

		scb = list_entry( pos, scb_t, list );
		ccb = (mbox_ccb_t*)scb->ccb;

		if (scb->state & SCB_ISSUED) {
			con_log(CL_ANN, (KERN_WARNING
				"megaraid: invalid scb state\n"));
			continue;
		}

		rm = ccb->raw_mbox;

		// LD  read/write operations
		if (rm[0] == MBOXCMD_LREAD || rm[0] == MBOXCMD_LWRITE ||
				rm[0] == MBOXCMD_LREAD64 ||
				rm[0] == MBOXCMD_LWRITE64) {

			if (ccb->mbox->logdrv < 0x80) {
				ccb->mbox->logdrv += 0x80;
			}
		}

		// LD  passthru operations
		if (ccb->pthru->islogical &&
				(rm[0] == MBOXCMD_PASSTHRU ||
				rm[0] == MBOXCMD_PASSTHRU64)) {

			if (ccb->pthru->logdrv < 0x80) {
				ccb->pthru->logdrv += 0x80;
			}
		}

		if (ccb->epthru->islogical && rm[0] == MBOXCMD_EXTPTHRU) {
			if (ccb->epthru->logdrv < 0x80) {
				ccb->epthru->logdrv += 0x80;
			}
		}
	}
}


/**
 * gather_hbainfo - HBA characteristics for the applications
 * @param adapter	: HBA soft state
 * @param hinfo		: pointer to the caller's host info strucuture
 */
static int
gather_hbainfo(adapter_t *adp, mraid_hba_info_t *hinfo)
{
	hinfo->pci_vendor_id	= adp->pdev->vendor;
	hinfo->pci_device_id	= adp->pdev->device;
	hinfo->subsys_vendor_id	= adp->pdev->subsystem_vendor;
	hinfo->subsys_device_id	= adp->pdev->subsystem_device;

	hinfo->pci_bus		= adp->pdev->bus->number;
	hinfo->pci_dev_fn	= adp->pdev->devfn;
	hinfo->pci_slot		= PCI_SLOT(adp->pdev->devfn);
	hinfo->irq		= adp->host->irq;
	hinfo->baseport		= ADAP2RAIDDEV(adp)->baseport;

	hinfo->unique_id	= (hinfo->pci_bus << 8) | adp->pdev->devfn;
	hinfo->host_no		= adp->host->host_no;

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
