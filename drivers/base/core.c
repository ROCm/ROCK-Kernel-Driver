/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/err.h>
#include "base.h"

struct device device_root = {
	bus_id:		"root",
	name:		"System root",
};

int (*platform_notify)(struct device * dev) = NULL;
int (*platform_notify_remove)(struct device * dev) = NULL;

spinlock_t device_lock = SPIN_LOCK_UNLOCKED;

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
	struct device *prev_dev;

	if (!dev || !strlen(dev->bus_id))
		return -EINVAL;

	spin_lock(&device_lock);
	INIT_LIST_HEAD(&dev->node);
	INIT_LIST_HEAD(&dev->children);
	INIT_LIST_HEAD(&dev->g_list);
	spin_lock_init(&dev->lock);
	atomic_set(&dev->refcount,2);

	if (dev != &device_root) {
		if (!dev->parent)
			dev->parent = &device_root;
		get_device(dev->parent);

		if (list_empty(&dev->parent->children))
			prev_dev = dev->parent;
		else
			prev_dev = list_entry(dev->parent->children.prev, struct device, node);
		list_add(&dev->g_list, &prev_dev->g_list);

		list_add_tail(&dev->node,&dev->parent->children);
	}
	spin_unlock(&device_lock);

	DBG("DEV: registering device: ID = '%s', name = %s\n",
	    dev->bus_id, dev->name);

	if ((error = device_make_dir(dev)))
		goto register_done;

	bus_add_device(dev);

	/* notify platform of device entry */
	if (platform_notify)
		platform_notify(dev);

 register_done:
	put_device(dev);
	if (error && dev->parent)
		put_device(dev->parent);
	return error;
}

/**
 * put_device - decrement reference count, and clean up when it hits 0
 * @dev:	device in question
 */
void put_device(struct device * dev)
{
	if (!atomic_dec_and_lock(&dev->refcount,&device_lock))
		return;
	list_del_init(&dev->node);
	list_del_init(&dev->g_list);
	spin_unlock(&device_lock);

	DBG("DEV: Unregistering device. ID = '%s', name = '%s'\n",
	    dev->bus_id,dev->name);

	/* Notify the platform of the removal, in case they
	 * need to do anything...
	 */
	if (platform_notify_remove)
		platform_notify_remove(dev);

	bus_remove_device(dev);

	/* Tell the driver to clean up after itself.
	 * Note that we likely didn't allocate the device,
	 * so this is the driver's chance to free that up...
	 */
	if (dev->driver && dev->driver->remove)
		dev->driver->remove(dev,REMOVE_FREE_RESOURCES);

	/* remove the driverfs directory */
	device_remove_dir(dev);

	if (dev->release)
		dev->release(dev);

	put_device(dev->parent);
}

static int __init device_init_root(void)
{
	return device_register(&device_root);
}

static int __init device_init(void)
{
	int error;

	error = init_driverfs_fs();
	if (error) {
		panic("DEV: could not initialize driverfs");
		return error;
	}
	error = device_init_root();
	if (error)
		printk(KERN_ERR "%s: device root init failed!\n", __FUNCTION__);
	return error;
}

core_initcall(device_init);

EXPORT_SYMBOL(device_register);
EXPORT_SYMBOL(put_device);
