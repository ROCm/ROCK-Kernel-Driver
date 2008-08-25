#ifndef _ASM_DMA_MAPPING_H_

#include "../../dma-mapping.h"

static inline int
address_needs_mapping(struct device *hwdev, dma_addr_t addr)
{
	dma_addr_t mask = 0xffffffff;
	/* If the device has a mask, use it, otherwise default to 32 bits */
	if (hwdev && hwdev->dma_mask)
		mask = *hwdev->dma_mask;
	return (addr & ~mask) != 0;
}

extern int range_straddles_page_boundary(paddr_t p, size_t size);

#endif /* _ASM_DMA_MAPPING_H_ */
