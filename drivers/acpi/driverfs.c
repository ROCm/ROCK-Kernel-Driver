/*
 * driverfs.c - ACPI bindings for driverfs.
 *
 * Copyright (c) 2002 Patrick Mochel
 * Copyright (c) 2002 The Open Source Development Lab
 *
 */

#include <linux/stat.h>
#include <linux/init.h>
#include <linux/driverfs_fs.h>

#include "acpi_bus.h"

static struct driver_dir_entry acpi_dir = {
	.name		= "acpi",
	.mode	= (S_IRWXU | S_IRUGO | S_IXUGO),
};
 
/* driverfs ops for ACPI attribute files go here, when/if
 * there are ACPI attribute files. 
 * For now, we just have directory creation and removal.
 */

void acpi_remove_dir(struct acpi_device * dev)
{
	if (dev)
		driverfs_remove_dir(&dev->driverfs_dir);
}

int acpi_create_dir(struct acpi_device * dev)
{
	struct driver_dir_entry * parent;

	parent = dev->parent ? &dev->parent->driverfs_dir : &acpi_dir;
	dev->driverfs_dir.name = dev->pnp.bus_id;
	dev->driverfs_dir.mode  = (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO);
	return driverfs_create_dir(&dev->driverfs_dir,parent);
}

static int __init acpi_driverfs_init(void)
{
	return driverfs_create_dir(&acpi_dir,NULL);
}

subsys_initcall(acpi_driverfs_init);
