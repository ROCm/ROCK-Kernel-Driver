/*
 * platform.c - platform 'psuedo' bus for legacy devices
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 * 
 * This file is released under the GPLv2
 *
 * Please see Documentation/driver-model/platform.txt for more
 * information.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>

struct device legacy_bus = {
	.name		= "legacy bus",
	.bus_id		= "legacy",
};

/**
 *	platform_device_register - add a platform-level device
 *	@dev:	platform device we're adding
 *
 */
int platform_device_register(struct platform_device * pdev)
{
	if (!pdev)
		return -EINVAL;

	if (!pdev->dev.parent)
		pdev->dev.parent = &legacy_bus;

	pdev->dev.bus = &platform_bus_type;
	
	snprintf(pdev->dev.bus_id,BUS_ID_SIZE,"%s%u",pdev->name,pdev->id);

	pr_debug("Registering platform device '%s'. Parent at %s\n",
		 pdev->dev.bus_id,pdev->dev.parent->bus_id);
	return device_register(&pdev->dev);
}

void platform_device_unregister(struct platform_device * pdev)
{
	if (pdev)
		device_unregister(&pdev->dev);
}


/**
 *	platform_match - bind platform device to platform driver.
 *	@dev:	device.
 *	@drv:	driver.
 *
 *	Platform device IDs are assumed to be encoded like this: 
 *	"<name><instance>", where <name> is a short description of the 
 *	type of device, like "pci" or "floppy", and <instance> is the 
 *	enumerated instance of the device, like '0' or '42'.
 *	Driver IDs are simply "<name>". 
 *	So, extract the <name> from the device, and compare it against 
 *	the name of the driver. Return whether they match or not.
 */

static int platform_match(struct device * dev, struct device_driver * drv)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);

	return (strncmp(pdev->name, drv->name, BUS_ID_SIZE) == 0);
}

struct bus_type platform_bus_type = {
	.name		= "platform",
	.match		= platform_match,
};

int __init platform_bus_init(void)
{
	device_register(&legacy_bus);
	return bus_register(&platform_bus_type);
}

EXPORT_SYMBOL(legacy_bus);
EXPORT_SYMBOL(platform_bus_type);
EXPORT_SYMBOL(platform_device_register);
EXPORT_SYMBOL(platform_device_unregister);
