/*
 * Dynamic DMA mapping support for AMD Hammer.
 * 
 * Use the integrated AGP GART in the Hammer northbridge as an IOMMU for PCI.
 * This allows to use PCI devices that only support 32bit addresses on systems
 * with more than 4GB. 
 *
 * See Documentation/DMA-mapping.txt for the interface specification.
 * 
 * Copyright 2002 Andi Kleen, SuSE Labs.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/agp_backend.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/topology.h>
#include <linux/interrupt.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/mtrr.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/proto.h>
#include <asm/cacheflush.h>
#include <asm/kdebug.h>
#include <asm/proto.h>

#ifdef CONFIG_PREEMPT
#define preempt_atomic() in_atomic()
#else
#define preempt_atomic() 1
#endif

dma_addr_t bad_dma_address;

unsigned long iommu_bus_base;	/* GART remapping area (physical) */
static unsigned long iommu_size; 	/* size of remapping area bytes */
static unsigned long iommu_pages;	/* .. and in pages */

u32 *iommu_gatt_base; 		/* Remapping table */

int no_iommu; 
static int no_agp; 
#ifdef CONFIG_IOMMU_DEBUG
int panic_on_overflow = 1; 
int force_iommu = 1;
#else
int panic_on_overflow = 0;
int force_iommu = 0;
#endif
int iommu_merge = 0; 
int iommu_sac_force = 0; 

/* If this is disabled the IOMMU will use an optimized flushing strategy
   of only flushing when an mapping is reused. With it true the GART is flushed 
   for every mapping. Problem is that doing the lazy flush seems to trigger
   bugs with some popular PCI cards, in particular 3ware (but has been also
   also seen with Qlogic at least). */
int iommu_fullflush = 1;

#define MAX_NB 8

/* Allocation bitmap for the remapping area */ 
static spinlock_t iommu_bitmap_lock = SPIN_LOCK_UNLOCKED;
static unsigned long *iommu_gart_bitmap; /* guarded by iommu_bitmap_lock */

#define GPTE_VALID    1
#define GPTE_COHERENT 2
#define GPTE_ENCODE(x) \
	(((x) & 0xfffff000) | (((x) >> 32) << 4) | GPTE_VALID | GPTE_COHERENT)
#define GPTE_DECODE(x) (((x) & 0xfffff000) | (((u64)(x) & 0xff0) << 28))

#define to_pages(addr,size) \
	(round_up(((addr) & ~PAGE_MASK) + (size), PAGE_SIZE) >> PAGE_SHIFT)

#define for_all_nb(dev) \
	dev = NULL;	\
	while ((dev = pci_find_device(PCI_VENDOR_ID_AMD, 0x1103, dev))!=NULL)\
	     if (dev->bus->number == 0 && 				     \
		    (PCI_SLOT(dev->devfn) >= 24) && (PCI_SLOT(dev->devfn) <= 31))

static struct pci_dev *northbridges[MAX_NB];
static u32 northbridge_flush_word[MAX_NB];

#define EMERGENCY_PAGES 32 /* = 128KB */ 

#ifdef CONFIG_AGP
#define AGPEXTERN extern
#else
#define AGPEXTERN
#endif

/* backdoor interface to AGP driver */
AGPEXTERN int agp_memory_reserved;
AGPEXTERN __u32 *agp_gatt_table;

static unsigned long next_bit;  /* protected by iommu_bitmap_lock */
static int need_flush; 		/* global flush state. set for each gart wrap */
static dma_addr_t pci_map_area(struct pci_dev *dev, unsigned long phys_mem, 
			       size_t size, int dir);

