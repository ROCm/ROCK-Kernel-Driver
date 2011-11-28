#ifndef _ASM_X86_DMA_MAPPING_H_

#define phys_to_dma _phys_to_dma_
#define dma_to_phys _dma_to_phys_

#include_next <asm/dma-mapping.h>

#undef phys_to_dma
#undef dma_to_phys

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return phys_to_machine(paddr);
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return machine_to_phys(daddr);
}

void dma_generic_free_coherent(struct device *, size_t, void *, dma_addr_t);

extern int range_straddles_page_boundary(paddr_t p, size_t size);

#endif /* _ASM_X86_DMA_MAPPING_H_ */
