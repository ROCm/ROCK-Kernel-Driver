/*
 * Dummy PCI Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2003 IBM Corp.
 * Copyright (C) 2002 Hiroshi Aono (h-aono@ap.jp.nec.com)
 * Copyright (C) 2002 Takayoshi Kochi (t-kouchi@cq.jp.nec.com)
 * Copyright (C) 2002 NEC Corporation
 * Copyright (C) 2002 Vladimir Kondratiev (vladimir.kondratiev@intel.com)
 * Copyright (C) 2003 Rolf Eike Beer (eike-kernel@sf-tec.de)
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
 * Send feedback to <eike-kernel@sf-tec.de>
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include "pci_hotplug.h"
#include "../pci.h"

#if !defined(CONFIG_HOTPLUG_PCI_DUMMY_MODULE)
	#define MY_NAME	"dummyphp"
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

/* name size which is used for entries in pcihpfs */
#define SLOT_NAME_SIZE	32		/* DUMMY-{BUS}:{DEV} */

struct dummy_slot {
	struct pci_bus	*bus;
	int		devfn;
};

static int debug;

#define DRIVER_VERSION	"0.1"
#define DRIVER_AUTHOR	"Rolf Eike Beer <eike-kernel@sf-tec.de>"
#define DRIVER_DESC	"Dummy PCI Hot Plug Controller Driver"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

static int enable_slot		(struct hotplug_slot *slot);
static int disable_slot		(struct hotplug_slot *slot);

/*
   This is a dummy driver, so we make us live as easy as possible:
   -if there is an adapter registered in the linux PCI system, then it is present
   -if there is an adapter present, the power is on (and vice versa!)
   -if the power is on, the latch is closed (and vice versa)
   -the attention LED is always off

   So:
   => latch_status = adapter_status = power_status
 */

static struct hotplug_slot_ops dummy_hotplug_slot_ops = {
	.owner			= THIS_MODULE,
	.enable_slot		= enable_slot,
	.disable_slot		= disable_slot,
};

/**
 * enable_slot - power on and enable a slot
 * @hotplug_slot: slot to enable
 */
static int
enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct dummy_slot *slot;

	if (!hotplug_slot)
		return -ENODEV;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	/* enable the specified slot */
	slot = (struct dummy_slot *) hotplug_slot->private;

	if (pci_scan_slot(slot->bus, slot->devfn)) {
		hotplug_slot->info->power_status = 1;
		hotplug_slot->info->adapter_status = 1;
		hotplug_slot->info->latch_status = 1;
		return 0;
	}
	return -ENODEV;
}


/**
 * disable_slot - disable any adapter in this slot
 * @hotplug_slot: slot to disable
 */
static int
disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct pci_dev* old_dev;
	int func;
	struct dummy_slot *slot;

	if (!hotplug_slot)
		return -ENODEV;

	slot = (struct dummy_slot *) hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	/* disable the specified slot */

	for (func=0; func<8; func++) {
		old_dev = pci_find_slot(slot->bus->number, slot->devfn+func);
		if (old_dev) {
			printk(KERN_INFO "Slot %s removed\n", old_dev->slot_name);
			pci_remove_device_safe(old_dev);
		}
	}
	hotplug_slot->info->power_status = 0;
	hotplug_slot->info->adapter_status = 0;
	hotplug_slot->info->latch_status = 0;


	return 0;
}


/**
 * dummyphp_get_power_status - look if an adapter is configured in this slot
 * @slot: the slot to test
 */
static int dummyphp_get_power_status(struct dummy_slot *slot)
{
	struct pci_dev* old_dev;
	int func;

	for (func = 0; func < 8; func++) {
		old_dev = pci_find_slot(slot->bus->number, slot->devfn+func);
		if (old_dev)
			return 1;
	}

	return 0;
}

/**
 * scan_pci_bus - add an entry for every slot on this bus
 * @bus: bus to scan
 */
static int __init
scan_pci_bus(const struct pci_bus *bus)
{
	struct dummy_slot *dslot;
	struct hotplug_slot *hp;
	int retval = 0;
	unsigned int devfn;
	struct pci_dev dev0;

	memset(&dev0, 0, sizeof(dev0));
	dev0.bus = (struct pci_bus*)bus;
	dev0.sysdata = bus->sysdata;
	for (devfn = 0; devfn < 0x100; devfn += 8) {
		hp = kmalloc(sizeof(struct hotplug_slot), GFP_KERNEL);
		if (!hp) {
			return -ENOMEM;
		}
		memset(hp, 0, sizeof(struct hotplug_slot));

		hp->info = kmalloc(sizeof(struct hotplug_slot_info), GFP_KERNEL);
		if (!hp->info) {
			kfree(hp);
			return -ENOMEM;
		}
		memset(hp->info, 0, sizeof(struct hotplug_slot_info));

		hp->name = kmalloc(SLOT_NAME_SIZE, GFP_KERNEL);
		dslot = kmalloc(sizeof(struct dummy_slot), GFP_KERNEL);
		if ( (!hp->name) || (!dslot) ) {
			kfree(hp->info);
			kfree(hp->name);
			kfree(dslot);
			kfree(hp);
			return -ENOMEM;
		}
		dslot->bus = (struct pci_bus *) bus;
		dslot->devfn = devfn;

		hp->ops = &dummy_hotplug_slot_ops;
		hp->private = (void *) dslot;

		hp->info->power_status = dummyphp_get_power_status(dslot);
		hp->info->latch_status = hp->info->power_status;
		hp->info->adapter_status = hp->info->power_status;

		/* set fixed values so we do not need callbacks for these */
		hp->info->attention_status = 0;
		hp->info->cur_bus_speed = PCI_SPEED_UNKNOWN;
		hp->info->max_bus_speed = PCI_SPEED_UNKNOWN;

		snprintf(hp->name, SLOT_NAME_SIZE, "DUMMY-%02x:%02x",
			dslot->bus->number,
			dslot->devfn / 8);

		retval = pci_hp_register(hp);
		if (retval) {
			err("pci_hp_register failed with error %d\n", retval);
			kfree(hp->info);
			kfree(hp->name);
			kfree(hp);
			kfree(dslot);
			return retval;
		}

	}
	return 0;
}

/**
 * scan_pci_buses - scan this bus and all child buses for slots
 * @list: list of buses to scan
 */
static int __init
pci_scan_buses(const struct list_head *list)
{
	int retval;
	const struct list_head *l;

	list_for_each(l,list) {
		const struct pci_bus *b = pci_bus_b(l);
		retval = scan_pci_bus(b);
		if (retval)
			return retval;
		retval = pci_scan_buses(&b->children);
		if (retval)
			return retval;
	}
	return 0;
}

static void __exit
cleanup_slots (void)
{
/*FIXME: look at every hotplug_slot if name begins with "DUMMY-" an kick them*/
}


static int __init
dummyphp_init(void)
{
	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");

	return pci_scan_buses(&pci_root_buses);
}


static void __exit
dummyphp_exit(void)
{
	cleanup_slots();
}

module_init(dummyphp_init);
module_exit(dummyphp_exit);
