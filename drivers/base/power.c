/*
 * power.c - power management functions for the device tree.
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 * 
 *  Kai Germaschewski contributed to the list walking routines.
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include "base.h"

#define to_dev(node) container_of(node,struct device,g_list)

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
	struct device * prev = NULL;
	int error = 0;

	if(level == SUSPEND_POWER_DOWN)
		printk(KERN_EMERG "Shutting down devices\n");
	else
		printk(KERN_EMERG "Suspending devices\n");

	spin_lock(&device_lock);
	list_for_each(node,&global_device_list) {
		struct device * dev = get_device_locked(to_dev(node));
		if (dev) {
			spin_unlock(&device_lock);
			if(dev->driver) {
				if(level == SUSPEND_POWER_DOWN) {
				       	if(dev->driver->remove)
						dev->driver->remove(dev);
				} else if(dev->driver->suspend) 
					error = dev->driver->suspend(dev,state,level);
			}
			if (prev)
				put_device(prev);
			prev = dev;
			spin_lock(&device_lock);
		}
	}
	spin_unlock(&device_lock);

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
	struct device * prev = NULL;

	spin_lock(&device_lock);
	list_for_each_prev(node,&global_device_list) {
		struct device * dev = get_device_locked(to_dev(node));
		if (dev) {
			spin_unlock(&device_lock);
			if (dev->driver && dev->driver->resume)
				dev->driver->resume(dev,level);
			if (prev)
				put_device(prev);
			prev = dev;
			spin_lock(&device_lock);
		}
	}
	spin_unlock(&device_lock);

	printk(KERN_EMERG "Devices Resumed\n");
}

/**
 * device_shutdown - call device_suspend with status set to shutdown, to 
 * cause all devices to remove themselves cleanly 
 */
void device_shutdown(void)
{
	device_suspend(4, SUSPEND_POWER_DOWN);
}

EXPORT_SYMBOL(device_suspend);
EXPORT_SYMBOL(device_resume);
EXPORT_SYMBOL(device_shutdown);
