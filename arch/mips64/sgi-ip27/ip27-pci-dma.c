/* $Id: ip27-pci-dma.c,v 1.1 2000/02/16 21:22:00 ralf Exp $
 *
 * Dynamic DMA mapping support.
 *
 * On the Origin there is dynamic DMA address translation for all PCI DMA.
 * However we don't use this facility yet but rely on the 2gb direct
 * mapped DMA window for PCI64.  So consistent alloc/free are merely page
 * allocation/freeing.  The rest of the dynamic DMA mapping interface is
 * implemented in <asm/pci.h>.  So this code will fail with more than
 * 2gb of memory.
 */
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <asm/io.h>

/* Pure 2^n version of get_order */
extern __inline__ int __get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC;
	int order = __get_order(size);

	if (hwdev == NULL || hwdev->dma_mask != 0xffffffff)
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, order);

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = (bus_to_baddr[hwdev->bus->number] | __pa(ret));
	}

	return ret;
}

void pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, __get_order(size));
}
