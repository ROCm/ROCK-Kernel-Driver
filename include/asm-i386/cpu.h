#ifndef _ASM_I386_CPU_H_
#define _ASM_I386_CPU_H_

#include <linux/device.h>
#include <linux/cpu.h>

#include <asm/topology.h>
#include <asm/node.h>

struct i386_cpu {
	struct cpu cpu;
};
extern struct i386_cpu cpu_devices[NR_CPUS];


#ifdef CONFIG_NUMA
static inline void arch_register_cpu(int num){
	int p_node = __cpu_to_node(num);
	
	if (p_node >= 0 && p_node < NR_CPUS)
		register_cpu(&cpu_devices[num].cpu, num, 
			&node_devices[p_node].node);
}
#else /* !CONFIG_NUMA */
static inline void arch_register_cpu(int num){
	register_cpu(&cpu_devices[num].cpu, num, (struct node *) NULL);
}
#endif /* CONFIG_NUMA */

#endif /* _ASM_I386_CPU_H_ */
