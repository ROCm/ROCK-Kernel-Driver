#ifndef _ASM_LINUX_DMA_MAPPING_H
#define _ASM_LINUX_DMA_MAPPING_H

#include <linux/err.h>

/* These definitions mirror those in pci.h, so they can be used
 * interchangeably with their PCI_ counterparts */
enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};

#define DMA_64BIT_MASK	0xffffffffffffffffULL
#define DMA_32BIT_MASK	0x00000000ffffffffULL

#include <asm/dma-mapping.h>

/* Backwards compat, remove in 2.7.x */
#define dma_sync_single		dma_sync_single_for_cpu
#define dma_sync_sg		dma_sync_sg_for_cpu

#ifndef ARCH_HAS_DMA_GET_REQUIRED_MASK
static inline u64 dma_get_required_mask(struct device *dev)
{
	extern unsigned long max_pfn; /* defined in bootmem.h but may
					 not be included */
	u32 low_totalram = ((max_pfn - 1) << PAGE_SHIFT);
	u32 high_totalram = ((max_pfn - 1) >> (32 - PAGE_SHIFT));
	u64 mask;

	if (!high_totalram) {
		/* convert to mask just covering totalram */
		low_totalram = (1 << (fls(low_totalram) - 1));
		low_totalram += low_totalram - 1;
		mask = low_totalram;
	} else {
		high_totalram = (1 << (fls(high_totalram) - 1));
		high_totalram += high_totalram - 1;
		mask = (((u64)high_totalram) << 32) + 0xffffffff;
	}
	return mask & *dev->dma_mask;
}
#endif

/* flags for the coherent memory api */
#define	DMA_MEMORY_MAP			0x01
#define DMA_MEMORY_IO			0x02
#define DMA_MEMORY_INCLUDES_CHILDREN	0x04
#define DMA_MEMORY_EXCLUSIVE		0x08

#ifndef ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY
static inline int
dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
			    dma_addr_t device_addr, size_t size, int flags)
{
	return 0;
}

static inline void
dma_release_declared_memory(struct device *dev)
{
}

static inline void *
dma_mark_declared_memory_occupied(struct device *dev,
				  dma_addr_t device_addr, size_t size)
{
	return ERR_PTR(-EBUSY);
}
#endif

#endif


