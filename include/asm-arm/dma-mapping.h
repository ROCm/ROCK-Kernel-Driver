#ifndef ASMARM_DMA_MAPPING_H
#define ASMARM_DMA_MAPPING_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/mm.h> /* need struct page */

#include <asm/scatterlist.h>

/*
 * DMA-consistent mapping functions.  These allocate/free a region of
 * uncached, unwrite-buffered mapped memory space for use with DMA
 * devices.  This is the "generic" version.  The PCI specific version
 * is in pci.h
 */
extern void *consistent_alloc(int gfp, size_t size, dma_addr_t *handle, unsigned long flags);
extern void consistent_free(void *vaddr, size_t size, dma_addr_t handle);
extern void consistent_sync(void *kaddr, size_t size, int rw);

/*
 * For SA-1111 these functions are "magic" and utilize bounce
 * bufferes as needed to work around SA-1111 DMA bugs.
 */
dma_addr_t sa1111_map_single(void *, size_t, int);
void sa1111_unmap_single(dma_addr_t, size_t, int);
int sa1111_map_sg(struct scatterlist *, int, int);
void sa1111_unmap_sg(struct scatterlist *, int, int);
void sa1111_dma_sync_single(dma_addr_t, size_t, int);
void sa1111_dma_sync_sg(struct scatterlist *, int, int);

#ifdef CONFIG_SA1111

extern struct bus_type sa1111_bus_type;

#define dmadev_is_sa1111(dev)	((dev)->bus == &sa1111_bus_type)

#else
#define dmadev_is_sa1111(dev)	(0)
#endif

/*
 * Return whether the given device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during bus mastering, then you would pass 0x00ffffff as the mask
 * to this function.
 */
static inline int dma_supported(struct device *dev, u64 mask)
{
	return dev->dma_mask && *dev->dma_mask != 0;
}

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

static inline int dma_get_cache_alignment(void)
{
	return 32;
}

static inline int dma_is_consistent(dma_addr_t handle)
{
	return 0;
}

/**
 * dma_alloc_coherent - allocate consistent memory for DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: required memory size
 * @handle: bus-specific DMA address
 *
 * Allocate some uncached, unbuffered memory for a device for
 * performing DMA.  This function allocates pages, and will
 * return the CPU-viewed address, and sets @handle to be the
 * device-viewed address.
 */
extern void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, int gfp);

/**
 * dma_free_coherent - free memory allocated by dma_alloc_coherent
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @size: size of memory originally requested in dma_alloc_coherent
 * @cpu_addr: CPU-view address returned from dma_alloc_coherent
 * @handle: device-view address returned from dma_alloc_coherent
 *
 * Free (and unmap) a DMA buffer previously allocated by
 * dma_alloc_coherent().
 *
 * References to memory and mappings associated with cpu_addr/handle
 * during and after this call executing are illegal.
 */
static inline void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		  dma_addr_t handle)
{
	consistent_free(cpu_addr, size, handle);
}

/**
 * dma_map_single - map a single buffer for streaming DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @cpu_addr: CPU direct mapped address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Ensure that any data held in the cache is appropriately discarded
 * or written back.
 *
 * The device owns this memory once this call has completed.  The CPU
 * can regain ownership by calling dma_unmap_single() or dma_sync_single().
 */
static inline dma_addr_t
dma_map_single(struct device *dev, void *cpu_addr, size_t size,
	       enum dma_data_direction dir)
{
	if (dmadev_is_sa1111(dev))
		return sa1111_map_single(cpu_addr, size, dir);

	consistent_sync(cpu_addr, size, dir);
	return __virt_to_bus((unsigned long)cpu_addr);
}

/**
 * dma_map_page - map a portion of a page for streaming DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @page: page that buffer resides in
 * @offset: offset into page for start of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Ensure that any data held in the cache is appropriately discarded
 * or written back.
 *
 * The device owns this memory once this call has completed.  The CPU
 * can regain ownership by calling dma_unmap_page() or dma_sync_single().
 */
static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size,
	     enum dma_data_direction dir)
{
	return dma_map_single(dev, page_address(page) + offset, size, (int)dir);
}

/**
 * dma_unmap_single - unmap a single buffer previously mapped
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Unmap a single streaming mode DMA translation.  The handle and size
 * must match what was provided in the previous dma_map_single() call.
 * All other usages are undefined.
 *
 * After this call, reads by the CPU to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static inline void
dma_unmap_single(struct device *dev, dma_addr_t handle, size_t size,
		 enum dma_data_direction dir)
{
	if (dmadev_is_sa1111(dev))
		sa1111_unmap_single(handle, size, dir);

	/* nothing to do */
}

/**
 * dma_unmap_page - unmap a buffer previously mapped through dma_map_page()
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Unmap a single streaming mode DMA translation.  The handle and size
 * must match what was provided in the previous dma_map_single() call.
 * All other usages are undefined.
 *
 * After this call, reads by the CPU to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static inline void
dma_unmap_page(struct device *dev, dma_addr_t handle, size_t size,
	       enum dma_data_direction dir)
{
	dma_unmap_single(dev, handle, size, (int)dir);
}

/**
 * dma_map_sg - map a set of SG buffers for streaming mode DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scatter-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction dir)
{
	int i;

	if (dmadev_is_sa1111(dev))
		return sa1111_map_sg(sg, nents, dir);

	for (i = 0; i < nents; i++, sg++) {
		char *virt;

		sg->dma_address = page_to_bus(sg->page) + sg->offset;
		virt = page_address(sg->page) + sg->offset;
		consistent_sync(virt, sg->length, dir);
	}

	return nents;
}

/**
 * dma_unmap_sg - unmap a set of SG buffers mapped by dma_map_sg
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Unmap a set of streaming mode DMA translations.
 * Again, CPU read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
	     enum dma_data_direction dir)
{
	if (dmadev_is_sa1111(dev)) {
		sa1111_unmap_sg(sg, nents, dir);
		return;
	}

	/* nothing to do */
}

/**
 * dma_sync_single
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Make physical memory consistent for a single streaming mode DMA
 * translation after a transfer.
 *
 * If you perform a pci_map_single() but wish to interrogate the
 * buffer using the cpu, yet do not wish to teardown the PCI dma
 * mapping, you must call this function before doing so.  At the
 * next point you give the PCI dma address back to the card, the
 * device again owns the buffer.
 */
static inline void
dma_sync_single(struct device *dev, dma_addr_t handle, size_t size,
		enum dma_data_direction dir)
{
	if (dmadev_is_sa1111(dev)) {
		sa1111_dma_sync_single(handle, size, dir);
		return;
	}

	consistent_sync((void *)__bus_to_virt(handle), size, dir);
}

/**
 * dma_sync_sg
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Make physical memory consistent for a set of streaming
 * mode DMA translations after a transfer.
 *
 * The same as pci_dma_sync_single but for a scatter-gather list,
 * same rules and usage.
 */
static inline void
dma_sync_sg(struct device *dev, struct scatterlist *sg, int nents,
	    enum dma_data_direction dir)
{
	int i;

	if (dmadev_is_sa1111(dev)) {
		sa1111_dma_sync_sg(sg, nents, dir);
		return;
	}

	for (i = 0; i < nents; i++, sg++) {
		char *virt = page_address(sg->page) + sg->offset;
		consistent_sync(virt, sg->length, dir);
	}
}

#endif /* __KERNEL__ */
#endif
