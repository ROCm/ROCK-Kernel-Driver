/*
 * Written by Pat Gaughen (gone@us.ibm.com) Mar 2002
 *
 */

#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <asm/smp.h>

#ifdef CONFIG_DISCONTIGMEM

#ifdef CONFIG_X86_NUMAQ
#include <asm/numaq.h>
#else
#define pfn_to_nid(pfn)		(0)
#endif /* CONFIG_X86_NUMAQ */

extern struct pglist_data *node_data[];

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

#define node_size(nid)		(node_data[nid]->node_size)
#define node_localnr(pfn, nid)	((pfn) - node_data[nid]->node_start_pfn)

/*
 * Following are macros that each numa implmentation must define.
 */

/*
 * Given a kernel address, find the home node of the underlying memory.
 */
#define kvaddr_to_nid(kaddr)	pfn_to_nid(__pa(kaddr) >> PAGE_SHIFT)

/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(nid)		(node_data[nid])

#define node_mem_map(nid)	(NODE_DATA(nid)->node_mem_map)
#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid)       (NODE_DATA(nid)->node_start_pfn + \
				 NODE_DATA(nid)->node_size)

#define local_mapnr(kvaddr) \
	( (__pa(kvaddr) >> PAGE_SHIFT) - node_start_pfn(kvaddr_to_nid(kvaddr)) )

#define kern_addr_valid(kaddr)	test_bit(local_mapnr(kaddr), \
		 NODE_DATA(kvaddr_to_nid(kaddr))->valid_addr_bitmap)

#define pfn_to_page(pfn)	(node_mem_map(pfn_to_nid(pfn)) + node_localnr(pfn, pfn_to_nid(pfn)))
#define page_to_pfn(page)	((page - page_zone(page)->zone_mem_map) + page_zone(page)->zone_start_pfn)
#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))
/*
 * pfn_valid should be made as fast as possible, and the current definition 
 * is valid for machines that are NUMA, but still contiguous, which is what
 * is currently supported. A more generalised, but slower definition would
 * be something like this - mbligh:
 * ( pfn_to_pgdat(pfn) && (pfn < node_end_pfn(pfn_to_nid(pfn))) ) 
 */ 
#define pfn_valid(pfn)          (pfn < num_physpages)
#endif /* CONFIG_DISCONTIGMEM */
#endif /* _ASM_MMZONE_H_ */
