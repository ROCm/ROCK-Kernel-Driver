/*
 * Dynamic DMA mapping support.
 *
 * This implementation is a fallback for platforms that do not support
 * I/O TLBs (aka DMA address translation hardware).
 * Copyright (C) 2000 Asit Mallick <Asit.K.Mallick@intel.com>
 * Copyright (C) 2000 Goutham Rao <goutham.rao@intel.com>
 * Copyright (C) 2000, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2005 Keir Fraser <keir@xensource.com>
 * 08/12/11 beckyb	Add highmem support
 */

#include <linux/cache.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/swiotlb.h>
#include <linux/pfn.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/iommu-helper.h>
#include <linux/highmem.h>
#include <asm/io.h>
#include <asm/pci.h>
#include <asm/dma.h>
#include <asm/uaccess.h>
#include <xen/gnttab.h>
#include <xen/interface/memory.h>
#include <asm/gnttab_dma.h>

#define OFFSET(val,align) ((unsigned long)((val) & ( (align) - 1)))

int swiotlb;
int swiotlb_force;

/*
 * Used to do a quick range check in unmap_single and
 * sync_single_*, to see if the memory was in fact allocated by this
 * API.
 */
static char *io_tlb_start, *io_tlb_end;

/*
 * The number of IO TLB blocks (in groups of 64) betweeen io_tlb_start and
 * io_tlb_end.  This is command line adjustable via setup_io_tlb_npages.
 */
static unsigned long io_tlb_nslabs;

/*
 * When the IOMMU overflows we return a fallback buffer. This sets the size.
 */
static unsigned long io_tlb_overflow = 32*1024;

void *io_tlb_overflow_buffer;

/*
 * This is a free list describing the number of free entries available from
 * each index
 */
static unsigned int *io_tlb_list;
static unsigned int io_tlb_index;

/*
 * We need to save away the original address corresponding to a mapped entry
 * for the sync operations.
 */
static phys_addr_t *io_tlb_orig_addr;

/*
 * Protect the above data structures in the map and unmap calls
 */
static DEFINE_SPINLOCK(io_tlb_lock);

static unsigned int dma_bits;
static unsigned int __initdata max_dma_bits = 32;
static int __init
setup_dma_bits(char *str)
{
	max_dma_bits = simple_strtoul(str, NULL, 0);
	return 0;
}
__setup("dma_bits=", setup_dma_bits);

static int __init
setup_io_tlb_npages(char *str)
{
	/* Unlike ia64, the size is aperture in megabytes, not 'slabs'! */
	if (isdigit(*str)) {
		io_tlb_nslabs = simple_strtoul(str, &str, 0) <<
			(20 - IO_TLB_SHIFT);
		io_tlb_nslabs = ALIGN(io_tlb_nslabs, IO_TLB_SEGSIZE);
	}
	if (*str == ',')
		++str;
	/*
         * NB. 'force' enables the swiotlb, but doesn't force its use for
         * every DMA like it does on native Linux. 'off' forcibly disables
         * use of the swiotlb.
         */
	if (!strcmp(str, "force"))
		swiotlb_force = 1;
	else if (!strcmp(str, "off"))
		swiotlb_force = -1;
	return 1;
}
__setup("swiotlb=", setup_io_tlb_npages);
/* make io_tlb_overflow tunable too? */

/* Note that this doesn't work with highmem page */
static dma_addr_t swiotlb_virt_to_bus(struct device *hwdev,
				      volatile void *address)
{
	return phys_to_dma(hwdev, virt_to_phys(address));
}

static void swiotlb_print_info(unsigned long bytes)
{
	printk(KERN_INFO "Software IO TLB enabled: \n"
	       " Aperture:     %lu megabytes\n"
	       " Address size: %u bits\n"
	       " Kernel range: %p - %p\n",
	       bytes >> 20, dma_bits,
	       io_tlb_start, io_tlb_end);
}

/*
 * Statically reserve bounce buffer space and initialize bounce buffer data
 * structures for the software IO TLB used to implement the PCI DMA API.
 */
