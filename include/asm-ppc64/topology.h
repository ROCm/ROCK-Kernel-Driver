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
	BUG_ON(node == -1);
#endif

	return node;
}

#define parent_node(node)	(node)

static inline cpumask_t node_to_cpumask(int node)
{
	return numa_cpumask_lookup_table[node];
}

static inline int node_to_first_cpu(int node)
{
	cpumask_t tmp;
	tmp = node_to_cpumask(node);
	return first_cpu(tmp);
}

#define pcibus_to_cpumask(bus)	(cpu_online_map)

#define nr_cpus_node(node)	(nr_cpus_in_node[node])

/* Cross-node load balancing interval. */
#define NODE_BALANCE_RATE 10

#else /* !CONFIG_NUMA */

#include <asm-generic/topology.h>

#endif /* CONFIG_NUMA */

#endif /* _ASM_PPC64_TOPOLOGY_H */
