/*
 * platform.c - platform 'pseudo' bus for legacy devices
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

struct device platform_bus = {
	.bus_id		= "platform",
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
		pdev->dev.parent = &platform_bus;

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
 *	So, extract the <name> from the platform_device structure, 
 *	and compare it against the name of the driver. Return whether 
 *	they match or not.
 */

static int platform_match(struct device * dev, struct device_driver * drv)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);

	return (strncmp(pdev->name, drv->name, BUS_ID_SIZE) == 0);
}

static int platform_suspend(struct device * dev, u32 state)
{
	int ret = 0;

	if (dev->driver && dev->driver->suspend) {
		ret = dev->driver->suspend(dev, state, SUSPEND_DISABLE);
		if (ret == 0)
			ret = dev->driver->suspend(dev, state, SUSPEND_SAVE_STATE);
		if (ret == 0)
			ret = dev->driver->suspend(dev, state, SUSPEND_POWER_DOWN);
	}
	return ret;
}

static int platform_resume(struct device * dev)
{
	int ret = 0;

	if (dev->driver && dev->driver->resume) {
		ret = dev->driver->resume(dev, RESUME_POWER_ON);
		if (ret == 0)
			ret = dev->driver->resume(dev, RESUME_RESTORE_STATE);
		if (ret == 0)
			ret = dev->driver->resume(dev, RESUME_ENABLE);
	}
	return ret;
}

struct bus_type platform_bus_type = {
	.name		= "platform",
	.match		= platform_match,
	.suspend	= platform_suspend,
	.resume		= platform_resume,
};

int __init platform_bus_init(void)
{
	device_register(&platform_bus);
	return bus_register(&platform_bus_type);
}

EXPORT_SYMBOL(platform_bus);
EXPORT_SYMBOL(platform_bus_type);
EXPORT_SYMBOL(platform_device_register);
EXPORT_SYMBOL(platform_device_unregister);
