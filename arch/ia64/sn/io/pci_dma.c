/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000,2002 Silicon Graphics, Inc. All rights reserved.
 *
 * Routines for PCI DMA mapping.  See Documentation/DMA-mapping.txt for
 * a description of how these routines should be used.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/module.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/driver.h>
#include <asm/sn/types.h>
#include <asm/sn/alenlist.h>
#include <asm/sn/pci/pci_bus_cvlink.h>
#include <asm/sn/nag.h>

/*
 * For ATE allocations
 */
pciio_dmamap_t get_free_pciio_dmamap(devfs_handle_t);
void free_pciio_dmamap(pcibr_dmamap_t);
static struct sn_dma_maps_s *find_sn_dma_map(dma_addr_t, unsigned char);

/*
 * Toplogy stuff
 */
extern devfs_handle_t busnum_to_pcibr_vhdl[];
extern nasid_t busnum_to_nid[];
extern void * busnum_to_atedmamaps[];

/**
 * get_free_pciio_dmamap - find and allocate an ATE
 * @pci_bus: PCI bus to get an entry for
 *
 * Finds and allocates an ATE on the PCI bus specified
 * by @pci_bus.
 */
pciio_dmamap_t
get_free_pciio_dmamap(devfs_handle_t pci_bus)
{
	int i;
	struct sn_dma_maps_s *sn_dma_map = NULL;

	/*
	 * Darn, we need to get the maps allocated for this bus.
	 */
	for (i = 0; i < MAX_PCI_XWIDGET; i++) {
		if (busnum_to_pcibr_vhdl[i] == pci_bus) {
			sn_dma_map = busnum_to_atedmamaps[i];
		}
	}

	/*
	 * Now get a free dmamap entry from this list.
	 */
	for (i = 0; i < MAX_ATE_MAPS; i++, sn_dma_map++) {
		if (!sn_dma_map->dma_addr) {
			sn_dma_map->dma_addr = -1;
			return( (pciio_dmamap_t) sn_dma_map );
		}
	}

	return NULL;
}

/**
 * free_pciio_dmamap - free an ATE
 * @dma_map: ATE to free
 *
 * Frees the ATE specified by @dma_map.
 */
void
free_pciio_dmamap(pcibr_dmamap_t dma_map)
{
	struct sn_dma_maps_s *sn_dma_map;

	sn_dma_map = (struct sn_dma_maps_s *) dma_map;
	sn_dma_map->dma_addr = 0;
}

/**
 * find_sn_dma_map - find an ATE associated with @dma_addr and @busnum
 * @dma_addr: DMA address to look for
 * @busnum: PCI bus to look on
 *
 * Finds the ATE associated with @dma_addr and @busnum.
 */
static struct sn_dma_maps_s *
find_sn_dma_map(dma_addr_t dma_addr, unsigned char busnum)
{

	struct sn_dma_maps_s *sn_dma_map = NULL;
	int i;

	sn_dma_map = busnum_to_atedmamaps[busnum];

	for (i = 0; i < MAX_ATE_MAPS; i++, sn_dma_map++) {
		if (sn_dma_map->dma_addr == dma_addr) {
			return sn_dma_map;
		}
	}

	return NULL;
}

/**
 * sn_dma_sync - try to flush DMA buffers into the coherence domain
 * @hwdev: device to flush
 *
 * This routine flushes all DMA buffers for the device into the II of
 * the destination hub.
 *
 * NOTE!: this does not mean that the data is in the "coherence domain",
 * but it is very close.  In other words, this routine *does not work*
 * as advertised due to hardware bugs.  That said, it should be good enough for
 * most situations.
 */
