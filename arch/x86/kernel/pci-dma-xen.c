#include <linux/dma-mapping.h>
#include <linux/dmar.h>
#include <linux/bootmem.h>
#include <linux/pci.h>

#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/calgary.h>
#include <asm/amd_iommu.h>

static int forbid_dac __read_mostly;

struct dma_mapping_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

static int iommu_sac_force __read_mostly;

#ifdef CONFIG_IOMMU_DEBUG
int panic_on_overflow __read_mostly = 1;
int force_iommu __read_mostly = 1;
#else
int panic_on_overflow __read_mostly = 0;
int force_iommu __read_mostly = 0;
#endif

int iommu_merge __read_mostly = 0;

int no_iommu __read_mostly;
/* Set this to 1 if there is a HW IOMMU in the system */
int iommu_detected __read_mostly = 0;

/* This tells the BIO block layer to assume merging. Default to off
   because we cannot guarantee merging later. */
int iommu_bio_merge __read_mostly = 0;
EXPORT_SYMBOL(iommu_bio_merge);

dma_addr_t bad_dma_address __read_mostly = 0;
EXPORT_SYMBOL(bad_dma_address);

/* Dummy device used for NULL arguments (normally ISA). Better would
   be probably a smaller DMA mask, but this is bug-to-bug compatible
   to older i386. */
struct device fallback_dev = {
	.bus_id = "fallback device",
	.coherent_dma_mask = DMA_32BIT_MASK,
	.dma_mask = &fallback_dev.coherent_dma_mask,
};

int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}
EXPORT_SYMBOL(dma_set_mask);

#if defined(CONFIG_X86_64) && !defined(CONFIG_XEN)
static __initdata void *dma32_bootmem_ptr;
static unsigned long dma32_bootmem_size __initdata = (128ULL<<20);

static int __init parse_dma32_size_opt(char *p)
{
	if (!p)
		return -EINVAL;
	dma32_bootmem_size = memparse(p, &p);
	return 0;
}
early_param("dma32_size", parse_dma32_size_opt);

void __init dma32_reserve_bootmem(void)
{
	unsigned long size, align;
	if (max_pfn <= MAX_DMA32_PFN)
		return;

	/*
	 * check aperture_64.c allocate_aperture() for reason about
	 * using 512M as goal
	 */
	align = 64ULL<<20;
	size = round_up(dma32_bootmem_size, align);
	dma32_bootmem_ptr = __alloc_bootmem_nopanic(size, align,
				 512ULL<<20);
	if (dma32_bootmem_ptr)
		dma32_bootmem_size = size;
	else
		dma32_bootmem_size = 0;
}
static void __init dma32_free_bootmem(void)
{

	if (max_pfn <= MAX_DMA32_PFN)
		return;

	if (!dma32_bootmem_ptr)
		return;

	free_bootmem(__pa(dma32_bootmem_ptr), dma32_bootmem_size);

	dma32_bootmem_ptr = NULL;
	dma32_bootmem_size = 0;
}
#else
#define dma32_free_bootmem() ((void)0)
#endif

static struct dma_mapping_ops swiotlb_dma_ops = {
	.mapping_error = swiotlb_dma_mapping_error,
	.map_single = swiotlb_map_single_phys,
	.unmap_single = swiotlb_unmap_single,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_single_range_for_cpu = swiotlb_sync_single_range_for_cpu,
	.sync_single_range_for_device = swiotlb_sync_single_range_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.map_sg = swiotlb_map_sg,
	.unmap_sg = swiotlb_unmap_sg,
	.dma_supported = swiotlb_dma_supported
};

void __init pci_iommu_alloc(void)
{
	/* free the range so iommu could get some range less than 4G */
	dma32_free_bootmem();
	/*
	 * The order of these functions is important for
	 * fall-back/fail-over reasons
	 */
	gart_iommu_hole_init();

	detect_calgary();

	detect_intel_iommu();

	amd_iommu_detect();

	swiotlb_init();
	if (swiotlb) {
		printk(KERN_INFO "PCI-DMA: Using software bounce buffering for IO (SWIOTLB)\n");
		dma_ops = &swiotlb_dma_ops;
	}
}

#ifndef CONFIG_XEN
unsigned long iommu_num_pages(unsigned long addr, unsigned long len)
{
	unsigned long size = roundup((addr & ~PAGE_MASK) + len, PAGE_SIZE);

	return size >> PAGE_SHIFT;
}
EXPORT_SYMBOL(iommu_num_pages);
#endif

/*
 * See <Documentation/x86_64/boot-options.txt> for the iommu kernel parameter
 * documentation.
 */