static unsigned long alloc_iommu(int size) 
{ 	
	unsigned long offset, flags;

	spin_lock_irqsave(&iommu_bitmap_lock, flags);	
	offset = find_next_zero_string(iommu_gart_bitmap,next_bit,iommu_pages,size);
	if (offset == -1) {
		need_flush = 1;
	       	offset = find_next_zero_string(iommu_gart_bitmap,0,next_bit,size);
	}
	if (offset != -1) { 
		set_bit_string(iommu_gart_bitmap, offset, size); 
		next_bit = offset+size; 
		if (next_bit >= iommu_pages) { 
			next_bit = 0;
			need_flush = 1;
		} 
	} 
	if (iommu_fullflush)
		need_flush = 1;
	spin_unlock_irqrestore(&iommu_bitmap_lock, flags);      
	return offset;
} 

static void free_iommu(unsigned long offset, int size)
{ 
	unsigned long flags;
	if (size == 1) { 
		clear_bit(offset, iommu_gart_bitmap); 
		return;
	}
	spin_lock_irqsave(&iommu_bitmap_lock, flags);
	__clear_bit_string(iommu_gart_bitmap, offset, size);
	spin_unlock_irqrestore(&iommu_bitmap_lock, flags);
} 

/* 
 * Use global flush state to avoid races with multiple flushers.
 */
static void flush_gart(struct pci_dev *dev)
{ 
	unsigned long flags;
	int bus = dev ? dev->bus->number : -1;
	cpumask_const_t bus_cpumask = pcibus_to_cpumask(bus);
	int flushed = 0;
	int i;

	spin_lock_irqsave(&iommu_bitmap_lock, flags);
	if (need_flush) { 
		for (i = 0; i < MAX_NB; i++) {
			u32 w;
			if (!northbridges[i]) 
				continue;
			if (bus >= 0 && !(cpu_isset_const(i, bus_cpumask)))
				continue;
			pci_write_config_dword(northbridges[i], 0x9c, 
					       northbridge_flush_word[i] | 1); 
			/* Make sure the hardware actually executed the flush. */
			do { 
				pci_read_config_dword(northbridges[i], 0x9c, &w);
			} while (w & 1);
			flushed++;
		} 
		if (!flushed) 
			printk("nothing to flush? %d\n", bus);
		need_flush = 0;
	} 
	spin_unlock_irqrestore(&iommu_bitmap_lock, flags);
} 

/* 
 * Allocate memory for a consistent mapping.
 */
void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t *dma_handle)
{
	void *memory;
	int gfp = preempt_atomic() ? GFP_ATOMIC : GFP_KERNEL; 
	unsigned long dma_mask = 0;
	u64 bus;

	if (hwdev) 
		dma_mask = hwdev->dev.coherent_dma_mask;
	if (dma_mask == 0) 
		dma_mask = 0xffffffff; 

	/* Kludge to make it bug-to-bug compatible with i386. i386
	   uses the normal dma_mask for alloc_consistent. */
	if (hwdev)
	dma_mask &= hwdev->dma_mask;

 again:
	memory = (void *)__get_free_pages(gfp, get_order(size));
	if (memory == NULL)
		return NULL; 

	{
		int high, mmu;
		bus = virt_to_bus(memory);
	        high = (bus + size) >= dma_mask;
		mmu = high;
		if (force_iommu && !(gfp & GFP_DMA)) 
			mmu = 1;
		if (no_iommu || dma_mask < 0xffffffffUL) { 
			if (high) {
				if (!(gfp & GFP_DMA)) { 
					gfp |= GFP_DMA; 
					goto again;
				}
				goto free;
			}
			mmu = 0; 
		} 	
		memset(memory, 0, size); 
		if (!mmu) { 
			*dma_handle = virt_to_bus(memory);
			return memory;
		}
	} 

	*dma_handle = pci_map_area(hwdev, bus, size, PCI_DMA_BIDIRECTIONAL);
	if (*dma_handle == bad_dma_address)
		goto error; 
	flush_gart(hwdev);	
	return memory; 
	
error:
	if (panic_on_overflow)
		panic("pci_alloc_consistent: overflow %lu bytes\n", size); 
free:
	free_pages((unsigned long)memory, get_order(size)); 
	return NULL; 
}

/* 
 * Unmap consistent memory.
 * The caller must ensure that the device has finished accessing the mapping.
 */
void pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t bus)
{
	pci_unmap_single(hwdev, bus, size, 0);
	free_pages((unsigned long)vaddr, get_order(size)); 		
}

#ifdef CONFIG_IOMMU_LEAK

#define SET_LEAK(x) if (iommu_leak_tab) \
			iommu_leak_tab[x] = __builtin_return_address(0);
#define CLEAR_LEAK(x) if (iommu_leak_tab) \
			iommu_leak_tab[x] = 0;

/* Debugging aid for drivers that don't free their IOMMU tables */
static void **iommu_leak_tab; 
static int leak_trace;
int iommu_leak_pages = 20; 
void dump_leak(void)
{
	int i;
	static int dump; 
	if (dump || !iommu_leak_tab) return;
	dump = 1;
	show_stack(NULL,NULL);
	/* Very crude. dump some from the end of the table too */ 
	printk("Dumping %d pages from end of IOMMU:\n", iommu_leak_pages); 
	for (i = 0; i < iommu_leak_pages; i+=2) {
		printk("%lu: ", iommu_pages-i);
		printk_address((unsigned long) iommu_leak_tab[iommu_pages-i]);
		printk("%c", (i+1)%2 == 0 ? '\n' : ' '); 
	} 
	printk("\n");
}
#else
#define SET_LEAK(x)
#define CLEAR_LEAK(x)
#endif

static void iommu_full(struct pci_dev *dev, size_t size, int dir)
{
	/* 
	 * Ran out of IOMMU space for this operation. This is very bad.
	 * Unfortunately the drivers cannot handle this operation properly.
	 * Return some non mapped prereserved space in the aperture and 
	 * let the Northbridge deal with it. This will result in garbage
	 * in the IO operation. When the size exceeds the prereserved space
	 * memory corruption will occur or random memory will be DMAed 
	 * out. Hopefully no network devices use single mappings that big.
	 */ 
	
	printk(KERN_ERR 
  "PCI-DMA: Out of IOMMU space for %lu bytes at device %s[%s]\n",
	       size, dev ? pci_pretty_name(dev) : "", dev ? dev->slot_name : "?");

	if (size > PAGE_SIZE*EMERGENCY_PAGES) {
		if (dir == PCI_DMA_FROMDEVICE || dir == PCI_DMA_BIDIRECTIONAL)
			panic("PCI-DMA: Memory will be corrupted\n");
		if (dir == PCI_DMA_TODEVICE || dir == PCI_DMA_BIDIRECTIONAL) 
			panic("PCI-DMA: Random memory will be DMAed\n"); 
	} 

#ifdef CONFIG_IOMMU_LEAK
	dump_leak(); 
#endif
} 

static inline int need_iommu(struct pci_dev *dev, unsigned long addr, size_t size)
{ 
	u64 mask = dev ? dev->dma_mask : 0xffffffff;
	int high = addr + size >= mask;
	int mmu = high;
	if (force_iommu) 
		mmu = 1; 
	if (no_iommu) { 
		if (high) 
			panic("PCI-DMA: high address but no IOMMU.\n"); 
		mmu = 0; 
	} 	
	return mmu; 
}

static inline int nonforced_iommu(struct pci_dev *dev, unsigned long addr, size_t size)
{ 
	u64 mask = dev ? dev->dma_mask : 0xffffffff;
	int high = addr + size >= mask;
	int mmu = high;
	if (no_iommu) { 
		if (high) 
			panic("PCI-DMA: high address but no IOMMU.\n"); 
		mmu = 0; 
	} 	
	return mmu; 
}

/* Map a single continuous physical area into the IOMMU.
 * Caller needs to check if the iommu is needed and flush.
 */
