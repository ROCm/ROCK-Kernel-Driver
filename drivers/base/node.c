/*
 * drivers/base/node.c - basic Node class support
 */

#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/node.h>
#include <linux/cpumask.h>
#include <linux/topology.h>

static struct sysdev_class node_class = {
	set_kset_name("node"),
};


static ssize_t node_read_cpumap(struct sys_device * dev, char * buf)
{
	struct node *node_dev = to_node(dev);
	cpumask_t tmp = node_dev->cpumap;
	int k, len = 0;

	for (k = 0; k < sizeof(cpumask_t)/sizeof(u16); ++k) {
		int j = sprintf(buf, "%04hx", (u16)cpus_coerce(tmp));
		len += j;
		buf += j;
		cpus_shift_right(tmp, tmp, 16);
	}
        len += sprintf(buf, "\n");
	return len;
}
static SYSDEV_ATTR(cpumap,S_IRUGO,node_read_cpumap,NULL);

#define K(x) ((x) << (PAGE_SHIFT - 10))
static ssize_t node_read_meminfo(struct sys_device * dev, char * buf)
{
	int nid = dev->id;
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
static SYSDEV_ATTR(meminfo,S_IRUGO,node_read_meminfo,NULL);


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
	node->sysdev.id = num;
	node->sysdev.cls = &node_class;
	error = sys_device_register(&node->sysdev);

	if (!error){
		sysdev_create_file(&node->sysdev, &attr_cpumap);
		sysdev_create_file(&node->sysdev, &attr_meminfo);
	}
	return error;
}


int __init register_node_type(void)
{
	return sysdev_class_register(&node_class);
}
postcore_initcall(register_node_type);
