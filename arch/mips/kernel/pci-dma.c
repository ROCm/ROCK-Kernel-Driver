/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000  Ani Joshi <ajoshi@unixbox.com>
 * Copyright (C) 2000, 2001  Ralf Baechle <ralf@gnu.org>
 * swiped from i386, and cloned for MIPS by Geert, polished by Ralf.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/pci.h>

#include <asm/io.h>

#ifndef UNCAC_BASE	/* Hack ... */
#define UNCAC_BASE	0x9000000000000000UL
#endif

void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t * dma_handle, int gfp)
{
	void *ret;
	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || (*dev->dma_mask < 0xffffffff))
		gfp |= GFP_DMA;
	ret = (void *) __get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
#if 0	/* Broken support for some platforms ...  */
		if (hwdev)
			bus = hwdev->bus;
		*dma_handle = bus_to_baddr(bus, __pa(ret));
#else
		*dma_handle = virt_to_phys(ret);
#endif
#ifdef CONFIG_NONCOHERENT_IO
		dma_cache_wback_inv((unsigned long) ret, size);
		ret = UNCAC_ADDR(ret);
#endif
	}

	return ret;
}

void dma_free_coherent(struct device *dev, size_t size,
                       void *vaddr, dma_addr_t dma_handle)
{
	unsigned long addr = (unsigned long) vaddr;

#ifdef CONFIG_NONCOHERENT_IO
	addr = CAC_ADDR(addr);
#endif
	free_pages(addr, get_order(size));
}

EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);
