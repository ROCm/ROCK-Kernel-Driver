/*
 *  PowerPC version derived from arch/arm/mm/consistent.c
 *    Copyright (C) 2001 Dan Malek (dmalek@jlc.net)
 *
 *  arch/ppc/mm/cachemap.c
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

int map_page(unsigned long va, phys_addr_t pa, int flags);

/* This function will allocate the requested contiguous pages and
 * map them into the kernel's vmalloc() space.  This is done so we
 * get unique mapping for these pages, outside of the kernel's 1:1
 * virtual:physical mapping.  This is necessary so we can cover large
 * portions of the kernel with single large page TLB entries, and
 * still get unique uncached pages for consistent DMA.
 */
void *consistent_alloc(int gfp, size_t size, dma_addr_t *dma_handle)
{
	int order, err;
	struct page *page, *free, *end;
	phys_addr_t pa;
	unsigned long flags, offset;
	struct vm_struct *area = NULL;
	unsigned long va = 0;

	BUG_ON(in_interrupt());

	/* Only allocate page size areas */
	size = PAGE_ALIGN(size);
	order = get_order(size);

	free = page = alloc_pages(gfp, order);
	if (! page)
		return NULL;

	pa = page_to_phys(page);
	*dma_handle = page_to_bus(page);
	end = page + (1 << order);

	/*
	 * we need to ensure that there are no cachelines in use,
	 * or worse dirty in this area.
	 */
	invalidate_dcache_range((unsigned long)page_address(page),
				(unsigned long)page_address(page) + size);

	/*
	 * alloc_pages() expects the block to be handled as a unit, so
	 * it only sets the page count on the first page.  We set the
	 * counts on each page so they can be freed individually
	 */
	for (; page < end; page++)
		set_page_count(page, 1);


	/* Allocate some common virtual space to map the new pages*/
	area = get_vm_area(size, VM_ALLOC);
	if (! area)
		goto out;

	va = (unsigned long) area->addr;

	flags = _PAGE_KERNEL | _PAGE_NO_CACHE;
	
	for (offset = 0; offset < size; offset += PAGE_SIZE) {
		err = map_page(va+offset, pa+offset, flags);
		if (err) {
			vfree((void *)va);
			va = 0;
			goto out;
		}

		free++;
	}

 out:
	/* Free pages which weren't mapped */
	for (; free < end; free++) {
		__free_page(free);
	}

	return (void *)va;
}

/*
 * free page(s) as defined by the above mapping.
 */
void consistent_free(void *vaddr)
{
	BUG_ON(in_interrupt());
	vfree(vaddr);
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

/*
 * consistent_sync_page make a page are consistent. identical
 * to consistent_sync, but takes a struct page instead of a virtual address
 */

void consistent_sync_page(struct page *page, unsigned long offset,
	size_t size, int direction)
{
	unsigned long start;

	start = (unsigned long)page_address(page) + offset;
	consistent_sync((void *)start, size, direction);
}
