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

#define to_dev(obj) container_of(obj,struct device,kobj)


/*
 * sysfs bindings for devices.
 */

#define to_dev_attr(_attr) container_of(_attr,struct device_attribute,attr)

extern struct attribute * dev_default_attrs[];

static ssize_t
dev_attr_show(struct kobject * kobj, struct attribute * attr,
	      char * buf, size_t count, loff_t off)
{
	struct device_attribute * dev_attr = to_dev_attr(attr);
	struct device * dev = to_dev(kobj);
	ssize_t ret = 0;

	if (dev_attr->show)
		ret = dev_attr->show(dev,buf,count,off);
	return ret;
}

static ssize_t
dev_attr_store(struct kobject * kobj, struct attribute * attr,
	       const char * buf, size_t count, loff_t off)
{
	struct device_attribute * dev_attr = to_dev_attr(attr);
	struct device * dev = to_dev(kobj);
	ssize_t ret = 0;

	if (dev_attr->store)
		ret = dev_attr->store(dev,buf,count,off);
	return ret;
}

static struct sysfs_ops dev_sysfs_ops = {
	.show	= dev_attr_show,
	.store	= dev_attr_store,
};

struct subsystem device_subsys = {
	.kobj		= {
		.name	= "devices",
	},
	.sysfs_ops	= &dev_sysfs_ops,
	.default_attrs	= dev_default_attrs,
};


int device_create_file(struct device * dev, struct device_attribute * attr)
{
	int error = 0;
	if (get_device(dev)) {
		error = sysfs_create_file(&dev->kobj,&attr->attr);
		put_device(dev);
	}
	return error;
}

void device_remove_file(struct device * dev, struct device_attribute * attr)
{
	if (get_device(dev)) {
		sysfs_remove_file(&dev->kobj,&attr->attr);
		put_device(dev);
	}
}

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

	strncpy(dev->kobj.name,dev->bus_id,KOBJ_NAME_LEN);
	if (dev->parent)
		dev->kobj.parent = &dev->parent->kobj;
	dev->kobj.subsys = &device_subsys;
	if ((error = kobject_register(&dev->kobj)))
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
	kobject_init(&dev->kobj);
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

	WARN_ON(dev->state == DEVICE_REGISTERED);

	if (dev->state == DEVICE_GONE)
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
	kobject_unregister(&dev->kobj);
	put_device(dev);
}

static int __init device_subsys_init(void)
{
	return subsystem_register(&device_subsys);
}

core_initcall(device_subsys_init);

EXPORT_SYMBOL(device_register);
EXPORT_SYMBOL(device_unregister);
EXPORT_SYMBOL(get_device);
EXPORT_SYMBOL(put_device);

EXPORT_SYMBOL(device_create_file);
EXPORT_SYMBOL(device_remove_file);
