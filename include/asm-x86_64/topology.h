#ifndef _ASM_X86_64_TOPOLOGY_H
#define _ASM_X86_64_TOPOLOGY_H

#include <linux/config.h>

#ifdef CONFIG_DISCONTIGMEM

#include <asm/mpspec.h>

/* Map the K8 CPU local memory controllers to a simple 1:1 CPU:NODE topology */

extern int fake_node;
/* This is actually a cpumask_t, but doesn't matter because we don't have
   >BITS_PER_LONG CPUs */
extern unsigned long cpu_online_map;

#define cpu_to_node(cpu)		(fake_node ? 0 : (cpu))
#define memblk_to_node(memblk) 	(fake_node ? 0 : (memblk))
#define parent_node(node)		(node)
#define node_to_first_cpu(node) 	(fake_node ? 0 : (node))
#define node_to_cpumask(node)	(fake_node ? cpu_online_map : (1UL << (node)))
#define node_to_memblk(node)		(node)

static inline unsigned long pcibus_to_cpumask(int bus)
{
	return mp_bus_to_cpumask[bus] & cpu_online_map; 
}

#define NODE_BALANCE_RATE 30	/* CHECKME */ 

#endif

#include <asm-generic/topology.h>

#endif
