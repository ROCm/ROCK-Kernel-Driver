/* 
 * AMD Standard Hot Plug Controller Driver
 *
 * Copyright (c) 1995,2001,2003 Compaq Computer Corporation
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2003 IBM Corp.
 * Copyright (C) 2002-2004 Advanced Micro Devices
 *
 * YOUR USE OF THIS CODE IS SUBJECT TO THE TERMS
 * AND CONDITIONS OF THE GNU GENERAL PUBLIC
 * LICENSE FOUND IN THE "GPL.TXT" FILE THAT IS
 * INCLUDED WITH THIS FILE AND POSTED AT
 * http://www.gnu.org/licenses/gpl.html
 *
 * Send feedback to <greg@kroah.com> <david.keck@amd.com>
 *
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/init.h>
#include "amdshpc.h"
#include "amdshpc_ddi.h"
#include "pci_hotplug.h"
#include "../../../arch/i386/pci/pci.h"

/* Global variables */
int amdshpc_debug;
struct shpc_context *amdshpc_ctrl_list; // used for the shpc state machine
struct controller *ctrl_list;	 	  // used only for resource management
struct pci_func *amdshpc_slot_list[256];

static int num_slots;
static void *amdshpc_rom_start;
static unsigned long shpc_instance;

#define DRIVER_VERSION	"1.01.00"
#define DRIVER_AUTHOR	"Dave Keck <david.keck@amd.com>"
#define DRIVER_DESC	"AMD Standard Hot Plug Controller Driver"
#define PCI_DEVICE_ID_AMD_GOLAM_7450	0x7450
#define PCI_DEVICE_ID_AMD_POGO_7458	0x7458

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
//MODULE_PARM(debug, "i");
//MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

static int enable_slot		(struct hotplug_slot *slot);
static int disable_slot		(struct hotplug_slot *slot);
static int set_attention_status (struct hotplug_slot *slot, u8 value);
static int hardware_test	(struct hotplug_slot *slot, u32 value);
static int get_power_status	(struct hotplug_slot *slot, u8 *value);
static int get_attention_status	(struct hotplug_slot *slot, u8 *value);
static int get_latch_status	(struct hotplug_slot *slot, u8 *value);
static int get_adapter_status	(struct hotplug_slot *slot, u8 *value);

// values to be returned to the PCI Hotplug Core
#define CORE_SLOT_DISABLED		0
#define CORE_SLOT_ENABLED		1

#define	CORE_INDICATOR_OFF		0
#define	CORE_INDICATOR_ON		1
#define	CORE_INDICATOR_BLINK		2

#define CORE_LATCH_CLOSED		1
#define CORE_LATCH_OPENED		0

static int init_slots (	struct controller *ctrl, int num_slots );
static void translate_slot_info (struct hotplug_slot_info *info,
								 struct slot_status_info *query);

static struct hotplug_slot_ops skel_hotplug_slot_ops = {
	.owner =		THIS_MODULE,
	.enable_slot =		enable_slot,
	.disable_slot =		disable_slot,
	.set_attention_status =	set_attention_status,
	.hardware_test =	hardware_test,
	.get_power_status =	get_power_status,
	.get_attention_status =	get_attention_status,
	.get_latch_status =	get_latch_status,
	.get_adapter_status =	get_adapter_status,
};

/* Inline functions to check the sanity of a pointer that is passed to us */
static inline int slot_paranoia_check (struct slot *slot, const char *function)
{
	if (!slot) {
		dbg("-->%s - slot == NULL", function);
		return -1;
	}
	if (slot->magic != SLOT_MAGIC) {
		dbg("-->%s - bad magic number for slot", function);
		return -1;
	}
	if (!slot->hotplug_slot) {
		dbg("-->%s - slot->hotplug_slot == NULL!", function);
		return -1;
	}
	return 0;
}

static inline struct slot *get_slot (struct hotplug_slot *hotplug_slot, const char *function)
{
	struct slot *slot;

	if (!hotplug_slot) {
		dbg("-->%s - hotplug_slot == NULL\n", function);
		return NULL;
	}

	slot = (struct slot *)hotplug_slot->private;
	if (slot_paranoia_check (slot, function))
				return NULL;
	return slot;
}

