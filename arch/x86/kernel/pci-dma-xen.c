#include <linux/dma-mapping.h>
#include <linux/dma-debug.h>
#include <linux/dmar.h>
#include <linux/bootmem.h>
#include <linux/pci.h>
#include <linux/kmemleak.h>

#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/calgary.h>
#include <asm/amd_iommu.h>

static int forbid_dac __read_mostly;

struct dma_map_ops *dma_ops;
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

/*
 * This variable becomes 1 if iommu=pt is passed on the kernel command line.
 * If this variable is 1, IOMMU implementations do no DMA translation for
 * devices and allow every device to access to whole physical memory. This is
 * useful if a user want to use an IOMMU only for KVM device assignment to
 * guests and not for driver dma translation.
 */
int iommu_pass_through __read_mostly;

dma_addr_t bad_dma_address __read_mostly = 0;
EXPORT_SYMBOL(bad_dma_address);

/* Dummy device used for NULL arguments (normally ISA). Better would
   be probably a smaller DMA mask, but this is bug-to-bug compatible
   to older i386. */
struct device x86_dma_fallback_dev = {
	.init_name = "fallback device",
	.coherent_dma_mask = DMA_BIT_MASK(32),
	.dma_mask = &x86_dma_fallback_dev.coherent_dma_mask,
};
EXPORT_SYMBOL(x86_dma_fallback_dev);

/* Number of entries preallocated for DMA-API debugging */
#define PREALLOC_DMA_DEBUG_ENTRIES       32768

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
	size = roundup(dma32_bootmem_size, align);
	dma32_bootmem_ptr = __alloc_bootmem_nopanic(size, align,
				 512ULL<<20);
	/*
	 * Kmemleak should not scan this block as it may not be mapped via the
	 * kernel direct mapping.
	 */
	kmemleak_ignore(dma32_bootmem_ptr);
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
#endif

static struct dma_map_ops swiotlb_dma_ops = {
	.alloc_coherent = dma_generic_alloc_coherent,
	.free_coherent = dma_generic_free_coherent,
	.mapping_error = swiotlb_dma_mapping_error,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_single_range_for_cpu = swiotlb_sync_single_range_for_cpu,
	.sync_single_range_for_device = swiotlb_sync_single_range_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.dma_supported = swiotlb_dma_supported
};

void __init pci_iommu_alloc(void)
{
#if defined(CONFIG_X86_64) && !defined(CONFIG_XEN)
	/* free the range so iommu could get some range less than 4G */
	dma32_free_bootmem();
#endif

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

void *dma_generic_alloc_coherent(struct device *dev, size_t size,
				 dma_addr_t *dma_addr, gfp_t flag)
{
	unsigned long dma_mask;
	struct page *page;
#ifndef CONFIG_XEN
	dma_addr_t addr;
#else
	void *memory;
#endif
	unsigned int order = get_order(size);

	dma_mask = dma_alloc_coherent_mask(dev, flag);

#ifndef CONFIG_XEN
	flag |= __GFP_ZERO;
again:
#else
	flag &= ~(__GFP_DMA | __GFP_DMA32);
#endif
	page = alloc_pages_node(dev_to_node(dev), flag, order);
	if (!page)
		return NULL;

#ifndef CONFIG_XEN
	addr = page_to_phys(page);
	if (addr + size > dma_mask) {
		__free_pages(page, order);

		if (dma_mask < DMA_BIT_MASK(32) && !(flag & GFP_DMA)) {
			flag = (flag & ~GFP_DMA32) | GFP_DMA;
			goto again;
		}

		return NULL;
	}

	*dma_addr = addr;
	return page_address(page);
#else
	memory = page_address(page);
	if (xen_create_contiguous_region((unsigned long)memory, order,
					 fls64(dma_mask))) {
		__free_pages(page, order);
		return NULL;
	}

	*dma_addr = virt_to_bus(memory);
	return memset(memory, 0, size);
#endif
}

#ifdef CONFIG_XEN
void dma_generic_free_coherent(struct device *dev, size_t size, void *vaddr,
			       dma_addr_t dma_addr)
{
	unsigned int order = get_order(size);
	unsigned long va = (unsigned long)vaddr;

	xen_destroy_contiguous_region(va, order);
	free_pages(va, order);
}
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
		if (!strncmp(p, "pt", 2))
			iommu_pass_through = 1;

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
	struct dma_map_ops *ops = get_dma_ops(dev);

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
	if (mask < DMA_BIT_MASK(24))
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
	if (iommu_sac_force && (mask >= DMA_BIT_MASK(40))) {
		dev_info(dev, "Force SAC with mask %Lx\n", mask);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(dma_supported);

static int __init pci_iommu_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);

#ifdef CONFIG_PCI
	dma_debug_add_bus(&pci_bus_type);
#endif

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

	amd_iommu_shutdown();
}
/* Must execute after PCI subsystem */
rootfs_initcall(pci_iommu_init);

#ifdef CONFIG_PCI
/* Many VIA bridges seem to corrupt data for DAC. Disable it here */

static __devinit void via_no_dac(struct pci_dev *dev)
{
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI && forbid_dac == 0) {
		dev_info(&dev->dev, "disabling DAC on VIA PCI bridge\n");
		forbid_dac = 1;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA, PCI_ANY_ID, via_no_dac);
#endif
