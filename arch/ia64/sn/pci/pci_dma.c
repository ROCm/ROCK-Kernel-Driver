/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000,2002-2004 Silicon Graphics, Inc. All rights reserved.
 *
 * Routines for PCI DMA mapping.  See Documentation/DMA-mapping.txt for
 * a description of how these routines should be used.
 */

#include <linux/module.h>
#include <asm/sn/sn_sal.h>
#include "pci/pcibus_provider_defs.h"
#include "pci/pcidev.h"
#include "pci/pcibr_provider.h"

void sn_pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents,
		     int direction);

/**
 * sn_pci_alloc_consistent - allocate memory for coherent DMA
 * @hwdev: device to allocate for
 * @size: size of the region
 * @dma_handle: DMA (bus) address
 *
 * pci_alloc_consistent() returns a pointer to a memory region suitable for
 * coherent DMA traffic to/from a PCI device.  On SN platforms, this means
 * that @dma_handle will have the %PCIIO_DMA_CMD flag set.
 *
 * This interface is usually used for "command" streams (e.g. the command
 * queue for a SCSI controller).  See Documentation/DMA-mapping.txt for
 * more information.
 *
 * Also known as platform_pci_alloc_consistent() by the IA64 machvec code.
 */
void *sn_pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			      dma_addr_t * dma_handle)
{
	void *cpuaddr;
	unsigned long phys_addr;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(hwdev);
	struct pcibus_bussoft *bussoft = SN_PCIDEV_BUSSOFT(hwdev);

	if (bussoft == NULL) {
		return NULL;
	}

	if (! IS_PCI_BRIDGE_ASIC(bussoft->bs_asic_type)) {
		return NULL;		/* unsupported asic type */
	}

	/*
	 * Allocate the memory.
	 * FIXME: We should be doing alloc_pages_node for the node closest
	 *        to the PCI device.
	 */
	if (!(cpuaddr = (void *)__get_free_pages(GFP_ATOMIC, get_order(size))))
		return NULL;

	memset(cpuaddr, 0x0, size);

	/* physical addr. of the memory we just got */
	phys_addr = __pa(cpuaddr);

	/*
	 * 64 bit address translations should never fail.
	 * 32 bit translations can fail if there are insufficient mapping
	 *   resources.
	 */

	*dma_handle = pcibr_dma_map(pcidev_info, phys_addr, size, SN_PCIDMA_CONSISTENT);
	if (!*dma_handle) {
		printk(KERN_ERR
		       "sn_pci_alloc_consistent():  failed  *dma_handle = 0x%lx hwdev->dev.coherent_dma_mask = 0x%lx \n",
		       *dma_handle, hwdev->dev.coherent_dma_mask);
		free_pages((unsigned long)cpuaddr, get_order(size));
		return NULL;
	}

	return cpuaddr;
}

/**
 * sn_pci_free_consistent - free memory associated with coherent DMAable region
 * @hwdev: device to free for
 * @size: size to free
 * @vaddr: kernel virtual address to free
 * @dma_handle: DMA address associated with this region
 *
 * Frees the memory allocated by pci_alloc_consistent().  Also known
 * as platform_pci_free_consistent() by the IA64 machvec code.
 */
void
sn_pci_free_consistent(struct pci_dev *hwdev, size_t size, void *vaddr,
		       dma_addr_t dma_handle)
{
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(hwdev);
	struct pcibus_bussoft *bussoft = SN_PCIDEV_BUSSOFT(hwdev);

	if (! bussoft) {
		return;
	}

	pcibr_dma_unmap(pcidev_info, dma_handle, 0);
	free_pages((unsigned long)vaddr, get_order(size));
}

/**
 * sn_pci_map_sg - map a scatter-gather list for DMA
 * @hwdev: device to map for
 * @sg: scatterlist to map
 * @nents: number of entries
 * @direction: direction of the DMA transaction
 *
 * Maps each entry of @sg for DMA.  Also known as platform_pci_map_sg by the
 * IA64 machvec code.
 */
