/*
 * suspend.c - Functions for putting devices to sleep. 
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include "power.h"
 
extern int sysdev_suspend(u32 state);

/*
 * The entries in the dpm_active list are in a depth first order, simply
 * because children are guaranteed to be discovered after parents, and 
 * are inserted at the back of the list on discovery. 
 * 
 * All list on the suspend path are done in reverse order, so we operate
 * on the leaves of the device tree (or forests, depending on how you want
 * to look at it ;) first. As nodes are removed from the back of the list, 
 * they are inserted into the front of their destintation lists. 
 *
 * Things are the reverse on the resume path - iterations are done in
 * forward order, and nodes are inserted at the back of their destination
 * lists. This way, the ancestors will be accessed before their descendents.
 */


/**
 *	suspend_device - Save state of one device.
 *	@dev:	Device.
 *	@state:	Power state device is entering.
 */

int suspend_device(struct device * dev, u32 state)
{
	int error = 0;

	dev_dbg(dev, "suspending\n");

	dev->power.prev_state = dev->power.power_state;

	if (dev->bus && dev->bus->suspend && !dev->power.power_state)
		error = dev->bus->suspend(dev,state);

	return error;
}


/**
 *	device_suspend - Save state and stop all devices in system.
 *	@state:		Power state to put each device in. 
 *
 *	Walk the dpm_active list, call ->suspend() for each device, and move
 *	it to dpm_off. 
 *	Check the return value for each. If it returns 0, then we move the
 *	the device to the dpm_off list. If it returns -EAGAIN, we move it to 
 *	the dpm_off_irq list. If we get a different error, try and back out. 
 *
 *	If we hit a failure with any of the devices, call device_resume()
 *	above to bring the suspended devices back to life. 
 *
 *	Note this function leaves dpm_sem held to
 *	a) block other devices from registering.
 *	b) prevent other PM operations from happening after we've begun.
 *	c) make sure we're exclusive when we disable interrupts.
 *
 */

int device_suspend(u32 state)
{
	int error = 0;

	down(&dpm_sem);
	while(!list_empty(&dpm_active)) {
		struct list_head * entry = dpm_active.prev;
		struct device * dev = to_device(entry);
		error = suspend_device(dev,state);

		if (!error) {
			list_del(&dev->power.entry);
			list_add(&dev->power.entry,&dpm_off);
		} else if (error == -EAGAIN) {
			list_del(&dev->power.entry);
			list_add(&dev->power.entry,&dpm_off_irq);
		} else {
			printk(KERN_ERR "Could not suspend device %s: "
				"error %d\n", kobject_name(&dev->kobj), error);
			goto Error;
		}
	}
 Done:
	up(&dpm_sem);
	return error;
 Error:
	dpm_resume();
	goto Done;
}

EXPORT_SYMBOL(device_suspend);


/**
 *	device_power_down - Shut down special devices.
 *	@state:		Power state to enter.
 *
 *	Walk the dpm_off_irq list, calling ->power_down() for each device that
 *	couldn't power down the device with interrupts enabled. When we're 
 *	done, power down system devices. 
 */

int device_power_down(u32 state)
{
	int error = 0;
	struct device * dev;

	list_for_each_entry_reverse(dev,&dpm_off_irq,power.entry) {
		if ((error = suspend_device(dev,state)))
			break;
	} 
	if (error)
		goto Error;
	if ((error = sysdev_suspend(state)))
		goto Error;
 Done:
	return error;
 Error:
	dpm_power_up();
	goto Done;
}

EXPORT_SYMBOL(device_power_down);

