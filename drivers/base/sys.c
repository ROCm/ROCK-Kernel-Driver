/*
 * sys.c - pseudo-bus for system 'devices' (cpus, PICs, timers, etc)
 *
 * Copyright (c) 2002 Patrick Mochel
 *              2002 Open Source Development Lab
 * 
 * This exports a 'system' bus type. 
 * By default, a 'sys' bus gets added to the root of the system. There will
 * always be core system devices. Devices can use sys_device_register() to
 * add themselves as children of the system bus.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>

/* The default system device parent. */
static struct device system_bus = {
       .name           = "System Bus",
       .bus_id         = "sys",
};

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

	if (!sysdev->dev.parent)
		sysdev->dev.parent = &system_bus;

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
		put_device(&sysdev->dev);
}

struct bus_type system_bus_type = {
	.name		= "system",
};

static int sys_bus_init(void)
{
	bus_register(&system_bus_type);
	return device_register(&system_bus);
}

postcore_initcall(sys_bus_init);
EXPORT_SYMBOL(system_bus_type);
EXPORT_SYMBOL(sys_device_register);
EXPORT_SYMBOL(sys_device_unregister);
