#ifndef _ASM_DMA_MAPPING_H_

#include "../../asm/dma-mapping.h"

void dma_generic_free_coherent(struct device *, size_t, void *, dma_addr_t);

#define address_needs_mapping(hwdev, addr, size) \
	!is_buffer_dma_capable(dma_get_mask(hwdev), addr, size)

extern int range_straddles_page_boundary(paddr_t p, size_t size);

#endif /* _ASM_DMA_MAPPING_H_ */
