/*
 * class.c - basic device class management
 */

#define DEBUG

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include "base.h"

static LIST_HEAD(class_list);

#define to_class_attr(_attr) container_of(_attr,struct devclass_attribute,attr)
#define to_class(obj) container_of(obj,struct device_class,subsys.kobj)

static ssize_t
devclass_attr_show(struct kobject * kobj, struct attribute * attr,
	      char * buf, size_t count, loff_t off)
{
	struct devclass_attribute * class_attr = to_class_attr(attr);
	struct device_class * dc = to_class(kobj);
	ssize_t ret = 0;

	if (class_attr->show)
		ret = class_attr->show(dc,buf,count,off);
	return ret;
}

static ssize_t
devclass_attr_store(struct kobject * kobj, struct attribute * attr,
	       const char * buf, size_t count, loff_t off)
{
	struct devclass_attribute * class_attr = to_class_attr(attr);
	struct device_class * dc = to_class(kobj);
	ssize_t ret = 0;

	if (class_attr->store)
		ret = class_attr->store(dc,buf,count,off);
	return ret;
}

static struct sysfs_ops class_sysfs_ops = {
	show:	devclass_attr_show,
	store:	devclass_attr_store,
};

static struct subsystem class_subsys = {
	.kobj	= { .name = "class", },
	.sysfs_ops	= &class_sysfs_ops,
};


static int devclass_dev_link(struct device_class * cls, struct device * dev)
{
	char	linkname[16];
	snprintf(linkname,16,"%u",dev->class_num);
	return sysfs_create_link(&cls->devsubsys.kobj,&dev->kobj,linkname);
}

static void devclass_dev_unlink(struct device_class * cls, struct device * dev)
{
	char	linkname[16];
	snprintf(linkname,16,"%u",dev->class_num);
	sysfs_remove_link(&cls->devsubsys.kobj,linkname);
}

static int devclass_drv_link(struct device_driver * drv)
{
	char	name[KOBJ_NAME_LEN * 3];
	snprintf(name,KOBJ_NAME_LEN * 3,"%s:%s",drv->bus->name,drv->name);
	return sysfs_create_link(&drv->devclass->drvsubsys.kobj,&drv->kobj,name);
}

static void devclass_drv_unlink(struct device_driver * drv)
{
	char	name[KOBJ_NAME_LEN * 3];
	snprintf(name,KOBJ_NAME_LEN * 3,"%s:%s",drv->bus->name,drv->name);
	return sysfs_remove_link(&drv->devclass->drvsubsys.kobj,name);
}


int devclass_create_file(struct device_class * cls, struct devclass_attribute * attr)
{
	int error;
	if (cls) {
		error = sysfs_create_file(&cls->subsys.kobj,&attr->attr);
	} else
		error = -EINVAL;
	return error;
}

void devclass_remove_file(struct device_class * cls, struct devclass_attribute * attr)
{
	if (cls)
		sysfs_remove_file(&cls->subsys.kobj,&attr->attr);
}


int devclass_add_driver(struct device_driver * drv)
{
	struct device_class * cls = get_devclass(drv->devclass);
	if (cls) {
		down_write(&cls->subsys.rwsem);
		pr_debug("device class %s: adding driver %s:%s\n",
			 cls->name,drv->bus->name,drv->name);
		list_add_tail(&drv->class_list,&cls->drivers);
		devclass_drv_link(drv);
		up_write(&cls->subsys.rwsem);
	}
	return 0;
}

void devclass_remove_driver(struct device_driver * drv)
{
	struct device_class * cls = drv->devclass;
	if (cls) {
		down_write(&cls->subsys.rwsem);
		pr_debug("device class %s: removing driver %s:%s\n",
			 cls->name,drv->bus->name,drv->name);
		list_del_init(&drv->class_list);
		devclass_drv_unlink(drv);
		up_write(&cls->subsys.rwsem);
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
			down_write(&cls->subsys.rwsem);
			pr_debug("device class %s: adding device %s\n",
				 cls->name,dev->name);
			if (cls->add_device) 
				error = cls->add_device(dev);
			if (!error) {
				enum_device(cls,dev);
				interface_add_dev(dev);
			}

			list_add_tail(&dev->class_list,&cls->devices);

			/* notify userspace (call /sbin/hotplug) */
			class_hotplug (dev, "add");

			up_write(&cls->subsys.rwsem);
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
			down_write(&cls->subsys.rwsem);
			pr_debug("device class %s: removing device %s\n",
				 cls->name,dev->name);
			interface_remove_dev(dev);
			unenum_device(cls,dev);

			list_del(&dev->class_list);

			/* notify userspace (call /sbin/hotplug) */
			class_hotplug (dev, "remove");

			if (cls->remove_device)
				cls->remove_device(dev);
			up_write(&cls->subsys.rwsem);
			put_devclass(cls);
		}
	}
}

struct device_class * get_devclass(struct device_class * cls)
{
	return cls ? container_of(subsys_get(&cls->subsys),struct device_class,subsys) : NULL;
}

void put_devclass(struct device_class * cls)
{
	subsys_put(&cls->subsys);
}


int devclass_register(struct device_class * cls)
{
	INIT_LIST_HEAD(&cls->drivers);
	INIT_LIST_HEAD(&cls->devices);

	pr_debug("device class '%s': registering\n",cls->name);
	strncpy(cls->subsys.kobj.name,cls->name,KOBJ_NAME_LEN);
	cls->subsys.parent = &class_subsys;
	subsystem_register(&cls->subsys);

	snprintf(cls->devsubsys.kobj.name,KOBJ_NAME_LEN,"devices");
	cls->devsubsys.parent = &cls->subsys;
	subsystem_register(&cls->devsubsys);

	snprintf(cls->drvsubsys.kobj.name,KOBJ_NAME_LEN,"drivers");
	cls->drvsubsys.parent = &cls->subsys;
	subsystem_register(&cls->drvsubsys);

	return 0;
}

void devclass_unregister(struct device_class * cls)
{
	pr_debug("device class '%s': unregistering\n",cls->name);
	subsystem_unregister(&cls->drvsubsys);
	subsystem_unregister(&cls->devsubsys);
	subsystem_unregister(&cls->subsys);
}

static int __init class_subsys_init(void)
{
	return subsystem_register(&class_subsys);
}

core_initcall(class_subsys_init);

EXPORT_SYMBOL(devclass_create_file);
EXPORT_SYMBOL(devclass_remove_file);
EXPORT_SYMBOL(devclass_register);
EXPORT_SYMBOL(devclass_unregister);
EXPORT_SYMBOL(get_devclass);
EXPORT_SYMBOL(put_devclass);

