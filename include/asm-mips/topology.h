#ifndef __ASM_TOPOLOGY_H
#define __ASM_TOPOLOGY_H

#include <linux/config.h>

#ifdef CONFIG_SGI_IP27

#include <asm/mmzone.h>

#define cpu_to_node(cpu)	(cputocnode(cpu))
#endif

#include <asm-generic/topology.h>

#endif /* __ASM_TOPOLOGY_H */
