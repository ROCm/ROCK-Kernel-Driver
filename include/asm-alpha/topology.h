#ifndef _ASM_ALPHA_TOPOLOGY_H
#define _ASM_ALPHA_TOPOLOGY_H

#include <linux/smp.h>
#include <linux/threads.h>
#include <asm/machvec.h>

#ifdef CONFIG_NUMA
static inline int cpu_to_node(int cpu)
{
	int node;
	
	if (!alpha_mv.cpuid_to_nid)
		return 0;

	node = alpha_mv.cpuid_to_nid(cpu);

#ifdef DEBUG_NUMA
	if (node < 0)
		BUG();
#endif

	return node;
}

static inline int node_to_cpumask(int node)
{
	unsigned long node_cpu_mask = 0;
	int cpu;

	for(cpu = 0; cpu < NR_CPUS; cpu++) {
		if (cpu_online(cpu) && (cpu_to_node(cpu) == node))
			node_cpu_mask |= 1UL << cpu;
	}

#if DEBUG_NUMA
	printk("node %d: cpu_mask: %016lx\n", node, node_cpu_mask);
#endif

	return node_cpu_mask;
}

# define node_to_memblk(node)		(node)
# define memblk_to_node(memblk)	(memblk)

/* Cross-node load balancing interval. */
# define NODE_BALANCE_RATE 10

#else /* CONFIG_NUMA */
# include <asm-generic/topology.h>
#endif /* !CONFIG_NUMA */

#endif /* _ASM_ALPHA_TOPOLOGY_H */
