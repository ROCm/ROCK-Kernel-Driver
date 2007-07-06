#ifndef _ASM_GENERIC_DMA_MAPPING_H
#define _ASM_GENERIC_DMA_MAPPING_H

/* This is used for archs that do not support DMA */

static inline void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   gfp_t flag)
{
	BUG();
	return NULL;
}

static inline void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		    dma_addr_t dma_handle)
{
	BUG();
}

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	BUG();

	return 0;
}

static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	     enum dma_data_direction direction)
{
	BUG();
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

#endif /* _ASM_GENERIC_DMA_MAPPING_H */
