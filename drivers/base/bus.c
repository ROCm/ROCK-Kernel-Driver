/*
 * bus.c - bus driver management
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 * 
 * 
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include "base.h"

static LIST_HEAD(bus_driver_list);

static struct driver_dir_entry bus_dir = {
	name:	"bus",
	mode:	(S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO),
};

/**
 * bus_add_device - add device to bus
 * @dev:	device being added
 *
 * Add the device to its bus's list of devices.
 * Create a symlink in the bus's 'devices' directory to the 
 * device's physical location.
 * Try and bind the device to a driver.
 */
int bus_add_device(struct device * dev)
{
	if (dev->bus) {
		get_bus(dev->bus);
		write_lock(&dev->bus->lock);
		list_add_tail(&dev->bus_list,&dev->bus->devices);
		write_unlock(&dev->bus->lock);
	}
	return 0;
}

/**
 * bus_remove_device - remove device from bus
 * @dev:	device to be removed
 *
 * Remove symlink from bus's directory.
 * Delete device from bus's list.
 */
void bus_remove_device(struct device * dev)
{
	if (dev->bus) {
		write_lock(&dev->bus->lock);
		list_del_init(&dev->bus_list);
		write_unlock(&dev->bus->lock);
		put_bus(dev->bus);
	}
}

static int bus_make_dir(struct bus_type * bus)
{
	int error;
	bus->dir.name = bus->name;

	error = device_create_dir(&bus->dir,&bus_dir);
	if (!error) {
		bus->device_dir.name = "devices";
		device_create_dir(&bus->device_dir,&bus->dir);

		bus->driver_dir.name = "drivers";
		device_create_dir(&bus->driver_dir,&bus->dir);
	}
	return error;
}


int bus_register(struct bus_type * bus)
{
	spin_lock(&device_lock);
	rwlock_init(&bus->lock);
	INIT_LIST_HEAD(&bus->devices);
	INIT_LIST_HEAD(&bus->drivers);
	list_add_tail(&bus->node,&bus_driver_list);
	atomic_set(&bus->refcount,2);
	spin_unlock(&device_lock);

	pr_debug("bus type '%s' registered\n",bus->name);

	/* give it some driverfs entities */
	bus_make_dir(bus);
	put_bus(bus);

	return 0;
}

void put_bus(struct bus_type * bus)
{
	if (!atomic_dec_and_lock(&bus->refcount,&device_lock))
		return;
	list_del_init(&bus->node);
	spin_unlock(&device_lock);

	/* remove driverfs entries */
	driverfs_remove_dir(&bus->driver_dir);
	driverfs_remove_dir(&bus->device_dir);
	driverfs_remove_dir(&bus->dir);
}

static int __init bus_init(void)
{
	/* make 'bus' driverfs directory */
	return driverfs_create_dir(&bus_dir,NULL);
}

subsys_initcall(bus_init);

EXPORT_SYMBOL(bus_add_device);
EXPORT_SYMBOL(bus_remove_device);
EXPORT_SYMBOL(bus_register);
EXPORT_SYMBOL(put_bus);
