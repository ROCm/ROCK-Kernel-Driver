#ifndef _M68K_SCATTERLIST_H
#define _M68K_SCATTERLIST_H

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    unsigned int length;
    unsigned long dvma_address;
};

struct mmu_sglist {
        char *addr;
        char *__dont_touch;
        unsigned int len;
        unsigned long dvma_addr;
};

/* This is bogus and should go away. */
#define ISA_DMA_THRESHOLD (0x00ffffff)

#endif /* !(_M68K_SCATTERLIST_H) */
