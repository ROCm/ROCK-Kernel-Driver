/*
 *  include/asm-s390/dma-mapping.h
 *
 *  S390 version
 *
 *  This file exists so that #include <dma-mapping.h> doesn't break anything.
 */

#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, int flag)
{
	BUG();
	return 0;
}

static inline void dma_free_coherent(struct device *dev, size_t size,
		       void *vaddr, dma_addr_t dma_handle)
{
	BUG();
}

#endif /* _ASM_DMA_MAPPING_H */
