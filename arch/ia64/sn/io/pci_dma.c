/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Leo Dagum
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/devfs_fs_kernel.h>

#ifndef LANGUAGE_C 
#define LANGUAGE_C 99
#endif
#ifndef _LANGUAGE_C
#define _LANGUAGE_C 99
#endif
#ifndef CONFIG_IA64_SGI_IO
#define CONFIG_IA64_SGI_IO 99
#endif

#include <asm/io.h>
#include <asm/sn/sgi.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/iobus.h>
#include <asm/sn/pci/pci_bus_cvlink.h>
#include <asm/sn/types.h>
#include <asm/sn/alenlist.h>

/*
 * this is REALLY ugly, blame it on gcc's lame inlining that we
 * have to put procedures in header files
 */
#if LANGUAGE_C == 99
#undef LANGUAGE_C
#endif
#if _LANGUAGE_C == 99
#undef _LANGUAGE_C
#endif
#if CONFIG_IA64_SGI_IO == 99
#undef CONFIG_IA64_SGI_IO
#endif

/*
 * sn1 platform specific pci_alloc_consistent()
 *
 * this interface is meant for "command" streams, i.e. called only
 * once for initializing a device, so we don't want prefetching or
 * write gathering turned on, hence the PCIIO_DMA_CMD flag
 */
void *
sn1_pci_alloc_consistent (struct pci_dev *hwdev, size_t size, dma_addr_t *dma_handle)
{
        void *ret;
        int gfp = GFP_ATOMIC;
	devfs_handle_t    vhdl;
	struct sn1_device_sysdata *device_sysdata;
	paddr_t temp_ptr;

	*dma_handle = (dma_addr_t) NULL;

	/*
	 * get vertex for the device
	 */
	device_sysdata = (struct sn1_device_sysdata *) hwdev->sysdata;
	vhdl = device_sysdata->vhdl;

        if ( ret = (void *)__get_free_pages(gfp, get_order(size)) ) {
		memset(ret, 0, size);
	} else {
		return(NULL);
	}

	temp_ptr = (paddr_t) __pa(ret);
	if (IS_PCIA64(hwdev)) {

		/*
		 * This device supports 64bits DMA addresses.
		 */
		*dma_handle = pciio_dmatrans_addr(vhdl, NULL, temp_ptr, size,
			PCIBR_BARRIER | PCIIO_BYTE_STREAM | PCIIO_DMA_CMD
			| PCIIO_DMA_A64 );
		return (ret);
	}

	/*
	 * Devices that supports 32 Bits upto 63 Bits DMA Address gets
	 * 32 Bits DMA addresses.
	 *
	 * First try to get 32 Bit Direct Map Support.
	 */
	if (IS_PCI32G(hwdev)) {
		*dma_handle = pciio_dmatrans_addr(vhdl, NULL, temp_ptr, size,
			PCIBR_BARRIER | PCIIO_BYTE_STREAM | PCIIO_DMA_CMD);
		if (dma_handle) {
			return (ret);
		} else {
			/*
			 * We need to map this request by using ATEs.
			 */
			printk("sn1_pci_alloc_consistent: 32Bits DMA Page Map support not available yet!");
			BUG();
		}
	}

	if (IS_PCI32L(hwdev)) {
		/*
		 * SNIA64 cannot support DMA Addresses smaller than 32 bits.
		 */
		return (NULL);
	}

        return NULL;
}

void
sn1_pci_free_consistent(struct pci_dev *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long) vaddr, get_order(size));
}

/*
 * On sn1 we use the alt_address entry of the scatterlist to store
 * the physical address corresponding to the given virtual address
 */
int
sn1_pci_map_sg (struct pci_dev *hwdev,
                        struct scatterlist *sg, int nents, int direction)
{

	int i;
	devfs_handle_t	vhdl;
	dma_addr_t dma_addr;
	paddr_t temp_ptr;
	struct sn1_device_sysdata *device_sysdata;


	if (direction == PCI_DMA_NONE)
		BUG();

	/*
	 * Handle 64 bit cards.
	 */
	device_sysdata = (struct sn1_device_sysdata *) hwdev->sysdata;
	vhdl = device_sysdata->vhdl;
	for (i = 0; i < nents; i++, sg++) {
		sg->orig_address = sg->address;
		dma_addr = 0;
		temp_ptr = (paddr_t) __pa(sg->address);

		/*
		 * Handle the most common case 64Bit cards.
		 */
		if (IS_PCIA64(hwdev)) {
			dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
				temp_ptr, sg->length,
				PCIBR_BARRIER | PCIIO_BYTE_STREAM |
				PCIIO_DMA_CMD | PCIIO_DMA_A64 );
			sg->address = (char *)dma_addr;
/* printk("pci_map_sg: 64Bits hwdev %p DMA Address 0x%p alt_address 0x%p orig_address 0x%p length 0x%x\n", hwdev, sg->address, sg->alt_address, sg->orig_address, sg->length); */
			continue;
		}

