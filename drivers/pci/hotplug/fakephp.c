/*
 * Fake PCI Hot Plug Controller Driver
 *
 * Copyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2003 IBM Corp.
 * Copyright (C) 2003 Rolf Eike Beer <eike-kernel@sf-tec.de>
 *
 * Based on ideas and code from:
 * 	Vladimir Kondratiev <vladimir.kondratiev@intel.com>
 *	Rolf Eike Beer <eike-kernel@sf-tec.de>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Send feedback to <greg@kroah.com>
 */

/*
 *
 * This driver will "emulate" removing PCI devices from the system.  If
 * the "power" file is written to with "0" then the specified PCI device
 * will be completely removed from the kernel.
 *
 * WARNING, this does NOT turn off the power to the PCI device.  This is
 * a "logical" removal, not a physical or electrical removal.
 *
 * Use this module at your own risk, you have been warned!
 *
 * Enabling PCI devices is left as an exercise for the reader...
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include "pci_hotplug.h"
#include "../pci.h"

#if !defined(CONFIG_HOTPLUG_PCI_FAKE_MODULE)
	#define MY_NAME	"fakephp"
#else
	#define MY_NAME	THIS_MODULE->name
#endif

#define dbg(format, arg...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG "%s: " format,	\
				MY_NAME , ## arg); 		\
	} while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME , ## arg)

#define DRIVER_AUTHOR	"Greg Kroah-Hartman <greg@kroah.com>"
#define DRIVER_DESC	"Fake PCI Hot Plug Controller Driver"

struct dummy_slot {
	struct list_head node;
	struct hotplug_slot *slot;
	struct pci_dev *dev;
};

static int debug;
static LIST_HEAD(slot_list);

static int enable_slot (struct hotplug_slot *slot);
static int disable_slot (struct hotplug_slot *slot);

static struct hotplug_slot_ops dummy_hotplug_slot_ops = {
	.owner			= THIS_MODULE,
	.enable_slot		= enable_slot,
	.disable_slot		= disable_slot,
};

static void dummy_release(struct hotplug_slot *slot)
{
	struct dummy_slot *dslot = slot->private;

	list_del(&dslot->node);
	kfree(dslot->slot->info);
	kfree(dslot->slot);
	pci_dev_put(dslot->dev);
	kfree(dslot);
}

static int add_slot(struct pci_dev *dev)
{
	struct dummy_slot *dslot;
	struct hotplug_slot *slot;
	int retval = -ENOMEM;

	slot = kmalloc(sizeof(struct hotplug_slot), GFP_KERNEL);
	if (!slot)
		goto error;
	memset(slot, 0, sizeof(*slot));

	slot->info = kmalloc(sizeof(struct hotplug_slot_info), GFP_KERNEL);
	if (!slot->info)
		goto error_slot;
	memset(slot->info, 0, sizeof(struct hotplug_slot_info));

	slot->info->power_status = 1;
	slot->info->max_bus_speed = PCI_SPEED_UNKNOWN;
	slot->info->cur_bus_speed = PCI_SPEED_UNKNOWN;

	slot->name = &dev->dev.bus_id[0];
	dbg("slot->name = %s\n", slot->name);

	dslot = kmalloc(sizeof(struct dummy_slot), GFP_KERNEL);
	if (!dslot)
		goto error_info;

	slot->ops = &dummy_hotplug_slot_ops;
	slot->release = &dummy_release;
	slot->private = dslot;

	retval = pci_hp_register(slot);
	if (retval) {
		err("pci_hp_register failed with error %d\n", retval);
		goto error_dslot;
	}

	dslot->slot = slot;
	dslot->dev = pci_dev_get(dev);
	list_add (&dslot->node, &slot_list);
	return retval;

error_dslot:
	kfree(dslot);
error_info:
	kfree(slot->info);
error_slot:
	kfree(slot);
error:
	return retval;
}

static int __init pci_scan_buses(void)
{
	struct pci_dev *dev = NULL;
	int retval = 0;

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		retval = add_slot(dev);
		if (retval) {
			pci_dev_put(dev);
			break;
		}
	}

	return retval;
}

static void remove_slot(struct dummy_slot *dslot)
{
	int retval;

	dbg("removing slot %s\n", dslot->slot->name);
	retval = pci_hp_deregister(dslot->slot);
	if (retval)
		err("Problem unregistering a slot %s\n", dslot->slot->name);
}

static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	return -ENODEV;
}

static int disable_slot(struct hotplug_slot *slot)
{
	struct dummy_slot *dslot;

	if (!slot)
		return -ENODEV;
	dslot = slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, slot->name);

	/* don't disable bridged devices just yet, we can't handle them easily... */
	if (dslot->dev->subordinate) {
		err("Can't remove PCI devices with other PCI devices behind it yet.\n");
		return -ENODEV;
	}

	/* remove the device from the pci core */
	pci_remove_bus_device(dslot->dev);

	/* blow away this sysfs entry and other parts. */
	remove_slot(dslot);

	return 0;
}

static void cleanup_slots (void)
{
	struct list_head *tmp;
	struct list_head *next;
	struct dummy_slot *dslot;

	list_for_each_safe (tmp, next, &slot_list) {
		dslot = list_entry (tmp, struct dummy_slot, node);
		remove_slot(dslot);
	}
	
}

static int __init dummyphp_init(void)
{
	info(DRIVER_DESC "\n");

	return pci_scan_buses();
}


static void __exit dummyphp_exit(void)
{
	cleanup_slots();
}

module_init(dummyphp_init);
module_exit(dummyphp_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

