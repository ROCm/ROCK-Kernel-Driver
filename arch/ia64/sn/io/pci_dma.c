/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000,2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/devfs_fs_kernel.h>

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

pciio_dmamap_t get_free_pciio_dmamap(devfs_handle_t);
void free_pciio_dmamap(pcibr_dmamap_t);
struct sn1_dma_maps_s *find_sn1_dma_map(dma_addr_t, unsigned char);
extern devfs_handle_t busnum_to_pcibr_vhdl[];
extern nasid_t busnum_to_nid[];
extern void * busnum_to_atedmamaps[];

/*
 * Get a free pciio_dmamap_t entry.
 */
pciio_dmamap_t
get_free_pciio_dmamap(devfs_handle_t pci_bus)
{
	int i;
	struct sn1_dma_maps_s *sn1_dma_map = NULL;

	/*
	 * Darn, we need to get the maps allocated for this bus.
	 */
	for (i=0; i<MAX_PCI_XWIDGET; i++) {
		if (busnum_to_pcibr_vhdl[i] == pci_bus) {
			sn1_dma_map = busnum_to_atedmamaps[i];
		}
	}

	/*
	 * Now get a free dmamap entry from this list.
	 */
	for (i=0; i<MAX_ATE_MAPS; i++, sn1_dma_map++) {
		if (!sn1_dma_map->dma_addr) {
			sn1_dma_map->dma_addr = -1;
			return( (pciio_dmamap_t) sn1_dma_map );
		}
	}

	return(NULL);

}

/*
 * Free pciio_dmamap_t entry.
 */
void
free_pciio_dmamap(pcibr_dmamap_t dma_map)
{
	struct sn1_dma_maps_s *sn1_dma_map;

	sn1_dma_map = (struct sn1_dma_maps_s *) dma_map;
	sn1_dma_map->dma_addr = 0;

}

/*
 * sn_dma_sync: This routine flushes all DMA buffers for the device into the II.
 *	This does not mean that the data is in the "Coherence Domain".  But it 
 *	is very close.
 */
void
sn_dma_sync( struct pci_dev *hwdev )
{

	struct sn1_device_sysdata *device_sysdata;
	volatile unsigned long dummy;

	/*
	 * It is expected that on IA64 platform, a DMA sync ensures that all 
	 * the DMA (dma_handle) are complete and coherent.
	 *	1. Flush Write Buffers from Bridge.
	 *	2. Flush Xbow Port.
	 */
	device_sysdata = (struct sn1_device_sysdata *)hwdev->sysdata;
	dummy = (volatile unsigned long ) *device_sysdata->dma_buf_sync;

	/*
	 * For the Xbow Port flush, we maybe denied the request because 
	 * someone else may be flushing the Port .. try again.
	 */
	while((volatile unsigned long ) *device_sysdata->xbow_buf_sync) {
		udelay(2);
	}
}


