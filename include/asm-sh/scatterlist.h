#ifndef __ASM_SH_SCATTERLIST_H
#define __ASM_SH_SCATTERLIST_H

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;
};

#define ISA_DMA_THRESHOLD (0x1fffffff)

#endif /* !(__ASM_SH_SCATTERLIST_H) */
