#ifndef _ASM_IA64_PCI_H
#define _ASM_IA64_PCI_H

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/scatterlist.h>

/*
 * Can be used to override the logic in pci_scan_bus for skipping already-configured bus
 * numbers - to be used for buggy BIOSes or architectures with incomplete PCI setup by the
 * loader.
 */
#define pcibios_assign_all_busses()     0

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

void pcibios_config_init(void);
struct pci_bus * pcibios_scan_root(void *acpi_handle, int segment, int bus);

struct pci_dev;

/*
 * The PCI address space does equal the physical memory address space.
 * The networking and block device layers use this boolean for bounce
 * buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)

static inline void
pcibios_set_master (struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

static inline void
pcibios_penalize_isa_irq (int irq)
{
	/* We don't do dynamic PCI IRQ allocation */
}

#define HAVE_ARCH_PCI_MWI 1
extern int pcibios_prep_mwi (struct pci_dev *);

/*
 * Dynamic DMA mapping API.  See Documentation/DMA-mapping.txt for details.
 */
#define pci_alloc_consistent		platform_pci_alloc_consistent
#define pci_free_consistent		platform_pci_free_consistent
#define pci_map_single			platform_pci_map_single
#define pci_unmap_single		platform_pci_unmap_single
#define pci_map_sg			platform_pci_map_sg
#define pci_unmap_sg			platform_pci_unmap_sg
#define pci_dma_sync_single		platform_pci_dma_sync_single
#define pci_dma_sync_sg			platform_pci_dma_sync_sg
#define pci_dma_supported		platform_pci_dma_supported

/* pci_unmap_{single,page} is not a nop, thus... */
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)	\
	dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)		\
	__u32 LEN_NAME;
#define pci_unmap_addr(PTR, ADDR_NAME)			\
	((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)		\
	(((PTR)->ADDR_NAME) = (VAL))
#define pci_unmap_len(PTR, LEN_NAME)			\
	((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)		\
	(((PTR)->LEN_NAME) = (VAL))

#define pci_map_page(dev,pg,off,size,dir)				\
	pci_map_single((dev), page_address(pg) + (off), (size), (dir))
#define pci_unmap_page(dev,dma_addr,size,dir)				\
	pci_unmap_single((dev), (dma_addr), (size), (dir))

/* The ia64 platform always supports 64-bit addressing. */
#define pci_dac_dma_supported(pci_dev, mask)	(1)

#define pci_dac_page_to_dma(dev,pg,off,dir)	((dma_addr_t) page_to_bus(pg) + (off))
#define pci_dac_dma_to_page(dev,dma_addr)	(virt_to_page(bus_to_virt(dma_addr)))
#define pci_dac_dma_to_offset(dev,dma_addr)	((dma_addr) & ~PAGE_MASK)
#define pci_dac_dma_sync_single(dev,dma_addr,len,dir)	do { /* nothing */ } while (0)

/* Return the index of the PCI controller for device PDEV. */
#define pci_controller_num(PDEV)	(0)

#define sg_dma_len(sg)		((sg)->dma_length)
#define sg_dma_address(sg)	((sg)->dma_address)

#define HAVE_PCI_MMAP
extern int pci_mmap_page_range (struct pci_dev *dev, struct vm_area_struct *vma,
				enum pci_mmap_state mmap_state, int write_combine);

struct pci_controller {
	void *acpi_handle;
	void *iommu;
	int segment;

	u64 mem_offset;
};

#define PCI_CONTROLLER(busdev) ((struct pci_controller *) busdev->sysdata)
#define PCI_SEGMENT(busdev)    (PCI_CONTROLLER(busdev)->segment)

/* generic pci stuff */
#include <asm-generic/pci.h>

#endif /* _ASM_IA64_PCI_H */
