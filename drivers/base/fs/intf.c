/*
 * intf.c - driverfs glue for device interfaces
 */

#include <linux/device.h>
#include <linux/slab.h>
#include "fs.h"

/**
 * intf_dev_link - symlink from interface's directory to device's directory
 *
 */
int intf_dev_link(struct intf_data * data)
{
	char	linkname[16];
	char	* path;
	int	length;
	int	error;

	length = get_devpath_length(data->dev);
	length += strlen("../../../root");

	if (length > PATH_MAX)
		return -ENAMETOOLONG;

	if (!(path = kmalloc(length,GFP_KERNEL)))
		return -ENOMEM;
	memset(path,0,length);
	strcpy(path,"../../../root");
	fill_devpath(data->dev,path,length);

	snprintf(linkname,16,"%u",data->intf_num);
	error = driverfs_create_symlink(&data->intf->dir,linkname,path);
	kfree(path);
	return error;
}

void intf_dev_unlink(struct intf_data * data)
{
	char	linkname[16];
	snprintf(linkname,16,"%u",data->intf_num);
	driverfs_remove_file(&data->intf->dir,linkname);
}

void intf_remove_dir(struct device_interface * intf)
{
	driverfs_remove_dir(&intf->dir);
}

int intf_make_dir(struct device_interface * intf)
{
	intf->dir.name = intf->name;
	return device_create_dir(&intf->dir,&intf->devclass->dir);
}