void __init
swiotlb_init_with_default_size(size_t default_size)
{
	unsigned long i, bytes;
	int rc;

	if (!io_tlb_nslabs) {
		io_tlb_nslabs = (default_size >> IO_TLB_SHIFT);
		io_tlb_nslabs = ALIGN(io_tlb_nslabs, IO_TLB_SEGSIZE);
	}

	bytes = io_tlb_nslabs << IO_TLB_SHIFT;

	/*
	 * Get IO TLB memory from the low pages
	 */
	io_tlb_start = alloc_bootmem_pages(bytes);
	if (!io_tlb_start)
		panic("Cannot allocate SWIOTLB buffer!\n");
	dma_bits = get_order(IO_TLB_SEGSIZE << IO_TLB_SHIFT) + PAGE_SHIFT;
	for (i = 0; i < io_tlb_nslabs; i += IO_TLB_SEGSIZE) {
		do {
			rc = xen_create_contiguous_region(
				(unsigned long)io_tlb_start + (i << IO_TLB_SHIFT),
				get_order(IO_TLB_SEGSIZE << IO_TLB_SHIFT),
				dma_bits);
		} while (rc && dma_bits++ < max_dma_bits);
		if (rc) {
			if (i == 0)
				panic("No suitable physical memory available for SWIOTLB buffer!\n"
				      "Use dom0_mem Xen boot parameter to reserve\n"
				      "some DMA memory (e.g., dom0_mem=-128M).\n");
			io_tlb_nslabs = i;
			i <<= IO_TLB_SHIFT;
			free_bootmem(__pa(io_tlb_start + i), bytes - i);
			bytes = i;
			for (dma_bits = 0; i > 0; i -= IO_TLB_SEGSIZE << IO_TLB_SHIFT) {
				unsigned int bits = fls64(virt_to_bus(io_tlb_start + i - 1));

				if (bits > dma_bits)
					dma_bits = bits;
			}
			break;
		}
	}
	io_tlb_end = io_tlb_start + bytes;

	/*
	 * Allocate and initialize the free list array.  This array is used
	 * to find contiguous free memory regions of size up to IO_TLB_SEGSIZE.
	 */
	io_tlb_list = alloc_bootmem(io_tlb_nslabs * sizeof(int));
	for (i = 0; i < io_tlb_nslabs; i++)
 		io_tlb_list[i] = IO_TLB_SEGSIZE - OFFSET(i, IO_TLB_SEGSIZE);
	io_tlb_index = 0;
	io_tlb_orig_addr = alloc_bootmem(io_tlb_nslabs * sizeof(phys_addr_t));

	/*
	 * Get the overflow emergency buffer
	 */
	io_tlb_overflow_buffer = alloc_bootmem(io_tlb_overflow);
	if (!io_tlb_overflow_buffer)
		panic("Cannot allocate SWIOTLB overflow buffer!\n");

	do {
		rc = xen_create_contiguous_region(
			(unsigned long)io_tlb_overflow_buffer,
			get_order(io_tlb_overflow),
			dma_bits);
	} while (rc && dma_bits++ < max_dma_bits);
	if (rc)
		panic("No suitable physical memory available for SWIOTLB overflow buffer!\n");

	swiotlb_print_info(bytes);
}

void __init
swiotlb_init(void)
{
	unsigned long ram_end;
	size_t defsz = 64 << 20; /* 64MB default size */

	if (swiotlb_force == 1) {
		swiotlb = 1;
	} else if ((swiotlb_force != -1) &&
		   is_running_on_xen() &&
		   is_initial_xendomain()) {
		/* Domain 0 always has a swiotlb. */
		ram_end = HYPERVISOR_memory_op(XENMEM_maximum_ram_page, NULL);
		if (ram_end <= 0x1ffff)
			defsz = 2 << 20; /* 2MB on <512MB systems. */
		else if (ram_end <= 0x3ffff)
			defsz = 4 << 20; /* 4MB on <1GB systems. */
		else if (ram_end <= 0x7ffff)
			defsz = 8 << 20; /* 8MB on <2GB systems. */
		swiotlb = 1;
	}

	if (swiotlb)
		swiotlb_init_with_default_size(defsz);
	else
		printk(KERN_INFO "Software IO TLB disabled\n");
}

static inline int range_needs_mapping(phys_addr_t pa, size_t size)
{
	return range_straddles_page_boundary(pa, size);
}

