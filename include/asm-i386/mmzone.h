/*
 * Written by Pat Gaughen (gone@us.ibm.com) Mar 2002
 *
 */

#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <asm/smp.h>

#ifdef CONFIG_DISCONTIGMEM

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
#define node_end_pfn(nid)						\
({									\
	pg_data_t *__pgdat = NODE_DATA(nid);				\
	__pgdat->node_start_pfn + __pgdat->node_size;			\
})

#define local_mapnr(kvaddr)						\
({									\
	unsigned long __pfn = __pa(kvaddr) >> PAGE_SHIFT;		\
	(__pfn - node_start_pfn(pfn_to_nid(__pfn)));			\
})

#define kern_addr_valid(kaddr)						\
({									\
	unsigned long __kaddr = (unsigned long)(kaddr);			\
	pg_data_t *__pgdat = NODE_DATA(kvaddr_to_nid(__kaddr));		\
	test_bit(local_mapnr(__kaddr), __pgdat->valid_addr_bitmap);	\
})

#define pfn_to_page(pfn)						\
({									\
	unsigned long __pfn = pfn;					\
	int __node  = pfn_to_nid(__pfn);				\
	&node_mem_map(__node)[node_localnr(__pfn,__node)];		\
})

#define page_to_pfn(pg)							\
({									\
	struct page *__page = pg;					\
	struct zone *__zone = page_zone(__page);			\
	(unsigned long)(__page - __zone->zone_mem_map)			\
		+ __zone->zone_start_pfn;				\
})
#define pmd_page(pmd)		(pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))
/*
 * pfn_valid should be made as fast as possible, and the current definition 
 * is valid for machines that are NUMA, but still contiguous, which is what
 * is currently supported. A more generalised, but slower definition would
 * be something like this - mbligh:
 * ( pfn_to_pgdat(pfn) && ((pfn) < node_end_pfn(pfn_to_nid(pfn))) ) 
 */ 
#define pfn_valid(pfn)          ((pfn) < num_physpages)

/*
 * generic node memory support, the following assumptions apply:
 *
 * 1) memory comes in 256Mb contigious chunks which are either present or not
 * 2) we will not have more than 64Gb in total
 *
 * for now assume that 64Gb is max amount of RAM for whole system
 *    64Gb / 4096bytes/page = 16777216 pages
 */
#define MAX_NR_PAGES 16777216
#define MAX_ELEMENTS 256
#define PAGES_PER_ELEMENT (MAX_NR_PAGES/MAX_ELEMENTS)

extern u8 physnode_map[];

static inline int pfn_to_nid(unsigned long pfn)
{
	return(physnode_map[(pfn) / PAGES_PER_ELEMENT]);
}
static inline struct pglist_data *pfn_to_pgdat(unsigned long pfn)
{
	return(NODE_DATA(pfn_to_nid(pfn)));
}

#ifdef CONFIG_X86_NUMAQ
#include <asm/numaq.h>
#elif CONFIG_NUMA	/* summit or generic arch */
#include <asm/srat.h>
#elif CONFIG_X86_PC
#define get_memcfg_numa get_memcfg_numa_flat
#define get_zholes_size(n) (0)
#else
#define pfn_to_nid(pfn)		(0)
#endif /* CONFIG_X86_NUMAQ */

#endif /* CONFIG_DISCONTIGMEM */
#endif /* _ASM_MMZONE_H_ */