static int enable_slot (struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	struct shpc_context *shpc_context;
	struct slot_status_info query;
	long status;
	int retval = 0;


	if (slot == NULL)
		return -ENODEV;

	dbg ("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	/*
	 *  enable the specified slot
	 */
	shpc_context = ( struct shpc_context * )slot->private;
	status = hp_StartAsyncRequest(shpc_context, slot->number,
		SHPC_ASYNC_ENABLE_SLOT, 0, slot );

	//
	// pretend async request was completed (we're not queuing slot requests)
	//
	hp_QuerySlotStatus( shpc_context, slot->number, &query );
	if( status == STATUS_SUCCESS ) {
		query.lu_slot_state = SLOT_ENABLE;
		query.lu_pi_state = INDICATOR_BLINK;
		if( query.lu_card_present &&
			( query.lu_mrl_implemented == HP_FALSE ||
			query.lu_mrl_opened == HP_FALSE ) &&
			query.lu_power_fault == HP_FALSE ) {
				query.lu_request_failed = HP_FALSE;
		}
		else {
			query.lu_request_failed = HP_TRUE;
		}
	}
	else {
		query.lu_request_failed = HP_TRUE;
	}

	//
	// translate the slot info to PCI HOTPLUG CORE values
	//
	translate_slot_info (hotplug_slot->info, &query);

	retval = ( query.lu_request_failed == HP_TRUE ) ? 0 : -1;
	return retval;
}


static int disable_slot (struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	struct shpc_context *shpc_context;
	struct slot_status_info query;
	long status;
	int retval = 0;

	if (slot == NULL)
		return -ENODEV;

	dbg ("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	/*
	 *  disable the specified slot
	 */
	shpc_context = ( struct shpc_context * )slot->private;
	status = hp_StartAsyncRequest(shpc_context, slot->number,
		SHPC_ASYNC_DISABLE_SLOT, 0, slot );

	//
	// pretend async request was completed (we're not queuing slot requests)
	//
	hp_QuerySlotStatus( shpc_context, slot->number, &query );
	if( status == STATUS_SUCCESS ) {
		query.lu_slot_state = SLOT_DISABLE;
		query.lu_pi_state = INDICATOR_BLINK;
		query.lu_request_failed = HP_FALSE;
	}
	else {
		query.lu_request_failed = HP_TRUE;
	}

	//
	// translate the slot info to CORE values
	//
	translate_slot_info (hotplug_slot->info, &query);

	retval = ( query.lu_request_failed == HP_TRUE ) ? 0 : -1;
	return retval;
}

static int set_attention_status (struct hotplug_slot *hotplug_slot, u8 status)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	struct shpc_context *shpc_context;
	int retval = 0;

	if (slot == NULL)
		return -ENODEV;

	dbg (" %s - physical_slot = %s  state = %d",__FUNCTION__, hotplug_slot->name, status);

	/*
	 *  turn light on/off
	 */
	shpc_context = (struct shpc_context *)slot->private;

	status = hp_StartAsyncRequest(shpc_context, slot->number,
		((status == CORE_INDICATOR_OFF) ? SHPC_ASYNC_LED_NORMAL : SHPC_ASYNC_LED_LOCATE), 10, slot);
	hotplug_slot->info->attention_status  = status;

	return retval;
}

static int get_power_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	struct shpc_context *shpc_context;
	struct slot_status_info query;
	int retval = 0;

	if (slot == NULL)
		return -ENODEV;

	dbg("%s - physical_slot = %s\n",__FUNCTION__, hotplug_slot->name);

	/*
	 * get the current power status of the specific
	 * slot and store it in the *value location.
	 */
	shpc_context = (struct shpc_context *)slot->private;
	hp_QuerySlotStatus(shpc_context, slot->number, &query);
	translate_slot_info (hotplug_slot->info, &query);
	*value = hotplug_slot->info->power_status;

	return retval;
}