static dma_addr_t pci_map_area(struct pci_dev *dev, unsigned long phys_mem, 
				size_t size, int dir)
{ 
	unsigned long npages = to_pages(phys_mem, size);
	unsigned long iommu_page = alloc_iommu(npages);
	int i;
	if (iommu_page == -1) {
		if (!nonforced_iommu(dev, phys_mem, size))
			return phys_mem; 
		if (panic_on_overflow)
			panic("pci_map_area overflow %lu bytes\n", size);
		iommu_full(dev, size, dir);
		return bad_dma_address;
	}

	for (i = 0; i < npages; i++) {
		iommu_gatt_base[iommu_page + i] = GPTE_ENCODE(phys_mem);
		SET_LEAK(iommu_page + i);
		phys_mem += PAGE_SIZE;
	}
	return iommu_bus_base + iommu_page*PAGE_SIZE + (phys_mem & ~PAGE_MASK);
}

/* Map a single area into the IOMMU */
dma_addr_t pci_map_single(struct pci_dev *dev, void *addr, size_t size, int dir)
{ 
	unsigned long phys_mem, bus;

	BUG_ON(dir == PCI_DMA_NONE);

#ifdef CONFIG_SWIOTLB
	if (swiotlb)
		return swiotlb_map_single(&dev->dev,addr,size,dir);
#endif

	phys_mem = virt_to_phys(addr); 
	if (!need_iommu(dev, phys_mem, size))
		return phys_mem; 

	bus = pci_map_area(dev, phys_mem, size, dir);
	flush_gart(dev); 
	return bus; 
} 

/* Fallback for pci_map_sg in case of overflow */ 
static int pci_map_sg_nonforce(struct pci_dev *dev, struct scatterlist *sg,
			       int nents, int dir)
{
	int i;

#ifdef CONFIG_IOMMU_DEBUG
	printk(KERN_DEBUG "pci_map_sg overflow\n");
#endif

 	for (i = 0; i < nents; i++ ) {
		struct scatterlist *s = &sg[i];
		unsigned long addr = page_to_phys(s->page) + s->offset; 
		if (nonforced_iommu(dev, addr, s->length)) { 
			addr = pci_map_area(dev, addr, s->length, dir); 
			if (addr == bad_dma_address) { 
				if (i > 0) 
					pci_unmap_sg(dev, sg, i, dir); 
				nents = 0; 
				sg[0].dma_length = 0;
				break;
			}
		}
		s->dma_address = addr;
		s->dma_length = s->length;
	}
	flush_gart(dev);
	return nents;
}

/* Map multiple scatterlist entries continuous into the first. */
static int __pci_map_cont(struct scatterlist *sg, int start, int stopat, 
		      struct scatterlist *sout, unsigned long pages)
{
	unsigned long iommu_start = alloc_iommu(pages);
	unsigned long iommu_page = iommu_start; 
	int i;

	if (iommu_start == -1)
		return -1;
	
	for (i = start; i < stopat; i++) {
		struct scatterlist *s = &sg[i];
		unsigned long pages, addr;
		unsigned long phys_addr = s->dma_address;
		
		BUG_ON(i > start && s->offset);
		if (i == start) {
			*sout = *s; 
			sout->dma_address = iommu_bus_base;
			sout->dma_address += iommu_page*PAGE_SIZE + s->offset;
			sout->dma_length = s->length;
		} else { 
			sout->dma_length += s->length; 
		}

		addr = phys_addr;
		pages = to_pages(s->offset, s->length); 
		while (pages--) { 
			iommu_gatt_base[iommu_page] = GPTE_ENCODE(addr); 
			SET_LEAK(iommu_page);
			addr += PAGE_SIZE;
			iommu_page++;
	} 
	} 
	BUG_ON(iommu_page - iommu_start != pages);	
	return 0;
}

static inline int pci_map_cont(struct scatterlist *sg, int start, int stopat, 
		      struct scatterlist *sout,
		      unsigned long pages, int need)
{
	if (!need) { 
		BUG_ON(stopat - start != 1);
		*sout = sg[start]; 
		sout->dma_length = sg[start].length; 
		return 0;
	} 
	return __pci_map_cont(sg, start, stopat, sout, pages);
}
		
/*
 * DMA map all entries in a scatterlist.
 * Merge chunks that have page aligned sizes into a continuous mapping. 
		 */
