#ifndef __ASM_MIPS_SCATTERLIST_H
#define __ASM_MIPS_SCATTERLIST_H

struct scatterlist {
    struct page *page;
    unsigned int offset;
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
