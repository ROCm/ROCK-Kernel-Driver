/*
 * power.c - power management functions for the device tree.
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 * 
 *  Kai Germaschewski contributed to the list walking routines.
 *
 * FIXME: The suspend and shutdown walks are identical. The resume walk
 * is simply walking the list backward. Anyway we can combine these (cleanly)?
 */

#include <linux/device.h>
#include <linux/module.h>
#include "base.h"

#define to_dev(node) container_of(node,struct device,g_list)

/**
 * device_suspend - suspend all devices on the device tree
 * @state:	state we're entering
 * @level:	what stage of the suspend process we're at 
 * 
 * The entries in the global device list are inserted such that they're in a 
 * depth-first ordering. So, simply iterate over the list, and call the driver's
 * suspend callback for each device.
 */
int device_suspend(u32 state, u32 level)
{
	struct list_head * node;
	struct device * prev = NULL;
	int error = 0;

	printk(KERN_EMERG "Suspending Devices\n");

	spin_lock(&device_lock);
	list_for_each(node,&device_root.g_list) {
		struct device * dev = get_device_locked(dev);
		if (dev) {
			spin_unlock(&device_lock);
			if (dev->driver && dev->driver->suspend)
				error = dev->driver->suspend(dev,state,level);
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
	list_for_each_prev(node,&device_root.g_list) {
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
 * device_shutdown - queisce all the devices before reboot/shutdown
 *
 * Do depth first iteration over device tree, calling ->remove() for each
 * device. This should ensure the devices are put into a sane state before
 * we reboot the system.
 *
 */
void device_shutdown(void)
{
	struct list_head * node;
	struct device * prev = NULL;

	printk(KERN_EMERG "Shutting down devices\n");

	spin_lock(&device_lock);
	list_for_each(node,&device_root.g_list) {
		struct device * dev = get_device_locked(to_dev(node));
		if (dev) {
			spin_unlock(&device_lock);
			if (dev->driver && dev->driver->remove)
				dev->driver->remove(dev);
			if (prev)
				put_device(prev);
			prev = dev;
			spin_lock(&device_lock);
		}
	}
	spin_unlock(&device_lock);
}

EXPORT_SYMBOL(device_suspend);
EXPORT_SYMBOL(device_resume);
EXPORT_SYMBOL(device_shutdown);
