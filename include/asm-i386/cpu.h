#ifndef _ASM_I386_CPU_H_
#define _ASM_I386_CPU_H_

#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/topology.h>

#include <asm/node.h>

struct i386_cpu {
	struct cpu cpu;
};
extern struct i386_cpu cpu_devices[NR_CPUS];


static inline int arch_register_cpu(int num){
	struct node *parent = NULL;
	
#ifdef CONFIG_NUMA
	int node = cpu_to_node(num);
	if (node_online(node))
		parent = &node_devices[node].node;
#endif /* CONFIG_NUMA */

	return register_cpu(&cpu_devices[num].cpu, num, parent);
}

#endif /* _ASM_I386_CPU_H_ */
