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
	down_write(&bus->rwsem);
	list_for_each(node,&bus->devices) {
		struct device * dev = get_device(to_dev(node));
		if (dev) {
			error = callback(dev,data);
			if (prev)
				put_device(prev);
			prev = dev;
			if (error)
				break;
		}
	}
	if (prev)
		put_device(prev);
	up_write(&bus->rwsem);
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
	down_write(&bus->rwsem);
	list_for_each(node,&bus->drivers) {
		struct device_driver * drv = get_driver(to_drv(node));
		if (drv) {
			error = callback(drv,data);
			if (prev)
				put_driver(prev);
			prev = drv;
			if (error)
				break;
		}
	}
	if (prev)
		put_driver(prev);
	up_write(&bus->rwsem);
	put_bus(bus);
	return error;
}

static void attach(struct device * dev)
{
	pr_debug("bound device '%s' to driver '%s'\n",
		 dev->bus_id,dev->driver->name);
	list_add_tail(&dev->driver_list,&dev->driver->devices);
}

static int bus_match(struct device * dev, struct device_driver * drv)
{
	int error = 0;
	if (dev->bus->match(dev,drv)) {
		dev->driver = drv;
		if (drv->probe) {
			if (!(error = drv->probe(dev)))
				attach(dev);
			else
				dev->driver = NULL;
		}
	}
	return error;
}

static int device_attach(struct device * dev)
{
	struct bus_type * bus = dev->bus;
	struct list_head * entry;
	int error = 0;

	if (dev->driver) {
		attach(dev);
		return 0;
	}

	if (!bus->match)
		return 0;

	list_for_each(entry,&bus->drivers) {
		struct device_driver * drv = 
			get_driver(container_of(entry,struct device_driver,bus_list));
		if (!drv)
			continue;
		error = bus_match(dev,drv);
		put_driver(drv);
		if (!error)
			break;
	}
	return error;
}

static int driver_attach(struct device_driver * drv)
{
	struct bus_type * bus = drv->bus;
	struct list_head * entry;
	int error = 0;

	if (!bus->match)
		return 0;

	list_for_each(entry,&bus->devices) {
		struct device * dev = container_of(entry,struct device,bus_list);
		if (get_device(dev)) {
			if (!bus_match(dev,drv) && dev->driver)
				devclass_add_device(dev);
			put_device(dev);
		}
	}
	return error;
}

static void detach(struct device * dev, struct device_driver * drv)
{
	if (drv) {
		list_del_init(&dev->driver_list);
		devclass_remove_device(dev);
		if (drv->remove)
			drv->remove(dev);
		dev->driver = NULL;
	}
}

static void device_detach(struct device * dev)
{
	detach(dev,dev->driver);
}

static void driver_detach(struct device_driver * drv)
{
	struct list_head * entry;
	list_for_each(entry,&drv->devices) {
		struct device * dev = container_of(entry,struct device,driver_list);
		if (get_device(dev)) {
			detach(dev,drv);
			put_device(dev);
		}
	}
	
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
	struct bus_type * bus = get_bus(dev->bus);
	if (bus) {
		down_write(&dev->bus->rwsem);
		pr_debug("bus %s: add device %s\n",bus->name,dev->bus_id);
		list_add_tail(&dev->bus_list,&dev->bus->devices);
		device_attach(dev);
		up_write(&dev->bus->rwsem);
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
		down_write(&dev->bus->rwsem);
		pr_debug("bus %s: remove device %s\n",dev->bus->name,dev->bus_id);
		device_remove_symlink(&dev->bus->device_dir,dev->bus_id);
		device_detach(dev);
		list_del_init(&dev->bus_list);
		up_write(&dev->bus->rwsem);
		put_bus(dev->bus);
	}
}

int bus_add_driver(struct device_driver * drv)
{
	struct bus_type * bus = get_bus(drv->bus);
	if (bus) {
		down_write(&bus->rwsem);
		pr_debug("bus %s: add driver %s\n",bus->name,drv->name);
		list_add_tail(&drv->bus_list,&bus->drivers);
		driver_attach(drv);
		up_write(&bus->rwsem);
		driver_make_dir(drv);
	}
	return 0;
}

void bus_remove_driver(struct device_driver * drv)
{
	if (drv->bus) {
		down_write(&drv->bus->rwsem);
		pr_debug("bus %s: remove driver %s\n",drv->bus->name,drv->name);
		driver_detach(drv);
		list_del_init(&drv->bus_list);
		up_write(&drv->bus->rwsem);
	}
}

struct bus_type * get_bus(struct bus_type * bus)
{
	struct bus_type * ret = bus;
	spin_lock(&device_lock);
	if (bus && bus->present && atomic_read(&bus->refcount))
		atomic_inc(&bus->refcount);
	else
		ret = NULL;
	spin_unlock(&device_lock);
	return ret;
}

void put_bus(struct bus_type * bus)
{
	if (!atomic_dec_and_lock(&bus->refcount,&device_lock))
		return;
	list_del_init(&bus->node);
	spin_unlock(&device_lock);
	BUG_ON(bus->present);
	bus_remove_dir(bus);
}

int bus_register(struct bus_type * bus)
{
	init_rwsem(&bus->rwsem);
	INIT_LIST_HEAD(&bus->devices);
	INIT_LIST_HEAD(&bus->drivers);
	atomic_set(&bus->refcount,2);
	bus->present = 1;

	spin_lock(&device_lock);
	list_add_tail(&bus->node,&bus_driver_list);
	spin_unlock(&device_lock);

	pr_debug("bus type '%s' registered\n",bus->name);

	/* give it some driverfs entities */
	bus_make_dir(bus);
	put_bus(bus);

	return 0;
}

void bus_unregister(struct bus_type * bus)
{
	spin_lock(&device_lock);
	bus->present = 0;
	spin_unlock(&device_lock);

	pr_debug("bus %s: unregistering\n",bus->name);
	put_bus(bus);
}

EXPORT_SYMBOL(bus_for_each_dev);
EXPORT_SYMBOL(bus_for_each_drv);
EXPORT_SYMBOL(bus_add_device);
EXPORT_SYMBOL(bus_remove_device);
EXPORT_SYMBOL(bus_register);
EXPORT_SYMBOL(bus_unregister);
EXPORT_SYMBOL(get_bus);
EXPORT_SYMBOL(put_bus);
