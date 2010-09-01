#ifndef _ASM_X86_AGP_H
#define _ASM_X86_AGP_H

#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/system.h>

/*
 * Functions to keep the agpgart mappings coherent with the MMU. The
 * GART gives the CPU a physical alias of pages in memory. The alias
 * region is mapped uncacheable. Make sure there are no conflicting
 * mappings with different cachability attributes for the same
 * page. This avoids data corruption on some CPUs.
 */

#define map_page_into_agp(page) ( \
	xen_create_contiguous_region((unsigned long)page_address(page), 0, 32) \
	?: set_pages_uc(page, 1))
#define unmap_page_from_agp(page) ( \
	xen_destroy_contiguous_region((unsigned long)page_address(page), 0), \
	/* only a fallback: xen_destroy_contiguous_region uses PAGE_KERNEL */ \
	set_pages_wb(page, 1))

/*
 * Could use CLFLUSH here if the cpu supports it. But then it would
 * need to be called for each cacheline of the whole page so it may
 * not be worth it. Would need a page for it.
 */
#define flush_agp_cache() wbinvd()

#define virt_to_gart virt_to_machine

/* GATT allocation. Returns/accepts GATT kernel virtual address. */
#define alloc_gatt_pages(order)	({                                          \
	char *_t; dma_addr_t _d;                                            \
	_t = dma_alloc_coherent(NULL,PAGE_SIZE<<(order),&_d,GFP_KERNEL);    \
	_t; })
#define free_gatt_pages(table, order)	\
	dma_free_coherent(NULL,PAGE_SIZE<<(order),(table),virt_to_bus(table))

#endif /* _ASM_X86_AGP_H */
