#ifndef __ASM_SCATTERLIST_H
#define __ASM_SCATTERLIST_H

struct scatterlist {
	struct page *	page;
	unsigned int	offset;
	dma_addr_t	dma_address;
	unsigned int	length;
};

#define ISA_DMA_THRESHOLD (0x00ffffffUL)

#endif /* __ASM_SCATTERLIST_H */
