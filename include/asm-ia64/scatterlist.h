#ifndef _ASM_IA64_SCATTERLIST_H
#define _ASM_IA64_SCATTERLIST_H

/*
 * Copyright (C) 1998-1999, 2001-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
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
