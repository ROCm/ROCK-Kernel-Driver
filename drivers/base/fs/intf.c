/*
 * intf.c - driverfs glue for device interfaces
 */

#include <linux/device.h>
#include <linux/slab.h>
#include "fs.h"


void intf_remove_dir(struct device_interface * intf)
{
	driverfs_remove_dir(&intf->dir);
}

int intf_make_dir(struct device_interface * intf)
{
	intf->dir.name = intf->name;
	return device_create_dir(&intf->dir,&intf->devclass->dir);
}
