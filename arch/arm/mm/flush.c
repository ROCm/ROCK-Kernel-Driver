/*
 *  linux/arch/arm/mm/flush.c
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <asm/cacheflush.h>

static void __flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *mpnt;
	struct prio_tree_iter iter;
	unsigned long offset;
	pgoff_t pgoff;

	__cpuc_flush_dcache_page(page_address(page));

	if (!mapping)
		return;

	/*
	 * With a VIVT cache, we need to also write back
	 * and invalidate any user data.
	 */
	pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

	flush_dcache_mmap_lock(mapping);
	vma_prio_tree_foreach(mpnt, &iter, &mapping->i_mmap, pgoff, pgoff) {
		/*
		 * If this VMA is not in our MM, we can ignore it.
		 */
		if (mpnt->vm_mm != mm)
			continue;
		if (!(mpnt->vm_flags & VM_MAYSHARE))
			continue;
		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		flush_cache_page(mpnt, mpnt->vm_start + offset);
	}
	flush_dcache_mmap_unlock(mapping);
}

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (mapping && !mapping_mapped(mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else
		__flush_dcache_page(page);
}
EXPORT_SYMBOL(flush_dcache_page);
