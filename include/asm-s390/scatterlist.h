#ifndef _ASMS390_SCATTERLIST_H
#define _ASMS390_SCATTERLIST_H

struct scatterlist {
    struct page *page;
    unsigned int offset;
    unsigned int length;
};

#define ISA_DMA_THRESHOLD (0xffffffffffffffff)

#endif /* _ASMS390X_SCATTERLIST_H */