int pci_map_sg(struct pci_dev *dev, struct scatterlist *sg, int nents, int dir)
{
	int i;
	int out;
	int start;
	unsigned long pages = 0;
	int need = 0, nextneed;

	BUG_ON(dir == PCI_DMA_NONE);
	if (nents == 0) 
		return 0;

#ifdef CONFIG_SWIOTLB
	if (swiotlb)
		return swiotlb_map_sg(&dev->dev,sg,nents,dir);
#endif

	out = 0;
	start = 0;
	for (i = 0; i < nents; i++) {
		struct scatterlist *s = &sg[i];
		dma_addr_t addr = page_to_phys(s->page) + s->offset;
		s->dma_address = addr;
		BUG_ON(s->length == 0); 

		nextneed = need_iommu(dev, addr, s->length); 

		/* Handle the previous not yet processed entries */
		if (i > start) {
			struct scatterlist *ps = &sg[i-1];
			/* Can only merge when the last chunk ends on a page 
			   boundary and the new one doesn't have an offset. */
			if (!iommu_merge || !nextneed || !need || s->offset ||
			    (ps->offset + ps->length) % PAGE_SIZE) { 
				if (pci_map_cont(sg, start, i, sg+out, pages, 
						 need) < 0)
					goto error;
				out++;
				pages = 0;
				start = i;	
			}
	}

		need = nextneed;
		pages += to_pages(s->offset, s->length);
	}
	if (pci_map_cont(sg, start, i, sg+out, pages, need) < 0)
		goto error;
	out++;
	flush_gart(dev);
	if (out < nents) 
		sg[out].dma_length = 0; 
	return out;

error:
	flush_gart(NULL);
	pci_unmap_sg(dev, sg, nents, dir);
	/* When it was forced try again unforced */
	if (force_iommu) 
		return pci_map_sg_nonforce(dev, sg, nents, dir);
	if (panic_on_overflow)
		panic("pci_map_sg: overflow on %lu pages\n", pages); 
	iommu_full(dev, pages << PAGE_SHIFT, dir);
	for (i = 0; i < nents; i++)
		sg[i].dma_address = bad_dma_address;
	return 0;
} 

/*
 * Free a PCI mapping.
 */ 
void pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr,
		      size_t size, int direction)
{
	unsigned long iommu_page; 
	int npages;
	int i;

#ifdef CONFIG_SWIOTLB
	if (swiotlb) {
		swiotlb_unmap_single(&hwdev->dev,dma_addr,size,direction);
		return;
	}
#endif

	if (dma_addr < iommu_bus_base + EMERGENCY_PAGES*PAGE_SIZE || 
	    dma_addr >= iommu_bus_base + iommu_size)
		return;
	iommu_page = (dma_addr - iommu_bus_base)>>PAGE_SHIFT;	
	npages = to_pages(dma_addr, size);
	for (i = 0; i < npages; i++) { 
		iommu_gatt_base[iommu_page + i] = 0; 
		CLEAR_LEAK(iommu_page + i);
	}
	free_iommu(iommu_page, npages);
}

/* 
 * Wrapper for pci_unmap_single working with scatterlists.
 */ 
void pci_unmap_sg(struct pci_dev *dev, struct scatterlist *sg, int nents, 
		  int dir)
{
	int i;
	for (i = 0; i < nents; i++) { 
		struct scatterlist *s = &sg[i];
		if (!s->dma_length || !s->length) 
			break;
		pci_unmap_single(dev, s->dma_address, s->dma_length, dir);
	}
}

