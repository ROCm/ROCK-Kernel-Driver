/*
 * BK Id: SCCS/s.scatterlist.h 1.5 05/17/01 18:14:25 cort
 */
#ifdef __KERNEL__
#ifndef _PPC_SCATTERLIST_H
#define _PPC_SCATTERLIST_H

#include <asm/dma.h>

struct scatterlist {
    char *  address;    /* Location data is to be transferred to */
    unsigned int length;
};


#endif /* !(_PPC_SCATTERLIST_H) */
#endif /* __KERNEL__ */