void
sn_dma_sync(struct pci_dev *hwdev)
{

#ifdef SN_DMA_SYNC

	struct sn_device_sysdata *device_sysdata;
	volatile unsigned long dummy;

	/*
	 * A DMA sync is supposed to ensure that 
	 * all the DMA from a particular device
	 * is complete and coherent.  We
	 * try to do this by
	 *	1. flushing the write wuffers from Bridge
	 *	2. flushing the Xbow port.
	 * Unfortunately, this only gets the DMA transactions 'very close' to
	 * the coherence domain, but not quite in it.
	 */
	device_sysdata = (struct sn_device_sysdata *)hwdev->sysdata;
	dummy = (volatile unsigned long ) *device_sysdata->dma_buf_sync;

	/*
	 * For the Xbow port flush, we may be denied the request because 
	 * someone else may be flushing the port .. try again.
	 */
	while((volatile unsigned long ) *device_sysdata->xbow_buf_sync) {
		udelay(2);
	}
#endif
}

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
 * more information.  Note that this routine will always put a 32 bit
 * DMA address into @dma_handle.  This is because most devices
 * that are capable of 64 bit PCI DMA transactions can't do 64 bit _coherent_
 * DMAs, and unfortunately this interface has to cater to the LCD.  Oh well.
 *
 * Also known as platform_pci_alloc_consistent() by the IA64 machvec code.
 */
