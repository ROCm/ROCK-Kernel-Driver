#ifndef _ASM_PPC64_TOPOLOGY_H
#define _ASM_PPC64_TOPOLOGY_H

#include <linux/config.h>
#include <asm/mmzone.h>

#ifdef CONFIG_NUMA

static inline int cpu_to_node(int cpu)
{
	int node;

	node = numa_cpu_lookup_table[cpu];

#ifdef DEBUG_NUMA
	if (node == -1)
		BUG();
#endif

	return node;
}

static inline int node_to_first_cpu(int node)
{
	int cpu;

	for(cpu = 0; cpu < NR_CPUS; cpu++)
		if (numa_cpu_lookup_table[cpu] == node)
			return cpu;

	BUG(); /* couldn't find a cpu on given node */
	return -1;
}

static inline unsigned long node_to_cpumask(int node)
{
	int cpu;
	unsigned long mask = 0UL;

	if (sizeof(unsigned long) * 8 < NR_CPUS)
		BUG();

	for(cpu = 0; cpu < NR_CPUS; cpu++)
		if (numa_cpu_lookup_table[cpu] == node)
			mask |= 1UL << cpu;

	return mask;
}

/* Cross-node load balancing interval. */
#define NODE_BALANCE_RATE 10

#else /* !CONFIG_NUMA */

#include <asm-generic/topology.h>

#endif /* CONFIG_NUMA */

#endif /* _ASM_PPC64_TOPOLOGY_H */
