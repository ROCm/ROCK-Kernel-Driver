/* $Id: iommu_common.c,v 1.4 2000/06/04 21:50:23 anton Exp $
 * iommu_common.c: UltraSparc SBUS/PCI common iommu code.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include "iommu_common.h"

/* You are _strongly_ advised to enable the following debugging code
 * any time you make changes to the sg code below, run it for a while
 * with filesystems mounted read-only before buying the farm... -DaveM
 */

#ifdef VERIFY_SG
int verify_lengths(struct scatterlist *sg, int nents, int npages)
{
	int sg_len, dma_len;
	int i, pgcount;

	sg_len = 0;
	for (i = 0; i < nents; i++)
		sg_len += sg[i].length;

	dma_len = 0;
	for (i = 0; i < nents && sg[i].dvma_length; i++)
		dma_len += sg[i].dvma_length;

	if (sg_len != dma_len) {
		printk("verify_lengths: Error, different, sg[%d] dma[%d]\n",
		       sg_len, dma_len);
		return -1;
	}

	pgcount = 0;
	for (i = 0; i < nents && sg[i].dvma_length; i++) {
		unsigned long start, end;

		start = sg[i].dvma_address;
		start = start & PAGE_MASK;

		end = sg[i].dvma_address + sg[i].dvma_length;
		end = (end + (PAGE_SIZE - 1)) & PAGE_MASK;

		pgcount += ((end - start) >> PAGE_SHIFT);
	}

	if (pgcount != npages) {
		printk("verify_lengths: Error, page count wrong, "
		       "npages[%d] pgcount[%d]\n",
		       npages, pgcount);
		return -1;
	}

	/* This test passes... */
	return 0;
}

int verify_one_map(struct scatterlist *dma_sg, struct scatterlist **__sg, int nents, iopte_t **__iopte)
{
	struct scatterlist *sg = *__sg;
	iopte_t *iopte = *__iopte;
	u32 dlen = dma_sg->dvma_length;
	u32 daddr = dma_sg->dvma_address;
	unsigned int sglen;
	unsigned long sgaddr;

	sglen = sg->length;
	sgaddr = (unsigned long) sg->address;
	while (dlen > 0) {
		unsigned long paddr;

		/* SG and DMA_SG must begin at the same sub-page boundary. */
		if ((sgaddr & ~PAGE_MASK) != (daddr & ~PAGE_MASK)) {
			printk("verify_one_map: Wrong start offset "
			       "sg[%08lx] dma[%08x]\n",
			       sgaddr, daddr);
			nents = -1;
			goto out;
		}

		/* Verify the IOPTE points to the right page. */
		paddr = iopte_val(*iopte) & IOPTE_PAGE;
		if ((paddr + PAGE_OFFSET) != (sgaddr & PAGE_MASK)) {
			printk("verify_one_map: IOPTE[%08lx] maps the "
			       "wrong page, should be [%08lx]\n",
			       iopte_val(*iopte), (sgaddr & PAGE_MASK) - PAGE_OFFSET);
			nents = -1;
			goto out;
		}

		/* If this SG crosses a page, adjust to that next page
		 * boundary and loop.
		 */
		if ((sgaddr & PAGE_MASK) ^ ((sgaddr + sglen - 1) & PAGE_MASK)) {
			unsigned long next_page, diff;

			next_page = (sgaddr + PAGE_SIZE) & PAGE_MASK;
			diff = next_page - sgaddr;
			sgaddr += diff;
			daddr += diff;
			sglen -= diff;
			dlen -= diff;
			if (dlen > 0)
				iopte++;
			continue;
		}

		/* SG wholly consumed within this page. */
		daddr += sglen;
		dlen -= sglen;

		if (dlen > 0 && ((daddr & ~PAGE_MASK) == 0))
			iopte++;

		sg++;
		if (--nents <= 0)
			break;
		sgaddr = (unsigned long) sg->address;
		sglen = sg->length;
	}
	if (dlen < 0) {
		/* Transfer overrun, big problems. */
		printk("verify_one_map: Transfer overrun by %d bytes.\n",
		       -dlen);
		nents = -1;
	} else {
		/* Advance to next dma_sg implies that the next iopte will
		 * begin it.
		 */
		iopte++;
	}

out:
	*__sg = sg;
	*__iopte = iopte;
	return nents;
}

int verify_maps(struct scatterlist *sg, int nents, iopte_t *iopte)
{
	struct scatterlist *dma_sg = sg;
	struct scatterlist *orig_dma_sg = dma_sg;
	int orig_nents = nents;

	for (;;) {
		nents = verify_one_map(dma_sg, &sg, nents, &iopte);
		if (nents <= 0)
			break;
		dma_sg++;
		if (dma_sg->dvma_length == 0)
			break;
	}

	if (nents > 0) {
		printk("verify_maps: dma maps consumed by some sgs remain (%d)\n",
		       nents);
		return -1;
	}

	if (nents < 0) {
		printk("verify_maps: Error, messed up mappings, "
		       "at sg %d dma_sg %d\n",
		       (int) (orig_nents + nents), (int) (dma_sg - orig_dma_sg));
		return -1;
	}

	/* This test passes... */
	return 0;
}

void verify_sglist(struct scatterlist *sg, int nents, iopte_t *iopte, int npages)
{
	if (verify_lengths(sg, nents, npages) < 0 ||
	    verify_maps(sg, nents, iopte) < 0) {
		int i;

		printk("verify_sglist: Crap, messed up mappings, dumping, iodma at %08x.\n",
		       (u32) (sg->dvma_address & PAGE_MASK));
		for (i = 0; i < nents; i++) {
			printk("sg(%d): address(%p) length(%x) "
			       "dma_address[%08x] dma_length[%08x]\n",
			       i,
			       sg[i].address, sg[i].length,
			       sg[i].dvma_address, sg[i].dvma_length);
		}
	}

	/* Seems to be ok */
}
#endif

/* Two addresses are "virtually contiguous" if and only if:
 * 1) They are equal, or...
 * 2) They are both on a page boundry
 */
#define VCONTIG(__X, __Y)	(((__X) == (__Y)) || \
				 (((__X) | (__Y)) << (64UL - PAGE_SHIFT)) == 0UL)

unsigned long prepare_sg(struct scatterlist *sg, int nents)
{
	struct scatterlist *dma_sg = sg;
	unsigned long prev;
	u32 dent_addr, dent_len;

	prev  = (unsigned long) sg->address;
	prev += (unsigned long) (dent_len = sg->length);
	dent_addr = (u32) ((unsigned long)sg->address & (PAGE_SIZE - 1UL));
	while (--nents) {
		unsigned long addr;

		sg++;
		addr = (unsigned long) sg->address;
		if (! VCONTIG(prev, addr)) {
			dma_sg->dvma_address = dent_addr;
			dma_sg->dvma_length = dent_len;
			dma_sg++;

			dent_addr = ((dent_addr +
				      dent_len +
				      (PAGE_SIZE - 1UL)) >> PAGE_SHIFT);
			dent_addr <<= PAGE_SHIFT;
			dent_addr += addr & (PAGE_SIZE - 1UL);
			dent_len = 0;
		}
		dent_len += sg->length;
		prev = addr + sg->length;
	}
	dma_sg->dvma_address = dent_addr;
	dma_sg->dvma_length = dent_len;

	return ((unsigned long) dent_addr +
		(unsigned long) dent_len +
		(PAGE_SIZE - 1UL)) >> PAGE_SHIFT;
}
