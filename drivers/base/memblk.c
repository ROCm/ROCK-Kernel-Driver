/*
 * drivers/base/memblk.c - basic Memory Block class support
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/memblk.h>
#include <linux/node.h>

#include <asm/topology.h>


static struct class memblk_class = {
	.name		= "memblk",
};


static struct device_driver memblk_driver = {
	.name		= "memblk",
	.bus		= &system_bus_type,
};


/*
 * register_memblk - Setup a driverfs device for a MemBlk
 * @num - MemBlk number to use when creating the device.
 *
 * Initialize and register the MemBlk device.
 */
int __init register_memblk(struct memblk *memblk, int num, struct node *root)
{
	memblk->node_id = memblk_to_node(num);
	memblk->sysdev.name = "memblk";
	memblk->sysdev.id = num;
	if (root)
		memblk->sysdev.root = &root->sysroot;
	snprintf(memblk->sysdev.dev.name, DEVICE_NAME_SIZE, "Memory Block %u", num);
	memblk->sysdev.dev.driver = &memblk_driver;
	return sys_device_register(&memblk->sysdev);
}


int __init register_memblk_type(void)
{
	int error;

	error = class_register(&memblk_class);
	if (!error) {
		error = driver_register(&memblk_driver);
		if (error)
			class_unregister(&memblk_class);
	}
	return error;
}
postcore_initcall(register_memblk_type);
