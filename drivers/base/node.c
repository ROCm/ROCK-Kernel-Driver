/*
 * drivers/base/node.c - basic Node class support
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/node.h>

#include <asm/topology.h>


static int node_add_device(struct device * dev)
{
	return 0;
}
struct device_class node_devclass = {
	.name		= "node",
	.add_device	= node_add_device,
};


struct device_driver node_driver = {
	.name		= "node",
	.bus		= &system_bus_type,
	.devclass	= &node_devclass,
};


static ssize_t node_read_cpumap(struct device * dev, char * buf, size_t count, loff_t off)
{
	struct node *node_dev = to_node(to_root(dev));
        return off ? 0 : sprintf(buf,"%lx\n",node_dev->cpumap);
}
static DEVICE_ATTR(cpumap,S_IRUGO,node_read_cpumap,NULL);


/*
 * register_node - Setup a driverfs device for a node.
 * @num - Node number to use when creating the device.
 *
 * Initialize and register the node device.
 */
void __init register_node(struct node *node, int num, struct node *parent)
{
	node->cpumap = __node_to_cpu_mask(num);
	node->sysroot.id = num;
	if (parent)
		node->sysroot.dev.parent = &parent->sysroot.sysdev;
	snprintf(node->sysroot.dev.name, DEVICE_NAME_SIZE, "Node %u", num);
	snprintf(node->sysroot.dev.bus_id, BUS_ID_SIZE, "node%u", num);
	node->sysroot.dev.driver = &node_driver;
	node->sysroot.dev.bus = &system_bus_type;
	if (!sys_register_root(&node->sysroot)){
		device_create_file(&node->sysroot.dev, &dev_attr_cpumap);
	}
}


static int __init register_node_type(void)
{
	driver_register(&node_driver);
	return devclass_register(&node_devclass);
}
postcore_initcall(register_node_type);