static int is_swiotlb_buffer(phys_addr_t paddr)
{
	return paddr >= virt_to_phys(io_tlb_start) &&
		paddr < virt_to_phys(io_tlb_end);
}

/*
 * Bounce: copy the swiotlb buffer back to the original dma location
 *
 * We use __copy_to_user_inatomic to transfer to the host buffer because the
 * buffer may be mapped read-only (e.g, in blkback driver) but lower-level
 * drivers map the buffer for DMA_BIDIRECTIONAL access. This causes an
 * unnecessary copy from the aperture to the host buffer, and a page fault.
 */
static void swiotlb_bounce(phys_addr_t phys, char *dma_addr, size_t size,
			   enum dma_data_direction dir)
{
	unsigned long pfn = PFN_DOWN(phys);

	if (PageHighMem(pfn_to_page(pfn))) {
		/* The buffer does not have a mapping.  Map it in and copy */
		unsigned int offset = phys & ~PAGE_MASK;
		char *buffer;
		unsigned int sz = 0;
		unsigned long flags;

		while (size) {
			sz = min_t(size_t, PAGE_SIZE - offset, size);

			local_irq_save(flags);
			buffer = kmap_atomic(pfn_to_page(pfn),
					     KM_BOUNCE_READ);
			if (dir == DMA_TO_DEVICE)
				memcpy(dma_addr, buffer + offset, sz);
			else if (__copy_to_user_inatomic(buffer + offset,
							 dma_addr, sz))
				/* inaccessible */;
			kunmap_atomic(buffer, KM_BOUNCE_READ);
			local_irq_restore(flags);

			size -= sz;
			pfn++;
			dma_addr += sz;
			offset = 0;
		}
	} else {
		if (dir == DMA_TO_DEVICE)
			memcpy(dma_addr, phys_to_virt(phys), size);
		else
			memcpy(phys_to_virt(phys), dma_addr, size);
	}
}

/*
 * Allocates bounce buffer and returns its kernel virtual address.
 */
static void *
map_single(struct device *hwdev, phys_addr_t phys, size_t size, int dir)
{
	unsigned long flags;
	char *dma_addr;
	unsigned int nslots, stride, index, wrap;
	int i;
	unsigned long mask;
	unsigned long offset_slots;
	unsigned long max_slots;

	mask = dma_get_seg_boundary(hwdev);
	offset_slots = -IO_TLB_SEGSIZE;

	/*
 	 * Carefully handle integer overflow which can occur when mask == ~0UL.
 	 */
	max_slots = mask + 1
		    ? ALIGN(mask + 1, 1 << IO_TLB_SHIFT) >> IO_TLB_SHIFT
		    : 1UL << (BITS_PER_LONG - IO_TLB_SHIFT);

	/*
	 * For mappings greater than a page, we limit the stride (and
	 * hence alignment) to a page size.
	 */
	nslots = ALIGN(size, 1 << IO_TLB_SHIFT) >> IO_TLB_SHIFT;
	if (size > PAGE_SIZE)
		stride = (1 << (PAGE_SHIFT - IO_TLB_SHIFT));
	else
		stride = 1;

	BUG_ON(!nslots);

	/*
	 * Find suitable number of IO TLB entries size that will fit this
	 * request and allocate a buffer from that IO TLB pool.
	 */
	spin_lock_irqsave(&io_tlb_lock, flags);
	index = ALIGN(io_tlb_index, stride);
	if (index >= io_tlb_nslabs)
		index = 0;
	wrap = index;

	do {
		while (iommu_is_span_boundary(index, nslots, offset_slots,
					      max_slots)) {
			index += stride;
			if (index >= io_tlb_nslabs)
				index = 0;
			if (index == wrap)
				goto not_found;
		}

		/*
		 * If we find a slot that indicates we have 'nslots' number of
		 * contiguous buffers, we allocate the buffers from that slot
		 * and mark the entries as '0' indicating unavailable.
		 */
		if (io_tlb_list[index] >= nslots) {
			int count = 0;

			for (i = index; i < (int) (index + nslots); i++)
				io_tlb_list[i] = 0;
			for (i = index - 1; (OFFSET(i, IO_TLB_SEGSIZE) != IO_TLB_SEGSIZE - 1) && io_tlb_list[i]; i--)
				io_tlb_list[i] = ++count;
			dma_addr = io_tlb_start + (index << IO_TLB_SHIFT);

			/*
			 * Update the indices to avoid searching in the next
			 * round.
			 */
			io_tlb_index = ((index + nslots) < io_tlb_nslabs
					? (index + nslots) : 0);

			goto found;
		}
		index += stride;
		if (index >= io_tlb_nslabs)
			index = 0;
	} while (index != wrap);

not_found:
	spin_unlock_irqrestore(&io_tlb_lock, flags);
	return NULL;
found:
	spin_unlock_irqrestore(&io_tlb_lock, flags);

	/*
	 * Save away the mapping from the original address to the DMA address.
	 * This is needed when we sync the memory.  Then we sync the buffer if
	 * needed.
	 */
	for (i = 0; i < nslots; i++)
		io_tlb_orig_addr[index+i] = phys + (i << IO_TLB_SHIFT);
	if (dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL)
		swiotlb_bounce(phys, dma_addr, size, DMA_TO_DEVICE);

	return dma_addr;
}

