/*
 * drivers/base/fs.c - driver model interface to driverfs 
 *
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

extern struct driver_file_entry * device_default_files[];

/**
 * device_create_file - create a driverfs file for a device
 * @dev:	device requesting file
 * @entry:	entry describing file
 *
 * Allocate space for file entry, copy descriptor, and create.
 */
int device_create_file(struct device * dev, struct driver_file_entry * entry)
{
	struct driver_file_entry * new_entry;
	int error = -ENOMEM;

	if (!dev)
		return -EINVAL;
	get_device(dev);

	new_entry = kmalloc(sizeof(*new_entry),GFP_KERNEL);
	if (!new_entry)
		goto done;

	memcpy(new_entry,entry,sizeof(*entry));
	error = driverfs_create_file(new_entry,&dev->dir);
	if (error)
		kfree(new_entry);
 done:
	put_device(dev);
	return error;
}

/**
 * device_remove_file - remove a device's file by name
 * @dev:	device requesting removal
 * @name:	name of the file
 *
 */
void device_remove_file(struct device * dev, const char * name)
{
	if (dev) {
		get_device(dev);
		driverfs_remove_file(&dev->dir,name);
		put_device(dev);
	}
}

/**
 * device_remove_dir - remove a device's directory
 * @dev:	device in question
 */
void device_remove_dir(struct device * dev)
{
	if (dev)
		driverfs_remove_dir(&dev->dir);
}

/**
 * device_make_dir - create a driverfs directory
 * @name:	name of directory
 * @parent:	dentry for the parent directory
 *
 * Do the initial creation of the device's driverfs directory
 * and populate it with the one default file.
 *
 * This is just a helper for device_register(), as we
 * don't export this function. (Yes, that means we don't allow
 * devices to create subdirectories).
 */
int device_make_dir(struct device * dev)
{
	struct driver_dir_entry * parent = NULL;
	struct driver_file_entry * entry;
	int error;
	int i;

	INIT_LIST_HEAD(&dev->dir.files);
	dev->dir.mode = (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO);
	dev->dir.name = dev->bus_id;

	if (dev->parent)
		parent = &dev->parent->dir;

	if ((error = driverfs_create_dir(&dev->dir,parent)))
		return error;

	for (i = 0; (entry = *(device_default_files + i)); i++) {
		if ((error = device_create_file(dev,entry))) {
			device_remove_dir(dev);
			return error;
		}
	}
	return 0;
}

void iobus_remove_dir(struct iobus * iobus)
{
	if (iobus)
		driverfs_remove_dir(&iobus->dir);
}

int iobus_make_dir(struct iobus * iobus)
{
	struct driver_dir_entry * parent = NULL;
	int error;

	INIT_LIST_HEAD(&iobus->dir.files);
	iobus->dir.mode = (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO);
	iobus->dir.name = iobus->bus_id;

	if (iobus->parent)
		parent = &iobus->parent->dir;

	error = driverfs_create_dir(&iobus->dir,parent);
	return error;
}

EXPORT_SYMBOL(device_create_file);
EXPORT_SYMBOL(device_remove_file);

