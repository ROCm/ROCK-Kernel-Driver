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


static ssize_t node_read_cpumap(struct device * dev, char * buf)
{
	struct node *node_dev = to_node(to_root(dev));
        return sprintf(buf,"%lx\n",node_dev->cpumap);
}
static DEVICE_ATTR(cpumap,S_IRUGO,node_read_cpumap,NULL);

#define K(x) ((x) << (PAGE_SHIFT - 10))
static ssize_t node_read_meminfo(struct device * dev, char * buf)
{
	struct sys_root *node = to_root(dev);
	int nid = node->id;
	struct sysinfo i;
	si_meminfo_node(&i, nid);
	return sprintf(buf, "\n"
		       "Node %d MemTotal:     %8lu kB\n"
		       "Node %d MemFree:      %8lu kB\n"
		       "Node %d MemUsed:      %8lu kB\n"
		       "Node %d HighTotal:    %8lu kB\n"
		       "Node %d HighFree:     %8lu kB\n"
		       "Node %d LowTotal:     %8lu kB\n"
		       "Node %d LowFree:      %8lu kB\n",
		       nid, K(i.totalram),
		       nid, K(i.freeram),
		       nid, K(i.totalram-i.freeram),
		       nid, K(i.totalhigh),
		       nid, K(i.freehigh),
		       nid, K(i.totalram-i.totalhigh),
		       nid, K(i.freeram-i.freehigh));
}
#undef K 
static DEVICE_ATTR(meminfo,S_IRUGO,node_read_meminfo,NULL);


/*
 * register_node - Setup a driverfs device for a node.
 * @num - Node number to use when creating the device.
 *
 * Initialize and register the node device.
 */
int __init register_node(struct node *node, int num, struct node *parent)
{
	int error;

	node->cpumap = node_to_cpumask(num);
	node->sysroot.id = num;
	if (parent)
		node->sysroot.dev.parent = &parent->sysroot.sysdev;
	snprintf(node->sysroot.dev.name, DEVICE_NAME_SIZE, "Node %u", num);
	snprintf(node->sysroot.dev.bus_id, BUS_ID_SIZE, "node%u", num);
	node->sysroot.dev.driver = &node_driver;
	node->sysroot.dev.bus = &system_bus_type;
	error = sys_register_root(&node->sysroot);
	if (!error){
		device_create_file(&node->sysroot.dev, &dev_attr_cpumap);
		device_create_file(&node->sysroot.dev, &dev_attr_meminfo);
	}
	return error;
}


int __init register_node_type(void)
{
	int error;
	if (!(error = devclass_register(&node_devclass)))
		if (error = driver_register(&node_driver))
			devclass_unregister(&node_devclass);
	return error;
}
postcore_initcall(register_node_type);
