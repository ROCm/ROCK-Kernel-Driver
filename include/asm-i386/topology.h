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

#ifdef CONFIG_X86_NUMAQ

#include <asm/smpboot.h>

/* Returns the number of the node containing CPU 'cpu' */
#define __cpu_to_node(cpu) (cpu_to_logical_apicid(cpu) >> 4)

/* Returns the number of the node containing MemBlk 'memblk' */
#define __memblk_to_node(memblk) (memblk)

/* Returns the number of the node containing Node 'node'.  This architecture is flat, 
   so it is a pretty simple function! */
#define __parent_node(node) (node)

/* Returns the number of the first CPU on Node 'node'.
 * This should be changed to a set of cached values
 * but this will do for now.
 */
static inline int __node_to_first_cpu(int node)
{
	int i, cpu, logical_apicid = node << 4;

	for(i = 1; i < 16; i <<= 1)
		/* check to see if the cpu is in the system */
		if ((cpu = logical_apicid_to_cpu(logical_apicid | i)) >= 0)
			/* if yes, return it to caller */
			return cpu;

	BUG(); /* couldn't find a cpu on given node */
	return -1;
}

/* Returns a bitmask of CPUs on Node 'node'.
 * This should be changed to a set of cached bitmasks
 * but this will do for now.
 */
static inline unsigned long __node_to_cpu_mask(int node)
{
	int i, cpu, logical_apicid = node << 4;
	unsigned long mask = 0UL;

	if (sizeof(unsigned long) * 8 < NR_CPUS)
		BUG();

	for(i = 1; i < 16; i <<= 1)
		/* check to see if the cpu is in the system */
		if ((cpu = logical_apicid_to_cpu(logical_apicid | i)) >= 0)
			/* if yes, add to bitmask */
			mask |= 1 << cpu;

	return mask;
}

/* Returns the number of the first MemBlk on Node 'node' */
#define __node_to_memblk(node) (node)

#else /* !CONFIG_X86_NUMAQ */
/*
 * Other i386 platforms should define their own version of the 
 * above macros here.
 */

#include <asm-generic/topology.h>

#endif /* CONFIG_X86_NUMAQ */

#endif /* _ASM_I386_TOPOLOGY_H */
