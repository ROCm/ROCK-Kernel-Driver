/*
 * sys.c - pseudo-bus for system 'devices' (cpus, PICs, timers, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 *               2002-3 Open Source Development Lab
 *
 * This file is released under the GPLv2
 * 
 * This exports a 'system' bus type. 
 * By default, a 'sys' bus gets added to the root of the system. There will
 * always be core system devices. Devices can use sys_device_register() to
 * add themselves as children of the system bus.
 */

#undef DEBUG

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>

/* The default system device parent. */
static struct device system_bus = {
       .name           = "System Bus",
       .bus_id         = "sys",
};


/**
 *	sys_register_root - add a subordinate system root
 *	@root:	new root
 *	
 *	This is for NUMA-like systems so they can accurately 
 *	represent the topology of the entire system.
 *	As boards are discovered, a new struct sys_root should 
 *	be allocated and registered. 
 *	The discovery mechanism should initialize the id field
 *	of the struture, as well as much of the embedded device
 *	structure as possible, inlcuding the name, the bus_id
 *	and parent fields.
 *
 *	This simply calls device_register on the embedded device.
 *	On success, it will use the struct @root->sysdev 
 *	device to create a pseudo-parent for system devices
 *	on that board.
 *
 *	The platform code can then use @root to specifiy the
 *	controlling board when discovering and registering 
 *	system devices.
 */
int sys_register_root(struct sys_root * root)
{
	int error = 0;

	if (!root)
		return -EINVAL;

	if (!root->dev.parent)
		root->dev.parent = &system_bus;

	pr_debug("Registering system board %d\n",root->id);

	error = device_register(&root->dev);
	if (!error) {
		strlcpy(root->sysdev.bus_id,"sys",BUS_ID_SIZE);
		strlcpy(root->sysdev.name,"System Bus",DEVICE_NAME_SIZE);
		root->sysdev.parent = &root->dev;
		error = device_register(&root->sysdev);
	};

	return error;
}

/**
 *	sys_unregister_root - remove subordinate root from tree
 *	@root:	subordinate root in question.
 *
 *	We only decrement the reference count on @root->sysdev 
 *	and @root->dev.
 *	If both are 0, they will be cleaned up by the core.
 */
void sys_unregister_root(struct sys_root *root)
{
	device_unregister(&root->sysdev);
	device_unregister(&root->dev);
}

/**
 *	sys_device_register - add a system device to the tree
 *	@sysdev:	device in question
 *
 *	The hardest part about this is getting the ancestry right.
 *	If the device has a parent - super! We do nothing.
 *	If the device doesn't, but @dev->root is set, then we're
 *	dealing with a NUMA like architecture where each root
 *	has a system pseudo-bus to foster the device.
 *	If not, then we fallback to system_bus (at the top of 
 *	this file). 
 *
 *	One way or another, we call device_register() on it and 
 *	are done.
 *
 *	The caller is also responsible for initializing the bus_id 
 *	and name fields of @sysdev->dev.
 */
int sys_device_register(struct sys_device * sysdev)
{
	if (!sysdev)
		return -EINVAL;

	if (!sysdev->dev.parent) {
		if (sysdev->root)
			sysdev->dev.parent = &sysdev->root->sysdev;
		else
			sysdev->dev.parent = &system_bus;
	}

	/* make sure bus type is set */
	if (!sysdev->dev.bus)
		sysdev->dev.bus = &system_bus_type;

	/* construct bus_id */
	snprintf(sysdev->dev.bus_id,BUS_ID_SIZE,"%s%u",sysdev->name,sysdev->id);

	pr_debug("Registering system device %s\n", sysdev->dev.bus_id);

	return device_register(&sysdev->dev);
}

void sys_device_unregister(struct sys_device * sysdev)
{
	if (sysdev)
		device_unregister(&sysdev->dev);
}

struct bus_type system_bus_type = {
	.name		= "system",
};

int __init sys_bus_init(void)
{
	bus_register(&system_bus_type);
	return device_register(&system_bus);
}

EXPORT_SYMBOL(system_bus_type);
EXPORT_SYMBOL(sys_device_register);
EXPORT_SYMBOL(sys_device_unregister);
