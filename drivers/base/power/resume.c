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
#include "power.h"

extern int sysdev_resume(void);


/**
 *	resume_device - Restore state for one device.
 *	@dev:	Device.
 *
 */

int resume_device(struct device * dev)
{
	if (dev->bus && dev->bus->resume)
		return dev->bus->resume(dev);
	return 0;
}


/**
 *	device_pm_resume - Restore state of each device in system.
 *
 *	Restore normal device state and release the dpm_sem.
 */

void device_pm_resume(void)
{
	while(!list_empty(&dpm_off)) {
		struct list_head * entry = dpm_off.next;
		struct device * dev = to_device(entry);
		list_del_init(entry);
		resume_device(dev);
		list_add_tail(entry,&dpm_active);
	}
	up(&dpm_sem);
}


/**
 *	device_power_up_irq - Power on some devices. 
 *
 *	Walk the dpm_off_irq list and power each device up. This 
 *	is used for devices that required they be powered down with
 *	interrupts disabled. As devices are powered on, they are moved to
 *	the dpm_suspended list.
 *
 *	Interrupts must be disabled when calling this. 
 */

void dpm_power_up(void)
{
	while(!list_empty(&dpm_off_irq)) {
		struct list_head * entry = dpm_off_irq.next;
		list_del_init(entry);
		resume_device(to_device(entry));
		list_add_tail(entry,&dpm_active);
	}
}


/**
 *	device_pm_power_up - Turn on all devices that need special attention.
 *
 *	Power on system devices then devices that required we shut them down
 *	with interrupts disabled.
 *	Called with interrupts disabled.
 */

void device_pm_power_up(void)
{
	sysdev_resume();
	dpm_power_up();
}

/**
 * device_resume - resume all the devices in the system
 * @level:	stage of resume process we're at 
 * 
 *	This function is deprecated, and should be replaced with appropriate
 *	calls to device_pm_power_up() and device_pm_resume() above.
 */

void device_resume(u32 level)
{

	printk("%s is deprecated. Called from:\n",__FUNCTION__);
	dump_stack();
}

EXPORT_SYMBOL(device_resume);
