#ifndef _ASM_IA64_SCATTERLIST_H
#define _ASM_IA64_SCATTERLIST_H

/*
 * Copyright (C) 1998, 1999, 2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

struct scatterlist {
	/* This will disappear in 2.5.x: */
	char *address;		/* location data is to be transferred to, NULL for highmem page */
	char *orig_address;	/* for use by swiotlb */

	/* These two are only valid if ADDRESS member of this struct is NULL.  */
	struct page *page;
	unsigned int offset;

	unsigned int length;	/* buffer length */
};

#define ISA_DMA_THRESHOLD	(~0UL)

#endif /* _ASM_IA64_SCATTERLIST_H */
