/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 *
 * PowerPC64 port:
 * Copyright (C) 2002 Anton Blanchard, IBM Corp.
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <linux/config.h>

typedef struct plat_pglist_data {
	pg_data_t	gendata;
} plat_pg_data_t;

/*
 * Following are macros that are specific to this numa platform.
 */

extern plat_pg_data_t plat_node_data[];

#define MAX_NUMNODES 4

/* XXX grab this from the device tree - Anton */
#define PHYSADDR_TO_NID(pa)		((pa) >> 36)
#define PLAT_NODE_DATA(n)		(&plat_node_data[(n)])
#define PLAT_NODE_DATA_STARTNR(n)	\
	(PLAT_NODE_DATA(n)->gendata.node_start_mapnr)
#define PLAT_NODE_DATA_SIZE(n)		(PLAT_NODE_DATA(n)->gendata.node_size)
#define PLAT_NODE_DATA_LOCALNR(p, n)	\
	(((p) - PLAT_NODE_DATA(n)->gendata.node_start_paddr) >> PAGE_SHIFT)

#ifdef CONFIG_DISCONTIGMEM

/*
 * Following are macros that each numa implmentation must define.
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define KVADDR_TO_NID(kaddr)	PHYSADDR_TO_NID(__pa(kaddr))

/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(n)	(&((PLAT_NODE_DATA(n))->gendata))

/*
 * NODE_MEM_MAP gives the kaddr for the mem_map of the node.
 */
#define NODE_MEM_MAP(nid)	(NODE_DATA(nid)->node_mem_map)

/*
 * Given a kaddr, ADDR_TO_MAPBASE finds the owning node of the memory
 * and returns the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr) \
			NODE_MEM_MAP(KVADDR_TO_NID((unsigned long)(kaddr)))

/*
 * Given a kaddr, LOCAL_BASE_ADDR finds the owning node of the memory
 * and returns the kaddr corresponding to first physical page in the
 * node's mem_map.
 */
#define LOCAL_BASE_ADDR(kaddr) \
	((unsigned long)__va(NODE_DATA(KVADDR_TO_NID(kaddr))->node_start_paddr))

#define LOCAL_MAP_NR(kvaddr) \
	(((unsigned long)(kvaddr)-LOCAL_BASE_ADDR(kvaddr)) >> PAGE_SHIFT)

#if 0
/* XXX fix - Anton */
#define kern_addr_valid(kaddr)	test_bit(LOCAL_MAP_NR(kaddr), \
					 NODE_DATA(KVADDR_TO_NID(kaddr))->valid_addr_bitmap)
#endif

#define discontigmem_pfn_to_page(pfn) \
({ \
	unsigned long kaddr = (unsigned long)__va(pfn << PAGE_SHIFT); \
	(ADDR_TO_MAPBASE(kaddr) + LOCAL_MAP_NR(kaddr)); \
})

#ifdef CONFIG_NUMA

/* XXX grab this from the device tree - Anton */
#define cputonode(cpu)	((cpu) >> 3)

#define numa_node_id()	cputonode(smp_processor_id())

#endif /* CONFIG_NUMA */
#endif /* CONFIG_DISCONTIGMEM */
#endif /* _ASM_MMZONE_H_ */
