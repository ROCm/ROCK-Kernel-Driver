/* $Id: scatterlist.h,v 1.6 2001/10/09 02:24:35 davem Exp $ */
#ifndef _SPARC_SCATTERLIST_H
#define _SPARC_SCATTERLIST_H

#include <linux/types.h>

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    unsigned int length;

    __u32 dvma_address; /* A place to hang host-specific addresses at. */
    __u32 dvma_length;
};

#define sg_dma_address(sg) ((sg)->dvma_address)
#define sg_dma_len(sg)     ((sg)->dvma_length)

#define ISA_DMA_THRESHOLD (~0UL)

#endif /* !(_SPARC_SCATTERLIST_H) */
