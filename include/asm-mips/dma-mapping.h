#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <asm/cache.h>

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

void *dma_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, int flag);

void dma_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle);

#ifdef CONFIG_MAPPED_DMA_IO

extern dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
	enum dma_data_direction direction);
extern void dma_unmap_single(struct device *dev, dma_addr_t dma_addr,
	size_t size, enum dma_data_direction direction);
extern int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	enum dma_data_direction direction);
extern dma_addr_t dma_map_page(struct device *dev, struct page *page,
	unsigned long offset, size_t size, enum dma_data_direction direction);
extern void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
	size_t size, enum dma_data_direction direction);
extern void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
	int nhwentries, enum dma_data_direction direction);
extern void dma_sync_single(struct device *dev, dma_addr_t dma_handle,
	size_t size, enum dma_data_direction direction);
extern void dma_sync_single_range(struct device *dev, dma_addr_t dma_handle,
	unsigned long offset, size_t size, enum dma_data_direction direction);
extern void dma_sync_sg(struct device *dev, struct scatterlist *sg, int nelems,
	enum dma_data_direction direction);

#else

static inline dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
	       enum dma_data_direction direction)
{
	unsigned long addr = (unsigned long) ptr;

	BUG_ON(direction == DMA_NONE);

	dma_cache_wback_inv(addr, size);

	return bus_to_baddr(hwdev->bus, __pa(ptr));
}

static inline void
dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	if (direction != DMA_TO_DEVICE) {
		unsigned long addr;

		addr = baddr_to_bus(hwdev->bus, dma_addr) + PAGE_OFFSET;
		dma_cache_wback_inv(addr, size);
	}
}

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nents; i++, sg++) {
		unsigned long addr;
 
		addr = (unsigned long) page_address(sg->page);
		if (addr)
		        dma_cache_wback_inv(addr + sg->offset, sg->length);
		sg->dma_address = (dma_addr_t) bus_to_baddr(hwdev->bus,
			page_to_phys(sg->page) + sg->offset);
	}

	return nents;
}

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page, unsigned long offset,
	     size_t size, enum dma_data_direction direction)
{
	unsigned long addr;

	BUG_ON(direction == DMA_NONE);
	addr = (unsigned long) page_address(page) + offset;
	dma_cache_wback_inv(addr, size);

	return bus_to_baddr(hwdev->bus, page_to_phys(page) + offset);
}

static inline void
dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	if (direction != DMA_TO_DEVICE) {
		unsigned long addr;

		addr = baddr_to_bus(hwdev->bus, dma_address) + PAGE_OFFSET;
		dma_cache_wback_inv(addr, size);
	}
}

static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	     enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	if (direction == DMA_TO_DEVICE)
		return;

	for (i = 0; i < nhwentries; i++, sg++) {
		unsigned long addr;

		if (!sg->page)
			BUG();

		addr = (unsigned long) page_address(sg->page);
		if (addr)
			dma_cache_wback_inv(addr + sg->offset, sg->length);
	}
}

static inline void
dma_sync_single(struct device *dev, dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	unsigned long addr;
 
	if (direction == DMA_NONE)
		BUG();
 
	addr = baddr_to_bus(hwdev->bus, dma_handle) + PAGE_OFFSET;
	dma_cache_wback_inv(addr, size);
}

static inline void
dma_sync_single_range(struct device *dev, dma_addr_t dma_handle,
		      unsigned long offset, size_t size,
		      enum dma_data_direction direction)
{
	unsigned long addr;

	if (direction == DMA_NONE)
		BUG();

	addr = baddr_to_bus(hwdev->bus, dma_handle) + PAGE_OFFSET;
	dma_cache_wback_inv(addr, size);
}

static inline void
dma_sync_sg(struct device *dev, struct scatterlist *sg, int nelems,
		 enum dma_data_direction direction)
{
#ifdef CONFIG_NONCOHERENT_IO
	int i;
#endif
 
	if (direction == DMA_NONE)
		BUG();
 
	/* Make sure that gcc doesn't leave the empty loop body.  */
#ifdef CONFIG_NONCOHERENT_IO
	for (i = 0; i < nelems; i++, sg++)
		dma_cache_wback_inv((unsigned long)page_address(sg->page),
		                    sg->length);
#endif
}
#endif /* CONFIG_MAPPED_DMA_IO  */

static inline int
dma_supported(struct device *dev, u64 mask)
{
	/*
	 * we fall back to GFP_DMA when the mask isn't all 1s,
	 * so we can't guarantee allocations that must be
	 * within a tighter range than GFP_DMA..
	 */
	if (mask < 0x00ffffff)
		return 0;

	return 1;
}

static inline int
dma_set_mask(struct device *dev, u64 mask)
{
	if(!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}

static inline int
dma_get_cache_alignment(void)
{
	/* XXX Largest on any MIPS */
	return 128;
}

#ifdef CONFIG_NONCOHERENT_IO
#define dma_is_consistent(d)	(0)
#else
#define dma_is_consistent(d)	(1)
#endif

static inline void
dma_cache_sync(void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
	dma_cache_wback_inv((unsigned long)vaddr, size);
}

#endif /* _ASM_DMA_MAPPING_H */
