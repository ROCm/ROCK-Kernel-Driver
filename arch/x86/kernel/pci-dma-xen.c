#include <linux/dma-mapping.h>
#include <linux/dma-debug.h>
#include <linux/export.h>
#include <linux/bootmem.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/kmemleak.h>

#include <asm/proto.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/x86_init.h>
#include <asm/iommu_table.h>

static int forbid_dac __read_mostly;

struct dma_map_ops *dma_ops = &nommu_dma_ops;
EXPORT_SYMBOL(dma_ops);

static int iommu_sac_force __read_mostly;

#ifdef CONFIG_IOMMU_DEBUG
int panic_on_overflow __read_mostly = 1;
int force_iommu __initdata = 1;
#else
int panic_on_overflow __read_mostly = 0;
int force_iommu __initdata = 0;
#endif

int iommu_merge __initdata;

int no_iommu __initdata;
#ifndef CONFIG_XEN
/* Set this to 1 if there is a HW IOMMU in the system */
int iommu_detected __read_mostly = 0;

/*
 * This variable becomes 1 if iommu=pt is passed on the kernel command line.
 * If this variable is 1, IOMMU implementations do no DMA translation for
 * devices and allow every device to access to whole physical memory. This is
 * useful if a user wants to use an IOMMU only for KVM device assignment to
 * guests and not for driver dma translation.
 */
int iommu_pass_through __read_mostly;

/*
 * Group multi-function PCI devices into a single device-group for the
 * iommu_device_group interface.  This tells the iommu driver to pretend
 * it cannot distinguish between functions of a device, exposing only one
 * group for the device.  Useful for disallowing use of individual PCI
 * functions from userspace drivers.
 */
int iommu_group_mf __read_mostly;
#endif

extern struct iommu_table_entry __iommu_table[], __iommu_table_end[];

/* Dummy device used for NULL arguments (normally ISA). */
struct device x86_dma_fallback_dev = {
	.init_name = "fallback device",
	.coherent_dma_mask = ISA_DMA_BIT_MASK,
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

static struct dma_map_ops swiotlb_dma_ops = {
	.alloc = dma_generic_alloc_coherent,
	.free = dma_generic_free_coherent,
	.mapping_error = swiotlb_dma_mapping_error,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.dma_supported = swiotlb_dma_supported
};

static int __init pci_xen_swiotlb_detect(void)
{
	return 1;
}

static void __init pci_xen_swiotlb_init(void)
{
	swiotlb_init(1);
	if (swiotlb) {
		printk(KERN_INFO "PCI-DMA: Using software bounce buffering for IO (SWIOTLB)\n");
		dma_ops = &swiotlb_dma_ops;
	}
}

IOMMU_INIT_FINISH(pci_xen_swiotlb_detect, NULL, pci_xen_swiotlb_init, NULL);

void __init pci_iommu_alloc(void)
{
	struct iommu_table_entry *p;

	sort_iommu_table(__iommu_table, __iommu_table_end);
	check_iommu_entries(__iommu_table, __iommu_table_end);

	for (p = __iommu_table; p < __iommu_table_end; p++) {
		if (p && p->detect && p->detect() > 0) {
			p->flags |= IOMMU_DETECTED;
			if (p->early_init)
				p->early_init();
			if (p->flags & IOMMU_FINISH_IF_DETECTED)
				break;
		}
	}
}
void *dma_generic_alloc_coherent(struct device *dev, size_t size,
				 dma_addr_t *dma_addr, gfp_t flag,
				 struct dma_attrs *attrs)
{
	unsigned long dma_mask;
	struct page *page;
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
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
	page = NULL;
	if (!(flag & GFP_ATOMIC))
		page = dma_alloc_from_contiguous(dev, count, order);
	if (!page)
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

void dma_generic_free_coherent(struct device *dev, size_t size, void *vaddr,
			       dma_addr_t dma_addr, struct dma_attrs *attrs)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct page *page = virt_to_page(vaddr);

	if (!dma_release_from_contiguous(dev, page, count)) {
		unsigned int order = get_order(size);
		unsigned long va = (unsigned long)vaddr;

		xen_destroy_contiguous_region(va, order);
		free_pages(va, order);
	}
}

/*
 * See <Documentation/x86/x86_64/boot-options.txt> for the iommu kernel
 * parameter documentation.
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
			forbid_dac = 1;
		if (!strncmp(p, "usedac", 6)) {
			forbid_dac = -1;
			return 1;
		}
#ifdef CONFIG_SWIOTLB
		if (!strncmp(p, "soft", 4))
			swiotlb = 1;
#endif
#ifndef CONFIG_XEN
		if (!strncmp(p, "pt", 2))
			iommu_pass_through = 1;
		if (!strncmp(p, "group_mf", 8))
			iommu_group_mf = 1;

		gart_parse_options(p);
#endif

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
	struct iommu_table_entry *p;
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);

#ifdef CONFIG_PCI
	dma_debug_add_bus(&pci_bus_type);
#endif
	x86_init.iommu.iommu_init();

	for (p = __iommu_table; p < __iommu_table_end; p++) {
		if (p && (p->flags & IOMMU_DETECTED) && p->late_init)
			p->late_init();
	}

	return 0;
}
/* Must execute after PCI subsystem */
rootfs_initcall(pci_iommu_init);

#ifdef CONFIG_PCI
/* Many VIA bridges seem to corrupt data for DAC. Disable it here */

static __devinit void via_no_dac(struct pci_dev *dev)
{
	if (forbid_dac == 0) {
		dev_info(&dev->dev, "disabling DAC on VIA PCI bridge\n");
		forbid_dac = 1;
	}
}
DECLARE_PCI_FIXUP_CLASS_FINAL(PCI_VENDOR_ID_VIA, PCI_ANY_ID,
				PCI_CLASS_BRIDGE_PCI, 8, via_no_dac);
#endif
