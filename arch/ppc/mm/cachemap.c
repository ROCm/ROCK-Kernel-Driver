/*
 * BK Id: %F% %I% %G% %U% %#%
 *
 *  PowerPC version derived from arch/arm/mm/consistent.c
 *    Copyright (C) 2001 Dan Malek (dmalek@jlc.net)
 *
 *  linux/arch/arm/mm/consistent.c
 *
 *  Copyright (C) 2000 Russell King
 *
 * Consistent memory allocators.  Used for DMA devices that want to
 * share uncached memory with the processor core.  The function return
 * is the virtual address and 'dma_handle' is the physical address.
 * Mostly stolen from the ARM port, with some changes for PowerPC.
 *						-- Dan
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/pci.h>

#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/hardirq.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include <asm/machdep.h>

extern int get_pteptr(struct mm_struct *mm, unsigned long addr, pte_t **ptep);

void *consistent_alloc(int gfp, size_t size, dma_addr_t *dma_handle)
{
	int order, rsize;
	unsigned long page;
	void *ret;
	pte_t	*pte;

	if (in_interrupt())
		BUG();

	/* Only allocate page size areas.
	*/
	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = __get_free_pages(gfp, order);
	if (!page) {
		BUG();
		return NULL;
	}

	/*
	 * we need to ensure that there are no cachelines in use,
	 * or worse dirty in this area.
	 */
	invalidate_dcache_range(page, page + size);

	ret = (void *)page;
	*dma_handle = virt_to_bus(ret);

	/* Chase down all of the PTEs and mark them uncached.
	*/
	rsize = (int)size;
	while (rsize > 0) {
		if (get_pteptr(&init_mm, page, &pte)) {
			pte_val(*pte) |= _PAGE_NO_CACHE | _PAGE_GUARDED;
			flush_tlb_page(find_vma(&init_mm,page),page);
		}
		else {
			BUG();
			return NULL;
		}
		page += PAGE_SIZE;
		rsize -= PAGE_SIZE;
	}

	return ret;
}

/*
 * free page(s) as defined by the above mapping.
 * The caller has to tell us the size so we can free the proper number
 * of pages.  We can't vmalloc() a new space for these pages and simply
 * call vfree() like some other architectures because we could end up
 * with aliased cache lines (or at least a cache line with the wrong
 * attributes).  This can happen when the PowerPC speculative loads
 * across page boundaries.
 */
void consistent_free(void *vaddr, size_t size)
{
	int order, rsize;
	unsigned long addr;
	pte_t	*pte;

	if (in_interrupt())
		BUG();

	size = PAGE_ALIGN(size);
	order = get_order(size);

	/* Chase down all of the PTEs and mark them cached again.
	*/
	addr = (unsigned long)vaddr;
	rsize = (int)size;
	while (rsize > 0) {
		if (get_pteptr(&init_mm, addr, &pte)) {
			pte_val(*pte) &= ~(_PAGE_NO_CACHE | _PAGE_GUARDED);
			flush_tlb_page(find_vma(&init_mm,addr),addr);
		}
		else {
			BUG();
			return;
		}
		addr += PAGE_SIZE;
		rsize -= PAGE_SIZE;
	}
	free_pages((unsigned long)vaddr, order);
}

/*
 * make an area consistent.
 */
void consistent_sync(void *vaddr, size_t size, int direction)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end   = start + size;

	switch (direction) {
	case PCI_DMA_NONE:
		BUG();
	case PCI_DMA_FROMDEVICE:	/* invalidate only */
		invalidate_dcache_range(start, end);
		break;
	case PCI_DMA_TODEVICE:		/* writeback only */
		clean_dcache_range(start, end);
		break;
	case PCI_DMA_BIDIRECTIONAL:	/* writeback and invalidate */
		flush_dcache_range(start, end);
		break;
	}
}
