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
	struct device * dev;
	struct device * prev = &device_root;
	int error = 0;

	printk(KERN_EMERG "Suspending Devices\n");

	get_device(prev);

	spin_lock(&device_lock);
	dev = g_list_to_dev(prev->g_list.next);
	while(dev != &device_root && !error) {
		get_device_locked(dev);
		spin_unlock(&device_lock);
		put_device(prev);

		if (dev->driver && dev->driver->suspend)
			error = dev->driver->suspend(dev,state,level);

		spin_lock(&device_lock);
		prev = dev;
		dev = g_list_to_dev(prev->g_list.next);
	}
	spin_unlock(&device_lock);
	put_device(prev);

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
	struct device * dev;
	struct device * prev = &device_root;

	get_device(prev);

	spin_lock(&device_lock);
	dev = g_list_to_dev(prev->g_list.prev);
	while(dev != &device_root) {
		get_device_locked(dev);
		spin_unlock(&device_lock);
		put_device(prev);

		if (dev->driver && dev->driver->resume)
			dev->driver->resume(dev,level);

		spin_lock(&device_lock);
		prev = dev;
		dev = g_list_to_dev(prev->g_list.prev);
	}
	spin_unlock(&device_lock);
	put_device(prev);

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
	struct device * dev;
	struct device * prev = &device_root;

	printk(KERN_EMERG "Shutting down devices\n");

	get_device(prev);

	spin_lock(&device_lock);
	dev = g_list_to_dev(prev->g_list.next);
	while(dev != &device_root) {
		dev = get_device_locked(dev);
		spin_unlock(&device_lock);
		put_device(prev);

		if (dev->driver && dev->driver->remove)
			dev->driver->remove(dev);

		spin_lock(&device_lock);
		prev = dev;
		dev = g_list_to_dev(prev->g_list.next);
	}
	spin_unlock(&device_lock);
	put_device(prev);
}

EXPORT_SYMBOL(device_suspend);
EXPORT_SYMBOL(device_resume);
EXPORT_SYMBOL(device_shutdown);
