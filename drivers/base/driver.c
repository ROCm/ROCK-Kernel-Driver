/*
 * driver.c - centralized device driver management
 *
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include "base.h"


int driver_for_each_dev(struct device_driver * drv, void * data, int (*callback)(struct device *, void * ))
{
	struct device * next;
	struct device * dev = NULL;
	struct list_head * node;
	int error = 0;

	get_driver(drv);
	spin_lock(&device_lock);
	node = drv->devices.next;
	while (node != &drv->devices) {
		next = list_entry(node,struct device,driver_list);
		get_device_locked(next);
		spin_unlock(&device_lock);

		if (dev)
			put_device(dev);
		dev = next;
		if ((error = callback(dev,data))) {
			put_device(dev);
			break;
		}
		spin_lock(&device_lock);
		node = dev->driver_list.next;
	}
	spin_unlock(&device_lock);
	if (dev)
		put_device(dev);
	put_driver(drv);
	return error;
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

	pr_debug("Registering driver '%s' with bus '%s'\n",drv->name,drv->bus->name);

	get_bus(drv->bus);
	atomic_set(&drv->refcount,2);
	rwlock_init(&drv->lock);
	INIT_LIST_HEAD(&drv->devices);
	spin_lock(&device_lock);
	list_add(&drv->bus_list,&drv->bus->drivers);
	spin_unlock(&device_lock);
	driver_make_dir(drv);
	driver_attach(drv);
	put_driver(drv);
	return 0;
}

static void __remove_driver(struct device_driver * drv)
{
	pr_debug("Unregistering driver '%s' from bus '%s'\n",drv->name,drv->bus->name);
	driver_detach(drv);
	driver_remove_dir(drv);
	if (drv->release)
		drv->release(drv);
	put_bus(drv->bus);
}

void remove_driver(struct device_driver * drv)
{
	spin_lock(&device_lock);
	atomic_set(&drv->refcount,0);
	list_del_init(&drv->bus_list);
	spin_unlock(&device_lock);
	__remove_driver(drv);
}

/**
 * put_driver - decrement driver's refcount and clean up if necessary
 * @drv:	driver in question
 */
void put_driver(struct device_driver * drv)
{
	if (!atomic_dec_and_lock(&drv->refcount,&device_lock))
		return;
	list_del_init(&drv->bus_list);
	spin_unlock(&device_lock);
	__remove_driver(drv);
}

EXPORT_SYMBOL(driver_for_each_dev);
EXPORT_SYMBOL(driver_register);
EXPORT_SYMBOL(put_driver);
EXPORT_SYMBOL(remove_driver);
