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
 
#define to_dev(node) container_of(node,struct device,kobj.entry)

extern struct subsystem devices_subsys;

extern int sysdev_save(u32 state);
extern int sysdev_suspend(u32 state);

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
	struct device * dev;
	int error = 0;

	down_write(&devices_subsys.rwsem);
	list_for_each_entry_reverse(dev,&devices_subsys.kset.list,kobj.entry) {
		if (dev->driver && dev->driver->suspend) {
			pr_debug("suspending device %s\n",dev->name);
			error = dev->driver->suspend(dev,state,level);
			if (error)
				printk(KERN_ERR "%s: suspend returned %d\n",
				       dev->name,error);
		}
	}
	up_write(&devices_subsys.rwsem);

	/*
	 * Make sure system devices are suspended.
	 */
	switch(level) {
	case SUSPEND_SAVE_STATE:
		sysdev_save(state);
		break;
	case SUSPEND_POWER_DOWN:
		sysdev_suspend(state);
		break;
	default:
		break;
	}

	return error;
}

