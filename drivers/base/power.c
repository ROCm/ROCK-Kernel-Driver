/*
 * power.c - power management functions for the device tree.
 * 
 * Copyright (c) 2002-3 Patrick Mochel
 *		 2002-3 Open Source Development Lab
 * 
 * This file is released under the GPLv2
 * 
 *  Kai Germaschewski contributed to the list walking routines.
 *
 */

#define DEBUG

#include <linux/device.h>
#include <linux/module.h>
#include <asm/semaphore.h>
#include "base.h"

#define to_dev(node) container_of(node,struct device,kobj.entry)

extern struct subsystem devices_subsys;

/**
 * device_suspend - suspend/remove all devices on the device ree
 * @state:	state we're entering
 * @level:	what stage of the suspend process we're at
 *    (emb: it seems that these two arguments are described backwards of what
 *          they actually mean .. is this correct?)
 *
 * The entries in the global device list are inserted such that they're in a
 * depth-first ordering.  So, simply interate over the list, and call the 
 * driver's suspend or remove callback for each device.
 */
int device_suspend(u32 state, u32 level)
{
	struct list_head * node;
	int error = 0;

	printk(KERN_EMERG "Suspending devices\n");

	down_write(&devices_subsys.rwsem);
	list_for_each(node,&devices_subsys.kset.list) {
		struct device * dev = to_dev(node);
		if (dev->driver && dev->driver->suspend) {
			pr_debug("suspending device %s\n",dev->name);
			error = dev->driver->suspend(dev,state,level);
			if (error)
				printk(KERN_ERR "%s: suspend returned %d\n",dev->name,error);
		}
	}
	up_write(&devices_subsys.rwsem);
	return error;
}

/**
 * device_resume - resume all the devices in the system
 * @level:	stage of resume process we're at 
 * 
 * Similar to device_suspend above, though we want to do a breadth-first
 * walk of the tree to make sure we wake up parents before children.
 * So, we iterate over the list backward. 
 */
void device_resume(u32 level)
{
	struct list_head * node;

	down_write(&devices_subsys.rwsem);
	list_for_each_prev(node,&devices_subsys.kset.list) {
		struct device * dev = to_dev(node);
		if (dev->driver && dev->driver->resume) {
			pr_debug("resuming device %s\n",dev->name);
			dev->driver->resume(dev,level);
		}
	}
	up_write(&devices_subsys.rwsem);

	printk(KERN_EMERG "Devices Resumed\n");
}

/**
 * device_shutdown - call ->remove() on each device to shutdown. 
 */
void device_shutdown(void)
{
	struct list_head * entry;
	
	printk(KERN_EMERG "Shutting down devices\n");

	down_write(&devices_subsys.rwsem);
	list_for_each(entry,&devices_subsys.kset.list) {
		struct device * dev = to_dev(entry);
		pr_debug("shutting down %s: ",dev->name);
		if (dev->driver && dev->driver->shutdown) {
			pr_debug("Ok\n");
			dev->driver->shutdown(dev);
		} else
			pr_debug("Ignored.\n");
	}
	up_write(&devices_subsys.rwsem);
}

EXPORT_SYMBOL(device_suspend);
EXPORT_SYMBOL(device_resume);
EXPORT_SYMBOL(device_shutdown);