		/*
		 * Handle 32Bits and greater cards.
		 */
		if (IS_PCI32G(hwdev)) {
			dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
				temp_ptr, sg->length,
				PCIBR_BARRIER | PCIIO_BYTE_STREAM |
				PCIIO_DMA_CMD);
			if (dma_addr) {
				sg->address = (char *)dma_addr;
/* printk("pci_map_single: 32Bit direct pciio_dmatrans_addr pcidev %p returns dma_addr 0x%lx\n", hwdev, dma_addr); */
				continue;
			} else {
				/*
				 * We need to map this request by using ATEs.
				 */
				printk("pci_map_single: 32Bits DMA Page Map support not available yet!");
				BUG();

			}
		}
	}

	return nents;

}

/*
 * Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
void
sn1_pci_unmap_sg (struct pci_dev *hwdev, struct scatterlist *sg, int nelems, int direction)
{
	int i;

	if (direction == PCI_DMA_NONE)
		BUG();
	for (i = 0; i < nelems; i++, sg++)
		if (sg->orig_address != sg->address) {
			/* phys_to_virt((dma_addr_t)sg->address | ~0x80000000); */
			sg->address = sg->orig_address;
			sg->orig_address = 0;
		}
}

/*
 * We map this to the one step pciio_dmamap_trans interface rather than
 * the two step pciio_dmamap_alloc/pciio_dmamap_addr because we have
 * no way of saving the dmamap handle from the alloc to later free
 * (which is pretty much unacceptable).
 *
 * TODO: simplify our interface;
 *       get rid of dev_desc and vhdl (seems redundant given a pci_dev);
 *       figure out how to save dmamap handle so can use two step.
 */
dma_addr_t sn1_pci_map_single (struct pci_dev *hwdev,
				void *ptr, size_t size, int direction)
{
	devfs_handle_t	vhdl;
	dma_addr_t dma_addr;
	paddr_t temp_ptr;
	struct sn1_device_sysdata *device_sysdata;


	if (direction == PCI_DMA_NONE)
		BUG();

	if (IS_PCI32L(hwdev)) {
		/*
		 * SNIA64 cannot support DMA Addresses smaller than 32 bits.
		 */
		return ((dma_addr_t) NULL);
	}

	/*
	 * find vertex for the device
	 */
	device_sysdata = (struct sn1_device_sysdata *)hwdev->sysdata;
	vhdl = device_sysdata->vhdl;
/* printk("pci_map_single: Called vhdl = 0x%p ptr = 0x%p size = %d\n", vhdl, ptr, size); */
	/*
	 * Call our dmamap interface
	 */
	dma_addr = 0;
	temp_ptr = (paddr_t) __pa(ptr);

	if (IS_PCIA64(hwdev)) {
		/*
		 * This device supports 64bits DMA addresses.
		 */
		dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
			temp_ptr, size,
			PCIBR_BARRIER | PCIIO_BYTE_STREAM | PCIIO_DMA_CMD
			| PCIIO_DMA_A64 );
/* printk("pci_map_single: 64Bit pciio_dmatrans_addr pcidev %p returns dma_addr 0x%lx\n", hwdev, dma_addr); */
		return (dma_addr);
	}

	/*
	 * Devices that supports 32 Bits upto 63 Bits DMA Address gets
	 * 32 Bits DMA addresses.
	 *
	 * First try to get 32 Bit Direct Map Support.
	 */
	if (IS_PCI32G(hwdev)) {
		dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
			temp_ptr, size,
			PCIBR_BARRIER | PCIIO_BYTE_STREAM | PCIIO_DMA_CMD);
		if (dma_addr) {
/* printk("pci_map_single: 32Bit direct pciio_dmatrans_addr pcidev %p returns dma_addr 0x%lx\n", hwdev, dma_addr); */
			return (dma_addr);
		} else {
			/*
			 * We need to map this request by using ATEs.
			 */
			printk("pci_map_single: 32Bits DMA Page Map support not available yet!");
			BUG();
		}
	}

	if (IS_PCI32L(hwdev)) {
		/*
		 * SNIA64 cannot support DMA Addresses smaller than 32 bits.
		 */
		return ((dma_addr_t) NULL);
	}

	return ((dma_addr_t) NULL);

}

void
sn1_pci_unmap_single (struct pci_dev *hwdev, dma_addr_t dma_addr, size_t size, int direction)
{
        if (direction == PCI_DMA_NONE)
                BUG();
        /* Nothing to do */
}

void
sn1_pci_dma_sync_single (struct pci_dev *hwdev, dma_addr_t dma_handle, size_t size, int direction)
{
        if (direction == PCI_DMA_NONE)
                BUG();
        /* Nothing to do */
}

void
sn1_pci_dma_sync_sg (struct pci_dev *hwdev, struct scatterlist *sg, int nelems, int direction)
{
        if (direction == PCI_DMA_NONE)
                BUG();
        /* Nothing to do */
}

unsigned long
sn1_dma_address (struct scatterlist *sg)
{
	return (sg->address);
}
