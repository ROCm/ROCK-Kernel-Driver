/*
 *  linux/include/asm-arm/proc-armv/cache.h
 *
 *  Copyright (C) 1999-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/mman.h>
#include <asm/glue.h>

/*
 * This flag is used to indicate that the page pointed to by a pte
 * is dirty and requires cleaning before returning it to the user.
 */
#define PG_dcache_dirty PG_arch_1

/*
 * Cache handling for 32-bit ARM processors.
 *
 * Note that on ARM, we have a more accurate specification than that
 * Linux's "flush".  We therefore do not use "flush" here, but instead
 * use:
 *
 * clean:      the act of pushing dirty cache entries out to memory.
 * invalidate: the act of discarding data held within the cache,
 *             whether it is dirty or not.
 */

/*
 * These are private to the dma-mapping API.  Do not use directly.
 * Their sole purpose is to ensure that data held in the cache
 * is visible to DMA, or data written by DMA to system memory is
 * visible to the CPU.
 */
#define dmac_inv_range			cpu_dcache_invalidate_range
#define dmac_clean_range		cpu_dcache_clean_range
#define dmac_flush_range(_s,_e)		cpu_cache_clean_invalidate_range((_s),(_e),0)

/*
 * Convert calls to our calling convention.
 */
#define flush_cache_all()		cpu_cache_clean_invalidate_all()

static inline void flush_cache_mm(struct mm_struct *mm)
{
	if (current->active_mm == mm)
		cpu_cache_clean_invalidate_all();
}

static inline void
flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	if (current->active_mm == vma->vm_mm)
		cpu_cache_clean_invalidate_range(start & PAGE_MASK,
					PAGE_ALIGN(end), vma->vm_flags);
}

static inline void
flush_cache_page(struct vm_area_struct *vma, unsigned long user_addr)
{
	if (current->active_mm == vma->vm_mm) {
		unsigned long addr = user_addr & PAGE_MASK;
		cpu_cache_clean_invalidate_range(addr, addr + PAGE_SIZE,
				vma->vm_flags & VM_EXEC);
	}
}

/*
 * Perform necessary cache operations to ensure that data previously
 * stored within this range of addresses can be executed by the CPU.
 */
#define flush_icache_range(s,e)		cpu_icache_invalidate_range(s,e)

/*
 * Perform necessary cache operations to ensure that the TLB will
 * see data written in the specified area.
 */
#define clean_dcache_area(start,size) \
	cpu_cache_clean_invalidate_range((unsigned long)start, \
					 ((unsigned long)start) + size, 0);

/*
 * flush_dcache_page is used when the kernel has written to the page
 * cache page at virtual address page->virtual.
 *
 * If this page isn't mapped (ie, page->mapping = NULL), or it has
 * userspace mappings (page->mapping->i_mmap or page->mapping->i_mmap_shared)
 * then we _must_ always clean + invalidate the dcache entries associated
 * with the kernel mapping.
 *
 * Otherwise we can defer the operation, and clean the cache when we are
 * about to change to user space.  This is the same method as used on SPARC64.
 * See update_mmu_cache for the user space part.
 */
#define mapping_mapped(map)	(!list_empty(&(map)->i_mmap) || \
				 !list_empty(&(map)->i_mmap_shared))

extern void __flush_dcache_page(struct page *);

static inline void flush_dcache_page(struct page *page)
{
	if (page->mapping && !mapping_mapped(page->mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else
		__flush_dcache_page(page);
}

#define flush_icache_user_range(vma,page,addr,len) \
	flush_dcache_page(page)

/*
 * We don't appear to need to do anything here.  In fact, if we did, we'd
 * duplicate cache flushing elsewhere performed by flush_dcache_page().
 */
#define flush_icache_page(vma,page)	do { } while (0)