static __init int iommu_setup(char *p)
{
	iommu_merge = 1;

	if (!p)
		return -EINVAL;

	while (*p) {
		if (!strncmp(p, "off", 3))
			no_iommu = 1;
		/* gart_parse_options has more force support */
		if (!strncmp(p, "force", 5))
			force_iommu = 1;
		if (!strncmp(p, "noforce", 7)) {
			iommu_merge = 0;
			force_iommu = 0;
		}

		if (!strncmp(p, "biomerge", 8)) {
			iommu_bio_merge = 4096;
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "panic", 5))
			panic_on_overflow = 1;
		if (!strncmp(p, "nopanic", 7))
			panic_on_overflow = 0;
		if (!strncmp(p, "merge", 5)) {
			iommu_merge = 1;
			force_iommu = 1;
		}
		if (!strncmp(p, "nomerge", 7))
			iommu_merge = 0;
		if (!strncmp(p, "forcesac", 8))
			iommu_sac_force = 1;
		if (!strncmp(p, "allowdac", 8))
			forbid_dac = 0;
		if (!strncmp(p, "nodac", 5))
			forbid_dac = -1;
		if (!strncmp(p, "usedac", 6)) {
			forbid_dac = -1;
			return 1;
		}
#ifdef CONFIG_SWIOTLB
		if (!strncmp(p, "soft", 4))
			swiotlb = 1;
#endif

		gart_parse_options(p);

#ifdef CONFIG_CALGARY_IOMMU
		if (!strncmp(p, "calgary", 7))
			use_calgary = 1;
#endif /* CONFIG_CALGARY_IOMMU */

		p += strcspn(p, ",");
		if (*p == ',')
			++p;
	}
	return 0;
}
early_param("iommu", iommu_setup);

static int check_pages_physically_contiguous(unsigned long pfn,
					     unsigned int offset,
					     size_t length)
{
	unsigned long next_mfn;
	int i;
	int nr_pages;

	next_mfn = pfn_to_mfn(pfn);
	nr_pages = (offset + length + PAGE_SIZE-1) >> PAGE_SHIFT;

	for (i = 1; i < nr_pages; i++) {
		if (pfn_to_mfn(++pfn) != ++next_mfn)
			return 0;
	}
	return 1;
}

int range_straddles_page_boundary(paddr_t p, size_t size)
{
	unsigned long pfn = p >> PAGE_SHIFT;
	unsigned int offset = p & ~PAGE_MASK;

	return ((offset + size > PAGE_SIZE) &&
		!check_pages_physically_contiguous(pfn, offset, size));
}

