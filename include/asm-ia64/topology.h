/*
 * linux/include/asm-ia64/topology.h
 *
 * Copyright (C) 2002, Erich Focht, NEC
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _ASM_IA64_TOPOLOGY_H
#define _ASM_IA64_TOPOLOGY_H

#include <asm/acpi.h>
#include <asm/numa.h>

/* Returns the number of the node containing CPU 'cpu' */
#ifdef CONFIG_NUMA
#define __cpu_to_node(cpu) cpu_to_node_map[cpu]
#else
#define __cpu_to_node(cpu) (0)
#endif

/*
 * Returns the number of the node containing MemBlk 'memblk'
 */
#ifdef CONFIG_ACPI_NUMA
#define __memblk_to_node(memblk) (node_memblk[memblk].nid)
#else
#define __memblk_to_node(memblk) (memblk)
#endif

/*
 * Returns the number of the node containing Node 'nid'.
 * Not implemented here. Multi-level hierarchies detected with
 * the help of node_distance().
 */
#define __parent_node(nid) (nid)

/*
 * Returns the number of the first CPU on Node 'node'.
 * Slow in the current implementation.
 * Who needs this?
 */
/* #define __node_to_first_cpu(node) pool_cpus[pool_ptr[node]] */
static inline int __node_to_first_cpu(int node)
{
	int i;

	for (i=0; i<NR_CPUS; i++)
		if (__cpu_to_node(i)==node)
			return i;
	BUG(); /* couldn't find a cpu on given node */
	return -1;
}

/*
 * Returns a bitmask of CPUs on Node 'node'.
 */
static inline unsigned long __node_to_cpu_mask(int node)
{
	int cpu;
	unsigned long mask = 0UL;

	for(cpu=0; cpu<NR_CPUS; cpu++)
		if (__cpu_to_node(cpu) == node)
			mask |= 1UL << cpu;
	return mask;
}

/*
 * Returns the number of the first MemBlk on Node 'node'
 * Should be fixed when IA64 discontigmem goes in.
 */
#define __node_to_memblk(node) (node)

#endif /* _ASM_IA64_TOPOLOGY_H */
