#ifndef _ASM_IA64_PCI_H
#define _ASM_IA64_PCI_H

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/scatterlist.h>

/*
 * Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes or
 * architectures with incomplete PCI setup by the loader.
 */
#define pcibios_assign_all_busses()     0

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

struct pci_dev;

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
#define sg_dma_address			platform_pci_dma_address

/*
 * Return whether the given PCI device DMA address mask can be supported properly.  For
 * example, if your device can only drive the low 24-bits during PCI bus mastering, then
 * you would pass 0x00ffffff as the mask to this function.
 */
static inline int
pci_dma_supported (struct pci_dev *hwdev, dma_addr_t mask)
{
	return 1;
}

#define sg_dma_len(sg)		((sg)->length)

#endif /* _ASM_IA64_PCI_H */