static int get_attention_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	struct shpc_context *shpc_context;
	struct slot_status_info query;
	int retval = 0;

	if (slot == NULL)
		return -ENODEV;

	dbg("%s - physical_slot = %s\n",__FUNCTION__, hotplug_slot->name);

	/*
	 * get the current attention status of the specific
	 * slot and store it in the *value location.
	 */
	shpc_context = (struct shpc_context *)slot->private;
	hp_QuerySlotStatus(shpc_context, slot->number, &query);
	translate_slot_info (hotplug_slot->info, &query);
	*value = hotplug_slot->info->attention_status;

	return retval;
}

static int get_latch_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	struct shpc_context *shpc_context;
	struct slot_status_info query;
	int retval = 0;

	if (slot == NULL)
		return -ENODEV;

	dbg("%s - physical_slot = %s\n",__FUNCTION__, hotplug_slot->name);

	/*
	 * get the current latch status of the specific
	 * slot and store it in the *value location.
	 */
	shpc_context = (struct shpc_context *)slot->private;
	hp_QuerySlotStatus(shpc_context, slot->number, &query);
	translate_slot_info (hotplug_slot->info, &query);
	*value = hotplug_slot->info->latch_status;

	return retval;
}

static int get_adapter_status (struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	struct shpc_context *shpc_context;
	struct slot_status_info query;
	int retval = 0;

	if (slot == NULL)
		return -ENODEV;

	dbg("%s - physical_slot = %s\n",__FUNCTION__, hotplug_slot->name);

	/*
	 * get the current adapter status of the specific
	 * slot and store it in the *value location.
	 */
	shpc_context = (struct shpc_context *)slot->private;
	hp_QuerySlotStatus(shpc_context, slot->number, &query);
	translate_slot_info (hotplug_slot->info, &query);
	*value = hotplug_slot->info->adapter_status;

	return retval;
}

static void translate_slot_info (struct hotplug_slot_info *info,
				 struct slot_status_info *query)
{
	// power indicator
	if( query->lu_pi_state == INDICATOR_OFF ) {
		info->power_status = CORE_INDICATOR_OFF;
	}
	else if( query->lu_pi_state == INDICATOR_ON ) {
		info->power_status = CORE_INDICATOR_ON;
	}
	else {
		info->power_status = CORE_INDICATOR_BLINK;
	}

	// attention indicator
	if( query->lu_ai_state == INDICATOR_OFF ) {
		info->attention_status = CORE_INDICATOR_OFF;
	}
	else if( query->lu_ai_state == INDICATOR_ON ) {
		info->attention_status = CORE_INDICATOR_ON;
	}
	else {
		info->attention_status = CORE_INDICATOR_BLINK;
	}

	// retention latch
	if( query->lu_mrl_implemented == HP_TRUE &&
		query->lu_mrl_opened == HP_TRUE ) {
		info->latch_status = CORE_LATCH_OPENED;
	}
	else {
		info->latch_status = CORE_LATCH_CLOSED;
	}

	// adapter status
	if( query->lu_slot_state == SLOT_ENABLE ) {
		info->adapter_status = CORE_SLOT_ENABLED;
	}
	else {
		info->adapter_status = CORE_SLOT_DISABLED;
	}
}

static int hardware_test (struct hotplug_slot *hotplug_slot, u32 value)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);
	int retval = 0;

	if (slot == NULL)
		return -ENODEV;

	dbg ("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	err ("No hardware tests are defined for this driver");
	retval = -ENODEV;

	/* Or you can specify a test if you want to */
	/* AMD driver does not have a test */
	return retval;
}

#define SLOT_NAME_SIZE	10
static void make_slot_name (struct slot *slot)
{
	unsigned long slot_psn;
	struct shpc_context *shpc_context;

	shpc_context = ( struct shpc_context * )slot->private;

	//
	// Get physical slot number
	//
	hp_Queryslot_psn(shpc_context, slot->number, &slot_psn);

	snprintf (slot->hotplug_slot->name, SLOT_NAME_SIZE, "%d", (char)slot_psn);
}

