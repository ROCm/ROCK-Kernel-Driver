/*
 * arch/ppc64/kernel/pci_iommu.c
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * 
 * Rewrite, cleanup, new allocation schemes, virtual merging: 
 * Copyright (C) 2004 Olof Johansson, IBM Corporation
 *               and  Ben. Herrenschmidt, IBM Corporation
 *
 * Dynamic DMA mapping support, platform-independent parts.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/bitops.h>

#define DBG(...)

#ifdef CONFIG_IOMMU_VMERGE
static int novmerge = 0;
#else
static int novmerge = 1;
#endif

static int __init setup_iommu(char *str)
{
	if (!strcmp(str, "novmerge"))
		novmerge = 1;
	else if (!strcmp(str, "vmerge"))
		novmerge = 0;
	return 1;
}

__setup("iommu=", setup_iommu);

static unsigned long iommu_range_alloc(struct iommu_table *tbl, unsigned long npages,
			      unsigned long *handle)
{ 
	unsigned long n, end, i, start;
	unsigned long hint;
	unsigned long limit;
	int largealloc = npages > 15;

	if (handle && *handle)
		hint = *handle;
	else
		hint = largealloc ? tbl->it_largehint : tbl->it_hint;

	/* Most of this is stolen from x86_64's bit string search function */

	start = hint;

	/* Use only half of the table for small allocs (less than 15 pages). */

	limit = largealloc ? tbl->it_mapsize : tbl->it_mapsize >> 1; 

	if (largealloc && start < (tbl->it_mapsize >> 1))
		start = tbl->it_mapsize >> 1;
	
 again:

	n = find_next_zero_bit(tbl->it_map, limit, start);

	end = n + npages;
	if (end >= limit) {
		if (hint) {
			start = largealloc ? tbl->it_mapsize >> 1 : 0;
			hint = 0;
			goto again;
		} else
			return NO_TCE;
	}

	for (i = n; i < end; i++)
		if (test_bit(i, tbl->it_map)) {
			start = i+1;
			goto again;
		}

	for (i = n; i < end; i++)
		__set_bit(i, tbl->it_map);

	/* Bump the hint to a new PHB cache line, which
	 * is 16 entries wide on all pSeries machines.
	 */
	if (largealloc)
		tbl->it_largehint = (end+tbl->it_blocksize-1) &
					~(tbl->it_blocksize-1);
	else 
		tbl->it_hint = (end+tbl->it_blocksize-1) &
				~(tbl->it_blocksize-1);

	if (handle)
		*handle = end;

	return n;
}

dma_addr_t iommu_alloc(struct iommu_table *tbl, void *page,
		       unsigned int npages, int direction, 
		       unsigned long *handle)
{
	unsigned long entry, flags;
	dma_addr_t retTce = NO_TCE;
	
	spin_lock_irqsave(&(tbl->it_lock), flags);

	/* Allocate a range of entries into the table */
	entry = iommu_range_alloc(tbl, npages, handle);
	if (unlikely(entry == NO_TCE)) {
		spin_unlock_irqrestore(&(tbl->it_lock), flags);
		return NO_TCE;
	}
	
	/* We got the tces we wanted */
	entry += tbl->it_offset;	/* Offset into real TCE table */
	retTce = entry << PAGE_SHIFT;	/* Set the return dma address */

	/* Put the TCEs in the HW table */
	ppc_md.tce_build(tbl, entry, npages, (unsigned long)page & PAGE_MASK, direction);

	/* Flush/invalidate TLBs if necessary */
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);

	return retTce;
}

static void __iommu_free(struct iommu_table *tbl, dma_addr_t dma_addr, 
			 unsigned int npages)
{
	unsigned long entry, free_entry;
	unsigned long i;

	entry = dma_addr >> PAGE_SHIFT;
	free_entry = entry - tbl->it_offset;

	if (((free_entry + npages) > tbl->it_mapsize) ||
	    (entry < tbl->it_offset)) {
		if (printk_ratelimit()) {
			printk(KERN_INFO "iommu_free: invalid entry\n");
			printk(KERN_INFO "\tentry     = 0x%lx\n", entry); 
			printk(KERN_INFO "\tdma_ddr   = 0x%lx\n", (u64)dma_addr); 
			printk(KERN_INFO "\tTable     = 0x%lx\n", (u64)tbl);
			printk(KERN_INFO "\tbus#      = 0x%lx\n", (u64)tbl->it_busno);
			printk(KERN_INFO "\tmapsize   = 0x%lx\n", (u64)tbl->it_mapsize);
			printk(KERN_INFO "\tstartOff  = 0x%lx\n", (u64)tbl->it_offset);
			printk(KERN_INFO "\tindex     = 0x%lx\n", (u64)tbl->it_index);
			WARN_ON(1);
		}
		return;
	}

	ppc_md.tce_free(tbl, entry, npages);
	
	for (i = 0; i < npages; i++)
		__clear_bit(free_entry+i, tbl->it_map);
}

void iommu_free(struct iommu_table *tbl, dma_addr_t dma_addr, 
		unsigned int npages)
{
	unsigned long flags;

	spin_lock_irqsave(&(tbl->it_lock), flags);

	__iommu_free(tbl, dma_addr, npages);

	/* Flush/invalidate TLBs if necessary */
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);
}

/* 
 * Build a iommu_table structure.  This contains a bit map which
 * is used to manage allocation of the tce space.
 */
struct iommu_table *iommu_init_table(struct iommu_table *tbl)
{
	unsigned long sz;
	static int welcomed = 0;

	/* it_size is in pages, it_mapsize in number of entries */
	tbl->it_mapsize = tbl->it_size * tbl->it_entrysize;

