/*
 * linux/include/asm-i386/topology.h
 *
 * Written by: Matthew Dobson, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <colpatch@us.ibm.com>
 */
#ifndef _ASM_I386_TOPOLOGY_H
#define _ASM_I386_TOPOLOGY_H

#ifdef CONFIG_NUMA

#include <asm/mpspec.h>

#include <linux/cpumask.h>

/* Mappings between logical cpu number and node number */
extern cpumask_t node_2_cpu_mask[];
extern int cpu_2_node[];

/* Returns the number of the node containing CPU 'cpu' */
static inline int cpu_to_node(int cpu)
{ 
	return cpu_2_node[cpu];
}

/* Returns the number of the node containing MemBlk 'memblk' */
#define memblk_to_node(memblk) (memblk)

/* Returns the number of the node containing Node 'node'.  This architecture is flat, 
   so it is a pretty simple function! */
#define parent_node(node) (node)

/* Returns a bitmask of CPUs on Node 'node'. */
static inline cpumask_t node_to_cpumask(int node)
{
	return node_2_cpu_mask[node];
}

/* Returns the number of the first CPU on Node 'node'. */
static inline int node_to_first_cpu(int node)
{ 
	cpumask_t mask = node_to_cpumask(node);
	return first_cpu(mask);
}

/* Returns the number of the first MemBlk on Node 'node' */
#define node_to_memblk(node) (node)

/* Returns the number of the node containing PCI bus 'bus' */
static inline cpumask_t pcibus_to_cpumask(int bus)
{
	return node_to_cpumask(mp_bus_id_to_node[bus]);
}

/* Cross-node load balancing interval. */
#define NODE_BALANCE_RATE 100

#else /* !CONFIG_NUMA */
/*
 * Other i386 platforms should define their own version of the 
 * above macros here.
 */

#include <asm-generic/topology.h>

#endif /* CONFIG_NUMA */

#endif /* _ASM_I386_TOPOLOGY_H */