int dma_supported(struct device *dev, u64 mask)
{
	struct dma_mapping_ops *ops = get_dma_ops(dev);

#ifdef CONFIG_PCI
	if (mask > 0xffffffff && forbid_dac > 0) {
		dev_info(dev, "PCI: Disallowing DAC for device\n");
		return 0;
	}
#endif

	if (ops->dma_supported)
		return ops->dma_supported(dev, mask);

	/* Copied from i386. Doesn't make much sense, because it will
	   only work for pci_alloc_coherent.
	   The caller just has to use GFP_DMA in this case. */
	if (mask < DMA_24BIT_MASK)
		return 0;

	/* Tell the device to use SAC when IOMMU force is on.  This
	   allows the driver to use cheaper accesses in some cases.

	   Problem with this is that if we overflow the IOMMU area and
	   return DAC as fallback address the device may not handle it
	   correctly.

	   As a special case some controllers have a 39bit address
	   mode that is as efficient as 32bit (aic79xx). Don't force
	   SAC for these.  Assume all masks <= 40 bits are of this
	   type. Normally this doesn't make any difference, but gives
	   more gentle handling of IOMMU overflow. */
	if (iommu_sac_force && (mask >= DMA_40BIT_MASK)) {
		dev_info(dev, "Force SAC with mask %Lx\n", mask);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(dma_supported);

/* Allocate DMA memory on node near device */
static struct page *
dma_alloc_pages(struct device *dev, gfp_t gfp, unsigned order)
{
	int node;

	node = dev_to_node(dev);

	return alloc_pages_node(node, gfp, order);
}

/*
 * Allocate memory for a coherent mapping.
 */
void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   gfp_t gfp)
{
#ifndef CONFIG_XEN
	struct dma_mapping_ops *ops = get_dma_ops(dev);
#endif
	void *memory = NULL;
	struct page *page;
	unsigned long dma_mask = 0;
	int noretry = 0;
	unsigned int order = get_order(size);

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM | __GFP_DMA32);

	if (dma_alloc_from_coherent(dev, size, dma_handle, &memory))
		return memory;

	if (!dev) {
		dev = &fallback_dev;
		gfp |= GFP_DMA;
	}
	dma_mask = dev->coherent_dma_mask;
	if (dma_mask == 0)
		dma_mask = (gfp & GFP_DMA) ? DMA_24BIT_MASK : DMA_32BIT_MASK;

	/* Device not DMA able */
	if (dev->dma_mask == NULL)
		return NULL;

	/* Don't invoke OOM killer or retry in lower 16MB DMA zone */
	if (gfp & __GFP_DMA)
		noretry = 1;

#ifdef CONFIG_XEN
	gfp &= ~(__GFP_DMA | __GFP_DMA32);
#else
#ifdef CONFIG_X86_64
	/* Why <=? Even when the mask is smaller than 4GB it is often
	   larger than 16MB and in this case we have a chance of
	   finding fitting memory in the next higher zone first. If
	   not retry with true GFP_DMA. -AK */
	if (dma_mask <= DMA_32BIT_MASK && !(gfp & GFP_DMA)) {
		gfp |= GFP_DMA32;
#endif

 again:
#endif
	page = dma_alloc_pages(dev,
		noretry ? gfp | __GFP_NORETRY : gfp, order);
	if (page == NULL)
		return NULL;

#ifndef CONFIG_XEN
	{
		int high, mmu;
		dma_addr_t bus = page_to_phys(page);
		memory = page_address(page);
		high = (bus + size) >= dma_mask;
		mmu = high;
		if (force_iommu && !(gfp & GFP_DMA))
			mmu = 1;
		else if (high) {
			free_pages((unsigned long)memory, order);

			/* Don't use the 16MB ZONE_DMA unless absolutely
			   needed. It's better to use remapping first. */
			if (dma_mask < DMA_32BIT_MASK && !(gfp & GFP_DMA)) {
				gfp = (gfp & ~GFP_DMA32) | GFP_DMA;
				goto again;
			}

			/* Let low level make its own zone decisions */
			gfp &= ~(GFP_DMA32|GFP_DMA);

			if (ops->alloc_coherent)
				return ops->alloc_coherent(dev, size,
							   dma_handle, gfp);
			return NULL;
		}

		memset(memory, 0, size);
		if (!mmu) {
			*dma_handle = bus;
			return memory;
		}
	}

	if (ops->alloc_coherent) {
		free_pages((unsigned long)memory, order);
		gfp &= ~(GFP_DMA|GFP_DMA32);
		return ops->alloc_coherent(dev, size, dma_handle, gfp);
	}

	if (ops->map_simple) {
		*dma_handle = ops->map_simple(dev, virt_to_bus(memory),
					      size,
					      PCI_DMA_BIDIRECTIONAL);
		if (*dma_handle != bad_dma_address)
			return memory;
	}
#else
	memory = page_address(page);
	if (xen_create_contiguous_region((unsigned long)memory, order,
					 fls64(dma_mask)) == 0) {
		memset(memory, 0, size);
		*dma_handle = virt_to_bus(memory);
		return memory;
	}
#endif

	if (panic_on_overflow)
		panic("dma_alloc_coherent: IOMMU overflow by %lu bytes\n",
		      (unsigned long)size);
	free_pages((unsigned long)memory, order);
	return NULL;
}
EXPORT_SYMBOL(dma_alloc_coherent);

/*
 * Unmap coherent memory.
 * The caller must ensure that the device has finished accessing the mapping.
 */
void dma_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t bus)
{
#ifndef CONFIG_XEN
	struct dma_mapping_ops *ops = get_dma_ops(dev);
#endif

	int order = get_order(size);
	WARN_ON(irqs_disabled());	/* for portability */
	if (dma_release_from_coherent(dev, order, vaddr))
		return;
#ifndef CONFIG_XEN
	if (ops->unmap_single)
		ops->unmap_single(dev, bus, size, 0);
#endif
	xen_destroy_contiguous_region((unsigned long)vaddr, order);
	free_pages((unsigned long)vaddr, order);
}
EXPORT_SYMBOL(dma_free_coherent);

static int __init pci_iommu_init(void)
{
	calgary_iommu_init();

	intel_iommu_init();

	amd_iommu_init();

	gart_iommu_init();

	no_iommu_init();
	return 0;
}

void pci_iommu_shutdown(void)
{
	gart_iommu_shutdown();
}
/* Must execute after PCI subsystem */
fs_initcall(pci_iommu_init);

#ifdef CONFIG_PCI
/* Many VIA bridges seem to corrupt data for DAC. Disable it here */

static __devinit void via_no_dac(struct pci_dev *dev)
{
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI && forbid_dac == 0) {
		printk(KERN_INFO "PCI: VIA PCI bridge detected."
				 "Disabling DAC.\n");
		forbid_dac = 1;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_ANY_ID, via_no_dac);
#endif