/*
 * dma_addr is the kernel virtual address of the bounce buffer to unmap.
 */
static void
do_unmap_single(struct device *hwdev, char *dma_addr, size_t size, int dir)
{
	unsigned long flags;
	int i, count, nslots = ALIGN(size, 1 << IO_TLB_SHIFT) >> IO_TLB_SHIFT;
	int index = (dma_addr - io_tlb_start) >> IO_TLB_SHIFT;
	phys_addr_t phys = io_tlb_orig_addr[index];

	/*
	 * First, sync the memory before unmapping the entry
	 */
	if (phys && ((dir == DMA_FROM_DEVICE) || (dir == DMA_BIDIRECTIONAL)))
		swiotlb_bounce(phys, dma_addr, size, DMA_FROM_DEVICE);

	/*
	 * Return the buffer to the free list by setting the corresponding
	 * entries to indicate the number of contigous entries available.
	 * While returning the entries to the free list, we merge the entries
	 * with slots below and above the pool being returned.
	 */
	spin_lock_irqsave(&io_tlb_lock, flags);
	{
		count = ((index + nslots) < ALIGN(index + 1, IO_TLB_SEGSIZE) ?
			 io_tlb_list[index + nslots] : 0);
		/*
		 * Step 1: return the slots to the free list, merging the
		 * slots with superceeding slots
		 */
		for (i = index + nslots - 1; i >= index; i--)
			io_tlb_list[i] = ++count;
		/*
		 * Step 2: merge the returned slots with the preceding slots,
		 * if available (non zero)
		 */
		for (i = index - 1;
		     (OFFSET(i, IO_TLB_SEGSIZE) !=
		      IO_TLB_SEGSIZE -1) && io_tlb_list[i];
		     i--)
			io_tlb_list[i] = ++count;
	}
	spin_unlock_irqrestore(&io_tlb_lock, flags);
}

static void
sync_single(struct device *hwdev, char *dma_addr, size_t size, int dir)
{
	int index = (dma_addr - io_tlb_start) >> IO_TLB_SHIFT;
	phys_addr_t phys = io_tlb_orig_addr[index];

	phys += ((unsigned long)dma_addr & ((1 << IO_TLB_SHIFT) - 1));

	BUG_ON((dir != DMA_FROM_DEVICE) && (dir != DMA_TO_DEVICE));
	swiotlb_bounce(phys, dma_addr, size, dir);
}

static void
swiotlb_full(struct device *dev, size_t size, int dir, int do_panic)
{
	/*
	 * Ran out of IOMMU space for this operation. This is very bad.
	 * Unfortunately the drivers cannot handle this operation properly.
	 * unless they check for pci_dma_mapping_error (most don't)
	 * When the mapping is small enough return a static buffer to limit
	 * the damage, or panic when the transfer is too big.
	 */
	printk(KERN_ERR "PCI-DMA: Out of SW-IOMMU space for %zu bytes at "
	       "device %s\n", size, dev ? dev_name(dev) : "?");

	if (size <= io_tlb_overflow || !do_panic)
		return;

	if (dir == DMA_BIDIRECTIONAL)
		panic("DMA: Random memory could be DMA accessed\n");
	if (dir == DMA_FROM_DEVICE)
		panic("DMA: Random memory could be DMA written\n");
	if (dir == DMA_TO_DEVICE)
		panic("DMA: Random memory could be DMA read\n");
}

