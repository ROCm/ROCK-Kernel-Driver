#ifndef _ASMS390X_SCATTERLIST_H
#define _ASMS390X_SCATTERLIST_H

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;
};

#define ISA_DMA_THRESHOLD (0xffffffffffffffff)

#endif /* _ASMS390X_SCATTERLIST_H */
