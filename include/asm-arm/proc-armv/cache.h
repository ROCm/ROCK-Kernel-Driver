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
 * Generic I + D cache
 */
#define flush_cache_all()						\
	do {								\
		cpu_cache_clean_invalidate_all();			\
	} while (0)

/* This is always called for current->mm */
#define flush_cache_mm(_mm)						\
	do {								\
		if ((_mm) == current->active_mm)			\
			cpu_cache_clean_invalidate_all();		\
	} while (0)

#define flush_cache_range(_vma,_start,_end)				\
	do {								\
		if ((_vma)->vm_mm == current->active_mm)		\
			cpu_cache_clean_invalidate_range((_start), (_end), 1); \
	} while (0)

#define flush_cache_page(_vma,_vmaddr)					\
	do {								\
		if ((_vma)->vm_mm == current->active_mm) {		\
			cpu_cache_clean_invalidate_range((_vmaddr),	\
				(_vmaddr) + PAGE_SIZE,			\
				((_vma)->vm_flags & VM_EXEC));		\
		} \
	} while (0)

/*
 * This flushes back any buffered write data.  We have to clean the entries
 * in the cache for this page.  This does not invalidate either I or D caches.
 *
 * Called from:
 *  1. fs/exec.c:put_dirty_page				- ok
 *     - page came from alloc_page(), so page->mapping = NULL.
 *     - flush_dcache_page called immediately prior.
 *
 *  2. kernel/ptrace.c:access_one_page			- flush_icache_page
 *     - flush_cache_page takes care of the user space side of the mapping.
 *     - page is either a page cache page (with page->mapping set, and
 *       hence page->mapping->i_mmap{,shared} also set) or an anonymous
 *       page.  I think this is ok.
 *
 *  3. kernel/ptrace.c:access_one_page			- bad
 *     - flush_cache_page takes care of the user space side of the mapping.
 *     - no apparant cache protection, reading the kernel virtual alias
 *
 *  4. mm/filemap.c:filemap_no_page			- ok
 *     - add_to_page_cache_* clears PG_arch_1.
 *     - page->mapping != NULL.
 *     - i_mmap or i_mmap_shared will be non-null if mmap'd
 *     - called from (8).
 *
 *  5. mm/memory.c:break_cow,do_wp_page			- {copy,clear}_user_page
 *     - need to ensure that copy_cow_page has pushed all data from the dcache
 *       to the page.
 *       - calls
 *         - clear_user_highpage -> clear_user_page
 *         - copy_user_highpage -> copy_user_page
 *
 *  6. mm/memory.c:do_swap_page				- flush_icache_page
 *     - flush_icache_page called afterwards - if flush_icache_page does the
 *       same as flush_dcache_page, update_mmu_cache will do the work for us.
 *     - update_mmu_cache called.
 *
 *  7. mm/memory.c:do_anonymous_page			- {copy,clear}_user_page
 *     - calls clear_user_highpage.  See (5)
 *
 *  8. mm/memory.c:do_no_page				- flush_icache_page
 *     - flush_icache_page called afterwards - if flush_icache_page does the
 *       same as flush_dcache_page, update_mmu_cache will do the work for us.
 *     - update_mmu_cache called.
 *     - When we place a user mapping, we will call update_mmu_cache,
 *       which will catch PG_arch_1 set.
 *
 *  9. mm/shmem.c:shmem_no_page				- ok
 *     - shmem_get_page clears PG_arch_1, as does add_to_page_cache (duplicate)
 *     - page->mapping != NULL.
 *     - i_mmap or i_mmap_shared will be non-null if mmap'd
 *     - called from (8).
 *
 * 10. mm/swapfile.c:try_to_unuse			- bad
 *     - this looks really dodgy - we're putting pages from the swap cache
 *       straight into processes, and the only cache handling appears to
 *       be flush_page_to_ram.
 */
#define flush_page_to_ram_ok
#ifdef flush_page_to_ram_ok
#define flush_page_to_ram(page)	do { } while (0)
#else
static __inline__ void flush_page_to_ram(struct page *page)
{
	cpu_flush_ram_page(page_address(page));
}
#endif

/*
 * D cache only
 */

#define invalidate_dcache_range(_s,_e)	cpu_dcache_invalidate_range((_s),(_e))
#define clean_dcache_range(_s,_e)	cpu_dcache_clean_range((_s),(_e))
#define flush_dcache_range(_s,_e)	cpu_cache_clean_invalidate_range((_s),(_e),0)

