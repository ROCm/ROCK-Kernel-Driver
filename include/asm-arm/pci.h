#ifndef ASMARM_PCI_H
#define ASMARM_PCI_H

#ifdef __KERNEL__
#include <linux/config.h>
#include <linux/dma-mapping.h>

#include <asm/hardware.h> /* for PCIBIOS_MIN_* */

#ifdef CONFIG_SA1111
/*
 * Keep the SA1111 DMA-mapping tricks until the USB layer gets
 * properly converted to the new DMA-mapping API, at which time
 * most of this file can die.
 */
#define SA1111_FAKE_PCIDEV ((struct pci_dev *) 1111)
#define pcidev_is_sa1111(dev) (dev == SA1111_FAKE_PCIDEV)
#else
#define pcidev_is_sa1111(dev) (0)
#endif


static inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

static inline void pcibios_penalize_isa_irq(int irq)
{
	/* We don't do dynamic PCI IRQ allocation */
}

/*
 * The PCI address space does equal the physical memory address space.
 * The networking and block device layers use this boolean for bounce
 * buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     (0)

static inline void *
pci_alloc_consistent(struct pci_dev *hwdev, size_t size, dma_addr_t *handle)
{
	int gfp = GFP_ATOMIC;

	if (hwdev == NULL || pcidev_is_sa1111(hwdev) ||
	    hwdev->dma_mask != 0xffffffff)
		gfp |= GFP_DMA;

	return consistent_alloc(gfp, size, handle, 0);
}

static inline void
pci_free_consistent(struct pci_dev *hwdev, size_t size, void *vaddr,
		    dma_addr_t handle)
{
	dma_free_coherent(hwdev ? &hwdev->dev : NULL, size, vaddr, handle);
}

static inline dma_addr_t
pci_map_single(struct pci_dev *hwdev, void *ptr, size_t size, int dir)
{
	if (pcidev_is_sa1111(hwdev))
		return sa1111_map_single(ptr, size, dir);

	return dma_map_single(hwdev ? &hwdev->dev : NULL, ptr, size, dir);
}

static inline void
pci_unmap_single(struct pci_dev *hwdev, dma_addr_t handle, size_t size, int dir)
{
	if (pcidev_is_sa1111(hwdev)) {
		sa1111_unmap_single(handle, size, dir);
		return;
	}

	return dma_unmap_single(hwdev ? &hwdev->dev : NULL, handle, size, dir);
}

static inline int
pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents, int dir)
{
	if (pcidev_is_sa1111(hwdev))
		return sa1111_map_sg(sg, nents, dir);

	return dma_map_sg(hwdev ? &hwdev->dev : NULL, sg, nents, dir);
}

static inline void
pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nents, int dir)
{
	if (pcidev_is_sa1111(hwdev)) {
		sa1111_unmap_sg(sg, nents, dir);
		return;
	}

	return dma_unmap_sg(hwdev ? &hwdev->dev : NULL, sg, nents, dir);
}

static inline dma_addr_t
pci_map_page(struct pci_dev *hwdev, struct page *page, unsigned long offset,
		size_t size, int dir)
{
	return	pci_map_single(hwdev, page_address(page) + offset, size, dir);
}

static inline void
pci_unmap_page(struct pci_dev *hwdev, dma_addr_t handle, size_t size, int dir)
{
	return pci_unmap_single(hwdev, handle, size, dir);
}

static inline void
pci_dma_sync_single(struct pci_dev *hwdev, dma_addr_t handle, size_t size, int dir)
{
	if (pcidev_is_sa1111(hwdev)) {
	  	sa1111_dma_sync_single(handle, size, dir);
		return;
	}

	return dma_sync_single(hwdev ? &hwdev->dev : NULL, handle, size, dir);
}

static inline void
pci_dma_sync_sg(struct pci_dev *hwdev, struct scatterlist *sg, int nelems, int dir)
{
	if (pcidev_is_sa1111(hwdev)) {
	  	sa1111_dma_sync_sg(sg, nelems, dir);
		return;
	}

	return dma_sync_sg(hwdev ? &hwdev->dev : NULL, sg, nelems, dir);
}

static inline int pci_dma_supported(struct pci_dev *hwdev, u64 mask)
{
	return 1;
}

/*
 * We don't support DAC DMA cycles.
 */
#define pci_dac_dma_supported(pci_dev, mask)	(0)


#if defined(CONFIG_SA1111) && !defined(CONFIG_PCI)
/*
 * SA-1111 needs these prototypes even when !defined(CONFIG_PCI)
 *
 * kmem_cache style wrapper around pci_alloc_consistent()
 */
struct pci_pool *pci_pool_create (const char *name, struct pci_dev *dev,
		size_t size, size_t align, size_t allocation);
void pci_pool_destroy (struct pci_pool *pool);

void *pci_pool_alloc (struct pci_pool *pool, int flags, dma_addr_t *handle);
void pci_pool_free (struct pci_pool *pool, void *vaddr, dma_addr_t addr);
#endif

/*
 * Whether pci_unmap_{single,page} is a nop depends upon the
 * configuration.
 */
#if defined(CONFIG_PCI) || defined(CONFIG_SA1111)
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)	dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)		__u32 LEN_NAME;
#define pci_unmap_addr(PTR, ADDR_NAME)		((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	(((PTR)->ADDR_NAME) = (VAL))
#define pci_unmap_len(PTR, LEN_NAME)		((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	(((PTR)->LEN_NAME) = (VAL))
#else
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)
#define pci_unmap_addr(PTR, ADDR_NAME)		(0)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	do { } while (0)
#define pci_unmap_len(PTR, LEN_NAME)		(0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	do { } while (0)
#endif

#define HAVE_PCI_MMAP
extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
                               enum pci_mmap_state mmap_state, int write_combine);

extern void
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			 struct resource *res);

#endif /* __KERNEL__ */
 
#endif
