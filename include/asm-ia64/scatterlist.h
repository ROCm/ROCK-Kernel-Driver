#ifndef _ASM_IA64_SCATTERLIST_H
#define _ASM_IA64_SCATTERLIST_H

/*
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

struct scatterlist {
	char *address;		/* location data is to be transferred to */
	/*
	 * Location of actual buffer if ADDRESS points to a DMA
	 * indirection buffer, NULL otherwise:
	 */
	char *alt_address;
	char *orig_address;	/* Save away the original buffer address (used by pci-dma.c) */
	unsigned int length;	/* buffer length */
};

#define ISA_DMA_THRESHOLD	(~0UL)

#endif /* _ASM_IA64_SCATTERLIST_H */
