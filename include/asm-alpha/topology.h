#ifndef _ASM_ALPHA_TOPOLOGY_H
#define _ASM_ALPHA_TOPOLOGY_H

#if defined(CONFIG_NUMA) && defined(CONFIG_ALPHA_WILDFIRE)

/* With wildfire assume 4 CPUs per node */
#define __cpu_to_node(cpu)		((cpu) >> 2)

#else /* !CONFIG_NUMA || !CONFIG_ALPHA_WILDFIRE */

#include <asm-generic/topology.h>

#endif /* CONFIG_NUMA && CONFIG_ALPHA_WILDFIRE */

#endif /* _ASM_ALPHA_TOPOLOGY_H */
