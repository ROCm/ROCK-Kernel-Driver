#ifndef _ASM_IA64_SCATTERLIST_H
#define _ASM_IA64_SCATTERLIST_H

/*
 * Modified 1998-1999, 2001-2002
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */

struct scatterlist {
	struct page *page;
	unsigned int offset;
	unsigned int length;	/* buffer length */

	dma_addr_t dma_address;
	unsigned int dma_length;
};

#define ISA_DMA_THRESHOLD	(~0UL)

#endif /* _ASM_IA64_SCATTERLIST_H */
