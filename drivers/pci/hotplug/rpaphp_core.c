/*
 * PCI Hot Plug Controller Driver for RPA-compliant PPC64 platform.
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <lxie@us.ibm.com>
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <asm/eeh.h>       /* for eeh_add_device() */
#include <asm/rtas.h>		/* rtas_call */
#include <asm/pci-bridge.h>	/* for pci_controller */
#include "../pci.h"		/* for pci_add_new_bus */
				/* and pci_do_scan_bus */
#include "rpaphp.h"
#include "pci_hotplug.h"

int debug;
static struct semaphore rpaphp_sem;
LIST_HEAD(rpaphp_slot_head);
int num_slots;

#define DRIVER_VERSION	"0.1"
#define DRIVER_AUTHOR	"Linda Xie <lxie@us.ibm.com>"
#define DRIVER_DESC	"RPA HOT Plug PCI Controller Driver"

#define MAX_LOC_CODE 128

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, 0644);

static int enable_slot(struct hotplug_slot *slot);
static int disable_slot(struct hotplug_slot *slot);
static int set_attention_status(struct hotplug_slot *slot, u8 value);
static int get_power_status(struct hotplug_slot *slot, u8 * value);
static int get_attention_status(struct hotplug_slot *slot, u8 * value);
static int get_adapter_status(struct hotplug_slot *slot, u8 * value);
static int get_max_bus_speed(struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value);
static int get_cur_bus_speed(struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value);

struct hotplug_slot_ops rpaphp_hotplug_slot_ops = {
	.owner = THIS_MODULE,
	.enable_slot = enable_slot,
	.disable_slot = disable_slot,
	.set_attention_status = set_attention_status,
	.get_power_status = get_power_status,
	.get_attention_status = get_attention_status,
	.get_adapter_status = get_adapter_status,
	.get_max_bus_speed = get_max_bus_speed,
	.get_cur_bus_speed = get_cur_bus_speed,
};

static int rpaphp_get_attention_status(struct slot *slot)
{
	return slot->hotplug_slot->info->attention_status;
}

/**
 * set_attention_status - set attention LED
 * echo 0 > attention -- set LED OFF
 * echo 1 > attention -- set LED ON
 * echo 2 > attention -- set LED ID(identify, light is blinking)
 *
 */
static int set_attention_status(struct hotplug_slot *hotplug_slot, u8 value)
{
	int retval;
	struct slot *slot = (struct slot *)hotplug_slot->private;

	down(&rpaphp_sem);
	switch (value) {
	case 0:
		retval = rpaphp_set_attention_status(slot, LED_OFF);
		hotplug_slot->info->attention_status = 0;
		break;
	case 1:
	default:
		retval = rpaphp_set_attention_status(slot, LED_ON);
		hotplug_slot->info->attention_status = 1;
		break;
	case 2:
		retval = rpaphp_set_attention_status(slot, LED_ID);
		hotplug_slot->info->attention_status = 2;
		break;
	}
	up(&rpaphp_sem);
	return retval;
}

/**
 * get_power_status - get power status of a slot
 * @hotplug_slot: slot to get status
 * @value: pointer to store status
 *
 *
 */
static int get_power_status(struct hotplug_slot *hotplug_slot, u8 * value)
{
	int retval;
	struct slot *slot = (struct slot *)hotplug_slot->private;

	down(&rpaphp_sem);
	retval = rpaphp_get_power_status(slot, value);
	up(&rpaphp_sem);
	return retval;
}

/**
 * get_attention_status - get attention LED status
 *
 *
 */
static int get_attention_status(struct hotplug_slot *hotplug_slot, u8 * value)
{
	int retval = 0;
	struct slot *slot = (struct slot *)hotplug_slot->private;

	down(&rpaphp_sem);
	*value = rpaphp_get_attention_status(slot);
	up(&rpaphp_sem);
	return retval;
}

