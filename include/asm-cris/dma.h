/* $Id: dma.h,v 1.1 2000/07/10 16:32:31 bjornw Exp $
 * linux/include/asm/dma.h: Defines for using and allocating dma channels.
 */

#ifndef _ASM_DMA_H
#define _ASM_DMA_H

/* it's useless on the Etrax, but unfortunately needed by the new
   bootmem allocator (but this should do it for this) */

#define MAX_DMA_ADDRESS PAGE_OFFSET

/* TODO: check nbr of channels on Etrax-100LX */

#define MAX_DMA_CHANNELS	10

/* These are in kernel/dma.c: */
extern int request_dma(unsigned int dmanr, const char * device_id); /* reserve a DMA channel */
extern void free_dma(unsigned int dmanr);	                    /* release it */

#endif /* _ASM_DMA_H */