int pci_dma_supported(struct pci_dev *dev, u64 mask)
{
	/* Copied from i386. Doesn't make much sense, because it will 
	   only work for pci_alloc_consistent. 
	   The caller just has to use GFP_DMA in this case. */
        if (mask < 0x00ffffff)
                return 0;

	/* Tell the device to use SAC when IOMMU force is on. 
	   This allows the driver to use cheaper accesses in some cases.

	   Problem with this is that if we overflow the IOMMU area
	   and return DAC as fallback address the device may not handle it correctly.
	   
	   As a special case some controllers have a 39bit address mode 
	   that is as efficient as 32bit (aic79xx). Don't force SAC for these.
	   Assume all masks <= 40 bits are of this type. Normally this doesn't
	   make any difference, but gives more gentle handling of IOMMU overflow. */
	if (iommu_sac_force && (mask >= 0xffffffffffULL)) { 
		printk(KERN_INFO "%s: Force SAC with mask %Lx\n", dev->slot_name,mask);
		return 0; 
	}

	return 1;
} 

EXPORT_SYMBOL(pci_unmap_sg);
EXPORT_SYMBOL(pci_map_sg);
EXPORT_SYMBOL(pci_map_single);
EXPORT_SYMBOL(pci_unmap_single);
EXPORT_SYMBOL(pci_dma_supported);
EXPORT_SYMBOL(no_iommu);
EXPORT_SYMBOL(force_iommu); 
EXPORT_SYMBOL(bad_dma_address);
EXPORT_SYMBOL(iommu_merge);

static __init unsigned long check_iommu_size(unsigned long aper, u64 aper_size)
{ 
	unsigned long a; 
	if (!iommu_size) { 
		iommu_size = aper_size; 
		if (!no_agp) 
			iommu_size /= 2; 
	} 

	a = aper + iommu_size; 
	iommu_size -= round_up(a, LARGE_PAGE_SIZE) - a;

	if (iommu_size < 64*1024*1024) 
		printk(KERN_WARNING
  "PCI-DMA: Warning: Small IOMMU %luMB. Consider increasing the AGP aperture in BIOS\n",iommu_size>>20); 
	
	return iommu_size;
} 

static __init unsigned read_aperture(struct pci_dev *dev, u32 *size) 
{ 
	unsigned aper_size = 0, aper_base_32;
	u64 aper_base;
	unsigned aper_order;

	pci_read_config_dword(dev, 0x94, &aper_base_32); 
	pci_read_config_dword(dev, 0x90, &aper_order);
	aper_order = (aper_order >> 1) & 7;	

	aper_base = aper_base_32 & 0x7fff; 
	aper_base <<= 25;

	aper_size = (32 * 1024 * 1024) << aper_order; 
	if (aper_base + aper_size >= 0xffffffff || !aper_size)
		aper_base = 0;

	*size = aper_size;
	return aper_base;
} 

/* 
 * Private Northbridge GATT initialization in case we cannot use the
 * AGP driver for some reason.  
 */
static __init int init_k8_gatt(struct agp_kern_info *info)
{ 
	struct pci_dev *dev;
	void *gatt;
	unsigned aper_base, new_aper_base;
	unsigned aper_size, gatt_size, new_aper_size;
	
	aper_size = aper_base = info->aper_size = 0;
	for_all_nb(dev) { 
		new_aper_base = read_aperture(dev, &new_aper_size); 
		if (!new_aper_base) 
			goto nommu; 
		
		if (!aper_base) { 
			aper_size = new_aper_size;
			aper_base = new_aper_base;
		} 
		if (aper_size != new_aper_size || aper_base != new_aper_base) 
			goto nommu;
	}
	if (!aper_base)
		goto nommu; 
	info->aper_base = aper_base;
	info->aper_size = aper_size>>20; 

	gatt_size = (aper_size >> PAGE_SHIFT) * sizeof(u32); 
	gatt = (void *)__get_free_pages(GFP_KERNEL, get_order(gatt_size)); 
	if (!gatt) 
		panic("Cannot allocate GATT table"); 
	memset(gatt, 0, gatt_size); 
	agp_gatt_table = gatt;
	
	for_all_nb(dev) { 
		u32 ctl; 
		u32 gatt_reg; 

		gatt_reg = __pa(gatt) >> 12; 
		gatt_reg <<= 4; 
		pci_write_config_dword(dev, 0x98, gatt_reg);
		pci_read_config_dword(dev, 0x90, &ctl); 

		ctl |= 1;
		ctl &= ~((1<<4) | (1<<5));

		pci_write_config_dword(dev, 0x90, ctl); 
	}
	flush_gart(NULL); 
	
	printk("PCI-DMA: aperture base @ %x size %u KB\n",aper_base, aper_size>>10); 
	return 0;

 nommu:
 	/* Should not happen anymore */
	printk(KERN_ERR "PCI-DMA: More than 4GB of RAM and no IOMMU\n"
	       KERN_ERR "PCI-DMA: 32bit PCI IO may malfunction."); 
	return -1; 
} 

