#ifndef _ASM_X86_SWIOTLB_H

#include "../../asm/swiotlb.h"

dma_addr_t swiotlb_map_single_phys(struct device *, phys_addr_t, size_t size,
				   int dir);

#endif /* _ASM_X86_SWIOTLB_H */
