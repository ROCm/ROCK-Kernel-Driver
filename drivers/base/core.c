/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>

#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

static struct device device_root = {
	bus_id:		"root",
	name:		"System Root",
};

int (*platform_notify)(struct device * dev) = NULL;
int (*platform_notify_remove)(struct device * dev) = NULL;

extern int device_make_dir(struct device * dev);
extern void device_remove_dir(struct device * dev);

static spinlock_t device_lock;

/**
 * device_register - register a device
 * @dev:	pointer to the device structure
 *
 * First, make sure that the device has a parent, create
 * a directory for it, then add it to the parent's list of
 * children.
 */
int device_register(struct device *dev)
{
	int error;

	if (!dev || !strlen(dev->bus_id))
		return -EINVAL;

	spin_lock(&device_lock);
	INIT_LIST_HEAD(&dev->node);
	INIT_LIST_HEAD(&dev->children);
	spin_lock_init(&dev->lock);
	atomic_set(&dev->refcount,2);

	if (dev != &device_root) {
		if (!dev->parent)
			dev->parent = &device_root;
		get_device(dev->parent);
		list_add_tail(&dev->node,&dev->parent->children);
	}
	spin_unlock(&device_lock);

	DBG("DEV: registering device: ID = '%s', name = %s\n",
	    dev->bus_id, dev->name);

	if ((error = device_make_dir(dev)))
		goto register_done;

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
 * put_device - clean up device
 * @dev:	device in question
 *
 * Decrement reference count for device.
 * If it hits 0, we need to clean it up.
 * However, we may be here in interrupt context, and it may
 * take some time to do proper clean up (removing files, calling
 * back down to device to clean up everything it has).
 * So, we remove it from its parent's list and add it to the list of
 * devices to be cleaned up.
 */
void put_device(struct device * dev)
{
	if (!atomic_dec_and_lock(&dev->refcount,&device_lock))
		return;
	list_del_init(&dev->node);
	spin_unlock(&device_lock);

	DBG("DEV: Unregistering device. ID = '%s', name = '%s'\n",
	    dev->bus_id,dev->name);

	/* remove the driverfs directory */
	device_remove_dir(dev);

	/* Notify the platform of the removal, in case they
	 * need to do anything...
	 */
	if (platform_notify_remove)
		platform_notify_remove(dev);

	/* Tell the driver to clean up after itself.
	 * Note that we likely didn't allocate the device,
	 * so this is the driver's chance to free that up...
	 */
	if (dev->driver && dev->driver->remove)
		dev->driver->remove(dev,REMOVE_FREE_RESOURCES);

	put_device(dev->parent);
}

static int __init device_init_root(void)
{
	return device_register(&device_root);
}

static int __init device_init(void)
{
	int error = 0;

	DBG("DEV: Initialising Device Tree\n");

	spin_lock_init(&device_lock);

	error = init_driverfs_fs();

	if (error) {
		panic("DEV: could not initialise driverfs\n");
		return error;
	}

	if ((error = device_init_root()))
		printk(KERN_ERR "%s: device root init failed!\n", __FUNCTION__);
	return error;
}

subsys_initcall(device_init);

EXPORT_SYMBOL(device_register);
EXPORT_SYMBOL(put_device);
