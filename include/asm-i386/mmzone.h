/*
 * Written by Pat Gaughen (gone@us.ibm.com) Mar 2002
 *
 */

#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#ifdef CONFIG_DISCONTIGMEM

#ifdef CONFIG_X86_NUMAQ
#include <asm/numaq.h>
#else
#define PHYSADDR_TO_NID(pa)	(0)
#define PFN_TO_NID(pfn)		(0)
#ifdef CONFIG_NUMA
#define _cpu_to_node(cpu) 0
#endif /* CONFIG_NUMA */
#endif /* CONFIG_X86_NUMAQ */

#ifdef CONFIG_NUMA
#define numa_node_id() _cpu_to_node(smp_processor_id())
#endif /* CONFIG_NUMA */

struct plat_pglist_data {
	pg_data_t	gendata;
};

extern struct plat_pglist_data *plat_node_data[];

/*
 * Following are macros that are specific to this numa platform.
 */
#define reserve_bootmem(addr, size) \
	reserve_bootmem_node(NODE_DATA(0), (addr), (size))
#define alloc_bootmem(x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low(x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), SMP_CACHE_BYTES, 0)
#define alloc_bootmem_pages(x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages(x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), PAGE_SIZE, 0)
#define alloc_bootmem_node(ignore, x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), SMP_CACHE_BYTES, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_pages_node(ignore, x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), PAGE_SIZE, __pa(MAX_DMA_ADDRESS))
#define alloc_bootmem_low_pages_node(ignore, x) \
	__alloc_bootmem_node(NODE_DATA(0), (x), PAGE_SIZE, 0)

#define PLAT_NODE_DATA(n)		(plat_node_data[(n)])
#define PLAT_NODE_DATA_STARTNR(n)	\
	(PLAT_NODE_DATA(n)->gendata.node_start_mapnr)
#define PLAT_NODE_DATA_SIZE(n)		(PLAT_NODE_DATA(n)->gendata.node_size)
#define PLAT_NODE_DATA_LOCALNR(pfn, n) \
	((pfn) - PLAT_NODE_DATA(n)->gendata.node_start_pfn)

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
 * and returns the the mem_map of that node.
 */
#define ADDR_TO_MAPBASE(kaddr) \
			NODE_MEM_MAP(KVADDR_TO_NID((unsigned long)(kaddr)))

/*
 * Given a kaddr, LOCAL_BASE_ADDR finds the owning node of the memory
 * and returns the kaddr corresponding to first physical page in the
 * node's mem_map.
 */
#define LOCAL_BASE_ADDR(kaddr)	((unsigned long)__va(NODE_DATA(KVADDR_TO_NID(kaddr))->node_start_pfn << PAGE_SHIFT))

#define LOCAL_MAP_NR(kvaddr) \
	(((unsigned long)(kvaddr)-LOCAL_BASE_ADDR(kvaddr)) >> PAGE_SHIFT)

#define kern_addr_valid(kaddr)	test_bit(LOCAL_MAP_NR(kaddr), \
					 NODE_DATA(KVADDR_TO_NID(kaddr))->valid_addr_bitmap)

#define pfn_to_page(pfn)	(NODE_MEM_MAP(PFN_TO_NID(pfn)) + PLAT_NODE_DATA_LOCALNR(pfn, PFN_TO_NID(pfn)))
#define page_to_pfn(page)	((page - page_zone(page)->zone_mem_map) + page_zone(page)->zone_start_pfn)
#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))
#endif /* CONFIG_DISCONTIGMEM */
#endif /* _ASM_MMZONE_H_ */
