/*
 * driver.c - centralized device driver management
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include "base.h"

/**
 * driver_make_dir - create a driverfs directory for a driver
 * @drv:	driver in question
 */
static int driver_make_dir(struct device_driver * drv)
{
	drv->dir.name = drv->name;
	return device_create_dir(&drv->dir,&drv->bus->driver_dir);
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
	write_lock(&drv->bus->lock);
	list_add(&drv->bus_list,&drv->bus->drivers);
	write_unlock(&drv->bus->lock);
	driver_make_dir(drv);
	driver_bind(drv);
	put_driver(drv);
	return 0;
}

/**
 * put_driver - decrement driver's refcount and clean up if necessary
 * @drv:	driver in question
 */
void put_driver(struct device_driver * drv)
{
	if (!atomic_dec_and_lock(&drv->refcount,&device_lock))
		return;
	spin_unlock(&device_lock);

	if (drv->bus) {
		pr_debug("Unregistering driver '%s' from bus '%s'\n",drv->name,drv->bus->name);

		driver_unbind(drv);
		write_lock(&drv->bus->lock);
		list_del_init(&drv->bus_list);
		write_unlock(&drv->bus->lock);

		driverfs_remove_dir(&drv->dir);
		put_bus(drv->bus);
	}
	if (drv->release)
		drv->release(drv);
}

EXPORT_SYMBOL(driver_register);
EXPORT_SYMBOL(put_driver);
