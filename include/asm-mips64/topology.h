#ifndef _ASM_MIPS64_TOPOLOGY_H
#define _ASM_MIPS64_TOPOLOGY_H

#include <asm/mmzone.h>

#define __cpu_to_node(cpu)		(cputocnode(cpu))

/* Get the rest of the topology definitions */
#include <asm-generic/topology.h>

#endif /* _ASM_MIPS64_TOPOLOGY_H */
