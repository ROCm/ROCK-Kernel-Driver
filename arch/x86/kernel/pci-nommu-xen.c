#include <linux/dma-mapping.h>
#include <linux/dmar.h>
#include <linux/bootmem.h>
#include <linux/pci.h>

#include <xen/gnttab.h>

#include <asm/iommu.h>
#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/swiotlb.h>
#include <asm/tlbflush.h>
#include <asm/gnttab_dma.h>
#include <asm/bug.h>

#define IOMMU_BUG_ON(test)				\
do {							\
	if (unlikely(test)) {				\
		printk(KERN_ALERT "Fatal DMA error! "	\
		       "Please use 'swiotlb=force'\n");	\
		BUG();					\
	}						\
} while (0)

static int
gnttab_map_sg(struct device *hwdev, struct scatterlist *sgl, int nents,
	      enum dma_data_direction dir, struct dma_attrs *attrs)
{
	unsigned int i;
	struct scatterlist *sg;

	WARN_ON(nents == 0 || sgl->length == 0);

	for_each_sg(sgl, sg, nents, i) {
		BUG_ON(!sg_page(sg));
		sg->dma_address =
			gnttab_dma_map_page(sg_page(sg)) + sg->offset;
		sg->dma_length  = sg->length;
		IOMMU_BUG_ON(!dma_capable(
			hwdev, sg->dma_address, sg->length));
		IOMMU_BUG_ON(range_straddles_page_boundary(
			page_to_pseudophys(sg_page(sg)) + sg->offset,
			sg->length));
	}

	return nents;
}

static void
gnttab_unmap_sg(struct device *hwdev, struct scatterlist *sgl, int nents,
		enum dma_data_direction dir, struct dma_attrs *attrs)
{
	unsigned int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i)
		gnttab_dma_unmap_page(sg->dma_address);
}

static dma_addr_t
gnttab_map_page(struct device *dev, struct page *page, unsigned long offset,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	dma_addr_t dma;

	WARN_ON(size == 0);

	dma = gnttab_dma_map_page(page) + offset;
	IOMMU_BUG_ON(range_straddles_page_boundary(offset, size));
	IOMMU_BUG_ON(!dma_capable(dev, dma, size));

	return dma;
}

static void
gnttab_unmap_page(struct device *dev, dma_addr_t dma_addr, size_t size,
		  enum dma_data_direction dir, struct dma_attrs *attrs)
{
	gnttab_dma_unmap_page(dma_addr);
}

static void nommu_sync_single_for_device(struct device *dev,
			dma_addr_t addr, size_t size,
			enum dma_data_direction dir)
{
	flush_write_buffers();
}


static void nommu_sync_sg_for_device(struct device *dev,
			struct scatterlist *sg, int nelems,
			enum dma_data_direction dir)
{
	flush_write_buffers();
}

struct dma_map_ops nommu_dma_ops = {
	.alloc_coherent		= dma_generic_alloc_coherent,
	.free_coherent		= dma_generic_free_coherent,
	.map_page		= gnttab_map_page,
	.unmap_page		= gnttab_unmap_page,
	.map_sg			= gnttab_map_sg,
	.unmap_sg		= gnttab_unmap_sg,
	.sync_single_for_device = nommu_sync_single_for_device,
	.sync_sg_for_device	= nommu_sync_sg_for_device,
	.dma_supported		= swiotlb_dma_supported,
};

void __init no_iommu_init(void)
{
	if (dma_ops)
		return;

	force_iommu = 0; /* no HW IOMMU */
	dma_ops = &nommu_dma_ops;
}