extern int agp_amd64_init(void);

static int __init pci_iommu_init(void)
{ 
	struct agp_kern_info info;
	unsigned long aper_size;
	unsigned long iommu_start;
	struct pci_dev *dev;
		

#ifndef CONFIG_AGP_AMD64
	no_agp = 1; 
#else
	/* Makefile puts PCI initialization via subsys_initcall first. */
	/* Add other K8 AGP bridge drivers here */
	no_agp = no_agp || 
		(agp_amd64_init() < 0) || 
		(agp_copy_info(&info) < 0); 
#endif	

	if (swiotlb) { 
		no_iommu = 1;
		printk(KERN_INFO "PCI-DMA: Using software bounce buffering for  IO (SWIOTLB)\n"); 
		return -1; 
	} 
	
	if (no_iommu || (!force_iommu && end_pfn < 0xffffffff>>PAGE_SHIFT) || 
	    !iommu_aperture) {
		printk(KERN_INFO "PCI-DMA: Disabling IOMMU.\n"); 
		no_iommu = 1;
		return -1;
	}

	if (no_agp) { 
		int err = -1;
		printk(KERN_INFO "PCI-DMA: Disabling AGP.\n");
		no_agp = 1;
		if (force_iommu || end_pfn >= 0xffffffff>>PAGE_SHIFT)
			err = init_k8_gatt(&info);
		if (err < 0) { 
			printk(KERN_INFO "PCI-DMA: Disabling IOMMU.\n"); 
			no_iommu = 1;
			return -1;
		}
	} 
	
	aper_size = info.aper_size * 1024 * 1024;	
	iommu_size = check_iommu_size(info.aper_base, aper_size); 
	iommu_pages = iommu_size >> PAGE_SHIFT; 

	iommu_gart_bitmap = (void*)__get_free_pages(GFP_KERNEL, 
						    get_order(iommu_pages/8)); 
	if (!iommu_gart_bitmap) 
		panic("Cannot allocate iommu bitmap\n"); 
	memset(iommu_gart_bitmap, 0, iommu_pages/8);

#ifdef CONFIG_IOMMU_LEAK
	if (leak_trace) { 
		iommu_leak_tab = (void *)__get_free_pages(GFP_KERNEL, 
				  get_order(iommu_pages*sizeof(void *)));
		if (iommu_leak_tab) 
			memset(iommu_leak_tab, 0, iommu_pages * 8); 
		else
			printk("PCI-DMA: Cannot allocate leak trace area\n"); 
	} 
#endif

	/* 
	 * Out of IOMMU space handling.
	 * Reserve some invalid pages at the beginning of the GART. 
	 */ 
	set_bit_string(iommu_gart_bitmap, 0, EMERGENCY_PAGES); 

	agp_memory_reserved = iommu_size;	
	printk(KERN_INFO
	       "PCI-DMA: Reserving %luMB of IOMMU area in the AGP aperture\n",
	       iommu_size>>20); 

	iommu_start = aper_size - iommu_size;	
	iommu_bus_base = info.aper_base + iommu_start; 
	bad_dma_address = iommu_bus_base;
	iommu_gatt_base = agp_gatt_table + (iommu_start>>PAGE_SHIFT);

	/* 
	 * Unmap the IOMMU part of the GART. The alias of the page is
	 * always mapped with cache enabled and there is no full cache
	 * coherency across the GART remapping. The unmapping avoids
	 * automatic prefetches from the CPU allocating cache lines in
	 * there. All CPU accesses are done via the direct mapping to
	 * the backing memory. The GART address is only used by PCI
	 * devices. 
	 */
	clear_kernel_mapping((unsigned long)__va(iommu_bus_base), iommu_size);

	for_all_nb(dev) {
		u32 flag; 
		int cpu = PCI_SLOT(dev->devfn) - 24;
		if (cpu >= MAX_NB)
			continue;
		northbridges[cpu] = dev;
		pci_read_config_dword(dev, 0x9c, &flag); /* cache flush word */
		northbridge_flush_word[cpu] = flag; 
	}
		     
	flush_gart(NULL);

	return 0;
} 

