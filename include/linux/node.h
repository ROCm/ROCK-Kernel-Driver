/*
 * include/linux/node.h - generic node definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct node' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/node.c
 * and system devices are handled in drivers/base/sys.c. 
 *
 * Nodes are exported via driverfs in the class/node/devices/
 * directory. 
 *
 * Per-node interfaces can be implemented using a struct device_interface. 
 * See the following for how to do this: 
 * - drivers/base/intf.c 
 * - Documentation/driver-model/interface.txt
 */
#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/device.h>

struct node {
	unsigned long cpumap;	/* Bitmap of CPUs on the Node */
	struct sys_root sysroot;
};

extern void register_node(struct node *, int, struct node *);

#define to_node(_root) container_of(_root, struct node, sysroot)
#define to_root(_dev) container_of(_dev, struct sys_root, dev)

#endif /* _LINUX_NODE_H_ */
