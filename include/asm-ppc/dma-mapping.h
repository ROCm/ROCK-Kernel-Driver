/*
 * This is based on both include/asm-sh/dma-mapping.h and
 * include/asm-ppc/pci.h
 */
#ifndef __ASM_PPC_DMA_MAPPING_H
#define __ASM_PPC_DMA_MAPPING_H

#include <linux/config.h>
/* we implement the API below in terms of the existing PCI one,
 * so include it */
#include <linux/pci.h>
/* need struct page definitions */
#include <linux/mm.h>
#include <linux/device.h>
#include <asm/scatterlist.h>
#include <asm/io.h>

#define dma_supported(dev, mask)	(1)

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t * dma_handle, int flag)
{
#ifdef CONFIG_PCI
	if (dev && dev->bus == &pci_bus_type)
		return pci_alloc_consistent(to_pci_dev(dev), size, dma_handle);
#endif

	return consistent_alloc(flag, size, dma_handle);
}

static inline void
dma_free_coherent(struct device *dev, size_t size, void *vaddr,
		  dma_addr_t dma_handle)
{
#ifdef CONFIG_PCI
	if (dev && dev->bus == &pci_bus_type) {
		pci_free_consistent(to_pci_dev(dev), size, vaddr, dma_handle);
		return;
	}
#endif

	consistent_free(vaddr);
}

static inline dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	consistent_sync(ptr, size, direction);

	return virt_to_bus(ptr);
}

/* We do nothing. */
#define dma_unmap_single(dev, addr, size, dir)	do { } while (0)

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size,
	     enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	consistent_sync_page(page, offset, size, direction);
	return (page - mem_map) * PAGE_SIZE + PCI_DRAM_OFFSET + offset;
}

/* We do nothing. */
#define dma_unmap_page(dev, addr, size, dir)	do { } while (0)

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nents; i++, sg++) {
		BUG_ON(!sg->page);
		consistent_sync_page(sg->page, sg->offset,
				     sg->length, direction);
		sg->dma_address = page_to_bus(sg->page) + sg->offset;
	}

	return nents;
}

/* We don't do anything here. */
#define dma_unmap_sg(dev, sg, nents, dir)	do { } while (0)

static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
			size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	consistent_sync(bus_to_virt(dma_handle), size, direction);
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
			   size_t size,
			   enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	consistent_sync(bus_to_virt(dma_handle), size, direction);
}

static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
		    int nelems, enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nelems; i++, sg++)
		consistent_sync_page(sg->page, sg->offset,
				     sg->length, direction);
}

static inline void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
		       int nelems, enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nelems; i++, sg++)
		consistent_sync_page(sg->page, sg->offset,
				     sg->length, direction);
}

/* Now for the API extensions over the pci_ one */

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_is_consistent(d)	(1)

static inline int dma_get_cache_alignment(void)
{
	/*
	 * Each processor family will define its own L1_CACHE_SHIFT,
	 * L1_CACHE_BYTES wraps to this, so this is always safe.
	 */
	return L1_CACHE_BYTES;
}

static inline void
dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
			      unsigned long offset, size_t size,
			      enum dma_data_direction direction)
{
	/* just sync everything, that's all the pci API can do */
	dma_sync_single_for_cpu(dev, dma_handle, offset + size, direction);
}

static inline void
dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size,
				 enum dma_data_direction direction)
{
	/* just sync everything, that's all the pci API can do */
	dma_sync_single_for_device(dev, dma_handle, offset + size, direction);
}

static inline void dma_cache_sync(void *vaddr, size_t size,
				  enum dma_data_direction direction)
{
	consistent_sync(vaddr, size, (int)direction);
}

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	return 0;
}

#endif				/* __ASM_PPC_DMA_MAPPING_H */