/* Must execute after PCI subsystem */
fs_initcall(pci_iommu_init);

/* iommu=[size][,noagp][,off][,force][,noforce][,leak][,memaper[=order]][,merge]
         [,forcesac][,fullflush][,nomerge]
   size  set size of iommu (in bytes) 
   noagp don't initialize the AGP driver and use full aperture.
   off   don't use the IOMMU
   leak  turn on simple iommu leak tracing (only when CONFIG_IOMMU_LEAK is on)
   memaper[=order] allocate an own aperture over RAM with size 32MB^order.  
   noforce don't force IOMMU usage. Default.
   force  Force IOMMU.
   merge  Do SG merging. Implies force (experimental)  
   nomerge Don't do SG merging.
   forcesac For SAC mode for masks <40bits  (experimental)
   fullflush Flush IOMMU on each allocation (default) 
   nofullflush Don't use IOMMU fullflush
   allowed  overwrite iommu off workarounds for specific chipsets.
   soft	 Use software bounce buffering (default for Intel machines)
*/
__init int iommu_setup(char *opt) 
{ 
    int arg;
    char *p = opt;
    
    for (;;) { 
	    if (!memcmp(p,"noagp", 5))
		    no_agp = 1;
	    if (!memcmp(p,"off", 3))
		    no_iommu = 1;
	    if (!memcmp(p,"force", 5)) {
		    force_iommu = 1;
		    iommu_aperture_allowed = 1;
	    }
	    if (!memcmp(p,"allowed",7))
		    iommu_aperture_allowed = 1;
	    if (!memcmp(p,"noforce", 7)) { 
		    iommu_merge = 0;
		    force_iommu = 0;
	    }
	    if (!memcmp(p, "memaper", 7)) { 
		    fallback_aper_force = 1; 
		    p += 7; 
		    if (*p == '=' && get_option(&p, &arg))
			    fallback_aper_order = arg;
	    } 
	    if (!memcmp(p, "panic", 5))
		    panic_on_overflow = 1;
	    if (!memcmp(p, "nopanic", 7))
		    panic_on_overflow = 0;	    
	    if (!memcmp(p, "merge", 5)) { 
		    iommu_merge = 1;
		    force_iommu = 1; 
	    }
	    if (!memcmp(p, "nomerge", 7))
		    iommu_merge = 0;
	    if (!memcmp(p, "forcesac", 8))
		    iommu_sac_force = 1;
	    if (!memcmp(p, "fullflush", 9))
		    iommu_fullflush = 1;
	    if (!memcmp(p, "nofullflush", 11))
		    iommu_fullflush = 0;
	    if (!memcmp(p, "soft", 4))
		    swiotlb = 1;
#ifdef CONFIG_IOMMU_LEAK
	    if (!memcmp(p,"leak", 4)) { 
		    leak_trace = 1;
		    p += 4; 
		    if (*p == '=') ++p;
		    if (isdigit(*p) && get_option(&p, &arg))
			    iommu_leak_pages = arg;
	    } else
#endif
	    if (isdigit(*p) && get_option(&p, &arg)) 
		    iommu_size = arg;
	    do {
		    if (*p == ' ' || *p == 0) 
			    return 0; 
	    } while (*p++ != ','); 
    }
    return 1;
} 
