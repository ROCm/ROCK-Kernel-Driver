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
extern int sysdev_restore(void);


/**
 *	resume_device - Restore state for one device.
 *	@dev:	Device.
 *
 */

int resume_device(struct device * dev)
{
	struct device_driver * drv = dev->driver;

	if (drv && drv->resume)
		return drv->resume(dev,RESUME_RESTORE_STATE);
	return 0;
}

/**
 *	dpm_resume - Restore all device state.
 *
 *	Walk the dpm_suspended list and restore each device. As they are 
 *	resumed, move the devices to the dpm_active list.
 */

int dpm_resume(void)
{
	while(!list_empty(&dpm_suspended)) {
		struct list_head * entry = dpm_suspended.next;
		struct device * dev = to_device(entry);
		list_del_init(entry);
		resume_device(dev);
		list_add_tail(entry,&dpm_active);
	}
	return 0;
}


/**
 *	device_pm_resume - Restore state of each device in system.
 *
 *	Restore system device state, then common device state. Finally,
 *	release dpm_sem, as we're done with device PM.
 */

void device_pm_resume(void)
{
	sysdev_restore();
	dpm_resume();
	up(&dpm_sem);
}


/**
 *	power_up_device - Power one device on.
 *	@dev:	Device.
 */

void power_up_device(struct device * dev)
{
	struct device_driver * drv = dev->driver;
	if (drv && drv->resume)
		drv->resume(dev,RESUME_POWER_ON);
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

void dpm_power_up_irq(void)
{
	while(!list_empty(&dpm_off_irq)) {
		struct list_head * entry = dpm_off_irq.next;
		list_del_init(entry);
		power_up_device(to_device(entry));
		list_add_tail(entry,&dpm_suspended);
	}
}


/**
 *	dpm_power_up - Power on most devices.
 *
 *	Walk the dpm_off list and power each device up. This is used
 *	to power on devices that were able to power down with interrupts
 *	enabled. 
 */

void dpm_power_up(void)
{
	while (!list_empty(&dpm_off)) {
		struct list_head * entry = dpm_off.next;
		list_del_init(entry);
		power_up_device(to_device(entry));
		list_add_tail(entry,&dpm_suspended);
	}
}


/**
 *	device_pm_power_up - Turn on all devices.
 *
 *	First, power on system devices, which must happen with interrupts 
 *	disbled. Then, power on devices that also require interrupts disabled.
 *	Turn interrupts back on, and finally power up the rest of the normal
 *	devices.
 */

void device_pm_power_up(void)
{
	sysdev_resume();
	dpm_power_up_irq();
	local_irq_enable();
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

