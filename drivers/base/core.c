/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/init.h>
#include "base.h"

LIST_HEAD(global_device_list);

int (*platform_notify)(struct device * dev) = NULL;
int (*platform_notify_remove)(struct device * dev) = NULL;

DECLARE_MUTEX(device_sem);

spinlock_t device_lock = SPIN_LOCK_UNLOCKED;

#define to_dev(node) container_of(node,struct device,driver_list)

}

int driver_attach(struct device_driver * drv)
{
	return bus_for_each_dev(drv->bus,drv,do_driver_attach);
}

void driver_detach(struct device_driver * drv)
{
	struct list_head * node;
	struct device * prev = NULL;

	spin_lock(&device_lock);
	list_for_each(node,&drv->devices) {
		struct device * dev = get_device_locked(to_dev(node));
		if (dev) {
			if (prev)
				list_del_init(&prev->driver_list);
			spin_unlock(&device_lock);
			device_detach(dev);
			if (prev)
				put_device(prev);
			prev = dev;
			spin_lock(&device_lock);
		}
	}
	spin_unlock(&device_lock);
}

/**
 * device_register - register a device
 * @dev:	pointer to the device structure
 *
 * First, make sure that the device has a parent, create
 * a directory for it, then add it to the parent's list of
 * children.
 *
 * Maintains a global list of all devices, in depth-first ordering.
 * The head for that list is device_root.g_list.
 */
int device_add(struct device *dev)
{
	int error;

	if (!dev || !strlen(dev->bus_id))
		return -EINVAL;

	down(&device_sem);
	dev->state = DEVICE_REGISTERED;
	if (dev->parent) {
		list_add_tail(&dev->g_list,&dev->parent->g_list);
		list_add_tail(&dev->node,&dev->parent->children);
	} else
		list_add_tail(&dev->g_list,&global_device_list);
	up(&device_sem);

	pr_debug("DEV: registering device: ID = '%s', name = %s\n",
		 dev->bus_id, dev->name);

	if ((error = device_make_dir(dev)))
		goto register_done;

	bus_add_device(dev);

	/* notify platform of device entry */
	if (platform_notify)
		platform_notify(dev);

	/* notify userspace of device entry */
	dev_hotplug(dev, "add");

	devclass_add_device(dev);
 register_done:
	if (error) {
		up(&device_sem);
		list_del_init(&dev->g_list);
		list_del_init(&dev->node);
		up(&device_sem);
	}
	return error;
}

void device_initialize(struct device *dev)
{
	INIT_LIST_HEAD(&dev->node);
	INIT_LIST_HEAD(&dev->children);
	INIT_LIST_HEAD(&dev->g_list);
	INIT_LIST_HEAD(&dev->driver_list);
	INIT_LIST_HEAD(&dev->bus_list);
	INIT_LIST_HEAD(&dev->intf_list);
	spin_lock_init(&dev->lock);
	atomic_set(&dev->refcount,1);
	dev->state = DEVICE_INITIALIZED;
	if (dev->parent)
		get_device(dev->parent);
}

/**
 * device_register - register a device
 * @dev:	pointer to the device structure
 *
 * First, make sure that the device has a parent, create
 * a directory for it, then add it to the parent's list of
 * children.
 *
 * Maintains a global list of all devices, in depth-first ordering.
 * The head for that list is device_root.g_list.
 */
int device_register(struct device *dev)
{
	int error;

	if (!dev || !strlen(dev->bus_id))
		return -EINVAL;

	device_initialize(dev);
	if (dev->parent)
		get_device(dev->parent);
	error = device_add(dev);
	if (error && dev->parent)
		put_device(dev->parent);
	return error;
}

struct device * get_device(struct device * dev)
{
	struct device * ret = dev;
	down(&device_sem);
	if (device_present(dev) && atomic_read(&dev->refcount) > 0)
		atomic_inc(&dev->refcount);
	else
		ret = NULL;
	up(&device_sem);
	return ret;
}

/**
 * put_device - decrement reference count, and clean up when it hits 0
 * @dev:	device in question
 */
void put_device(struct device * dev)
{
	down(&device_sem);
	if (!atomic_dec_and_test(&dev->refcount)) {
		up(&device_sem);
		return;
	}
	list_del_init(&dev->node);
	list_del_init(&dev->g_list);
	up(&device_sem);

	BUG_ON((dev->state != DEVICE_GONE));

	device_del(dev);
}

void device_del(struct device * dev)
{
	struct device * parent = dev->parent;

	/* Notify the platform of the removal, in case they
	 * need to do anything...
	 */
	if (platform_notify_remove)
		platform_notify_remove(dev);

	/* notify userspace that this device is about to disappear */
	dev_hotplug (dev, "remove");

	bus_remove_device(dev);

	/* remove the driverfs directory */
	device_remove_dir(dev);

	if (dev->release)
		dev->release(dev);

	if (parent)
		put_device(parent);
}

/**
 * device_unregister - unlink device
 * @dev:	device going away
 *
 * The device has been removed from the system, so we disavow knowledge
 * of it. It might not be the final reference to the device, so we mark
 * it as !present, so no more references to it can be acquired.
 * In the end, we decrement the final reference count for it.
 */
void device_unregister(struct device * dev)
{
	down(&device_sem);
	dev->state = DEVICE_GONE;
	up(&device_sem);

	pr_debug("DEV: Unregistering device. ID = '%s', name = '%s'\n",
		 dev->bus_id,dev->name);
	put_device(dev);
}

EXPORT_SYMBOL(device_register);
EXPORT_SYMBOL(device_unregister);
EXPORT_SYMBOL(get_device);
EXPORT_SYMBOL(put_device);
