#ifndef _ASM_PPC64_TOPOLOGY_H
#define _ASM_PPC64_TOPOLOGY_H

#include <asm/mmzone.h>

#ifdef CONFIG_NUMA

static inline int __cpu_to_node(int cpu)
{
	int node;

	node = numa_cpu_lookup_table[cpu];

#ifdef DEBUG_NUMA
	if (node == -1)
		BUG();
#endif

	return node;
}

static inline int __node_to_first_cpu(int node)
{
	int cpu;

	for(cpu = 0; cpu < NR_CPUS; cpu++)
		if (numa_cpu_lookup_table[cpu] == node)
			return cpu;

	BUG(); /* couldn't find a cpu on given node */
	return -1;
}

static inline unsigned long __node_to_cpu_mask(int node)
{
	int cpu;
	unsigned long mask = 0UL;

	if (sizeof(unsigned long) * 8 < NR_CPUS)
		BUG();

	for(cpu = 0; cpu < NR_CPUS; cpu++)
		if (numa_cpu_lookup_table[cpu] == node)
			mask |= 1 << cpu;

	return mask;
}

#else /* !CONFIG_NUMA */

#define __cpu_to_node(cpu)		(0)
#define __memblk_to_node(memblk)	(0)
#define __parent_node(nid)		(0)
#define __node_to_first_cpu(node)	(0)
#define __node_to_cpu_mask(node)	(cpu_online_map)
#define __node_to_memblk(node)		(0)

#endif /* CONFIG_NUMA */

#endif /* _ASM_PPC64_TOPOLOGY_H */