static int get_adapter_status(struct hotplug_slot *hotplug_slot, u8 * value)
{
	struct slot *slot = (struct slot *)hotplug_slot->private;
	int retval = 0;

	down(&rpaphp_sem);
	/*  have to go through this */
	switch (slot->dev_type) {
	case PCI_DEV:
		retval = rpaphp_get_pci_adapter_status(slot, 0, value);
		break;
	case VIO_DEV:
		retval = rpaphp_get_vio_adapter_status(slot, 0, value);
		break;
	default:
		retval = -EINVAL;
	}
	up(&rpaphp_sem);
	return retval;
}

static int get_max_bus_speed(struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value)
{
	struct slot *slot = (struct slot *)hotplug_slot->private;

	down(&rpaphp_sem);
	switch (slot->type) {
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
		*value = PCI_SPEED_33MHz;	/* speed for case 1-6 */
		break;
	case 7:
	case 8:
		*value = PCI_SPEED_66MHz;
		break;
	case 11:
	case 14:
		*value = PCI_SPEED_66MHz_PCIX;
		break;
	case 12:
	case 15:
		*value = PCI_SPEED_100MHz_PCIX;
		break;
	case 13:
	case 16:
		*value = PCI_SPEED_133MHz_PCIX;
		break;
	default:
		*value = PCI_SPEED_UNKNOWN;
		break;

	}
	up(&rpaphp_sem);
	return 0;
}

/* return dummy value because not sure if PRA provides any method... */
static int get_cur_bus_speed(struct hotplug_slot *hotplug_slot, enum pci_bus_speed *value)
{
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);

	if (slot == NULL)
		return -ENODEV;

	*value = PCI_SPEED_UNKNOWN;
	return 0;
}

int rpaphp_remove_slot(struct slot *slot)
{
	int retval = 0;
	struct hotplug_slot *php_slot = slot->hotplug_slot;

	list_del(&slot->rpaphp_slot_list);
	
	/* remove "php_location" file */
	rpaphp_sysfs_remove_attr_location(php_slot);

	retval = pci_hp_deregister(php_slot);
	if (retval)
		err("Problem unregistering a slot %s\n", slot->name);

	num_slots--;

	dbg("%s - Exit: rc[%d]\n", __FUNCTION__, retval);
	return retval;
}

static int is_php_dn(struct device_node *dn, int **indexes, int **names, int **types,
	  int **power_domains)
{
	*indexes = (int *) get_property(dn, "ibm,drc-indexes", NULL);
	if (!*indexes)
		return (0);
	/* &names[1] contains NULL terminated slot names */
	*names = (int *) get_property(dn, "ibm,drc-names", NULL);
	if (!*names)
		return (0);
	/* &types[1] contains NULL terminated slot types */
	*types = (int *) get_property(dn, "ibm,drc-types", NULL);
	if (!*types)
		return (0);
	/* power_domains[1...n] are the slot power domains */
	*power_domains = (int *) get_property(dn,
					      "ibm,drc-power-domains", NULL);
	if (!*power_domains)
		return (0);
	if (strcmp(dn->name, "pci") == 0 &&
	    !get_property(dn, "ibm,fw-pci-hot-plug-ctrl", NULL))
		return (0);
	return (1);
}

static inline int is_vdevice_root(struct device_node *dn)
{
	return !strcmp(dn->name, "vdevice");
}

/*************************************
 * Add  Hot Plug slot(s) to sysfs
 *
 ************************************/
int rpaphp_add_slot(struct device_node *dn)
{
	struct slot *slot;
	int retval = 0;
	int i;
	int *indexes, *names, *types, *power_domains;
	char *name, *type;

	dbg("Entry %s: dn->full_name=%s\n", __FUNCTION__, dn->full_name);

	if (dn->parent && is_vdevice_root(dn->parent)) {
		/* register a VIO device */
		retval = register_vio_slot(dn);
		goto exit;
	}

	/* register PCI devices */
	if (dn->name != 0 && strcmp(dn->name, "pci") == 0 &&
	    is_php_dn(dn, &indexes, &names, &types, &power_domains)) {

		name = (char *) &names[1];
		type = (char *) &types[1];
		for (i = 0; i < indexes[0];
		     i++,
		     name += (strlen(name) + 1), type += (strlen(type) + 1)) {
			if (!(slot = alloc_slot_struct(dn, indexes[i + 1], name,
						       power_domains[i + 1]))) {
				retval = -ENOMEM;
				goto exit;
			}
			slot->type = simple_strtoul(type, NULL, 10);
			if (slot->type < 1 || slot->type > 16)
				slot->type = 0;
			retval = register_pci_slot(slot);

		}		/* for indexes */
	}			/* end of PCI device_node */
      exit:
	dbg("%s - Exit: num_slots=%d rc[%d]\n",
	    __FUNCTION__, num_slots, retval);
	return retval;
}

