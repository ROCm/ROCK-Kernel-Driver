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
	write_unlock(&device_lock);
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
	struct device_class * cls = dev->driver->devclass;
	int error = 0;
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

int devclass_register(struct device_class * cls)
{
	INIT_LIST_HEAD(&cls->drivers);
	INIT_LIST_HEAD(&cls->intf_list);

	pr_debug("registering device class '%s'\n",cls->name);

	spin_lock(&device_lock);
	list_add_tail(&cls->node,&class_list);
	spin_unlock(&device_lock);
	devclass_make_dir(cls);
	return 0;
}

void devclass_unregister(struct device_class * cls)
{
	pr_debug("unregistering device class '%s'\n",cls->name);
	devclass_remove_dir(cls);
	spin_lock(&device_lock);
	list_del_init(&class_list);
	spin_unlock(&device_lock);
}

EXPORT_SYMBOL(devclass_register);
EXPORT_SYMBOL(devclass_unregister);

