#ifndef __ASM_MIPS_SCATTERLIST_H
#define __ASM_MIPS_SCATTERLIST_H

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;
    
    __u32 dvma_address;
};

struct mmu_sglist {
        char *addr;
        char *__dont_touch;
        unsigned int len;
        __u32 dvma_addr;
};

#define ISA_DMA_THRESHOLD (0x00ffffff)

#endif /* __ASM_MIPS_SCATTERLIST_H */
