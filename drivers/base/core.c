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

#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

static struct iobus device_root = {
	bus_id: "root",
	name:	"Logical System Root",
};

int (*platform_notify)(struct device * dev) = NULL;
int (*platform_notify_remove)(struct device * dev) = NULL;

extern int device_make_dir(struct device * dev);
extern void device_remove_dir(struct device * dev);

extern int iobus_make_dir(struct iobus * iobus);
extern void iobus_remove_dir(struct iobus * iobus);

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
	BUG_ON(!dev->parent);

	spin_lock(&device_lock);
	INIT_LIST_HEAD(&dev->node);
	spin_lock_init(&dev->lock);
	atomic_set(&dev->refcount,2);

	get_iobus(dev->parent);
	list_add_tail(&dev->node,&dev->parent->devices);
	spin_unlock(&device_lock);

	DBG("DEV: registering device: ID = '%s', name = %s, parent = %s\n",
	    dev->bus_id, dev->name, parent->bus_id);

	if ((error = device_make_dir(dev)))
		goto register_done;

	/* notify platform of device entry */
	if (platform_notify)
		platform_notify(dev);

 register_done:
	put_device(dev);
	if (error)
		put_iobus(dev->parent);
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

	if (dev->subordinate)
		iobus_remove_dir(dev->subordinate);

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

	put_iobus(dev->parent);
}

int iobus_register(struct iobus *bus)
{
	int error;

	if (!bus || !strlen(bus->bus_id))
		return -EINVAL;
	
	spin_lock(&device_lock);
	atomic_set(&bus->refcount,2);
	spin_lock_init(&bus->lock);
	INIT_LIST_HEAD(&bus->node);
	INIT_LIST_HEAD(&bus->devices);
	INIT_LIST_HEAD(&bus->children);

	if (bus != &device_root) {
		if (!bus->parent)
			bus->parent = &device_root;
		get_iobus(bus->parent);
		list_add_tail(&bus->node,&bus->parent->children);
	}
	spin_unlock(&device_lock);

	DBG("DEV: registering bus. ID = '%s' name = '%s' parent = %p\n",
	    bus->bus_id,bus->name,bus->parent);

	error = iobus_make_dir(bus);

	put_iobus(bus);
	if (error && bus->parent)
		put_iobus(bus->parent);
	return error;
}

/**
 * iobus_unregister - remove bus and children from device tree
 * @bus:	pointer to bus structure
 *
 * Remove device from parent's list of children and decrement
 * reference count on controlling device. That should take care of
 * the rest of the cleanup.
 */
void put_iobus(struct iobus * iobus)
{
	if (!atomic_dec_and_lock(&iobus->refcount,&device_lock))
		return;
	list_del_init(&iobus->node);
	spin_unlock(&device_lock);

	if (!list_empty(&iobus->devices) ||
	    !list_empty(&iobus->children))
		BUG();

	put_iobus(iobus->parent);
	/* unregister itself */
	put_device(iobus->self);
}

static int __init device_init_root(void)
{
	/* initialize parent bus lists */
	return iobus_register(&device_root);
}

static int __init device_driver_init(void)
{
	int error = 0;

	DBG("DEV: Initialising Device Tree\n");

	spin_lock_init(&device_lock);

	error = init_driverfs_fs();

	if (error) {
		panic("DEV: could not initialise driverfs\n");
		return error;
	}

	error = device_init_root();
	if (error) {
		printk(KERN_ERR "%s: device root init failed!\n", __FUNCTION__);
		return error;
	}

	DBG("DEV: Done Initialising\n");
	return error;
}

subsys_initcall(device_driver_init);

EXPORT_SYMBOL(device_register);
EXPORT_SYMBOL(put_device);
EXPORT_SYMBOL(iobus_register);
EXPORT_SYMBOL(put_iobus);
