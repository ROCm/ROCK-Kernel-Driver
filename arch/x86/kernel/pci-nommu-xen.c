#include <linux/dma-mapping.h>
#include <linux/dmar.h>
#include <linux/bootmem.h>
#include <linux/pci.h>

#include <xen/gnttab.h>

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
	      int direction)
{
	unsigned int i;
	struct scatterlist *sg;

	WARN_ON(nents == 0 || sgl->length == 0);

	for_each_sg(sgl, sg, nents, i) {
		BUG_ON(!sg_page(sg));
		sg->dma_address =
			gnttab_dma_map_page(sg_page(sg)) + sg->offset;
		sg->dma_length  = sg->length;
		IOMMU_BUG_ON(address_needs_mapping(
			hwdev, sg->dma_address));
		IOMMU_BUG_ON(range_straddles_page_boundary(
			page_to_pseudophys(sg_page(sg)) + sg->offset,
			sg->length));
	}

	return nents;
}

static void
gnttab_unmap_sg(struct device *hwdev, struct scatterlist *sgl, int nents,
		int direction)
{
	unsigned int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i)
		gnttab_dma_unmap_page(sg->dma_address);
}

static dma_addr_t
gnttab_map_single(struct device *dev, phys_addr_t paddr, size_t size,
		  int direction)
{
	dma_addr_t dma;

	WARN_ON(size == 0);

	dma = gnttab_dma_map_page(pfn_to_page(paddr >> PAGE_SHIFT)) +
	      offset_in_page(paddr);
	IOMMU_BUG_ON(range_straddles_page_boundary(paddr, size));
	IOMMU_BUG_ON(address_needs_mapping(dev, dma));

	return dma;
}

static void
gnttab_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		    int direction)
{
	gnttab_dma_unmap_page(dma_addr);
}

static int nommu_mapping_error(dma_addr_t dma_addr)
{
	return (dma_addr == bad_dma_address);
}

static const struct dma_mapping_ops nommu_dma_ops = {
	.map_single = gnttab_map_single,
	.unmap_single = gnttab_unmap_single,
	.map_sg = gnttab_map_sg,
	.unmap_sg = gnttab_unmap_sg,
	.dma_supported = swiotlb_dma_supported,
	.mapping_error = nommu_mapping_error
};

void __init no_iommu_init(void)
{
	if (dma_ops)
		return;

	force_iommu = 0; /* no HW IOMMU */
	dma_ops = &nommu_dma_ops;
}
