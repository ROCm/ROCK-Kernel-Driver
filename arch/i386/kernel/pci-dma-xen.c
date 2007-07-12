/*
 * Dynamic DMA mapping support.
 *
 * On i386 there is no hardware dynamic DMA address translation,
 * so consistent alloc/free are merely page allocation/freeing.
 * The rest of the dynamic DMA mapping interface is implemented
 * in asm/pci.h.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <xen/balloon.h>
#include <xen/gnttab.h>
#include <asm/swiotlb.h>
#include <asm/tlbflush.h>
#include <asm-i386/mach-xen/asm/swiotlb.h>
#include <asm-i386/mach-xen/asm/gnttab_dma.h>
#include <asm/bug.h>

#ifdef __x86_64__
#include <asm/proto.h>

int iommu_merge __read_mostly = 0;
EXPORT_SYMBOL(iommu_merge);

dma_addr_t bad_dma_address __read_mostly;
EXPORT_SYMBOL(bad_dma_address);

/* This tells the BIO block layer to assume merging. Default to off
   because we cannot guarantee merging later. */
int iommu_bio_merge __read_mostly = 0;
EXPORT_SYMBOL(iommu_bio_merge);

int force_iommu __read_mostly= 0;

__init int iommu_setup(char *p)
{
    return 1;
}

void __init pci_iommu_alloc(void)
{
#ifdef CONFIG_SWIOTLB
	pci_swiotlb_init();
#endif
}

static int __init pci_iommu_init(void)
{
	no_iommu_init();
	return 0;
}

/* Must execute after PCI subsystem */
fs_initcall(pci_iommu_init);
#endif

struct dma_coherent_mem {
	void		*virt_base;
	u32		device_base;
	int		size;
	int		flags;
	unsigned long	*bitmap;
};

#define IOMMU_BUG_ON(test)				\
do {							\
	if (unlikely(test)) {				\
		printk(KERN_ALERT "Fatal DMA error! "	\
		       "Please use 'swiotlb=force'\n");	\
		BUG();					\
	}						\
} while (0)

int
dma_map_sg(struct device *hwdev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	int i, rc;

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(nents == 0 || sg[0].length == 0);

	if (swiotlb) {
		rc = swiotlb_map_sg(hwdev, sg, nents, direction);
	} else {
		for (i = 0; i < nents; i++ ) {
			sg[i].dma_address =
				gnttab_dma_map_page(sg[i].page) + sg[i].offset;
			sg[i].dma_length  = sg[i].length;
			BUG_ON(!sg[i].page);
			IOMMU_BUG_ON(address_needs_mapping(
				hwdev, sg[i].dma_address));
			IOMMU_BUG_ON(range_straddles_page_boundary(
				page_to_pseudophys(sg[i].page) + sg[i].offset,
				sg[i].length));
		}
		rc = nents;
	}

	flush_write_buffers();
	return rc;
}
EXPORT_SYMBOL(dma_map_sg);

void
dma_unmap_sg(struct device *hwdev, struct scatterlist *sg, int nents,
	     enum dma_data_direction direction)
{
	int i;

	BUG_ON(!valid_dma_direction(direction));
	if (swiotlb)
		swiotlb_unmap_sg(hwdev, sg, nents, direction);
	else {
		for (i = 0; i < nents; i++ )
			gnttab_dma_unmap_page(sg[i].dma_address);
	}
}
EXPORT_SYMBOL(dma_unmap_sg);

#ifdef CONFIG_HIGHMEM
dma_addr_t
dma_map_page(struct device *dev, struct page *page, unsigned long offset,
	     size_t size, enum dma_data_direction direction)
{
	dma_addr_t dma_addr;

	BUG_ON(!valid_dma_direction(direction));
	if (swiotlb) {
		dma_addr = swiotlb_map_page(
			dev, page, offset, size, direction);
	} else {
		dma_addr = gnttab_dma_map_page(page) + offset;
		IOMMU_BUG_ON(address_needs_mapping(dev, dma_addr));
	}

	return dma_addr;
}
EXPORT_SYMBOL(dma_map_page);

void
dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (swiotlb)
		swiotlb_unmap_page(dev, dma_address, size, direction);
	else
		gnttab_dma_unmap_page(dma_address);
}
EXPORT_SYMBOL(dma_unmap_page);
#endif /* CONFIG_HIGHMEM */

int
dma_mapping_error(dma_addr_t dma_addr)
{
	if (swiotlb)
		return swiotlb_dma_mapping_error(dma_addr);
	return 0;
}
EXPORT_SYMBOL(dma_mapping_error);

int
dma_supported(struct device *dev, u64 mask)
{
	if (swiotlb)
		return swiotlb_dma_supported(dev, mask);
	/*
	 * By default we'll BUG when an infeasible DMA is requested, and
	 * request swiotlb=force (see IOMMU_BUG_ON).
	 */
	return 1;
}
EXPORT_SYMBOL(dma_supported);

