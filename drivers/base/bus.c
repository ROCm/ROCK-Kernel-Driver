/*
 * bus.c - bus driver management
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 * 
 * 
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include "base.h"

static LIST_HEAD(bus_driver_list);

#define to_dev(node) container_of(node,struct device,bus_list)
#define to_drv(node) container_of(node,struct device_driver,bus_list)

/**
 * bus_for_each_dev - walk list of devices and do something to each
 * @bus:	bus in question
 * @data:	data for the callback
 * @callback:	caller-defined action to perform on each device
 *
 * Why do we do this? So we can guarantee proper locking and reference
 * counting on devices as we touch each one.
 *
 * Algorithm:
 * Take device_lock and get the first node in the list. 
 * Try and increment the reference count on it. If we can't, it's in the
 * process of being removed, but that process hasn't acquired device_lock.
 * It's still in the list, so we grab the next node and try that one. 
 * We drop the lock to call the callback.
 * We can't decrement the reference count yet, because we need the next
 * node in the list. So, we set @prev to point to the current device.
 * On the next go-round, we decrement the reference count on @prev, so if 
 * it's being removed, it won't affect us.
 */
int bus_for_each_dev(struct bus_type * bus, void * data, 
		     int (*callback)(struct device * dev, void * data))
{
	struct list_head * node;
	struct device * prev = NULL;
	int error = 0;

	get_bus(bus);
	spin_lock(&device_lock);
	list_for_each(node,&bus->devices) {
		struct device * dev = get_device_locked(to_dev(node));
		if (dev) {
			spin_unlock(&device_lock);
			error = callback(dev,data);
			if (prev)
				put_device(prev);
			prev = dev;
			spin_lock(&device_lock);
			if (error)
				break;
		}
	}
	spin_unlock(&device_lock);
	if (prev)
		put_device(prev);
	put_bus(bus);
	return error;
}

int bus_for_each_drv(struct bus_type * bus, void * data,
		     int (*callback)(struct device_driver * drv, void * data))
{
	struct list_head * node;
	struct device_driver * prev = NULL;
	int error = 0;

	/* pin bus in memory */
	get_bus(bus);

	spin_lock(&device_lock);
	list_for_each(node,&bus->drivers) {
		struct device_driver * drv = get_driver(to_drv(node));
		if (drv) {
			spin_unlock(&device_lock);
			error = callback(drv,data);
			if (prev)
				put_driver(prev);
			prev = drv;
			spin_lock(&device_lock);
			if (error)
				break;
		}
	}
	spin_unlock(&device_lock);
	if (prev)
		put_driver(prev);
	put_bus(bus);
	return error;
}

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
		pr_debug("registering %s with bus '%s'\n",dev->bus_id,dev->bus->name);
		get_bus(dev->bus);
		spin_lock(&device_lock);
		list_add_tail(&dev->bus_list,&dev->bus->devices);
		spin_unlock(&device_lock);
		device_bus_link(dev);
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
		device_remove_symlink(&dev->bus->device_dir,dev->bus_id);
		put_bus(dev->bus);
	}
}

int bus_register(struct bus_type * bus)
{
	rwlock_init(&bus->lock);
	INIT_LIST_HEAD(&bus->devices);
	INIT_LIST_HEAD(&bus->drivers);
	atomic_set(&bus->refcount,2);

	spin_lock(&device_lock);
	list_add_tail(&bus->node,&bus_driver_list);
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
	bus_remove_dir(bus);
}

EXPORT_SYMBOL(bus_for_each_dev);
EXPORT_SYMBOL(bus_for_each_drv);
EXPORT_SYMBOL(bus_add_device);
EXPORT_SYMBOL(bus_remove_device);
EXPORT_SYMBOL(bus_register);
EXPORT_SYMBOL(put_bus);