/*
 * init_slots - initialize 'struct slot' structures for each slot
 *
 */
static void init_slots(void)
{
	struct device_node *dn;

	for (dn = find_all_nodes(); dn; dn = dn->next)
		rpaphp_add_slot(dn);
}

static int init_rpa(void)
{

	init_MUTEX(&rpaphp_sem);

	/* initialize internal data structure etc. */
	init_slots();
	if (!num_slots)
		return -ENODEV;

	return 0;
}

static void cleanup_slots(void)
{
	struct list_head *tmp, *n;
	struct slot *slot;

	/*
	 * Unregister all of our slots with the pci_hotplug subsystem,
	 * and free up all memory that we had allocated.
	 * memory will be freed in release_slot callback. 
	 */

	list_for_each_safe(tmp, n, &rpaphp_slot_head) {
		slot = list_entry(tmp, struct slot, rpaphp_slot_list);
		list_del(&slot->rpaphp_slot_list);
		pci_hp_deregister(slot->hotplug_slot);
	}
	return;
}

static int __init rpaphp_init(void)
{
	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");

	/* read all the PRA info from the system */
	return init_rpa();
}

static void __exit rpaphp_exit(void)
{
	cleanup_slots();
}

static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	int retval = 0;
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);

	if (slot == NULL)
		return -ENODEV;

	if (slot->state == CONFIGURED) {
		dbg("%s: %s is already enabled\n", __FUNCTION__, slot->name);
		goto exit;
	}

	dbg("ENABLING SLOT %s\n", slot->name);
	down(&rpaphp_sem);
	switch (slot->dev_type) {
	case PCI_DEV:
		retval = rpaphp_enable_pci_slot(slot);
		break;
	case VIO_DEV:
		retval = rpaphp_enable_vio_slot(slot);
		break;
	default:
		retval = -EINVAL;
	}
	up(&rpaphp_sem);
      exit:
	dbg("%s - Exit: rc[%d]\n", __FUNCTION__, retval);
	return retval;
}

static int disable_slot(struct hotplug_slot *hotplug_slot)
{
	int retval;
	struct slot *slot = get_slot(hotplug_slot, __FUNCTION__);

	if (slot == NULL)
		return -ENODEV;

	dbg("%s - Entry: slot[%s]\n", __FUNCTION__, slot->name);

	if (slot->state == NOT_CONFIGURED) {
		dbg("%s: %s is already disabled\n", __FUNCTION__, slot->name);
		goto exit;
	}

	dbg("DISABLING SLOT %s\n", slot->name);
	down(&rpaphp_sem);
	switch (slot->dev_type) {
	case PCI_DEV:
		rpaphp_set_attention_status(slot, LED_ID);
		retval = rpaphp_unconfig_pci_adapter(slot);
		rpaphp_set_attention_status(slot, LED_OFF);
		break;
	case VIO_DEV:
		retval = rpaphp_unconfig_vio_adapter(slot);
		break;
	default:
		retval = -ENODEV;
	}
	up(&rpaphp_sem);
      exit:
	dbg("%s - Exit: rc[%d]\n", __FUNCTION__, retval);
	return retval;
}

module_init(rpaphp_init);
module_exit(rpaphp_exit);

EXPORT_SYMBOL_GPL(rpaphp_add_slot);
EXPORT_SYMBOL_GPL(rpaphp_remove_slot);
EXPORT_SYMBOL_GPL(rpaphp_slot_head);