int
sn_pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents,
	      int direction)
{

	int i;
	unsigned long phys_addr;
	struct scatterlist *saved_sg = sg;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(hwdev);
	struct pcibus_bussoft *bussoft = SN_PCIDEV_BUSSOFT(hwdev);

	/* can't go anywhere w/o a direction in life */
	if (direction == PCI_DMA_NONE)
		BUG();

	if (! bussoft) {
		return 0;
	}

	/* SN cannot support DMA addresses smaller than 32 bits. */
	if (hwdev->dma_mask < 0x7fffffff)
		return 0;

	/*
	 * Setup a DMA address for each entry in the
	 * scatterlist.
	 */
	for (i = 0; i < nents; i++, sg++) {
		phys_addr =
		    __pa((unsigned long)page_address(sg->page) + sg->offset);
		sg->dma_address = pcibr_dma_map(pcidev_info, phys_addr, sg->length, 0);

		if (!sg->dma_address) {
			printk(KERN_ERR "sn_pci_map_sg: Unable to allocate "
			       "anymore page map entries.\n");
			/*
			 * We will need to free all previously allocated entries.
			 */
			if (i > 0) {
				sn_pci_unmap_sg(hwdev, saved_sg, i, direction);
			}
			return (0);
		}

		sg->dma_length = sg->length;
	}

	return nents;

}

/**
 * sn_pci_unmap_sg - unmap a scatter-gather list
 * @hwdev: device to unmap
 * @sg: scatterlist to unmap
 * @nents: number of scatterlist entries
 * @direction: DMA direction
 *
 * Unmap a set of streaming mode DMA translations.  Again, cpu read rules
 * concerning calls here are the same as for pci_unmap_single() below.  Also
 * known as sn_pci_unmap_sg() by the IA64 machvec code.
 */
void
sn_pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents,
		int direction)
{
	int i;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(hwdev);
	struct pcibus_bussoft *bussoft = SN_PCIDEV_BUSSOFT(hwdev);

	/* can't go anywhere w/o a direction in life */
	if (direction == PCI_DMA_NONE)
		BUG();

	if (! bussoft) {
		return;
	}

	for (i = 0; i < nents; i++, sg++) {
		pcibr_dma_unmap(pcidev_info, sg->dma_address, direction);
		sg->dma_address = (dma_addr_t) NULL;
		sg->dma_length = 0;
	}
}

/**
 * sn_pci_map_single - map a single region for DMA
 * @hwdev: device to map for
 * @ptr: kernel virtual address of the region to map
 * @size: size of the region
 * @direction: DMA direction
 *
 * Map the region pointed to by @ptr for DMA and return the
 * DMA address.   Also known as platform_pci_map_single() by
 * the IA64 machvec code.
 *
 * We map this to the one step pcibr_dmamap_trans interface rather than
 * the two step pcibr_dmamap_alloc/pcibr_dmamap_addr because we have
 * no way of saving the dmamap handle from the alloc to later free
 * (which is pretty much unacceptable).
 *
 * TODO: simplify our interface;
 *       get rid of dev_desc and vhdl (seems redundant given a pci_dev);
 *       figure out how to save dmamap handle so can use two step.
 */
dma_addr_t
sn_pci_map_single(struct pci_dev *hwdev, void *ptr, size_t size, int direction)
{
	dma_addr_t dma_addr;
	unsigned long phys_addr;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(hwdev);
	struct pcibus_bussoft *bussoft = SN_PCIDEV_BUSSOFT(hwdev);

	if (direction == PCI_DMA_NONE)
		BUG();

	if (bussoft == NULL) {
		return 0;
	}

	if (! IS_PCI_BRIDGE_ASIC(bussoft->bs_asic_type)) {
		return 0;		/* unsupported asic type */
	}

	/* SN cannot support DMA addresses smaller than 32 bits. */
	if (hwdev->dma_mask < 0x7fffffff)
		return 0;

	/*
	 * Call our dmamap interface
	 */

	phys_addr = __pa(ptr);
	dma_addr = pcibr_dma_map(pcidev_info, phys_addr, size, 0);
	if (!dma_addr) {
		printk(KERN_ERR "pci_map_single: Unable to allocate anymore "
		       "page map entries.\n");
		return 0;
	}
	return ((dma_addr_t) dma_addr);
}

/**
 * sn_pci_dma_sync_single_* - make sure all DMAs or CPU accesses
 * have completed
 * @hwdev: device to sync
 * @dma_handle: DMA address to sync
 * @size: size of region
 * @direction: DMA direction
 *
 * This routine is supposed to sync the DMA region specified
 * by @dma_handle into the 'coherence domain'.  We do not need to do
 * anything on our platform.
 */
