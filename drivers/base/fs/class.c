/*
 * class.c - driverfs bindings for device classes.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include "fs.h"

static struct driver_dir_entry class_dir;

void devclass_remove_dir(struct device_class * dc)
{
	driverfs_remove_dir(&dc->device_dir);
	driverfs_remove_dir(&dc->driver_dir);
	driverfs_remove_dir(&dc->dir);
}

int devclass_make_dir(struct device_class * dc)
{
	int error;

	dc->dir.name = dc->name;
	error = device_create_dir(&dc->dir,&class_dir);
	if (!error) {
		dc->driver_dir.name = "drivers";
		error = device_create_dir(&dc->driver_dir,&dc->dir);
		if (!error) {
			dc->device_dir.name = "devices";
			error = device_create_dir(&dc->device_dir,&dc->dir);
		}
		if (error)
			driverfs_remove_dir(&dc->dir);
	}
	return error;
}

static struct driver_dir_entry class_dir = {
	name:	"class",
	mode:	(S_IRWXU | S_IRUGO | S_IXUGO),
};

static int __init devclass_driverfs_init(void)
{
	return driverfs_create_dir(&class_dir,NULL);
}

core_initcall(devclass_driverfs_init);

EXPORT_SYMBOL(devclass_create_file);
EXPORT_SYMBOL(devclass_remove_file);
