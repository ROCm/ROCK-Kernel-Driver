/*
 * platform.c - platform 'psuedo' bus for legacy devices
 *
 * Please see Documentation/driver-model/platform.txt for more
 * information.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>

static int platform_match(struct device * dev, struct device_driver * drv)
{
	return 0;
}

struct bus_type platform_bus = {
	.name		= "platform",
	.match		= platform_match,
};

static int __init platform_bus_init(void)
{
	return bus_register(&platform_bus);
}

postcore_initcall(platform_bus_init);
