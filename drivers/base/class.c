/*
 * class.c - basic device class management
 */

#include <linux/device.h>
#include <linux/module.h>
#include "base.h"

static LIST_HEAD(class_list);

int devclass_add_driver(struct device_driver * drv)
{
	struct device_class * cls = get_devclass(drv->devclass);
	if (cls) {
		down_write(&cls->rwsem);
		pr_debug("device class %s: adding driver %s:%s\n",
			 cls->name,drv->bus->name,drv->name);
		list_add_tail(&drv->class_list,&cls->drivers);
		devclass_drv_link(drv);
		up_write(&cls->rwsem);
	}
	return 0;
}

void devclass_remove_driver(struct device_driver * drv)
{
	struct device_class * cls = drv->devclass;
	if (cls) {
		down_write(&cls->rwsem);
		pr_debug("device class %s: removing driver %s:%s\n",
			 cls->name,drv->bus->name,drv->name);
		list_del_init(&drv->class_list);
		devclass_drv_unlink(drv);
		up_write(&cls->rwsem);
		put_devclass(cls);
	}
}


static void enum_device(struct device_class * cls, struct device * dev)
{
	u32 val;
	val = cls->devnum++;
	dev->class_num = val;
	devclass_dev_link(cls,dev);
}

static void unenum_device(struct device_class * cls, struct device * dev)
{
	devclass_dev_unlink(cls,dev);
	dev->class_num = 0;
}

/**
 *	devclass_add_device - register device with device class
 *	@dev:   device to be registered 
 *
 *	This is called when a device is either registered with the 
 *	core, or after the a driver module is loaded and bound to
 *	the device. 
 *	The class is determined by looking at @dev's driver, so one
 *	way or another, it must be bound to something. Once the 
 *	class is determined, it's set to prevent against concurrent
 *	calls for the same device stomping on each other. 
 *
 *	/sbin/hotplug should be called once the device is added to 
 *	class and all the interfaces. 
 */
int devclass_add_device(struct device * dev)
{
	struct device_class * cls;
	int error = 0;

	if (dev->driver) {
		cls = get_devclass(dev->driver->devclass);
		if (cls) {
			down_write(&cls->rwsem);
			pr_debug("device class %s: adding device %s\n",
				 cls->name,dev->name);
			if (cls->add_device) 
				error = cls->add_device(dev);
			if (!error) {
				enum_device(cls,dev);
				interface_add(cls,dev);
			}

			/* notify userspace (call /sbin/hotplug) */
			class_hotplug (dev, "add");

			up_write(&cls->rwsem);
			if (error)
				put_devclass(cls);
		}
	}
	return error;
}

void devclass_remove_device(struct device * dev)
{
	struct device_class * cls;

	if (dev->driver) {
		cls = dev->driver->devclass;
		if (cls) {
			down_write(&cls->rwsem);
			pr_debug("device class %s: removing device %s\n",
				 cls->name,dev->name);
			interface_remove(cls,dev);
			unenum_device(cls,dev);

			/* notify userspace (call /sbin/hotplug) */
			class_hotplug (dev, "remove");

			if (cls->remove_device)
				cls->remove_device(dev);
			up_write(&cls->rwsem);
			put_devclass(cls);
		}
	}
}

struct device_class * get_devclass(struct device_class * cls)
{
	struct device_class * ret = cls;
	spin_lock(&device_lock);
	if (cls && cls->present && atomic_read(&cls->refcount) > 0)
		atomic_inc(&cls->refcount);
	else
		ret = NULL;
	spin_unlock(&device_lock);
	return ret;
}

void put_devclass(struct device_class * cls)
{
	if (atomic_dec_and_lock(&cls->refcount,&device_lock)) {
		list_del_init(&cls->node);
		spin_unlock(&device_lock);
		devclass_remove_dir(cls);
	}
}


int devclass_register(struct device_class * cls)
{
	INIT_LIST_HEAD(&cls->drivers);
	INIT_LIST_HEAD(&cls->intf_list);
	init_rwsem(&cls->rwsem);
	atomic_set(&cls->refcount,2);
	cls->present = 1;
	pr_debug("device class '%s': registering\n",cls->name);

	spin_lock(&device_lock);
	list_add_tail(&cls->node,&class_list);
	spin_unlock(&device_lock);
	devclass_make_dir(cls);
	put_devclass(cls);
	return 0;
}

void devclass_unregister(struct device_class * cls)
{
	spin_lock(&device_lock);
	cls->present = 0;
	spin_unlock(&device_lock);
	pr_debug("device class '%s': unregistering\n",cls->name);
	put_devclass(cls);
}

EXPORT_SYMBOL(devclass_register);
EXPORT_SYMBOL(devclass_unregister);
EXPORT_SYMBOL(get_devclass);
EXPORT_SYMBOL(put_devclass);

