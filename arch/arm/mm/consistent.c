/*
 *  linux/arch/arm/mm/consistent.c
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Dynamic DMA mapping support.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

/*
 * This allocates one page of cache-coherent memory space and returns
 * both the virtual and a "dma" address to that space.  It is not clear
 * whether this could be called from an interrupt context or not.  For
 * now, we expressly forbid it, especially as some of the stuff we do
 * here is not interrupt context safe.
 *
 * Note that this does *not* zero the allocated area!
 */
void *consistent_alloc(int gfp, size_t size, dma_addr_t *dma_handle)
{
	int order;
	unsigned long page;
	void *ret;

	if (in_interrupt())
		BUG();

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = __get_free_pages(gfp, order);
	if (!page)
		goto no_page;

	ret = __ioremap(virt_to_phys((void *)page), size, 0);
	if (ret) {
		/* free wasted pages */
		unsigned long end;

		/*
		 * we need to ensure that there are no
		 * cachelines in use, or worse dirty in
		 * this area.
		 */
		invalidate_dcache_range(page, page + size);
		invalidate_dcache_range((unsigned long)ret, (unsigned long)ret + size);

		*dma_handle = __virt_to_bus(page);

		end = page + (PAGE_SIZE << order);
		page += size;
		while (page < end) {
			free_page(page);
			page += PAGE_SIZE;
		}
		return ret;
	}

	free_pages(page, order);
no_page:
	BUG();
	return NULL;
}

void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size, dma_addr_t *handle)
{
	void *__ret;
	int __gfp = GFP_KERNEL;

#ifdef CONFIG_PCI
	if ((hwdev) == NULL ||
	    (hwdev)->dma_mask != 0xffffffff)
#endif
		__gfp |= GFP_DMA;

	__ret = consistent_alloc(__gfp, (size),
				 (handle));
	return __ret;
}

/*
 * free a page as defined by the above mapping.  We expressly forbid
 * calling this from interrupt context.
 */
void consistent_free(void *vaddr)
{
	if (in_interrupt())
		BUG();

	__iounmap(vaddr);
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
