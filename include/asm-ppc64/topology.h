#ifndef _ASM_PPC64_TOPOLOGY_H
#define _ASM_PPC64_TOPOLOGY_H

#include <asm/mmzone.h>

#ifdef CONFIG_NUMA
/* XXX grab this from the device tree - Anton */
#define __cpu_to_node(cpu)	((cpu) >> CPU_SHIFT_BITS)
#endif /* CONFIG_NUMA */

/* Get the rest of the topology definitions */
#include <asm-generic/topology.h>

#endif /* _ASM_PPC64_TOPOLOGY_H */