	if (systemcfg->platform == PLATFORM_POWERMAC)
		tbl->it_mapsize = tbl->it_size * (PAGE_SIZE / sizeof(unsigned int));
	else
		tbl->it_mapsize = tbl->it_size * (PAGE_SIZE / sizeof(union tce_entry));

	/* sz is the number of bytes needed for the bitmap */
	sz = (tbl->it_mapsize + 7) >> 3;

	tbl->it_map = (unsigned long *)__get_free_pages(GFP_ATOMIC, get_order(sz));
	
	if (!tbl->it_map)
		panic("iommu_init_table: Can't allocate memory, size %ld bytes\n", sz);

	memset(tbl->it_map, 0, sz);

	tbl->it_hint = 0;
	tbl->it_largehint = 0;
	spin_lock_init(&tbl->it_lock);

	if (!welcomed) {
		printk(KERN_INFO "IOMMU table initialized, virtual merging %s\n",
		       novmerge ? "disabled" : "enabled");
		welcomed = 1;
	}

	return tbl;
}


int iommu_alloc_sg(struct iommu_table *tbl, struct scatterlist *sglist, int nelems,
		   int direction, unsigned long *handle)
{
	dma_addr_t dma_next, dma_addr;
	unsigned long flags, vaddr, npages, entry;
	struct scatterlist *s, *outs, *segstart, *ps;
	int outcount;

	/* Initialize some stuffs */
	outs = s = segstart = &sglist[0];
	outcount = 1;
	ps = NULL;

	/* Init first segment length for error handling */
	outs->dma_length = 0;

	DBG("mapping %d elements:\n", nelems);

	spin_lock_irqsave(&(tbl->it_lock), flags);

	for (s = outs; nelems; nelems--, s++) {
		/* Allocate iommu entries for that segment */
		vaddr = (unsigned long)page_address(s->page) + s->offset;
		npages = PAGE_ALIGN(vaddr + s->length) - (vaddr & PAGE_MASK);
		npages >>= PAGE_SHIFT;
		entry = iommu_range_alloc(tbl, npages, handle);

		DBG("  - vaddr: %lx, size: %lx\n", vaddr, s->length);

		/* Handle failure */
		if (unlikely(entry == NO_TCE)) {
			if (printk_ratelimit())
				printk(KERN_INFO "iommu_alloc failed, tbl %p vaddr %lx"
				       " npages %lx\n", tbl, vaddr, npages);
			goto failure;
		}

		/* Convert entry to a dma_addr_t */
		entry += tbl->it_offset;
		dma_addr = entry << PAGE_SHIFT;
		dma_addr |= s->offset;

		DBG("  - %lx pages, entry: %lx, dma_addr: %lx\n",
			    npages, entry, dma_addr);

		/* Insert into HW table */
		ppc_md.tce_build(tbl, entry, npages, vaddr & PAGE_MASK, direction);

		/* If we are in an open segment, try merging */
		if (segstart != s) {
			DBG("  - trying merge...\n");
			/* We cannot merge is:
			 * - allocated dma_addr isn't contiguous to previous allocation
			 * - current entry has an offset into the page
			 * - previous entry didn't end on a page boundary
			 */
			if (novmerge || (dma_addr != dma_next) || s->offset ||
			    (ps->offset + ps->length) % PAGE_SIZE) {
				/* Can't merge: create a new segment */
				segstart = s;
				outcount++; outs++;
				DBG("    can't merge, new segment.\n");
			} else {
				outs->dma_length += s->length;
				DBG("    merged, new len: %lx\n", outs->dma_length);
			}
		}

		/* If we are beginning a new segment, fill entries */
		if (segstart == s) {
			DBG("  - filling new segment.\n");
			outs->dma_address = dma_addr;
			outs->dma_length = s->length;
		}

		/* Calculate next page pointer for contiguous check */
		dma_next = (dma_addr & PAGE_MASK) + (npages << PAGE_SHIFT);

		DBG("  - dma next is: %lx\n", dma_next);

		/* Keep a pointer to the previous entry */
		ps = s;
	}

	/* Make sure the update is visible to hardware. */
	mb();

	/* Flush/invalidate TLBs if necessary */
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);

	DBG("mapped %d elements:\n", outcount);

	/* For the sake of iommu_free_sg, we clear out the length in the
	 * next entry of the sglist if we didn't fill the list completely
	 */
	if (outcount < nelems) {
		outs++;
		outs->dma_address = NO_TCE;
		outs->dma_length = 0;
	}
	return outcount;

 failure:
	spin_unlock_irqrestore(&(tbl->it_lock), flags);
	for (s = &sglist[0]; s <= outs; s++) {
		if (s->dma_length != 0) {
			vaddr = s->dma_address & PAGE_MASK;
			npages = (PAGE_ALIGN(s->dma_address + s->dma_length) - vaddr)
				>> PAGE_SHIFT;
			iommu_free(tbl, vaddr, npages);
		}
	}
	return 0;
}


void iommu_free_sg(struct iommu_table *tbl, struct scatterlist *sglist, int nelems,
		   int direction)
{
	unsigned long flags;

	/* Lock the whole operation to try to free as a "chunk" */
	spin_lock_irqsave(&(tbl->it_lock), flags);

	while (nelems--) {
		unsigned int npages;
		dma_addr_t dma_handle = sglist->dma_address;

		if (sglist->dma_length == 0)
			break;
		npages = (PAGE_ALIGN(dma_handle + sglist->dma_length)
			  - (dma_handle & PAGE_MASK)) >> PAGE_SHIFT;
		__iommu_free(tbl, dma_handle, npages);
		sglist++;
	}

	/* Flush/invalidate TLBs if necessary */
	if (ppc_md.tce_flush)
		ppc_md.tce_flush(tbl);

	spin_unlock_irqrestore(&(tbl->it_lock), flags);
}
