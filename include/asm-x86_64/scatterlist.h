#ifndef _X8664_SCATTERLIST_H
#define _X8664_SCATTERLIST_H

struct scatterlist {
    struct page		*page;
    unsigned int	offset;
    dma_addr_t		dma_address;
    unsigned int	length;
};

#define ISA_DMA_THRESHOLD (0x00ffffff)

#endif 
