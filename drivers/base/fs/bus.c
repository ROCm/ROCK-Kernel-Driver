#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/stat.h>
#include "fs.h"

static struct driver_dir_entry bus_dir;

#define to_bus_attr(_attr) container_of(_attr,struct bus_attribute,attr)

#define to_bus(dir) container_of(dir,struct bus_type,dir)


/* driverfs ops for device attribute files */

static int
bus_attr_open(struct driver_dir_entry * dir)
{
	struct bus_type * bus = to_bus(dir);
	get_bus(bus);
	return 0;
}

static int
bus_attr_close(struct driver_dir_entry * dir)
{
	struct bus_type * bus = to_bus(dir);
	put_bus(bus);
	return 0;
}

static ssize_t
bus_attr_show(struct driver_dir_entry * dir, struct attribute * attr,
	      char * buf, size_t count, loff_t off)
{
	struct bus_attribute * bus_attr = to_bus_attr(attr);
	struct bus_type * bus = to_bus(dir);
	ssize_t ret = 0;

	if (bus_attr->show)
		ret = bus_attr->show(bus,buf,count,off);
	return ret;
}

static ssize_t
bus_attr_store(struct driver_dir_entry * dir, struct attribute * attr,
	       const char * buf, size_t count, loff_t off)
{
	struct bus_attribute * bus_attr = to_bus_attr(attr);
	struct bus_type * bus = to_bus(dir);
	ssize_t ret = 0;

	if (bus_attr->store)
		ret = bus_attr->store(bus,buf,count,off);
	return ret;
}

static struct driverfs_ops bus_attr_ops = {
	.open	= bus_attr_open,
	.close	= bus_attr_close,
	.show	= bus_attr_show,
	.store	= bus_attr_store,
};

int bus_create_file(struct bus_type * bus, struct bus_attribute * attr)
{
	int error;
	if (get_bus(bus)) {
		error = driverfs_create_file(&attr->attr,&bus->dir);
		put_bus(bus);
	} else
		error = -EINVAL;
	return error;
}

void bus_remove_file(struct bus_type * bus, struct bus_attribute * attr)
{
	if (get_bus(bus)) {
		driverfs_remove_file(&bus->dir,attr->attr.name);
		put_bus(bus);
	}
}

int bus_make_dir(struct bus_type * bus)
{
	int error;
	bus->dir.name = bus->name;
	bus->dir.ops = &bus_attr_ops;

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

static struct driver_dir_entry bus_dir = {
	.name	= "bus",
	.mode	= (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO),
};

static int __init bus_init(void)
{
	/* make 'bus' driverfs directory */
	return driverfs_create_dir(&bus_dir,NULL);
}

core_initcall(bus_init);

EXPORT_SYMBOL(bus_create_file);
EXPORT_SYMBOL(bus_remove_file);
