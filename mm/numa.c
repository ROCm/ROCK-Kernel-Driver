/*
 * Written by Kanoj Sarcar, SGI, Aug 1999
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/spinlock.h>

int numnodes = 1;	/* Initialized for UMA platforms */

static bootmem_data_t contig_bootmem_data;
pg_data_t contig_page_data = { .bdata = &contig_bootmem_data };

#ifndef CONFIG_DISCONTIGMEM

/*
 * This is meant to be invoked by platforms whose physical memory starts
 * at a considerably higher value than 0. Examples are Super-H, ARM, m68k.
 * Should be invoked with paramters (0, 0, unsigned long *[], start_paddr).
 */
void __init free_area_init_node(int nid, pg_data_t *pgdat, struct page *pmap,
	unsigned long *zones_size, unsigned long node_start_pfn, 
	unsigned long *zholes_size)
{
	unsigned long size;

	contig_page_data.node_id = 0;
	contig_page_data.node_start_pfn = node_start_pfn;
	calculate_totalpages (&contig_page_data, zones_size, zholes_size);
	if (pmap == (struct page *)0) {
		size = (pgdat->node_size + 1) * sizeof(struct page);
		pmap = (struct page *) alloc_bootmem_node(pgdat, size);
	}
	contig_page_data.node_mem_map = pmap;
	free_area_init_core(&contig_page_data, zones_size, zholes_size);
	mem_map = contig_page_data.node_mem_map;
}

#endif /* !CONFIG_DISCONTIGMEM */

struct page * alloc_pages_node(int nid, unsigned int gfp_mask, unsigned int order)
{
#ifdef CONFIG_NUMA
	return __alloc_pages(gfp_mask, order, NODE_DATA(nid)->node_zonelists + (gfp_mask & GFP_ZONEMASK));
#else
	return alloc_pages(gfp_mask, order);
#endif
}

#ifdef CONFIG_DISCONTIGMEM

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

/*
 * Nodes can be initialized parallely, in no particular order.
 */
void __init free_area_init_node(int nid, pg_data_t *pgdat, struct page *pmap,
	unsigned long *zones_size, unsigned long node_start_pfn, 
	unsigned long *zholes_size)
{
	int i;
	unsigned long size;

	pgdat->node_id = nid;
	pgdat->node_start_pfn = node_start_pfn;
	calculate_totalpages (pgdat, zones_size, zholes_size);
	if (pmap == (struct page *)0) {
		size = (pgdat->node_size + 1) * sizeof(struct page); 
		pmap = (struct page *) alloc_bootmem_node(pgdat, size);
	}
	pgdat->node_mem_map = pmap;
	free_area_init_core(pgdat, zones_size, zholes_size);

	/*
	 * Get space for the valid bitmap.
	 */
	size = 0;
	for (i = 0; i < MAX_NR_ZONES; i++)
		size += zones_size[i];
	size = LONG_ALIGN((size + 7) >> 3);
	pgdat->valid_addr_bitmap = (unsigned long *)alloc_bootmem_node(pgdat, size);
	memset(pgdat->valid_addr_bitmap, 0, size);
}

static struct page * alloc_pages_pgdat(pg_data_t *pgdat, unsigned int gfp_mask,
	unsigned int order)
{
	return __alloc_pages(gfp_mask, order, pgdat->node_zonelists + (gfp_mask & GFP_ZONEMASK));
}

/*
 * This can be refined. Currently, tries to do round robin, instead
 * should do concentratic circle search, starting from current node.
 */
struct page * _alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	struct page *ret = 0;
	pg_data_t *start, *temp;
#ifndef CONFIG_NUMA
	unsigned long flags;
	static pg_data_t *next = 0;
#endif

	if (order >= MAX_ORDER)
		return NULL;
#ifdef CONFIG_NUMA
	temp = NODE_DATA(numa_node_id());
#else
	if (!next)
		next = pgdat_list;
	temp = next;
	next = next->pgdat_next;
#endif
	start = temp;
	while (temp) {
		if ((ret = alloc_pages_pgdat(temp, gfp_mask, order)))
			return(ret);
		temp = temp->pgdat_next;
	}
	temp = pgdat_list;
	while (temp != start) {
		if ((ret = alloc_pages_pgdat(temp, gfp_mask, order)))
			return(ret);
		temp = temp->pgdat_next;
	}
	return(0);
}

#endif /* CONFIG_DISCONTIGMEM */