static void release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = get_slot (hotplug_slot, __FUNCTION__);

	if (slot == NULL)
		return;

	dbg ("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot->name);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

static int init_slots (struct controller *ctrl, int num_slots)
{
	struct slot *slot;
	struct hotplug_slot *hotplug_slot;
	struct hotplug_slot_info *info;
	char *name;
	int retval = 0;
	int i;
	u8 value;

	/*
	 * Create a structure for each slot, and register that slot
	 * with the pci_hotplug subsystem.
	 */
	for (i = 0; i < num_slots; ++i) {
		slot = kmalloc (sizeof (struct slot), GFP_KERNEL);
		if (!slot)
			return -ENOMEM;
		memset(slot, 0, sizeof(struct slot));

		hotplug_slot = kmalloc (sizeof (struct hotplug_slot), GFP_KERNEL);
		if (!hotplug_slot) {
			kfree (slot);
			return -ENOMEM;
		}
		memset(hotplug_slot, 0, sizeof (struct hotplug_slot));
		slot->hotplug_slot = hotplug_slot;

		info = kmalloc (sizeof (struct hotplug_slot_info), GFP_KERNEL);
		if (!info) {
			kfree (hotplug_slot);
			kfree (slot);
			return -ENOMEM;
		}
		memset(info, 0, sizeof (struct hotplug_slot_info));
		hotplug_slot->info = info;

		name = kmalloc (SLOT_NAME_SIZE, GFP_KERNEL);
		if (!name) {
			kfree (info);
			kfree (hotplug_slot);
			kfree (slot);
			return -ENOMEM;
		}
		hotplug_slot->name = name;

		slot->magic = SLOT_MAGIC;
		slot->number = i;
		slot->private = (void*) ctrl->shpc_context;

		hotplug_slot->private = slot;
		hotplug_slot->release = &release_slot;
		make_slot_name (slot);
		hotplug_slot->ops = &skel_hotplug_slot_ops;

		/*
		 * Initilize the slot info structure with some known
		 * good values.
		 */
		get_power_status(hotplug_slot, &value);
		info->power_status = value;
		get_attention_status(hotplug_slot, &value);
		info->attention_status = value;
		get_latch_status(hotplug_slot, &value);
		info->latch_status = value;
		get_adapter_status(hotplug_slot, &value);
		info->adapter_status = value;

		dbg ("registering slot %d\n", i);
		retval = pci_hp_register (slot->hotplug_slot);
		if (retval) {
			err ("pci_hp_register failed with error %d\n", retval);
			kfree (info);
			kfree (name);
			kfree (hotplug_slot);
			kfree (slot);
			return retval;
		}

		/* add slot to our internal list */
		list_add (&slot->slot_list, &slot_list);
	}

	return retval;
}

static int amdshpc_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int retval;
	int loop;
	u8 pcix_slots;
	u8 x;
	u8 max_split_transactions;
	u16 vendor_id;
	u16 device_id;
	u32 rc;
	long status = STATUS_SUCCESS;
	struct controller *ctrl;
	struct shpc_context *shpc_context;
	struct slot_config_info slot_config;
	u32 pme_status_control_reg;

	rc = pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor_id);
	dbg( "%s-->Vendor ID: %x\n",__FUNCTION__, vendor_id);
	if (rc || (vendor_id != PCI_VENDOR_ID_AMD)) {
		err(msg_HPC_non_amd);
		return -ENODEV;
	}

	rc = pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id);
	dbg( "%s-->Device ID: %x\n",__FUNCTION__, device_id);
	if (rc || (device_id != PCI_DEVICE_ID_AMD_GOLAM_7450) & (device_id != PCI_DEVICE_ID_AMD_POGO_7458)) {
		err(msg_HPC_not_amd_hp);
		return -ENODEV;
	}
	
	if (vendor_id == PCI_VENDOR_ID_AMD) {

		shpc_context = (struct shpc_context *)kmalloc(sizeof(struct shpc_context), GFP_KERNEL);
		if (!shpc_context) {
			err("%s : out of memory\n",__FUNCTION__);
			return -ENOMEM;
		}
		memset(shpc_context, 0, sizeof(struct shpc_context));

		ctrl = (struct controller *)kmalloc(sizeof(struct controller), GFP_KERNEL);
		if (!ctrl) {
			err("%s : out of memory\n", __FUNCTION__);
			rc =  -ENOMEM;
			goto err_free_shpc_context;
		}
		memset(ctrl, 0, sizeof(struct controller));

		/* Set Vendor ID, so it can be accessed later from other functions */
		ctrl->vendor_id = vendor_id;

	} else {
		err(msg_HPC_not_supported);
		return -ENODEV;
	}

	ctrl->shpc_context = shpc_context;
	ctrl->pci_dev = pdev;
	ctrl->interrupt = pdev->irq;
	ctrl->device = PCI_SLOT(pdev->devfn);
	ctrl->function = PCI_FUNC(pdev->devfn);

	//
	// the AMD hotplug bus is behind a bridge
	//
	ctrl->pci_bus = pdev->subordinate;
	ctrl->bus = pdev->subordinate->number;

	dbg( "%s-->bus = %d   device = %d   function = %d\n",__FUNCTION__, ctrl->bus, ctrl->device, ctrl->function);

	info("Found PCI hot plug controller on bus %d\n", pdev->bus->number);
	info("Checking if MMIO region available for this HP controller...\n");
		    
	//
	// Get memory mapped I/O region
	//
	dbg( "%s-->pdev = %p\n",__FUNCTION__, pdev);
	dbg("%s -->pci resource start %lx\n",__FUNCTION__, pci_resource_start(pdev, 0));
	dbg("%s -->pci resource len   %lx\n",__FUNCTION__, pci_resource_len  (pdev, 0));
	if (!request_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0), MY_NAME)) {
		err("MMIO region not available, skipping\n");
		rc = -ENOMEM;
		goto err_free_ctrl;
	}

	//
	// Get linear address to put in controller structure
	//
	shpc_context->mmio_base_addr = ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	if (!shpc_context->mmio_base_addr) {
		err("cannot remap MMIO region %lx @ %lx\n", pci_resource_len(pdev, 0), pci_resource_start(pdev, 0));
		rc = -ENODEV;
		goto err_free_mem_region;
	}

	dbg("%s -->shpc_context->mmio_base_addr = %p",__FUNCTION__, (unsigned long*)shpc_context->mmio_base_addr);

	hp_AddDevice(shpc_context, ctrl, shpc_context->async_callback, shpc_instance++);
	
	//
	// Clear PME enable bit
	//
	pci_read_config_dword(pdev, SHPC_PME_CONTROL_REG_OFFSET, &pme_status_control_reg);
	pme_status_control_reg |= (0 << PME_ENABLE);
	pci_write_config_dword(pdev, SHPC_PME_CONTROL_REG_OFFSET, pme_status_control_reg);

	// Initialize controller
	shpc_context->interrupt = pdev->irq;
	dbg("%s -->shpc_context->interrupt = %d", __FUNCTION__,pdev->irq);
	if (!hp_StartDevice(shpc_context)){
		rc = -ENODEV;
		goto err_iounmap;
	}
	//
	// Get data for GOLAM and POGO PCI-X Errata issues
	//
	// get bus info to detect devices behind the shpc bridge
	pci_read_config_dword(pdev, PCI_BUS_INFO_OFFSET, &ctrl->bus_info.AsDWord);
	
	// set PCI-X load tuning parameters
	if (device_id == PCI_DEVICE_ID_AMD_GOLAM_7450){
		ctrl->pcix_max_split_transactions = 8;
		ctrl->pcix_max_read_byte_count = 2048;
	} else if (device_id == PCI_DEVICE_ID_AMD_POGO_7458) {
		ctrl->pcix_max_split_transactions = 8;
		ctrl->pcix_max_read_byte_count = 4096;
	}

	pcix_slots = shpc_context->number_of_slots;
	max_split_transactions = ctrl->pcix_max_split_transactions;

	// Assign split transactions to each slot
	for (x=0; x < shpc_context->number_of_slots; x++) {
		ctrl->max_split_trans_perslot[x] = max_split_transactions / pcix_slots;

		// update remaining credits
		max_split_transactions -= ctrl->max_split_trans_perslot[x];
		--pcix_slots;
	}

	//
	// initialize this array only once
	//
	if (shpc_context->shpc_instance == 0 ) {
		dbg("%s  Initialize slot lists\n",__FUNCTION__);
		for (loop = 0; loop < 256; loop++) {
			amdshpc_slot_list[loop] = NULL;
		}
	}

	if (!amdshpc_ctrl_list) {
		amdshpc_ctrl_list = shpc_context;
		shpc_context->next = NULL;
	} else {
		amdshpc_ctrl_list->next = shpc_context;
		shpc_context->next = NULL;
	}

	if (!ctrl_list) {
		ctrl_list = ctrl;
		ctrl->next = NULL;
	} else {
		ctrl_list->next = ctrl;
		ctrl->next = NULL;
	}

	// Map rom address so we can get the HPRT table
	amdshpc_rom_start = ioremap(ROM_PHY_ADDR, ROM_PHY_LEN);
	if (!amdshpc_rom_start) {
		err ("Could not ioremap memory region for ROM\n");
		retval = -EIO;;
		iounmap(amdshpc_rom_start);
		return retval;
	}

	//**************************************************
	//
	//	Save configuration headers for this and
	//	subordinate PCI buses
	//
	//**************************************************

	// find the physical slot number of the first hot plug slot
	status = hp_QuerySlots(shpc_context, &slot_config);
	// first slot on a bridged bus is always #1
	ctrl->first_slot = 1;
	dbg("%s  hp_QuerySlots: first_slot = %d, FDN = %d PSN_UP = %d\n",__FUNCTION__,
						ctrl->first_slot, slot_config.lu_base_FDN, slot_config.lu_PSN_up);

	if (rc) {
		err(msg_initialization_err, rc);
		goto err_iounmap;
	}

	if (!status) {
		err(msg_initialization_err, (int)status);
		goto err_iounmap;
	}

	// Store PCI Config Space for all devices on this bus
	rc = amdshpc_save_config(ctrl, ctrl->bus, &slot_config);
	if (rc) {
		err("%s: unable to save PCI configuration data, error %d",__FUNCTION__, rc);
		goto err_iounmap;
	}

	//
	// Get IO, memory, and IRQ resources for new PCI devices
	//
	rc = amdshpc_find_available_resources(ctrl, amdshpc_rom_start);
	if (rc) {
		dbg("%s -->amdshpc_find_available_resources = 0x%x\n",__FUNCTION__, rc);
		err("unable to locate PCI configuration resources for hot plug.\n");
		goto err_iounmap;
	}
	
	//
	// set global variable num_slots
	//
	num_slots = shpc_context->number_of_slots;

	dbg("%s   about to call init_slots()",__FUNCTION__);
	rc = init_slots(ctrl, num_slots);
	if (rc){
		goto err_iounmap;
	}

	return 0;

