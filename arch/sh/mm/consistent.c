/*
 * arch/sh/mm/consistent.c
 *
 * Copyright (C) 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>

void *consistent_alloc(int gfp, size_t size, dma_addr_t *handle)
{
	struct page *page, *end, *free;
	void *ret;
	int order;

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;

	ret = (void *)P2SEGADDR(page_to_bus(page));

	/*
	 * We must flush the cache before we pass it on to the device
	 */
	dma_cache_wback_inv(ret, size);

	*handle = (unsigned long)ret;

	free = page + (size >> PAGE_SHIFT);
	end  = page + (1 << order);

	do {
		set_page_count(page, 1);
		page++;
	} while (size -= PAGE_SIZE);

	/*
	 * Free any unused pages
	 */
	while (page < end) {
		set_page_count(page, 1);
		__free_page(page);
		page++;
	}

	return ret;
}

void consistent_free(void *vaddr, size_t size)
{
	unsigned long addr = P1SEGADDR((unsigned long)vaddr);

	free_pages(addr, get_order(size));
}

void consistent_sync(void *vaddr, size_t size, int direction)
{
	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		dma_cache_inv(vaddr, size);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		dma_cache_wback(vaddr, size);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		dma_cache_wback_inv(vaddr, size);
		break;
	default:
		BUG();
	}
}

