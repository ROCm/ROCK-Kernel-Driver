/*
 * cpu.c - basic cpu class support
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpu.h>

static int cpu_add_device(struct device * dev)
{
	return 0;
}

struct device_class cpu_devclass = {
	.name		= "cpu",
	.add_device	= cpu_add_device,
};


static int __init cpu_devclass_init(void)
{
	return devclass_register(&cpu_devclass);
}

postcore_initcall(cpu_devclass_init);

EXPORT_SYMBOL(cpu_devclass);
