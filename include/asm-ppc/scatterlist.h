#ifdef __KERNEL__
#ifndef _PPC_SCATTERLIST_H
#define _PPC_SCATTERLIST_H

#include <asm/dma.h>

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    char * alt_address; /* Location of actual if address is a 
			 * dma indirect buffer.  NULL otherwise */
    unsigned int length;
};


#endif /* !(_PPC_SCATTERLIST_H) */
#endif /* __KERNEL__ */