void *
sn_pci_alloc_consistent(struct pci_dev *hwdev, size_t size, dma_addr_t *dma_handle)
{
        void *cpuaddr;
	devfs_handle_t vhdl;
	struct sn_device_sysdata *device_sysdata;
	unsigned long phys_addr;
	pciio_dmamap_t dma_map = 0;
	struct sn_dma_maps_s *sn_dma_map;

	*dma_handle = 0;

	/* We can't easily support < 32 bit devices */
	if (IS_PCI32L(hwdev))
		return NULL;

	/*
	 * Get hwgraph vertex for the device
	 */
	device_sysdata = (struct sn_device_sysdata *) hwdev->sysdata;
	vhdl = device_sysdata->vhdl;

	/*
	 * Allocate the memory.  FIXME: if we're allocating for
	 * two devices on the same bus, we should at least try to
	 * allocate memory in the same 2 GB window to avoid using
	 * ATEs for the translation.  See the comment above about the
	 * 32 bit requirement for this function.
	 */
	if(!(cpuaddr = (void *)__get_free_pages(GFP_ATOMIC, get_order(size))))
		return NULL;

	memset(cpuaddr, 0, size); /* have to zero it out */

	/* physical addr. of the memory we just got */
	phys_addr = __pa(cpuaddr);

	/*
	 * This will try to use a Direct Map register to do the
	 * 32 bit DMA mapping, but it may not succeed if another
	 * device on the same bus is already mapped with different
	 * attributes or to a different memory region.
	 */
#ifdef CONFIG_IA64_SGI_SN1
	*dma_handle = pciio_dmatrans_addr(vhdl, NULL, phys_addr, size,
					  PCIIO_BYTE_STREAM |
					  PCIIO_DMA_CMD);
#elif defined(CONFIG_IA64_SGI_SN2)
	*dma_handle = pciio_dmatrans_addr(vhdl, NULL, phys_addr, size,
			((IS_PIC_DEVICE(hwdev)) ? 0 : PCIIO_BYTE_STREAM) |
					  PCIIO_DMA_CMD);
#else
#error unsupported platform
#endif

	/*
	 * It is a 32 bit card and we cannot do direct mapping,
	 * so we try to use an ATE.
	 */
	if (!(*dma_handle)) {
#ifdef CONFIG_IA64_SGI_SN1
		dma_map = pciio_dmamap_alloc(vhdl, NULL, size,
					     PCIIO_BYTE_STREAM |
					     PCIIO_DMA_CMD);
#elif defined(CONFIG_IA64_SGI_SN2)
		dma_map = pciio_dmamap_alloc(vhdl, NULL, size,
				((IS_PIC_DEVICE(hwdev)) ? 0 : PCIIO_BYTE_STREAM) |
					     PCIIO_DMA_CMD);
#else
#error unsupported platform
#endif
		if (!dma_map) {
			printk(KERN_ERR "sn_pci_alloc_consistent: Unable to "
			       "allocate anymore 32 bit page map entries.\n");
			BUG();
		}
		*dma_handle = (dma_addr_t) pciio_dmamap_addr(dma_map,phys_addr,
							     size);
		sn_dma_map = (struct sn_dma_maps_s *)dma_map;
		sn_dma_map->dma_addr = *dma_handle;
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
sn_pci_free_consistent(struct pci_dev *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	struct sn_dma_maps_s *sn_dma_map = NULL;

	/*
	 * Get the sn_dma_map entry.
	 */
	if (IS_PCI32_MAPPED(dma_handle))
		sn_dma_map = find_sn_dma_map(dma_handle, hwdev->bus->number);

	/*
	 * and free it if necessary...
	 */
	if (sn_dma_map) {
		pciio_dmamap_done((pciio_dmamap_t)sn_dma_map);
		pciio_dmamap_free((pciio_dmamap_t)sn_dma_map);
		sn_dma_map->dma_addr = (dma_addr_t)NULL;
	}
	free_pages((unsigned long) vaddr, get_order(size));
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
sn_pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents, int direction)
{

	int i;
	devfs_handle_t vhdl;
	dma_addr_t dma_addr;
	unsigned long phys_addr;
	struct sn_device_sysdata *device_sysdata;
	pciio_dmamap_t dma_map;

	/* can't go anywhere w/o a direction in life */
	if (direction == PCI_DMA_NONE)
		BUG();

	/*
	 * Get the hwgraph vertex for the device
	 */
	device_sysdata = (struct sn_device_sysdata *) hwdev->sysdata;
	vhdl = device_sysdata->vhdl;

	/*
	 * Setup a DMA address for each entry in the
	 * scatterlist.
	 */
	for (i = 0; i < nents; i++, sg++) {
		/* this catches incorrectly written drivers that
                   attempt to map scatterlists that they have
                   previously mapped.  we print a warning and
                   continue, but the driver should be fixed */
		switch (((u64)sg->dma_address) >> 60) {
		case 0xa:
		case 0xb:
#ifdef DEBUG
/* This needs to be cleaned up at some point. */
			NAG("A PCI driver (for device at%8s) has attempted to "
			    "map a scatterlist that was previously mapped at "
			    "%p - this is currently being worked around.\n",
			    hwdev->slot_name, (void *)sg->dma_address);
			phys_addr = (u64)sg->dma_address & TO_PHYS_MASK;
			break;
#endif
		default: /* not previously mapped, get the phys. addr */
			phys_addr = __pa(sg->dma_address);
			break;
		}
		sg->page = NULL;
		dma_addr = 0;

		/*
		 * Handle the most common case: 64 bit cards.  This
		 * call should always succeed.
		 */
		if (IS_PCIA64(hwdev)) {
			dma_addr = pciio_dmatrans_addr(vhdl, NULL, phys_addr,
						       sg->length,
			       ((IS_PIC_DEVICE(hwdev)) ? 0 : PCIIO_BYTE_STREAM) |
						       PCIIO_DMA_DATA |
						       PCIIO_DMA_A64);
			sg->dma_address = (char *)dma_addr;
			continue;
		}

		/*
		 * Handle 32-63 bit cards via direct mapping
		 */
		if (IS_PCI32G(hwdev)) {
#ifdef CONFIG_IA64_SGI_SN1
			dma_addr = pciio_dmatrans_addr(vhdl, NULL, phys_addr,
						       sg->length,
						       PCIIO_BYTE_STREAM |
						       PCIIO_DMA_DATA);
#elif defined(CONFIG_IA64_SGI_SN2)
			dma_addr = pciio_dmatrans_addr(vhdl, NULL, phys_addr,
						       sg->length,
					((IS_PIC_DEVICE(hwdev)) ? 0 : PCIIO_BYTE_STREAM) |
						       PCIIO_DMA_DATA);
#else
#error unsupported platform
#endif
			/*
			 * See if we got a direct map entry
			 */
			if (dma_addr) {
				sg->dma_address = (char *)dma_addr;
				continue;
			}

		}

		/*
		 * It is a 32 bit card and we cannot do direct mapping,
		 * so we use an ATE.
		 */
		dma_map = 0;
#ifdef CONFIG_IA64_SGI_SN1
		dma_map = pciio_dmamap_alloc(vhdl, NULL, sg->length,
					     PCIIO_BYTE_STREAM |
					     PCIIO_DMA_DATA);
#elif defined(CONFIG_IA64_SGI_SN2)
		dma_map = pciio_dmamap_alloc(vhdl, NULL, sg->length,
				((IS_PIC_DEVICE(hwdev)) ? 0 : PCIIO_BYTE_STREAM) |
					     PCIIO_DMA_DATA);
#else
#error unsupported platform
#endif
		if (!dma_map) {
			printk(KERN_ERR "sn_pci_map_sg: Unable to allocate "
			       "anymore 32 bit page map entries.\n");
			BUG();
		}
		dma_addr = pciio_dmamap_addr(dma_map, phys_addr, sg->length);
		sg->dma_address = (char *)dma_addr;
		sg->page = (struct page *)dma_map;
		
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
sn_pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents, int direction)
{
	int i;
	struct sn_dma_maps_s *sn_dma_map;

	/* can't go anywhere w/o a direction in life */
	if (direction == PCI_DMA_NONE)
		BUG();

	for (i = 0; i < nents; i++, sg++)
		if (sg->page) {
			/*
			 * We maintain the DMA Map pointer in sg->page if 
			 * it is ever allocated.
			 */
			sg->dma_address = 0;
			sn_dma_map = (struct sn_dma_maps_s *)sg->page;
			pciio_dmamap_done((pciio_dmamap_t)sn_dma_map);
			pciio_dmamap_free((pciio_dmamap_t)sn_dma_map);
			sn_dma_map->dma_addr = 0;
			sg->page = 0;
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
 * We map this to the one step pciio_dmamap_trans interface rather than
 * the two step pciio_dmamap_alloc/pciio_dmamap_addr because we have
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
	devfs_handle_t vhdl;
	dma_addr_t dma_addr;
	unsigned long phys_addr;
	struct sn_device_sysdata *device_sysdata;
	pciio_dmamap_t dma_map = NULL;
	struct sn_dma_maps_s *sn_dma_map;

	if (direction == PCI_DMA_NONE)
		BUG();

	/* SN cannot support DMA addresses smaller than 32 bits. */
	if (IS_PCI32L(hwdev))
		return 0;

	/*
	 * find vertex for the device
	 */
	device_sysdata = (struct sn_device_sysdata *)hwdev->sysdata;
	vhdl = device_sysdata->vhdl;

	/*
	 * Call our dmamap interface
	 */
	dma_addr = 0;
	phys_addr = __pa(ptr);

	if (IS_PCIA64(hwdev)) {
		/* This device supports 64 bit DMA addresses. */
		dma_addr = pciio_dmatrans_addr(vhdl, NULL, phys_addr, size,
		       ((IS_PIC_DEVICE(hwdev)) ? 0 : PCIIO_BYTE_STREAM) |
					       PCIIO_DMA_DATA |
					       PCIIO_DMA_A64);
		return dma_addr;
	}

	/*
	 * Devices that support 32 bit to 63 bit DMA addresses get
	 * 32 bit DMA addresses.
	 *
	 * First try to get a 32 bit direct map register.
	 */
	if (IS_PCI32G(hwdev)) {
#ifdef CONFIG_IA64_SGI_SN1
		dma_addr = pciio_dmatrans_addr(vhdl, NULL, phys_addr, size,
					       PCIIO_BYTE_STREAM |
					       PCIIO_DMA_DATA);
#elif defined(CONFIG_IA64_SGI_SN2)
		dma_addr = pciio_dmatrans_addr(vhdl, NULL, phys_addr, size,
			((IS_PIC_DEVICE(hwdev)) ? 0 : PCIIO_BYTE_STREAM) |
					       PCIIO_DMA_DATA);
#else
#error unsupported platform
#endif
		if (dma_addr)
			return dma_addr;
	}

	/*
	 * It's a 32 bit card and we cannot do direct mapping so
	 * let's use the PMU instead.
	 */
	dma_map = NULL;
#ifdef CONFIG_IA64_SGI_SN1
	dma_map = pciio_dmamap_alloc(vhdl, NULL, size, PCIIO_BYTE_STREAM |
				     PCIIO_DMA_DATA);
#elif defined(CONFIG_IA64_SGI_SN2)
	dma_map = pciio_dmamap_alloc(vhdl, NULL, size, 
			((IS_PIC_DEVICE(hwdev)) ? 0 : PCIIO_BYTE_STREAM) |
			PCIIO_DMA_DATA);
#else
#error unsupported platform
#endif

	if (!dma_map) {
		printk(KERN_ERR "pci_map_single: Unable to allocate anymore "
		       "32 bit page map entries.\n");
		BUG();
	}

	dma_addr = (dma_addr_t) pciio_dmamap_addr(dma_map, phys_addr, size);
	sn_dma_map = (struct sn_dma_maps_s *)dma_map;
	sn_dma_map->dma_addr = dma_addr;

	return ((dma_addr_t)dma_addr);
}

/**
 * sn_pci_unmap_single - unmap a region used for DMA
 * @hwdev: device to unmap
 * @dma_addr: DMA address to unmap
 * @size: size of region
 * @direction: DMA direction
 *
 * Unmaps the region pointed to by @dma_addr.  Also known as
 * platform_pci_unmap_single() by the IA64 machvec code.
 */
void
sn_pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr, size_t size, int direction)
{
	struct sn_dma_maps_s *sn_dma_map = NULL;

        if (direction == PCI_DMA_NONE)
		BUG();

	/*
	 * Get the sn_dma_map entry.
	 */
	if (IS_PCI32_MAPPED(dma_addr))
		sn_dma_map = find_sn_dma_map(dma_addr, hwdev->bus->number);

	/*
	 * and free it if necessary...
	 */
	if (sn_dma_map) {
		pciio_dmamap_done((pciio_dmamap_t)sn_dma_map);
		pciio_dmamap_free((pciio_dmamap_t)sn_dma_map);
		sn_dma_map->dma_addr = (dma_addr_t)NULL;
	}
}

/**
 * sn_pci_dma_sync_single - make sure all DMAs have completed
 * @hwdev: device to sync
 * @dma_handle: DMA address to sync
 * @size: size of region
 * @direction: DMA direction
 *
 * This routine is supposed to sync the DMA region specified
 * by @dma_handle into the 'coherence domain'.  See sn_dma_sync()
 * above for more information.   Also known as
 * platform_pci_dma_sync_single() by the IA64 machvec code.
 */
void
sn_pci_dma_sync_single(struct pci_dev *hwdev, dma_addr_t dma_handle, size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
                BUG();

	sn_dma_sync(hwdev);
}

/**
 * sn_pci_dma_sync_sg - make sure all DMAs have completed
 * @hwdev: device to sync
 * @sg: scatterlist to sync
 * @nents: number of entries in the scatterlist
 * @direction: DMA direction
 *
 * This routine is supposed to sync the DMA regions specified
 * by @sg into the 'coherence domain'.  See sn_dma_sync()
 * above for more information.   Also known as
 * platform_pci_dma_sync_sg() by the IA64 machvec code.
 */
void
sn_pci_dma_sync_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents, int direction)
{
        if (direction == PCI_DMA_NONE)
                BUG();

	sn_dma_sync(hwdev);
}

/**
 * sn_dma_address - get the DMA address for the first entry of a scatterlist
 * @sg: sg to look at
 *
 * Gets the DMA address for the scatterlist @sg.  Also known as
 * platform_dma_address() by the IA64 machvec code.
 */
unsigned long
sn_dma_address(struct scatterlist *sg)
{
	return ((unsigned long)sg->dma_address);
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
int
sn_pci_dma_supported(struct pci_dev *hwdev, u64 mask)
{
	if (mask < 0xffffffff)
		return 0;
	return 1;
}

EXPORT_SYMBOL(sn_pci_unmap_single);
EXPORT_SYMBOL(sn_pci_map_single);
EXPORT_SYMBOL(sn_pci_dma_sync_single);
EXPORT_SYMBOL(sn_pci_map_sg);
EXPORT_SYMBOL(sn_pci_unmap_sg);
EXPORT_SYMBOL(sn_pci_alloc_consistent);
EXPORT_SYMBOL(sn_pci_free_consistent);
EXPORT_SYMBOL(sn_dma_address);
EXPORT_SYMBOL(sn_pci_dma_supported);