err_iounmap:
	iounmap((void *)shpc_context->mmio_base_addr);
err_free_mem_region:
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
err_free_shpc_context:
	kfree(shpc_context);
err_free_ctrl:
	kfree(ctrl);
	return rc;
}

static void cleanup_slots (void)
{
	struct list_head *tmp;
	struct list_head *next;
	struct slot *slot;

	/*
	 * Unregister all of our slots with the pci_hotplug subsystem.
	 * The memory will be freed in the release_slot() callback.
	 */
	list_for_each_safe(tmp, next, &slot_list) {
		slot = list_entry (tmp, struct slot, slot_list);
		list_del (&slot->slot_list);
		pci_hp_deregister (slot->hotplug_slot);
	}

	return;
}
static void unload_amdshpc(void)
{
	struct pci_func *next;
	struct pci_func *TempSlot;
	int loop;
	struct shpc_context *shpc_context;
	struct shpc_context *tshpc_context;
	struct controller *ctrl;
	struct controller *tctrl;
	struct pci_resource *res;
	struct pci_resource *tres;

	ctrl = ctrl_list;

	while (ctrl) {
		//reclaim PCI mem
		release_mem_region(pci_resource_start(ctrl->pci_dev, 0),
				   pci_resource_len(ctrl->pci_dev, 0));

		res = ctrl->io_head;
		while (res) {
			tres = res;
			res = res->next;
			kfree(tres);
		}

		res = ctrl->mem_head;
		while (res) {
			tres = res;
			res = res->next;
			kfree(tres);
		}

		res = ctrl->p_mem_head;
		while (res) {
			tres = res;
			res = res->next;
			kfree(tres);
		}

		res = ctrl->bus_head;
		while (res) {
			tres = res;
			res = res->next;
			kfree(tres);
		}

		tctrl = ctrl;
		ctrl = ctrl->next;
		kfree(tctrl);
	}

	for (loop = 0; loop < 256; loop++) {
		next = amdshpc_slot_list[loop];
		while (next != NULL) {
			res = next->io_head;
			while (res) {
				tres = res;
				res = res->next;
				kfree(tres);
			}

			res = next->mem_head;
			while (res) {
				tres = res;
				res = res->next;
				kfree(tres);
			}

			res = next->p_mem_head;
			while (res) {
				tres = res;
				res = res->next;
				kfree(tres);
			}

			res = next->bus_head;
			while (res) {
				tres = res;
				res = res->next;
				kfree(tres);
			}

			TempSlot = next;
			next = next->next;
			kfree(TempSlot);
		}
	}

	shpc_context = amdshpc_ctrl_list;

	while(shpc_context){

		dbg("%s -->shpc_context = %p",__FUNCTION__ , shpc_context);
		dbg("%s -->kill_amdshpc() instance = %d", __FUNCTION__ ,shpc_context->shpc_instance);
		hp_StopDevice(shpc_context);

		//Free IRQ associated with hot plug device
		free_irq(shpc_context->interrupt, shpc_context);

		//Unmap the memory
		iounmap(shpc_context->mmio_base_addr);

		// free the controller memory
		tshpc_context = shpc_context;
		shpc_context = shpc_context->next;
		kfree(tshpc_context);
	}

	//unmap the rom address
	if (amdshpc_rom_start)
		iounmap(amdshpc_rom_start);
}