void *dma_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
	unsigned int order = get_order(size);
	unsigned long vstart;
	u64 mask;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (mem) {
		int page = bitmap_find_free_region(mem->bitmap, mem->size,
						     order);
		if (page >= 0) {
			*dma_handle = mem->device_base + (page << PAGE_SHIFT);
			ret = mem->virt_base + (page << PAGE_SHIFT);
			memset(ret, 0, size);
			return ret;
		}
		if (mem->flags & DMA_MEMORY_EXCLUSIVE)
			return NULL;
	}

	if (dev == NULL || (dev->coherent_dma_mask < 0xffffffff))
		gfp |= GFP_DMA;

	vstart = __get_free_pages(gfp, order);
	ret = (void *)vstart;

	if (dev != NULL && dev->coherent_dma_mask)
		mask = dev->coherent_dma_mask;
	else
		mask = 0xffffffff;

	if (ret != NULL) {
		if (xen_create_contiguous_region(vstart, order,
						 fls64(mask)) != 0) {
			free_pages(vstart, order);
			return NULL;
		}
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}
	return ret;
}
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
	int order = get_order(size);
	
	if (mem && vaddr >= mem->virt_base && vaddr < (mem->virt_base + (mem->size << PAGE_SHIFT))) {
		int page = (vaddr - mem->virt_base) >> PAGE_SHIFT;

		bitmap_release_region(mem->bitmap, page, order);
	} else {
		xen_destroy_contiguous_region((unsigned long)vaddr, order);
		free_pages((unsigned long)vaddr, order);
	}
}
EXPORT_SYMBOL(dma_free_coherent);

#ifdef ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY
int dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
				dma_addr_t device_addr, size_t size, int flags)
{
	void __iomem *mem_base = NULL;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

	if ((flags & (DMA_MEMORY_MAP | DMA_MEMORY_IO)) == 0)
		goto out;
	if (!size)
		goto out;
	if (dev->dma_mem)
		goto out;

	/* FIXME: this routine just ignores DMA_MEMORY_INCLUDES_CHILDREN */

	mem_base = ioremap(bus_addr, size);
	if (!mem_base)
		goto out;

	dev->dma_mem = kzalloc(sizeof(struct dma_coherent_mem), GFP_KERNEL);
	if (!dev->dma_mem)
		goto out;
	dev->dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!dev->dma_mem->bitmap)
		goto free1_out;

	dev->dma_mem->virt_base = mem_base;
	dev->dma_mem->device_base = device_addr;
	dev->dma_mem->size = pages;
	dev->dma_mem->flags = flags;

	if (flags & DMA_MEMORY_MAP)
		return DMA_MEMORY_MAP;

	return DMA_MEMORY_IO;

 free1_out:
	kfree(dev->dma_mem);
 out:
	if (mem_base)
		iounmap(mem_base);
	return 0;
}
EXPORT_SYMBOL(dma_declare_coherent_memory);

void dma_release_declared_memory(struct device *dev)
{
	struct dma_coherent_mem *mem = dev->dma_mem;
	
	if(!mem)
		return;
	dev->dma_mem = NULL;
	iounmap(mem->virt_base);
	kfree(mem->bitmap);
	kfree(mem);
}
EXPORT_SYMBOL(dma_release_declared_memory);

void *dma_mark_declared_memory_occupied(struct device *dev,
					dma_addr_t device_addr, size_t size)
{
	struct dma_coherent_mem *mem = dev->dma_mem;
	int pages = (size + (device_addr & ~PAGE_MASK) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int pos, err;

	if (!mem)
		return ERR_PTR(-EINVAL);

	pos = (device_addr - mem->device_base) >> PAGE_SHIFT;
	err = bitmap_allocate_region(mem->bitmap, pos, get_order(pages));
	if (err != 0)
		return ERR_PTR(err);
	return mem->virt_base + (pos << PAGE_SHIFT);
}
EXPORT_SYMBOL(dma_mark_declared_memory_occupied);
#endif /* ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY */

#if defined(CONFIG_PCI) && !defined(CONFIG_XEN)
/* Many VIA bridges seem to corrupt data for DAC. Disable it here */

int forbid_dac;
EXPORT_SYMBOL(forbid_dac);

static __devinit void via_no_dac(struct pci_dev *dev)
{
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI && forbid_dac == 0) {
		printk(KERN_INFO "PCI: VIA PCI bridge detected. Disabling DAC.\n");
		forbid_dac = 1;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_ANY_ID, via_no_dac);

static int check_iommu(char *s)
{
	if (!strcmp(s, "usedac")) {
		forbid_dac = -1;
		return 1;
	}
	return 0;
}
__setup("iommu=", check_iommu);
#endif

dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
	       enum dma_data_direction direction)
{
	dma_addr_t dma;

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(size == 0);

	if (swiotlb) {
		dma = swiotlb_map_single(dev, ptr, size, direction);
	} else {
		dma = gnttab_dma_map_page(virt_to_page(ptr)) +
		      offset_in_page(ptr);
		IOMMU_BUG_ON(range_straddles_page_boundary(__pa(ptr), size));
		IOMMU_BUG_ON(address_needs_mapping(dev, dma));
	}

	flush_write_buffers();
	return dma;
}
EXPORT_SYMBOL(dma_map_single);

void
dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (swiotlb)
		swiotlb_unmap_single(dev, dma_addr, size, direction);
	else
		gnttab_dma_unmap_page(dma_addr);
}
EXPORT_SYMBOL(dma_unmap_single);

void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle, size_t size,
			enum dma_data_direction direction)
{
	if (swiotlb)
		swiotlb_sync_single_for_cpu(dev, dma_handle, size, direction);
}
EXPORT_SYMBOL(dma_sync_single_for_cpu);

void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle, size_t size,
                           enum dma_data_direction direction)
{
	if (swiotlb)
		swiotlb_sync_single_for_device(dev, dma_handle, size, direction);
}
EXPORT_SYMBOL(dma_sync_single_for_device);
