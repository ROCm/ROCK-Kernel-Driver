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

#undef DEBUG

#include <linux/device.h>
#include <linux/module.h>
#include <asm/semaphore.h>
#include "base.h"

#define to_dev(node) container_of(node,struct device,kobj.entry)

extern struct subsystem devices_subsys;

/**
 * We handle system devices differently - we suspend and shut them 
 * down first and resume them first. That way, we do anything stupid like
 * shutting down the interrupt controller before any devices..
 *
 * Note that there are not different stages for power management calls - 
 * they only get one called once when interrupts are disabled. 
 */

extern int sysdev_shutdown(void);
extern int sysdev_save(u32 state);
extern int sysdev_suspend(u32 state);
extern int sysdev_resume(void);
extern int sysdev_restore(void);

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

	printk(KERN_EMERG "Suspending devices\n");

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

	switch (level) {
	case RESUME_POWER_ON:
		sysdev_resume();
		break;
	case RESUME_RESTORE_STATE:
		sysdev_restore();
		break;
	default:
		break;
	}

	down_write(&devices_subsys.rwsem);
	list_for_each_entry(dev,&devices_subsys.kset.list,kobj.entry) {
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
	struct device * dev;
	
	printk(KERN_EMERG "Shutting down devices\n");

	down_write(&devices_subsys.rwsem);
	list_for_each_entry_reverse(dev,&devices_subsys.kset.list,kobj.entry) {
		pr_debug("shutting down %s: ",dev->name);
		if (dev->driver && dev->driver->shutdown) {
			pr_debug("Ok\n");
			dev->driver->shutdown(dev);
		} else
			pr_debug("Ignored.\n");
	}
	up_write(&devices_subsys.rwsem);

	sysdev_shutdown();
}

EXPORT_SYMBOL(device_suspend);
EXPORT_SYMBOL(device_resume);
EXPORT_SYMBOL(device_shutdown);
