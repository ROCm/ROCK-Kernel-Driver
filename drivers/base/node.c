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
	cpumask_t mask = node_dev->cpumap;
	int len;

	/* 2004/06/03: buf currently PAGE_SIZE, need > 1 char per 4 bits. */
	BUILD_BUG_ON(NR_CPUS/4 > PAGE_SIZE/2);

	len = cpumask_scnprintf(buf, PAGE_SIZE-1, mask);
	len += sprintf(buf + len, "\n");
	return len;
}

static SYSDEV_ATTR(cpumap,S_IRUGO,node_read_cpumap,NULL);

/* Can be overwritten by architecture specific code. */
int __attribute__((weak)) hugetlb_report_node_meminfo(int node, char *buf)
{
	return 0;
}

#define K(x) ((x) << (PAGE_SHIFT - 10))
static ssize_t node_read_meminfo(struct sys_device * dev, char * buf)
{
	int n;
	int nid = dev->id;
	struct sysinfo i;
	si_meminfo_node(&i, nid);
	n = sprintf(buf, "\n"
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
	n += hugetlb_report_node_meminfo(nid, buf + n);
	return n;
}

#undef K 
static SYSDEV_ATTR(meminfo,S_IRUGO,node_read_meminfo,NULL);

static ssize_t node_read_numastat(struct sys_device * dev, char * buf)
{
	unsigned long numa_hit, numa_miss, interleave_hit, numa_foreign;
	unsigned long local_node, other_node;
	int i, cpu;
	pg_data_t *pg = NODE_DATA(dev->id);
	numa_hit = 0;
	numa_miss = 0;
	interleave_hit = 0;
	numa_foreign = 0;
	local_node = 0;
	other_node = 0;
	for (i = 0; i < MAX_NR_ZONES; i++) {
		struct zone *z = &pg->node_zones[i];
		for (cpu = 0; cpu < NR_CPUS; cpu++) {
			struct per_cpu_pageset *ps = &z->pageset[cpu];
			numa_hit += ps->numa_hit;
			numa_miss += ps->numa_miss;
			numa_foreign += ps->numa_foreign;
			interleave_hit += ps->interleave_hit;
			local_node += ps->local_node;
			other_node += ps->other_node;
		}
	}
	return sprintf(buf,
		       "numa_hit %lu\n"
		       "numa_miss %lu\n"
		       "numa_foreign %lu\n"
		       "interleave_hit %lu\n"
		       "local_node %lu\n"
		       "other_node %lu\n",
		       numa_hit,
		       numa_miss,
		       numa_foreign,
		       interleave_hit,
		       local_node,
		       other_node);
}
static SYSDEV_ATTR(numastat,S_IRUGO,node_read_numastat,NULL);

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
	error = sysdev_register(&node->sysdev);

	if (!error){
		sysdev_create_file(&node->sysdev, &attr_cpumap);
		sysdev_create_file(&node->sysdev, &attr_meminfo);
		sysdev_create_file(&node->sysdev, &attr_numastat);
	}
	return error;
}


int __init register_node_type(void)
{
	return sysdev_class_register(&node_class);
}
postcore_initcall(register_node_type);