struct sn1_dma_maps_s *
find_sn1_dma_map(dma_addr_t dma_addr, unsigned char busnum)
{

	struct sn1_dma_maps_s *sn1_dma_map = NULL;
	int i;

	sn1_dma_map = busnum_to_atedmamaps[busnum];

	for (i=0; i<MAX_ATE_MAPS; i++, sn1_dma_map++) {
		if (sn1_dma_map->dma_addr == dma_addr) {
			return( sn1_dma_map );
		}
	}

printk("find_pciio_dmamap: Unable find the corresponding dma map\n");

	return(NULL);

}

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

        if ( (ret = (void *)__get_free_pages(gfp, get_order(size))) ) {
		memset(ret, 0, size);
	} else {
		return(NULL);
	}

	temp_ptr = (paddr_t) __pa(ret);
	if (IS_PCIA64(hwdev)) {

		/*
		 * This device supports 64bits DMA addresses.
		 */
#ifdef CONFIG_IA64_SGI_SN1
		*dma_handle = pciio_dmatrans_addr(vhdl, NULL, temp_ptr, size,
			PCIBR_BARRIER | PCIIO_BYTE_STREAM | PCIIO_DMA_CMD
			| PCIIO_DMA_A64 );
#else /* SN2 */
		*dma_handle = pciio_dmatrans_addr(vhdl, NULL, temp_ptr, size,
			PCIBR_BARRIER | PCIIO_DMA_CMD | PCIIO_DMA_A64 );
#endif

		return (ret);
	}

	/*
	 * Devices that supports 32 Bits upto 63 Bits DMA Address gets
	 * 32 Bits DMA addresses.
	 *
	 * First try to get 32 Bit Direct Map Support.
	 */
	if (IS_PCI32G(hwdev)) {
#ifdef CONFIG_IA64_SGI_SN1
		*dma_handle = pciio_dmatrans_addr(vhdl, NULL, temp_ptr, size,
			PCIBR_BARRIER | PCIIO_BYTE_STREAM | PCIIO_DMA_CMD);
#else /* SN2 */
		*dma_handle = pciio_dmatrans_addr(vhdl, NULL, temp_ptr, size,
			PCIBR_BARRIER | PCIIO_DMA_CMD);
#endif

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
 * On sn1 we use the page entry of the scatterlist to store
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
	pciio_dmamap_t dma_map;



	if (direction == PCI_DMA_NONE)
		BUG();

	/*
	 * Handle 64 bit cards.
	 */
	device_sysdata = (struct sn1_device_sysdata *) hwdev->sysdata;
	vhdl = device_sysdata->vhdl;
	for (i = 0; i < nents; i++, sg++) {
		/* this catches incorrectly written drivers that
                   attempt to map scatterlists that they have
                   previously mapped.  we print a warning and
                   continue, but the driver should be fixed */
		switch (((u64)sg->address) >> 60) {
		case 0xa:
		case 0xb:
#ifdef DEBUG
/* This needs to be cleaned up at some point. */
			NAG("A PCI driver (for device at%8s) has attempted to "
			    "map a scatterlist that was previously mapped at "
			    "%p - this is currently being worked around.\n",
			    hwdev->slot_name, (void *)sg->address);
#endif
			temp_ptr = (u64)sg->address & TO_PHYS_MASK;
			break;
		case 0xe: /* a good address, we now map it. */
			temp_ptr = (paddr_t) __pa(sg->address);
			break;
		default:
			printk(KERN_ERR
			       "Very bad address (%p) passed to sn1_pci_map_sg\n",
			       (void *)sg->address);
			BUG();
		}
		sg->page = (char *)NULL;
		dma_addr = 0;

		/*
		 * Handle the most common case 64Bit cards.
		 */
		if (IS_PCIA64(hwdev)) {
#ifdef CONFIG_IA64_SGI_SN1
			dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
				temp_ptr, sg->length,
				PCIIO_BYTE_STREAM | PCIIO_DMA_DATA |
				PCIIO_DMA_A64 );
#else
			dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
				temp_ptr, sg->length,
				PCIIO_DMA_DATA | PCIIO_DMA_A64 );
#endif
			sg->address = (char *)dma_addr;
			continue;
		}

		/*
		 * Handle 32Bits and greater cards.
		 */
		if (IS_PCI32G(hwdev)) {
#ifdef CONFIG_IA64_SGI_SN1
			dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
				temp_ptr, sg->length,
				PCIIO_BYTE_STREAM | PCIIO_DMA_DATA);
#else
			dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
				temp_ptr, sg->length, PCIIO_DMA_DATA);
#endif
			if (dma_addr) {
				sg->address = (char *)dma_addr;
				continue;
			}

		}

		/*
		 * It is a 32bit card and we cannot do Direct mapping.
		 * Let's 32Bit Page map the request.
		 */
		dma_map = NULL;
#ifdef CONFIG_IA64_SGI_SN1
		dma_map = pciio_dmamap_alloc(vhdl, NULL, sg->length, 
				PCIIO_BYTE_STREAM | PCIIO_DMA_DATA);
#else
		dma_map = pciio_dmamap_alloc(vhdl, NULL, sg->length, PCIIO_DMA_DATA);
