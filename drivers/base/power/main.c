/*
 * drivers/base/power/main.c - Where the driver meets power management.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 *
 * This file is released under the GPLv2
 *
 *
 * The driver model core calls device_pm_add() when a device is registered.
 * This will intialize the embedded device_pm_info object in the device
 * and add it to the list of power-controlled devices. sysfs entries for
 * controlling device power management will also be added.
 *
 * A different set of lists than the global subsystem list are used to 
 * keep track of power info because we use different lists to hold 
 * devices based on what stage of the power management process they 
 * are in. The power domain dependencies may also differ from the 
 * ancestral dependencies that the subsystem list maintains.
 */

#define DEBUG

#include <linux/device.h>


static LIST_HEAD(dpm_active_list);

static spinlock_t dpm_lock = SPIN_LOCK_UNLOCKED;
static DECLARE_MUTEX(dpm_sem);

static struct attribute power_attrs[] = {
	{ .name = NULL },
};
static struct attribute_group pm_attr_group = {
	.name	= "pm",
	.attrs	= power_attrs,
};

int device_pm_add(struct device * dev)
{
	int error;

	pr_debug("PM: Adding info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus", dev->kobj.name);
	down(&dpm_sem);
	spin_lock(&dpm_lock);
	list_add_tail(&dev->power.entry,&dpm_active_list);
	spin_unlock(&dpm_lock);
	error = sysfs_create_group(&dev->kobj,&pm_attr_group);
	up(&dpm_sem);
	return error;
}

void device_pm_remove(struct device * dev)
{
	pr_debug("PM: Removing info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus", dev->kobj.name);
	down(&dpm_sem);
	sysfs_remove_group(&dev->kobj,&pm_attr_group);
	spin_lock(&dpm_lock);
	list_del(&dev->power.entry);
	spin_unlock(&dpm_lock);
	up(&dpm_sem);
}
