/*
 * drivers/base/memblk.c - basic Memory Block class support
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/memblk.h>
#include <linux/node.h>
#include <linux/topology.h>


static struct sysdev_class memblk_class = {
	set_kset_name("memblk"),
};

/*
 * register_memblk - Setup a driverfs device for a MemBlk
 * @num - MemBlk number to use when creating the device.
 *
 * Initialize and register the MemBlk device.
 */
int __init register_memblk(struct memblk *memblk, int num, struct node *root)
{
	int error;

	memblk->node_id = memblk_to_node(num);
	memblk->sysdev.cls = &memblk_class,
	memblk->sysdev.id = num;

	error = sys_device_register(&memblk->sysdev);
	if (!error) 
		error = sysfs_create_link(&root->sysdev.kobj,
					  &memblk->sysdev.kobj,
					  kobject_name(&memblk->sysdev.kobj));
	return error;
}


int __init register_memblk_type(void)
{
	return sysdev_class_register(&memblk_class);
}
postcore_initcall(register_memblk_type);
