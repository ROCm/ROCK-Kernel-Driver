/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_PCI_H
#define _ASM_PCI_H

#include <linux/config.h>
#include <linux/mm.h>

#ifdef __KERNEL__

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

extern unsigned int pcibios_assign_all_busses(void);

#define pcibios_scan_all_fns(a, b)	0

extern unsigned long PCIBIOS_MIN_IO;
extern unsigned long PCIBIOS_MIN_MEM;

#define PCIBIOS_MIN_CARDBUS_IO	0x4000

extern void pcibios_set_master(struct pci_dev *dev);

static inline void pcibios_penalize_isa_irq(int irq)
{
	/* We don't do dynamic PCI IRQ allocation */
}

/*
 * Dynamic DMA mapping stuff.
 * MIPS has everything mapped statically.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

struct pci_dev;

/*
 * The PCI address space does equal the physical memory address space.  The
 * networking and block device layers use this boolean for bounce buffer
 * decisions.  This is set if any hose does not have an IOMMU.
 */
extern unsigned int PCI_DMA_BUS_IS_PHYS;

#ifdef CONFIG_MAPPED_DMA_IO

/* pci_unmap_{single,page} is not a nop, thus... */
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)	dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)		__u32 LEN_NAME;
#define pci_unmap_addr(PTR, ADDR_NAME)		((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	(((PTR)->ADDR_NAME) = (VAL))
#define pci_unmap_len(PTR, LEN_NAME)		((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	(((PTR)->LEN_NAME) = (VAL))

#else /* CONFIG_MAPPED_DMA_IO  */

/* pci_unmap_{page,single} is a nop so... */
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)
#define pci_unmap_addr(PTR, ADDR_NAME)		(0)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	do { } while (0)
#define pci_unmap_len(PTR, LEN_NAME)		(0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	do { } while (0)

#endif /* CONFIG_MAPPED_DMA_IO  */

/* This is always fine. */
#define pci_dac_dma_supported(pci_dev, mask)	(1)

extern dma64_addr_t pci_dac_page_to_dma(struct pci_dev *pdev,
	struct page *page, unsigned long offset, int direction);
extern struct page *pci_dac_dma_to_page(struct pci_dev *pdev,
	dma64_addr_t dma_addr);
extern unsigned long pci_dac_dma_to_offset(struct pci_dev *pdev,
	dma64_addr_t dma_addr);
extern void pci_dac_dma_sync_single_for_cpu(struct pci_dev *pdev,
	dma64_addr_t dma_addr, size_t len, int direction);
extern void pci_dac_dma_sync_single_for_device(struct pci_dev *pdev,
	dma64_addr_t dma_addr, size_t len, int direction);

extern void pcibios_resource_to_bus(struct pci_dev *dev,
	struct pci_bus_region *region, struct resource *res);

#endif /* __KERNEL__ */

/* implement the pci_ DMA API in terms of the generic device dma_ one */
#include <asm-generic/pci-dma-compat.h>

static inline void pcibios_add_platform_entries(struct pci_dev *dev)
{
}

#endif /* _ASM_PCI_H */
