/*
 * include/linux/cpu.h - generic cpu definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct cpu' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/cpu.c
 * and system devices are handled in drivers/base/sys.c. 
 *
 * CPUs are exported via driverfs in the class/cpu/devices/
 * directory. 
 *
 * Per-cpu interfaces can be implemented using a struct device_interface. 
 * See the following for how to do this: 
 * - drivers/base/intf.c 
 * - Documentation/driver-model/interface.txt
 */
#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/sysdev.h>
#include <linux/node.h>
#include <asm/semaphore.h>

struct cpu {
	int node_id;		/* The node which contains the CPU */
	struct sys_device sysdev;
};

extern int register_cpu(struct cpu *, int, struct node *);
extern struct sysdev_class cpu_sysdev_class;

/* Stop CPUs going up and down. */
extern struct semaphore cpucontrol;
#endif /* _LINUX_CPU_H_ */
