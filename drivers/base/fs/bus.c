#include <linux/device.h>
#include <linux/init.h>
#include <linux/stat.h>
#include "fs.h"

static struct driver_dir_entry bus_dir = {
	name:	"bus",
	mode:	(S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO),
};

int bus_make_dir(struct bus_type * bus)
{
	int error;
	bus->dir.name = bus->name;

	error = device_create_dir(&bus->dir,&bus_dir);
	if (!error) {
		bus->device_dir.name = "devices";
		device_create_dir(&bus->device_dir,&bus->dir);

		bus->driver_dir.name = "drivers";
		device_create_dir(&bus->driver_dir,&bus->dir);
	}
	return error;
}

void bus_remove_dir(struct bus_type * bus)
{
	/* remove driverfs entries */
	driverfs_remove_dir(&bus->driver_dir);
	driverfs_remove_dir(&bus->device_dir);
	driverfs_remove_dir(&bus->dir);
}

static int __init bus_init(void)
{
	/* make 'bus' driverfs directory */
	return driverfs_create_dir(&bus_dir,NULL);
}

core_initcall(bus_init);

