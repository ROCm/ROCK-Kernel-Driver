#ifndef AGP_H
#define AGP_H 1

/* dummy for now */

#define map_page_into_agp(page) 
#define unmap_page_from_agp(page) 
#define flush_agp_mappings() 
#define flush_agp_cache() mb()

/*
 * Page-protection value to be used for AGP memory mapped into kernel space.  For
 * platforms which use coherent AGP DMA, this can be PAGE_KERNEL.  For others, it needs to
 * be an uncached mapping (such as write-combining).
 */
#define PAGE_AGP			PAGE_KERNEL_NOCACHE

#endif
