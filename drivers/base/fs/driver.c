#include <linux/device.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/err.h>
#include "fs.h"

#define to_drv_attr(_attr) container_of(_attr,struct driver_attribute,attr)

#define to_drv(d) container_of(d, struct device_driver, dir)


/* driverfs ops for device attribute files */

static int
drv_attr_open(struct driver_dir_entry * dir)
{
	struct device_driver * drv = to_drv(dir);
	get_driver(drv);
	return 0;
}

static int
drv_attr_close(struct driver_dir_entry * dir)
{
	struct device_driver * drv = to_drv(dir);
	put_driver(drv);
	return 0;
}

static ssize_t
drv_attr_show(struct driver_dir_entry * dir, struct attribute * attr,
	      char * buf, size_t count, loff_t off)
{
	struct driver_attribute * drv_attr = to_drv_attr(attr);
	struct device_driver * drv = to_drv(dir);
	ssize_t ret = 0;

	if (drv_attr->show)
		ret = drv_attr->show(drv,buf,count,off);
	return ret;
}

static ssize_t
drv_attr_store(struct driver_dir_entry * dir, struct attribute * attr,
	       const char * buf, size_t count, loff_t off)
{
	struct driver_attribute * drv_attr = to_drv_attr(attr);
	struct device_driver * drv = to_drv(dir);
	ssize_t ret = 0;

	if (drv_attr->store)
		ret = drv_attr->store(drv,buf,count,off);
	return ret;
}

static struct driverfs_ops drv_attr_ops = {
	.open	= drv_attr_open,
	.close	= drv_attr_close,
	.show	= drv_attr_show,
	.store	= drv_attr_store,
};

int driver_create_file(struct device_driver * drv, struct driver_attribute * attr)
{
	int error;
	if (get_driver(drv)) {
		error = driverfs_create_file(&attr->attr,&drv->dir);
		put_driver(drv);
	} else
		error = -EINVAL;
	return error;
}

void driver_remove_file(struct device_driver * drv, struct driver_attribute * attr)
{
	if (get_driver(drv)) {
		driverfs_remove_file(&drv->dir,attr->attr.name);
		put_driver(drv);
	}
}

/**
 * driver_make_dir - create a driverfs directory for a driver
 * @drv:	driver in question
 */
int driver_make_dir(struct device_driver * drv)
{
	drv->dir.name = drv->name;
	drv->dir.ops = &drv_attr_ops;

	return device_create_dir(&drv->dir,&drv->bus->driver_dir);
}

void driver_remove_dir(struct device_driver * drv)
{
	driverfs_remove_dir(&drv->dir);
}

EXPORT_SYMBOL(driver_create_file);
EXPORT_SYMBOL(driver_remove_file);