/*
 * Map a single buffer of the indicated size for DMA in streaming mode.  The
 * PCI address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory until
 * either swiotlb_unmap_page or swiotlb_dma_sync_single is performed.
 */
dma_addr_t swiotlb_map_page(struct device *dev, struct page *page,
			    unsigned long offset, size_t size,
			    enum dma_data_direction dir,
			    struct dma_attrs *attrs)
{
	phys_addr_t phys = page_to_pseudophys(page) + offset;
	dma_addr_t dev_addr = gnttab_dma_map_page(page) + offset;
	void *map;

	BUG_ON(dir == DMA_NONE);

	/*
	 * If the address happens to be in the device's DMA window,
	 * we can safely return the device addr and not worry about bounce
	 * buffering it.
	 */
	if (dma_capable(dev, dev_addr, size)/* && !swiotlb_force*/)
		return dev_addr;

	/*
	 * Oh well, have to allocate and map a bounce buffer.
	 */
	gnttab_dma_unmap_page(dev_addr);
	map = map_single(dev, phys, size, dir);
	if (!map) {
		swiotlb_full(dev, size, dir, 1);
		map = io_tlb_overflow_buffer;
	}

	dev_addr = swiotlb_virt_to_bus(dev, map);
	return dev_addr;
}
EXPORT_SYMBOL_GPL(swiotlb_map_page);

/*
 * Unmap a single streaming mode DMA translation.  The dma_addr and size must
 * match what was provided for in a previous swiotlb_map_page call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static void unmap_single(struct device *hwdev, dma_addr_t dev_addr,
			 size_t size, int dir)
{
	phys_addr_t paddr = dma_to_phys(hwdev, dev_addr);

	BUG_ON(dir == DMA_NONE);

	if (is_swiotlb_buffer(paddr)) {
		do_unmap_single(hwdev, phys_to_virt(paddr), size, dir);
		return;
	}

	gnttab_dma_unmap_page(dev_addr);
}

void swiotlb_unmap_page(struct device *hwdev, dma_addr_t dev_addr,
			size_t size, enum dma_data_direction dir,
			struct dma_attrs *attrs)
{
	unmap_single(hwdev, dev_addr, size, dir);
}
EXPORT_SYMBOL_GPL(swiotlb_unmap_page);

/*
 * Make physical memory consistent for a single streaming mode DMA translation
 * after a transfer.
 *
 * If you perform a swiotlb_map_page() but wish to interrogate the buffer
 * using the cpu, yet do not wish to teardown the PCI dma mapping, you must
 * call this function before doing so.  At the next point you give the PCI dma
 * address back to the card, you must first perform a
 * swiotlb_dma_sync_for_device, and then the device again owns the buffer
 */
void
swiotlb_sync_single_for_cpu(struct device *hwdev, dma_addr_t dev_addr,
			    size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr = dma_to_phys(hwdev, dev_addr);

	BUG_ON(dir == DMA_NONE);

	if (is_swiotlb_buffer(paddr))
		sync_single(hwdev, phys_to_virt(paddr), size, dir);
}
EXPORT_SYMBOL(swiotlb_sync_single_for_cpu);

void
swiotlb_sync_single_for_device(struct device *hwdev, dma_addr_t dev_addr,
			       size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr = dma_to_phys(hwdev, dev_addr);

	BUG_ON(dir == DMA_NONE);

	if (is_swiotlb_buffer(paddr))
		sync_single(hwdev, phys_to_virt(paddr), size, dir);
}
EXPORT_SYMBOL(swiotlb_sync_single_for_device);

void
swiotlb_sync_single_range_for_cpu(struct device *hwdev, dma_addr_t dev_addr,
				  unsigned long offset, size_t size,
				  enum dma_data_direction dir)
{
	swiotlb_sync_single_for_cpu(hwdev, dev_addr + offset, size, dir);
}
EXPORT_SYMBOL_GPL(swiotlb_sync_single_range_for_cpu);

void
swiotlb_sync_single_range_for_device(struct device *hwdev, dma_addr_t dev_addr,
				     unsigned long offset, size_t size,
				     enum dma_data_direction dir)
{
	swiotlb_sync_single_for_device(hwdev, dev_addr + offset, size, dir);
}
EXPORT_SYMBOL_GPL(swiotlb_sync_single_range_for_device);