#endif
		if (!dma_map) {
			printk("pci_map_sg: Unable to allocate anymore 32Bits Page Map entries.\n");
			BUG();
		}
		dma_addr = (dma_addr_t)pciio_dmamap_addr(dma_map, temp_ptr, sg->length);
		/* printk("pci_map_sg: dma_map 0x%p Phys Addr 0x%p dma_addr 0x%p\n", dma_map, temp_ptr, dma_addr); */
		sg->address = (char *)dma_addr;
		sg->page = (char *)dma_map;
		
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
	struct sn1_dma_maps_s *sn1_dma_map;


	if (direction == PCI_DMA_NONE)
		BUG();

	for (i = 0; i < nelems; i++, sg++)
		if (sg->page) {
			/*
			 * We maintain the DMA Map pointer in sg->page if 
			 * it is ever allocated.
			 */
			/* phys_to_virt((dma_addr_t)sg->address | ~0x80000000); */
			/* sg->address = sg->page; */
			sg->address = (char *)-1;
			sn1_dma_map = (struct sn1_dma_maps_s *)sg->page;
			pciio_dmamap_done((pciio_dmamap_t)sn1_dma_map);
			pciio_dmamap_free((pciio_dmamap_t)sn1_dma_map);
			sn1_dma_map->dma_addr = 0;
			sg->page = 0;
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
	pciio_dmamap_t dma_map = NULL;
	struct sn1_dma_maps_s *sn1_dma_map;


	if (direction == PCI_DMA_NONE)
		BUG();


	/*
	 * find vertex for the device
	 */
	device_sysdata = (struct sn1_device_sysdata *)hwdev->sysdata;
	vhdl = device_sysdata->vhdl;

	/*
	 * Call our dmamap interface
	 */
	dma_addr = 0;
	temp_ptr = (paddr_t) __pa(ptr);

	if (IS_PCIA64(hwdev)) {
		/*
		 * This device supports 64bits DMA addresses.
		 */
#ifdef CONFIG_IA64_SGI_SN1
		dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
			temp_ptr, size,
			PCIIO_BYTE_STREAM | PCIIO_DMA_DATA | PCIIO_DMA_A64 );
#else
		dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
			temp_ptr, size, PCIIO_DMA_DATA | PCIIO_DMA_A64 );
#endif
		return (dma_addr);
	}

	/*
	 * Devices that supports 32 Bits upto 63 Bits DMA Address gets
	 * 32 Bits DMA addresses.
	 *
	 * First try to get 32 Bit Direct Map Support.
	 */
	if (IS_PCI32G(hwdev)) {
#ifdef CONFIG_IA64_SGI_SN1
		dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
			temp_ptr, size,
			PCIIO_BYTE_STREAM | PCIIO_DMA_DATA);
#else
		dma_addr = (dma_addr_t) pciio_dmatrans_addr(vhdl, NULL,
			temp_ptr, size, PCIIO_DMA_DATA);
#endif
		if (dma_addr) {
			return (dma_addr);
		}
	}

	if (IS_PCI32L(hwdev)) {
		/*
		 * SNIA64 cannot support DMA Addresses smaller than 32 bits.
		 */
		return ((dma_addr_t) NULL);
        }

	/*
	 * It is a 32bit card and we cannot do Direct mapping.
	 * Let's 32Bit Page map the request.
	 */
	dma_map = NULL;
#ifdef CONFIG_IA64_SGI_SN1
	dma_map = pciio_dmamap_alloc(vhdl, NULL, size, PCIIO_BYTE_STREAM | 
				     PCIIO_DMA_DATA);
#else
	dma_map = pciio_dmamap_alloc(vhdl, NULL, size, PCIIO_DMA_DATA);
#endif
	if (!dma_map) {
		printk("pci_map_single: Unable to allocate anymore 32Bits Page Map entries.\n");
		BUG();
	}

	dma_addr = (dma_addr_t) pciio_dmamap_addr(dma_map, temp_ptr, size);
	/* printk("pci_map_single: dma_map 0x%p Phys Addr 0x%p dma_addr 0x%p\n", dma_map,
		temp_ptr, dma_addr); */
	sn1_dma_map = (struct sn1_dma_maps_s *)dma_map;
	sn1_dma_map->dma_addr = dma_addr;

	return ((dma_addr_t)dma_addr);
}

void
sn1_pci_unmap_single (struct pci_dev *hwdev, dma_addr_t dma_addr, size_t size, int direction)
{

	struct sn1_dma_maps_s *sn1_dma_map = NULL;

        if (direction == PCI_DMA_NONE)
		BUG();

	/*
	 * Get the sn1_dma_map entry.
	 */
	if (IS_PCI32_MAPPED(dma_addr))
		sn1_dma_map = find_sn1_dma_map(dma_addr, hwdev->bus->number);

	if (sn1_dma_map) {
		pciio_dmamap_done((pciio_dmamap_t)sn1_dma_map);
		pciio_dmamap_free((pciio_dmamap_t)sn1_dma_map);
		sn1_dma_map->dma_addr = (dma_addr_t)NULL;
	}

}

void
sn1_pci_dma_sync_single (struct pci_dev *hwdev, dma_addr_t dma_handle, size_t size, int direction)
{
	
        if (direction == PCI_DMA_NONE)
                BUG();

	sn_dma_sync(hwdev);

}

void
sn1_pci_dma_sync_sg (struct pci_dev *hwdev, struct scatterlist *sg, int nelems, int direction)
{
        if (direction == PCI_DMA_NONE)
                BUG();

	sn_dma_sync(hwdev);

}

unsigned long
sn1_dma_address (struct scatterlist *sg)
{
	return ((unsigned long)sg->address);
}
