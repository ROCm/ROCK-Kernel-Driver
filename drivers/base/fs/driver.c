#include <linux/device.h>
#include "fs.h"

/**
 * driver_make_dir - create a driverfs directory for a driver
 * @drv:	driver in question
 */
int driver_make_dir(struct device_driver * drv)
{
	drv->dir.name = drv->name;
	return device_create_dir(&drv->dir,&drv->bus->driver_dir);
}

void driver_remove_dir(struct device_driver * drv)
{
	driverfs_remove_dir(&drv->dir);
}

