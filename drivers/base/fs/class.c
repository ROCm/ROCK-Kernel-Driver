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


#define to_class_attr(_attr) container_of(_attr,struct devclass_attribute,attr)

#define to_class(d) container_of(d,struct device_class,dir)


static ssize_t
devclass_attr_show(struct driver_dir_entry * dir, struct attribute * attr,
	      char * buf, size_t count, loff_t off)
{
	struct devclass_attribute * class_attr = to_class_attr(attr);
	struct device_class * dc = to_class(dir);
	ssize_t ret = 0;

	if (class_attr->show)
		ret = class_attr->show(dc,buf,count,off);
	return ret;
}

static ssize_t
devclass_attr_store(struct driver_dir_entry * dir, struct attribute * attr,
	       const char * buf, size_t count, loff_t off)
{
	struct devclass_attribute * class_attr = to_class_attr(attr);
	struct device_class * dc = to_class(dir);
	ssize_t ret = 0;

	if (class_attr->store)
		ret = class_attr->store(dc,buf,count,off);
	return ret;
}

static struct driverfs_ops devclass_attr_ops = {
	show:	devclass_attr_show,
	store:	devclass_attr_store,
};

int devclass_create_file(struct device_class * dc, struct devclass_attribute * attr)
{
	int error;
	if (dc) {
		error = driverfs_create_file(&attr->attr,&dc->dir);
	} else
		error = -EINVAL;
	return error;
}

void devclass_remove_file(struct device_class * dc, struct devclass_attribute * attr)
{
	if (dc)
		driverfs_remove_file(&dc->dir,attr->attr.name);
}

/**
 *	devclass_dev_link - create symlink to device's directory
 *	@cls - device class we're a part of 
 *	@dev - device we're linking to 
 *
 *	Create a symlink in the class's devices/ directory to @dev's 
 *	directory in the physical hierarchy. The name is the device's 
 *	class-enumerated value (struct device::class_num). We're 
 *	creating:
 *		class/<class name>/devices/<link name> ->
 *		root/<path to device>/<device's dir>
 *	So, the link looks like:
 *		../../../root/<path to device>/
 */
int devclass_dev_link(struct device_class * cls, struct device * dev)
{
	char	linkname[16];
	char	* path;
	int	length;
	int	error;

	length = get_devpath_length(dev);
	length += strlen("../../../root");

	if (length > PATH_MAX)
		return -ENAMETOOLONG;

	if (!(path = kmalloc(length,GFP_KERNEL)))
		return -ENOMEM;
	memset(path,0,length);
	strcpy(path,"../../../root");
	fill_devpath(dev,path,length);
	
	snprintf(linkname,16,"%u",dev->class_num);
	error = driverfs_create_symlink(&cls->device_dir,linkname,path);
	kfree(path);
	return error;
}

void devclass_dev_unlink(struct device_class * cls, struct device * dev)
{
	char	linkname[16];

	snprintf(linkname,16,"%u",dev->class_num);
	driverfs_remove_file(&cls->device_dir,linkname);
}

/**
 *	devclass_drv_link - create symlink to driver's directory
 *	@drv:	driver we're linking up
 *
 *	Create a symlink in the class's drivers/ directory to @drv's
 *	directory (in the bus's directory). It's name is <bus>:<driver>
 *	to prevent naming conflicts.
 *
 *	We're creating 
 *		class/<class name>/drivers/<link name> -> 
 *		bus/<bus name>/drivers/<driver name>/
 *	So, the link looks like: 
 *		../../../bus/<bus name>/drivers/<driver name>
 */
int devclass_drv_link(struct device_driver * drv)
{
	char	* name;
	char	* path;
	int	namelen;
	int	pathlen;
	int	error = 0;

	namelen = strlen(drv->name) + strlen(drv->bus->name) + 2;
	name = kmalloc(namelen,GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	snprintf(name,namelen,"%s:%s",drv->bus->name,drv->name);

	pathlen = strlen("../../../bus/") + 
		strlen(drv->bus->name) + 
		strlen("/drivers/") + 
		strlen(drv->name) + 1;
	if (!(path = kmalloc(pathlen,GFP_KERNEL))) {
		error = -ENOMEM;
		goto Done;
	}
	snprintf(path,pathlen,"%s%s%s%s",
		 "../../../bus/",
		 drv->bus->name,
		 "/drivers/",
		 drv->name);

	error = driverfs_create_symlink(&drv->devclass->driver_dir,name,path);
 Done:
	kfree(name);
	kfree(path);
	return error;
}

void devclass_drv_unlink(struct device_driver * drv)
{
	char	* name;
	int	length;

	length = strlen(drv->name) + strlen(drv->bus->name) + 2;
	if ((name = kmalloc(length,GFP_KERNEL))) {
		driverfs_remove_file(&drv->devclass->driver_dir,name);
		kfree(name);
	}
}

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
	dc->dir.ops = &devclass_attr_ops;
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
