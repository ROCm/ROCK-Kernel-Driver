#ifndef _ASM_ALPHA_TOPOLOGY_H
#define _ASM_ALPHA_TOPOLOGY_H

#ifdef CONFIG_NUMA
#ifdef CONFIG_ALPHA_WILDFIRE
/* With wildfire assume 4 CPUs per node */
#define __cpu_to_node(cpu)	((cpu) >> 2)
#endif /* CONFIG_ALPHA_WILDFIRE */
#endif /* CONFIG_NUMA */

/* Get the rest of the topology definitions */
#include <asm-generic/topology.h>

#endif /* _ASM_ALPHA_TOPOLOGY_H */