void
sn_pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr, size_t size,
		    int direction)
{
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(hwdev);
	struct pcibus_bussoft *bussoft = SN_PCIDEV_BUSSOFT(hwdev);

	if (direction == PCI_DMA_NONE)
		BUG();

	if (bussoft == NULL) {
		return;
	}

	if (! IS_PCI_BRIDGE_ASIC(bussoft->bs_asic_type)) {
		return;		/* unsupported asic type */
	}

	pcibr_dma_unmap(pcidev_info, dma_addr, direction);
}

/**
 * sn_dma_supported - test a DMA mask
 * @hwdev: device to test
 * @mask: DMA mask to test
 *
 * Return whether the given PCI device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during PCI bus mastering, then you would pass 0x00ffffff as the mask to
 * this function.  Of course, SN only supports devices that have 32 or more
 * address bits when using the PMU.  We could theoretically support <32 bit
 * cards using direct mapping, but we'll worry about that later--on the off
 * chance that someone actually wants to use such a card.
 */
int sn_pci_dma_supported(struct pci_dev *hwdev, u64 mask)
{
	if (mask < 0x7fffffff)
		return 0;
	return 1;
}

/*
 * New generic DMA routines just wrap sn2 PCI routines until we
 * support other bus types (if ever).
 */

int sn_dma_supported(struct device *dev, u64 mask)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return sn_pci_dma_supported(to_pci_dev(dev), mask);
}

EXPORT_SYMBOL(sn_dma_supported);

int sn_dma_set_mask(struct device *dev, u64 dma_mask)
{
	BUG_ON(dev->bus != &pci_bus_type);

	if (!sn_dma_supported(dev, dma_mask))
		return 0;

	*dev->dma_mask = dma_mask;
	return 1;
}

EXPORT_SYMBOL(sn_dma_set_mask);

void *sn_dma_alloc_coherent(struct device *dev, size_t size,
			    dma_addr_t * dma_handle, int flag)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return sn_pci_alloc_consistent(to_pci_dev(dev), size, dma_handle);
}

EXPORT_SYMBOL(sn_dma_alloc_coherent);

void
sn_dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		     dma_addr_t dma_handle)
{
	BUG_ON(dev->bus != &pci_bus_type);

	sn_pci_free_consistent(to_pci_dev(dev), size, cpu_addr, dma_handle);
}

EXPORT_SYMBOL(sn_dma_free_coherent);

dma_addr_t
sn_dma_map_single(struct device *dev, void *cpu_addr, size_t size,
		  int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return sn_pci_map_single(to_pci_dev(dev), cpu_addr, size,
				 (int)direction);
}

EXPORT_SYMBOL(sn_dma_map_single);

void
sn_dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		    int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	sn_pci_unmap_single(to_pci_dev(dev), dma_addr, size, (int)direction);
}

EXPORT_SYMBOL(sn_dma_unmap_single);

dma_addr_t
sn_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return pci_map_page(to_pci_dev(dev), page, offset, size,
			    (int)direction);
}

EXPORT_SYMBOL(sn_dma_map_page);

void
sn_dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
		  int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_unmap_page(to_pci_dev(dev), dma_address, size, (int)direction);
}

EXPORT_SYMBOL(sn_dma_unmap_page);

int
sn_dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	      int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return sn_pci_map_sg(to_pci_dev(dev), sg, nents, (int)direction);
}

EXPORT_SYMBOL(sn_dma_map_sg);

void
sn_dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
		int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	sn_pci_unmap_sg(to_pci_dev(dev), sg, nhwentries, (int)direction);
}

EXPORT_SYMBOL(sn_dma_unmap_sg);

void
sn_dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
			   size_t size, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}

EXPORT_SYMBOL(sn_dma_sync_single_for_cpu);

void
sn_dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
			      size_t size, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}

EXPORT_SYMBOL(sn_dma_sync_single_for_device);

void
sn_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
		       int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}

EXPORT_SYMBOL(sn_dma_sync_sg_for_cpu);

void
sn_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			  int nelems, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}

int sn_dma_mapping_error(dma_addr_t dma_addr)
{
	return 0;
}

EXPORT_SYMBOL(sn_dma_sync_sg_for_device);
EXPORT_SYMBOL(sn_pci_unmap_single);
EXPORT_SYMBOL(sn_pci_map_single);
EXPORT_SYMBOL(sn_pci_map_sg);
EXPORT_SYMBOL(sn_pci_unmap_sg);
EXPORT_SYMBOL(sn_pci_alloc_consistent);
EXPORT_SYMBOL(sn_pci_free_consistent);
EXPORT_SYMBOL(sn_pci_dma_supported);
EXPORT_SYMBOL(sn_dma_mapping_error);