static struct pci_device_id hpcd_pci_tbl[] __devinitdata = {
	{
	/* handle AMD Standard Hotplug controller */

	class:	 ((PCI_CLASS_BRIDGE_PCI << 8) | 0x00),
	class_mask:	~0,

	/* AMD makes it */
	vendor:	 	PCI_VENDOR_ID_AMD,
	device:	 	PCI_ANY_ID,
	subvendor:	PCI_ANY_ID,
	subdevice:	PCI_ANY_ID,

	}, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, hpcd_pci_tbl);



static struct pci_driver amdshpc_driver = {
	name:		"pci_hotplug",
	id_table:	hpcd_pci_tbl,
	probe:		amdshpc_probe,
	/* remove:	amdshpc_remove_one, */
};


static int __init amdshpc_init(void)
{
	int result;

	amdshpc_debug = debug;
	/*
	 * Do specific initialization stuff for your driver here
	 * Like initilizing your controller hardware (if any) and
	 * determining the number of slots you have in the system
	 * right now.
	 */

	result = pci_module_init(&amdshpc_driver);
	dbg("%s -->pci_module_init = %d\n",__FUNCTION__ , result);
	if (result)
		return result;


	info (DRIVER_DESC " version: " DRIVER_VERSION "\n");
	return 0;
}

static void __exit amdshpc_exit(void)
{
	//
	// Clean everything up.
	//
	dbg("%s -->unload_amdshpc()\n",__FUNCTION__ );
	unload_amdshpc();

	cleanup_slots();

	dbg("%s -->pci_unregister_driver\n",__FUNCTION__ );
	pci_unregister_driver(&amdshpc_driver);

}

module_init(amdshpc_init);
module_exit(amdshpc_exit);
