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

#ifdef CONFIG_PCI
extern unsigned int pcibios_assign_all_busses(void);
#else
#define pcibios_assign_all_busses()	0
#endif

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

static inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

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

#if defined(CONFIG_DDB5074) || defined(CONFIG_DDB5476)
#undef PCIBIOS_MIN_IO
#undef PCIBIOS_MIN_MEM
#define PCIBIOS_MIN_IO		0x0100000
#define PCIBIOS_MIN_MEM		0x1000000
#endif

struct pci_dev;

/*
 * The PCI address space does equal the physical memory address space.  The
 * networking and block device layers use this boolean for bounce buffer
 * decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)

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

static inline dma64_addr_t pci_dac_page_to_dma(struct pci_dev *pdev,
	struct page *page, unsigned long offset, int direction)
{
	dma64_addr_t addr = page_to_phys(page) + offset;

	return (dma64_addr_t) bus_to_baddr(pdev->bus, addr);
}

static inline struct page *pci_dac_dma_to_page(struct pci_dev *pdev,
	dma64_addr_t dma_addr)
{
	unsigned long poff = baddr_to_bus(pdev->bus, dma_addr) >> PAGE_SHIFT;

	return mem_map + poff;
}

static inline unsigned long pci_dac_dma_to_offset(struct pci_dev *pdev,
	dma64_addr_t dma_addr)
{
	return dma_addr & ~PAGE_MASK;
}

static inline void pci_dac_dma_sync_single(struct pci_dev *pdev,
	dma64_addr_t dma_addr, size_t len, int direction)
{
	unsigned long addr;

	if (direction == PCI_DMA_NONE)
		BUG();

	addr = baddr_to_bus(pdev->bus, dma_addr) + PAGE_OFFSET;
	dma_cache_wback_inv(addr, len);
}

#endif /* __KERNEL__ */

/* implement the pci_ DMA API in terms of the generic device dma_ one */
#include <asm-generic/pci-dma-compat.h>

/* generic pci stuff */
#include <asm-generic/pci.h>

#endif /* _ASM_PCI_H */
