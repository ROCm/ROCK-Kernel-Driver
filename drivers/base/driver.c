/*
 * driver.c - centralized device driver management
 *
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "base.h"

#define to_dev(node) container_of(node,struct device,driver_list)

/*
 * helpers for creating driver attributes in sysfs
 */

int driver_create_file(struct device_driver * drv, struct driver_attribute * attr)
{
	int error;
	if (get_driver(drv)) {
		error = sysfs_create_file(&drv->kobj,&attr->attr);
		put_driver(drv);
	} else
		error = -EINVAL;
	return error;
}

void driver_remove_file(struct device_driver * drv, struct driver_attribute * attr)
{
	if (get_driver(drv)) {
		sysfs_remove_file(&drv->kobj,&attr->attr);
		put_driver(drv);
	}
}

int driver_for_each_dev(struct device_driver * drv, void * data, 
			int (*callback)(struct device *, void * ))
{
	struct list_head * node;
	int error = 0;

	drv = get_driver(drv);
	if (drv) {
		down_read(&drv->bus->rwsem);
		list_for_each(node,&drv->devices) {
			struct device * dev = get_device(to_dev(node));
			if (dev) {
				error = callback(dev,data);
				put_device(dev);
				if (error)
					break;
			}
		}
		up_read(&drv->bus->rwsem);
		put_driver(drv);
	}
	return error;
}

struct device_driver * get_driver(struct device_driver * drv)
{
	struct device_driver * ret = drv;
	spin_lock(&device_lock);
	if (drv && drv->present && atomic_read(&drv->refcount) > 0)
		atomic_inc(&drv->refcount);
	else
		ret = NULL;
	spin_unlock(&device_lock);
	return ret;
}


void remove_driver(struct device_driver * drv)
{
	BUG();
}

/**
 * put_driver - decrement driver's refcount and clean up if necessary
 * @drv:	driver in question
 */
void put_driver(struct device_driver * drv)
{
	struct bus_type * bus = drv->bus;
	if (!atomic_dec_and_lock(&drv->refcount,&device_lock))
		return;
	spin_unlock(&device_lock);
	BUG_ON(drv->present);
	if (drv->release)
		drv->release(drv);
	put_bus(bus);
}

/**
 * driver_register - register driver with bus
 * @drv:	driver to register
 * 
 * Add to bus's list of devices
 */
int driver_register(struct device_driver * drv)
{
	if (!drv->bus)
		return -EINVAL;

	pr_debug("driver %s:%s: registering\n",drv->bus->name,drv->name);

	kobject_init(&drv->kobj);
	strncpy(drv->kobj.name,drv->name,KOBJ_NAME_LEN);
	drv->kobj.subsys = &drv->bus->drvsubsys;
	kobject_register(&drv->kobj);

	get_bus(drv->bus);
	atomic_set(&drv->refcount,2);
	rwlock_init(&drv->lock);
	INIT_LIST_HEAD(&drv->devices);
	drv->present = 1;
	bus_add_driver(drv);
	put_driver(drv);
	return 0;
}

void driver_unregister(struct device_driver * drv)
{
	spin_lock(&device_lock);
	drv->present = 0;
	spin_unlock(&device_lock);
	pr_debug("driver %s:%s: unregistering\n",drv->bus->name,drv->name);
	put_driver(drv);
}

EXPORT_SYMBOL(driver_for_each_dev);
EXPORT_SYMBOL(driver_register);
EXPORT_SYMBOL(driver_unregister);
EXPORT_SYMBOL(get_driver);
EXPORT_SYMBOL(put_driver);

EXPORT_SYMBOL(driver_create_file);
EXPORT_SYMBOL(driver_remove_file);
