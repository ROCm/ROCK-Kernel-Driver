/*
 * include/linux/memblk.h - generic memblk definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct memblk' here, which can be embedded in per-arch 
 * definitions of memory blocks.
 *
 * Basic handling of the devices is done in drivers/base/memblk.c
 * and system devices are handled in drivers/base/sys.c. 
 *
 * MemBlks are exported via driverfs in the class/memblk/devices/
 * directory. 
 *
 * Per-memblk interfaces can be implemented using a struct device_interface. 
 * See the following for how to do this: 
 * - drivers/base/intf.c 
 * - Documentation/driver-model/interface.txt
 */
#ifndef _LINUX_MEMBLK_H_
#define _LINUX_MEMBLK_H_

#include <linux/device.h>
#include <linux/node.h>

struct memblk {
	int node_id;		/* The node which contains the MemBlk */
	struct sys_device sysdev;
};

extern int register_memblk(struct memblk *, int, struct node *);

#endif /* _LINUX_MEMBLK_H_ */
