/*
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

/*
 * Map a single buffer of the indicated size for DMA in streaming mode.
 * The 32-bit bus address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory
 * until either pci_unmap_single or pci_dma_sync_single is performed.
 */
dma_addr_t pci_map_single(struct pci_dev *hwdev, void *ptr, size_t size,
                          int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();

	return (bus_to_baddr[hwdev->bus->number] | __pa(ptr));
}

/*
 * Unmap a single streaming mode DMA translation.  The dma_addr and size
 * must match what was provided for in a previous pci_map_single call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guarenteed to see
 * whatever the device wrote there.
 */
void pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr,
                     size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();

	/* Nothing to do */
}

/*
 * Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scather-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
int pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents,
               int direction)
{
	int i;

	if (direction == PCI_DMA_NONE)
		BUG();

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for (i = 0; i < nents; i++, sg++) {
		sg->address = (char *)(bus_to_baddr[hwdev->bus->number] | __pa(sg->address));
	}

	return nents;
}

/*
 * Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
void pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents,
                  int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();

	/* Nothing to do */
}

/*
 * Make physical memory consistent for a single
 * streaming mode DMA translation after a transfer.
 *
 * If you perform a pci_map_single() but wish to interrogate the
 * buffer using the cpu, yet do not wish to teardown the PCI dma
 * mapping, you must call this function before doing so.  At the
 * next point you give the PCI dma address back to the card, the
 * device again owns the buffer.
 */
void pci_dma_sync_single(struct pci_dev *hwdev, dma_addr_t dma_handle,
                         size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
}

/*
 * Make physical memory consistent for a set of streaming
 * mode DMA translations after a transfer.
 *
 * The same as pci_dma_sync_single but for a scatter-gather list,
 * same rules and usage.
 */
void pci_dma_sync_sg(struct pci_dev *hwdev, struct scatterlist *sg,
                     int nelems, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
}
