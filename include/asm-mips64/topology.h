#ifndef __ASM_TOPOLOGY_H
#define __ASM_TOPOLOGY_H

#if CONFIG_SGI_IP27

#include <asm/mmzone.h>

#define cpu_to_node(cpu)	(cputocnode(cpu))
#endif

#include <asm-generic/topology.h>

#endif /* __ASM_TOPOLOGY_H */