#define mapping_mapped(map)	(!list_empty(&(map)->i_mmap) || \
				 !list_empty(&(map)->i_mmap_shared))

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
static inline void flush_dcache_page(struct page *page)
{
	if (page->mapping && !mapping_mapped(page->mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else {
		unsigned long virt = (unsigned long)page_address(page);
		cpu_cache_clean_invalidate_range(virt, virt + PAGE_SIZE, 0);
	}
}

/*
 * flush_icache_page makes the kernel page address consistent with the
 * user space mappings.  The functionality is the same as flush_dcache_page,
 * except we can do an optimisation and only clean the caches here if
 * vma->vm_mm == current->active_mm.
 *
 * This function is misnamed IMHO.  There are three places where it
 * is called, each of which is preceded immediately by a call to
 * flush_page_to_ram:
 */
#ifdef flush_page_to_ram_ok
static inline void flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	if (page->mapping && !mapping_mapped(page->mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else if (vma->vm_mm == current->active_mm) {
		unsigned long virt = (unsigned long)page_address(page);
		cpu_cache_clean_invalidate_range(virt, virt + PAGE_SIZE, 0);
	}
}
#else
#define flush_icache_page(vma,pg)	do { } while (0)
#endif

#define clean_dcache_entry(_s)		cpu_dcache_clean_entry((unsigned long)(_s))

/*
 * I cache coherency stuff.
 *
 * This *is not* just icache.  It is to make data written to memory
 * consistent such that instructions fetched from the region are what
 * we expect.
 *
 * This generally means that we have to clean out the Dcache and write
 * buffers, and maybe flush the Icache in the specified range.
 */
#define flush_icache_range(_s,_e)					\
	do {								\
		cpu_icache_invalidate_range((_s), (_e));		\
	} while (0)

/*
 *	TLB Management
 *	==============
 *
 *	The arch/arm/mm/tlb-*.S files implement these methods.
 *
 *	The TLB specific code is expected to perform whatever tests it
 *	needs to determine if it should invalidate the TLB for each
 *	call.  Start addresses are inclusive and end addresses are
 *	exclusive; it is safe to round these addresses down.
 *
 *	flush_tlb_all()
 *
 *		Invalidate the entire TLB.
 *
 *	flush_tlb_mm(mm)
 *
 *		Invalidate all TLB entries in a particular address
 *		space.
 *		- mm	- mm_struct describing address space
 *
 *	flush_tlb_range(mm,start,end)
 *
 *		Invalidate a range of TLB entries in the specified
 *		address space.
 *		- mm	- mm_struct describing address space
 *		- start - start address (may not be aligned)
 *		- end	- end address (exclusive, may not be aligned)
 *
 *	flush_tlb_page(vaddr,vma)
 *
 *		Invalidate the specified page in the specified address range.
 *		- vaddr - virtual address (may not be aligned)
 *		- vma	- vma_struct describing address range
 *
 *	flush_kern_tlb_page(kaddr)
 *
 *		Invalidate the TLB entry for the specified page.  The address
 *		will be in the kernels virtual memory space.  Current uses
 *		only require the D-TLB to be invalidated.
 *		- kaddr - Kernel virtual memory address
 */

struct cpu_tlb_fns {
	void (*flush_kern_all)(void);
	void (*flush_user_mm)(struct mm_struct *);
	void (*flush_user_range)(unsigned long, unsigned long, struct vm_area_struct *);
	void (*flush_user_page)(unsigned long, struct vm_area_struct *);
	void (*flush_kern_page)(unsigned long);
};

/*
 * Convert calls to our calling convention.
 */
#define flush_tlb_all()			__cpu_flush_kern_tlb_all()
#define flush_tlb_mm(mm)		__cpu_flush_user_tlb_mm(mm)
#define flush_tlb_range(vma,start,end)	__cpu_flush_user_tlb_range(start,end,vma)
#define flush_tlb_page(vma,vaddr)	__cpu_flush_user_tlb_page(vaddr,vma)
#define flush_kern_tlb_page(kaddr)	__cpu_flush_kern_tlb_page(kaddr)

/*
 * Now select the calling method
 */
#ifdef MULTI_TLB

extern struct cpu_tlb_fns cpu_tlb;

#define __cpu_flush_kern_tlb_all	cpu_tlb.flush_kern_all
#define __cpu_flush_user_tlb_mm		cpu_tlb.flush_user_mm
#define __cpu_flush_user_tlb_range	cpu_tlb.flush_user_range
#define __cpu_flush_user_tlb_page	cpu_tlb.flush_user_page
#define __cpu_flush_kern_tlb_page	cpu_tlb.flush_kern_page

#else

#define __cpu_flush_kern_tlb_all	__glue(_TLB,_flush_kern_tlb_all)
#define __cpu_flush_user_tlb_mm		__glue(_TLB,_flush_user_tlb_mm)
#define __cpu_flush_user_tlb_range	__glue(_TLB,_flush_user_tlb_range)
#define __cpu_flush_user_tlb_page	__glue(_TLB,_flush_user_tlb_page)
#define __cpu_flush_kern_tlb_page	__glue(_TLB,_flush_kern_tlb_page)

extern void __cpu_flush_kern_tlb_all(void);
extern void __cpu_flush_user_tlb_mm(struct mm_struct *);
extern void __cpu_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
extern void __cpu_flush_user_tlb_page(unsigned long, struct vm_area_struct *);
extern void __cpu_flush_kern_tlb_page(unsigned long);

#endif

/*
 * if PG_dcache_dirty is set for the page, we need to ensure that any
 * cache entries for the kernels virtual memory range are written
 * back to the page.
 */
extern void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t pte);

/*
 * Old ARM MEMC stuff.  This supports the reversed mapping handling that
 * we have on the older 26-bit machines.  We don't have a MEMC chip, so...
 */
#define memc_update_all()		do { } while (0)
#define memc_update_mm(mm)		do { } while (0)
#define memc_update_addr(mm,pte,log)	do { } while (0)
#define memc_clear(mm,physaddr)		do { } while (0)
