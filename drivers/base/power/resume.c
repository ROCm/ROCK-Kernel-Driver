/*
 * resume.c - Functions for waking devices up.
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


extern int sysdev_resume(void);
extern int sysdev_restore(void);


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
}

