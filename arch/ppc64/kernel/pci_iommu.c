/*
 * arch/ppc64/kernel/pci_iommu.c
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * 
 * Rewrite, cleanup, new allocation schemes: 
 * Copyright (C) 2004 Olof Johansson, IBM Corporation
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
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include "pci.h"

#ifdef CONFIG_PPC_ISERIES
#include <asm/iSeries/iSeries_pci.h>
#endif /* CONFIG_PPC_ISERIES */

#define DBG(...)

static inline struct iommu_table *devnode_table(struct pci_dev *dev)
{
	if (!dev)
		dev = ppc64_isabridge_dev;
	if (!dev)
		return NULL;

#ifdef CONFIG_PPC_ISERIES
	return ISERIES_DEVNODE(dev)->iommu_table;
#endif /* CONFIG_PPC_ISERIES */

#ifdef CONFIG_PPC_PSERIES
	return PCI_GET_DN(dev)->iommu_table;
#endif /* CONFIG_PPC_PSERIES */
}


/* Allocates a contiguous real buffer and creates mappings over it.
 * Returns the virtual address of the buffer and sets dma_handle
 * to the dma address (mapping) of the first page.
 */
void *pci_iommu_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t *dma_handle)
{
	struct iommu_table *tbl;
	void *ret = NULL;
	dma_addr_t mapping;
	unsigned int npages, order;

	size = PAGE_ALIGN(size);
	npages = size >> PAGE_SHIFT;
	order = get_order(size);

 	/* Client asked for way too much space.  This is checked later anyway */
	/* It is easier to debug here for the drivers than in the tce tables.*/
	if (order >= IOMAP_MAX_ORDER) {
		printk("PCI_DMA: pci_alloc_consistent size too large: 0x%lx\n",
			size);
		return NULL;
	}

	tbl = devnode_table(hwdev); 

	if (!tbl)
		return NULL;

	/* Alloc enough pages (and possibly more) */
	ret = (void *)__get_free_pages(GFP_ATOMIC, order);

	if (!ret)
		return NULL;

	memset(ret, 0, size);

	/* Set up tces to cover the allocated range */
	mapping = iommu_alloc(tbl, ret, npages, PCI_DMA_BIDIRECTIONAL);

	if (mapping == DMA_ERROR_CODE) {
		free_pages((unsigned long)ret, order);
		ret = NULL;
	} else
		*dma_handle = mapping;

	return ret;
}


void pci_iommu_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	struct iommu_table *tbl;
	unsigned int npages;
	
	size = PAGE_ALIGN(size);
	npages = size >> PAGE_SHIFT;

	tbl = devnode_table(hwdev); 

	if (tbl) {
		iommu_free(tbl, dma_handle, npages);
		free_pages((unsigned long)vaddr, get_order(size));
	}
}


/* Creates TCEs for a user provided buffer.  The user buffer must be 
 * contiguous real kernel storage (not vmalloc).  The address of the buffer
 * passed here is the kernel (virtual) address of the buffer.  The buffer
 * need not be page aligned, the dma_addr_t returned will point to the same
 * byte within the page as vaddr.
 */
dma_addr_t pci_iommu_map_single(struct pci_dev *hwdev, void *vaddr,
				size_t size, int direction)
{
	struct iommu_table * tbl;
	dma_addr_t dma_handle = DMA_ERROR_CODE;
	unsigned long uaddr;
	unsigned int npages;

	BUG_ON(direction == PCI_DMA_NONE);

	uaddr = (unsigned long)vaddr;
	npages = PAGE_ALIGN(uaddr + size) - (uaddr & PAGE_MASK);
	npages >>= PAGE_SHIFT;

	tbl = devnode_table(hwdev); 

	if (tbl) {
		dma_handle = iommu_alloc(tbl, vaddr, npages, direction);
		if (dma_handle == DMA_ERROR_CODE) {
			if (printk_ratelimit())  {
				printk(KERN_INFO "iommu_alloc failed, tbl %p vaddr %p npages %d\n",
				       tbl, vaddr, npages);
			}
		} else 
			dma_handle |= (uaddr & ~PAGE_MASK);
	}

	return dma_handle;
}


void pci_iommu_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_handle,
		      size_t size, int direction)
{
	struct iommu_table *tbl;
	unsigned int npages;
	
	BUG_ON(direction == PCI_DMA_NONE);

	npages = (PAGE_ALIGN(dma_handle + size) - (dma_handle & PAGE_MASK))
		>> PAGE_SHIFT;

	tbl = devnode_table(hwdev); 

	if (tbl) 
		iommu_free(tbl, dma_handle, npages);
}


int pci_iommu_map_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems,
	       int direction)
{
	struct iommu_table * tbl;

	BUG_ON(direction == PCI_DMA_NONE);

	if (nelems == 0)
		return 0;

	tbl = devnode_table(pdev); 
	if (!tbl)
		return 0;

	return iommu_alloc_sg(tbl, &pdev->dev, sglist, nelems, direction);
}

void pci_iommu_unmap_sg(struct pci_dev *pdev, struct scatterlist *sglist, int nelems,
		  int direction)
{
	struct iommu_table *tbl;

	BUG_ON(direction == PCI_DMA_NONE);

	tbl = devnode_table(pdev); 
	if (!tbl)
		return;

	iommu_free_sg(tbl, sglist, nelems);
}

/* We support DMA to/from any memory page via the iommu */
static int pci_iommu_dma_supported(struct pci_dev *pdev, u64 mask)
{
	return 1;
}

void pci_iommu_init(void)
{
	pci_dma_ops.pci_alloc_consistent = pci_iommu_alloc_consistent;
	pci_dma_ops.pci_free_consistent = pci_iommu_free_consistent;
	pci_dma_ops.pci_map_single = pci_iommu_map_single;
	pci_dma_ops.pci_unmap_single = pci_iommu_unmap_single;
	pci_dma_ops.pci_map_sg = pci_iommu_map_sg;
	pci_dma_ops.pci_unmap_sg = pci_iommu_unmap_sg;
	pci_dma_ops.pci_dma_supported = pci_iommu_dma_supported;
}
