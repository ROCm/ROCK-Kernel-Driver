#ifndef _ALPHA_SCATTERLIST_H
#define _ALPHA_SCATTERLIST_H

#include <linux/types.h>

struct scatterlist {
	char *address;			/* Source/target vaddr.  */
	char *alt_address;		/* Location of actual if address is a
					   dma indirect buffer, else NULL.  */
	dma_addr_t dma_address;
	unsigned int length;
	unsigned int dma_length;
};

#define sg_dma_address(sg) ((sg)->dma_address)
#define sg_dma_len(sg)     ((sg)->dma_length)

#define ISA_DMA_THRESHOLD (~0UL)

#endif /* !(_ALPHA_SCATTERLIST_H) */
