/*
 * class.c - basic device class management
 */

#include <linux/device.h>
#include <linux/module.h>
#include "base.h"

static LIST_HEAD(class_list);

int devclass_add_driver(struct device_driver * drv)
{
	if (drv->devclass) {
		pr_debug("Registering driver %s:%s with class %s\n",
			 drv->bus->name,drv->name,drv->devclass->name);

		spin_lock(&device_lock);
		list_add_tail(&drv->class_list,&drv->devclass->drivers);
		spin_unlock(&device_lock);
		devclass_drv_link(drv);
	}
	return 0;
}

void devclass_remove_driver(struct device_driver * drv)
{
	if (drv->devclass) {
		pr_debug("Removing driver %s:%s:%s\n",
			 drv->devclass->name,drv->bus->name,drv->name);
		spin_lock(&device_lock);
		list_del_init(&drv->class_list);
		spin_unlock(&device_lock);
		devclass_drv_unlink(drv);
	}
}


static void enum_device(struct device_class * cls, struct device * dev)
{
	u32 val;
	spin_lock(&device_lock);
	val = cls->devnum++;
	spin_unlock(&device_lock);
	dev->class_num = val;
	devclass_dev_link(cls,dev);
}

static void unenum_device(struct device_class * cls, struct device * dev)
{
	devclass_dev_unlink(cls,dev);
	dev->class_num = 0;
}

int devclass_add_device(struct device * dev)
{
	struct device_class * cls;
	int error = 0;

	if (dev->driver) {
		cls = dev->driver->devclass;
		if (cls) {
			pr_debug("adding device '%s' to class '%s'\n",
				 dev->name,cls->name);
			if (cls->add_device) 
				error = cls->add_device(dev);
			if (!error) {
				enum_device(cls,dev);
				interface_add(cls,dev);
			}
		}
	}
	return error;
}

void devclass_remove_device(struct device * dev)
{
	struct device_class * cls = dev->driver->devclass;
	if (cls) {
		pr_debug("removing device '%s' from class '%s'\n",
			 dev->name,cls->name);
		interface_remove(cls,dev);
		unenum_device(cls,dev);
		if (cls->remove_device)
			cls->remove_device(dev);
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