/*
 * Map a set of buffers described by scatterlist in streaming mode for DMA.
 * This is the scatter-gather version of the above swiotlb_map_page
 * interface.  Here the scatter gather list elements are each tagged with the
 * appropriate dma address and length.  They are obtained via
 * sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for swiotlb_map_page are the
 * same here.
 */
int
swiotlb_map_sg_attrs(struct device *hwdev, struct scatterlist *sgl, int nelems,
		     enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i) {
		dma_addr_t dev_addr = gnttab_dma_map_page(sg_page(sg))
				      + sg->offset;
		phys_addr_t paddr = page_to_pseudophys(sg_page(sg))
				   + sg->offset;

		if (range_needs_mapping(paddr, sg->length) ||
		    !dma_capable(hwdev, dev_addr, sg->length)) {
			void *map;

			gnttab_dma_unmap_page(dev_addr);
			map = map_single(hwdev, paddr,
					 sg->length, dir);
			if (!map) {
				/* Don't panic here, we expect map_sg users
				   to do proper error handling. */
				swiotlb_full(hwdev, sg->length, dir, 0);
				swiotlb_unmap_sg_attrs(hwdev, sgl, i, dir,
						       attrs);
				sgl[0].dma_length = 0;
				return 0;
			}
			sg->dma_address = swiotlb_virt_to_bus(hwdev, map);
		} else
			sg->dma_address = dev_addr;
		sg->dma_length = sg->length;
	}
	return nelems;
}
EXPORT_SYMBOL(swiotlb_map_sg_attrs);

int
swiotlb_map_sg(struct device *hwdev, struct scatterlist *sgl, int nelems,
	       int dir)
{
	return swiotlb_map_sg_attrs(hwdev, sgl, nelems, dir, NULL);
}
EXPORT_SYMBOL(swiotlb_map_sg);

/*
 * Unmap a set of streaming mode DMA translations.  Again, cpu read rules
 * concerning calls here are the same as for swiotlb_unmap_page() above.
 */
void
swiotlb_unmap_sg_attrs(struct device *hwdev, struct scatterlist *sgl,
		       int nelems, enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i)
		unmap_single(hwdev, sg->dma_address, sg->dma_length, dir);

}
EXPORT_SYMBOL(swiotlb_unmap_sg_attrs);

void
swiotlb_unmap_sg(struct device *hwdev, struct scatterlist *sgl, int nelems,
		 int dir)
{
	return swiotlb_unmap_sg_attrs(hwdev, sgl, nelems, dir, NULL);
}
EXPORT_SYMBOL(swiotlb_unmap_sg);

/*
 * Make physical memory consistent for a set of streaming mode DMA translations
 * after a transfer.
 *
 * The same as swiotlb_sync_single_* but for a scatter-gather list, same rules
 * and usage.
 */
void
swiotlb_sync_sg_for_cpu(struct device *hwdev, struct scatterlist *sgl,
			int nelems, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nelems, i)
		swiotlb_sync_single_for_cpu(hwdev, sg->dma_address,
					    sg->dma_length, dir);
}
EXPORT_SYMBOL(swiotlb_sync_sg_for_cpu);

void
swiotlb_sync_sg_for_device(struct device *hwdev, struct scatterlist *sgl,
			   int nelems, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nelems, i)
		swiotlb_sync_single_for_device(hwdev, sg->dma_address,
					       sg->dma_length, dir);
}
EXPORT_SYMBOL(swiotlb_sync_sg_for_device);

int
swiotlb_dma_mapping_error(struct device *hwdev, dma_addr_t dma_addr)
{
	return (dma_addr == swiotlb_virt_to_bus(hwdev, io_tlb_overflow_buffer));
}
EXPORT_SYMBOL(swiotlb_dma_mapping_error);

/*
 * Return whether the given PCI device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during PCI bus mastering, then you would pass 0x00ffffff as the mask to
 * this function.
 */
int
swiotlb_dma_supported (struct device *hwdev, u64 mask)
{
	return (mask >= ((1UL << dma_bits) - 1));
}
EXPORT_SYMBOL(swiotlb_dma_supported);
