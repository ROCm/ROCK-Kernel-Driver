/* $Id: iommu_common.h,v 1.1 1999/12/17 12:31:54 jj Exp $
 * iommu_common.h: UltraSparc SBUS/PCI common iommu declarations.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/page.h>
#include <asm/iommu.h>
#include <asm/scatterlist.h>

/* You are _strongly_ advised to enable the following debugging code
 * any time you make changes to the sg code below, run it for a while
 * with filesystems mounted read-only before buying the farm... -DaveM
 */
#undef VERIFY_SG

#ifdef VERIFY_SG
int verify_lengths(struct scatterlist *sg, int nents, int npages);
int verify_one_map(struct scatterlist *dma_sg, struct scatterlist **__sg, int nents, iopte_t **__iopte);
int verify_maps(struct scatterlist *sg, int nents, iopte_t *iopte);
void verify_sglist(struct scatterlist *sg, int nents, iopte_t *iopte, int npages);
#endif

/* Two addresses are "virtually contiguous" if and only if:
 * 1) They are equal, or...
 * 2) They are both on a page boundry
 */
#define VCONTIG(__X, __Y)	(((__X) == (__Y)) || \
				 (((__X) | (__Y)) << (64UL - PAGE_SHIFT)) == 0UL)

unsigned long prepare_sg(struct scatterlist *sg, int nents);
