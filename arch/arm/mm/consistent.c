/*
 *  linux/arch/arm/mm/consistent.c
 *
 *  Copyright (C) 2000-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  DMA uncached mapping support.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/tlbflush.h>

#define CONSISTENT_BASE	(0xffc00000)
#define CONSISTENT_END	(0xffe00000)
#define CONSISTENT_OFFSET(x)	(((unsigned long)(x) - CONSISTENT_BASE) >> PAGE_SHIFT)

/*
 * This is the page table (2MB) covering uncached, DMA consistent allocations
 */
static pte_t *consistent_pte;
static spinlock_t consistent_lock = SPIN_LOCK_UNLOCKED;

/*
 * VM region handling support.
 *
 * This should become something generic, handling VM region allocations for
 * vmalloc and similar (ioremap, module space, etc).
 *
 * I envisage vmalloc()'s supporting vm_struct becoming:
 *
 *  struct vm_struct {
 *    struct vm_region	region;
 *    unsigned long	flags;
 *    struct page	**pages;
 *    unsigned int	nr_pages;
 *    unsigned long	phys_addr;
 *  };
 *
 * get_vm_area() would then call vm_region_alloc with an appropriate
 * struct vm_region head (eg):
 *
 *  struct vm_region vmalloc_head = {
 *	.vm_list	= LIST_HEAD_INIT(vmalloc_head.vm_list),
 *	.vm_start	= VMALLOC_START,
 *	.vm_end		= VMALLOC_END,
 *  };
 *
 * However, vmalloc_head.vm_start is variable (typically, it is dependent on
 * the amount of RAM found at boot time.)  I would imagine that get_vm_area()
 * would have to initialise this each time prior to calling vm_region_alloc().
 */
struct vm_region {
	struct list_head	vm_list;
	unsigned long		vm_start;
	unsigned long		vm_end;
};

static struct vm_region consistent_head = {
	.vm_list	= LIST_HEAD_INIT(consistent_head.vm_list),
	.vm_start	= CONSISTENT_BASE,
	.vm_end		= CONSISTENT_END,
};

static struct vm_region *
vm_region_alloc(struct vm_region *head, size_t size, int gfp)
{
	unsigned long addr = head->vm_start, end = head->vm_end - size;
	unsigned long flags;
	struct vm_region *c, *new;

	new = kmalloc(sizeof(struct vm_region), gfp);
	if (!new)
		goto out;

	spin_lock_irqsave(&consistent_lock, flags);

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if ((addr + size) < addr)
			goto nospc;
		if ((addr + size) <= c->vm_start)
			goto found;
		addr = c->vm_end;
		if (addr > end)
			goto nospc;
	}

 found:
	/*
	 * Insert this entry _before_ the one we found.
	 */
	list_add_tail(&new->vm_list, &c->vm_list);
	new->vm_start = addr;
	new->vm_end = addr + size;

	spin_unlock_irqrestore(&consistent_lock, flags);
	return new;

 nospc:
	spin_unlock_irqrestore(&consistent_lock, flags);
	kfree(new);
 out:
	return NULL;
}

static struct vm_region *vm_region_find(struct vm_region *head, unsigned long addr)
{
	struct vm_region *c;
	
	list_for_each_entry(c, &head->vm_list, vm_list) {
		if (c->vm_start == addr)
			goto out;
	}
	c = NULL;
 out:
	return c;
}

#ifdef CONFIG_HUGETLB_PAGE
#error ARM Coherent DMA allocator does not (yet) support huge TLB
#endif

static void *
__dma_alloc(struct device *dev, size_t size, dma_addr_t *handle, int gfp,
	    pgprot_t prot)
{
	struct page *page;
	struct vm_region *c;
	unsigned long order;
	u64 mask = 0x00ffffff, limit; /* ISA default */

	if (!consistent_pte) {
		printk(KERN_ERR "%s: not initialised\n", __func__);
		dump_stack();
		return NULL;
	}

	if (dev) {
		mask = dev->coherent_dma_mask;
		if (mask == 0) {
			dev_warn(dev, "coherent DMA mask is unset\n");
			return NULL;
		}
	}

	size = PAGE_ALIGN(size);
	limit = (mask + 1) & ~mask;
	if ((limit && size >= limit) || size >= (CONSISTENT_END - CONSISTENT_BASE)) {
		printk(KERN_WARNING "coherent allocation too big (requested %#x mask %#Lx)\n",
		       size, mask);
		*handle = ~0;
		return NULL;
	}

	order = get_order(size);

	if (mask != 0xffffffff)
		gfp |= GFP_DMA;

	page = alloc_pages(gfp, order);
	if (!page)
		goto no_page;

	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	{
		unsigned long kaddr = (unsigned long)page_address(page);
		memset(page_address(page), 0, size);
		dmac_flush_range(kaddr, kaddr + size);
	}

	/*
	 * Allocate a virtual address in the consistent mapping region.
	 */
	c = vm_region_alloc(&consistent_head, size,
			    gfp & ~(__GFP_DMA | __GFP_HIGHMEM));
	if (c) {
		pte_t *pte = consistent_pte + CONSISTENT_OFFSET(c->vm_start);
		struct page *end = page + (1 << order);

		/*
		 * Set the "dma handle"
		 */
		*handle = page_to_dma(dev, page);

		do {
			BUG_ON(!pte_none(*pte));

			set_page_count(page, 1);
			SetPageReserved(page);
			set_pte(pte, mk_pte(page, prot));
			page++;
			pte++;
		} while (size -= PAGE_SIZE);

		/*
		 * Free the otherwise unused pages.
		 */
		while (page < end) {
			set_page_count(page, 1);
			__free_page(page);
			page++;
		}

		return (void *)c->vm_start;
	}

	if (page)
		__free_pages(page, order);
 no_page:
	return NULL;
}

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, int gfp)
{
	return __dma_alloc(dev, size, handle, gfp,
			   pgprot_noncached(pgprot_kernel));
}
EXPORT_SYMBOL(dma_alloc_coherent);

/*
 * Allocate a writecombining region, in much the same way as
 * dma_alloc_coherent above.
 */
void *
dma_alloc_writecombine(struct device *dev, size_t size, dma_addr_t *handle, int gfp)
{
	return __dma_alloc(dev, size, handle, gfp,
			   pgprot_writecombine(pgprot_kernel));
}
EXPORT_SYMBOL(dma_alloc_writecombine);

/*
 * free a page as defined by the above mapping.
 */
void dma_free_coherent(struct device *dev, size_t size, void *cpu_addr, dma_addr_t handle)
{
	struct vm_region *c;
	unsigned long flags;
	pte_t *ptep;

	size = PAGE_ALIGN(size);

	spin_lock_irqsave(&consistent_lock, flags);

	c = vm_region_find(&consistent_head, (unsigned long)cpu_addr);
	if (!c)
		goto no_area;

	if ((c->vm_end - c->vm_start) != size) {
		printk(KERN_ERR "%s: freeing wrong coherent size (%ld != %d)\n",
		       __func__, c->vm_end - c->vm_start, size);
		dump_stack();
		size = c->vm_end - c->vm_start;
	}

	ptep = consistent_pte + CONSISTENT_OFFSET(c->vm_start);
	do {
		pte_t pte = ptep_get_and_clear(ptep);
		unsigned long pfn;

		ptep++;

		if (!pte_none(pte) && pte_present(pte)) {
			pfn = pte_pfn(pte);

			if (pfn_valid(pfn)) {
				struct page *page = pfn_to_page(pfn);
				ClearPageReserved(page);

				__free_page(page);
				continue;
			}
		}

		printk(KERN_CRIT "%s: bad page in kernel page table\n",
		       __func__);
	} while (size -= PAGE_SIZE);

	flush_tlb_kernel_range(c->vm_start, c->vm_end);

	list_del(&c->vm_list);

	spin_unlock_irqrestore(&consistent_lock, flags);

	kfree(c);
	return;

 no_area:
	spin_unlock_irqrestore(&consistent_lock, flags);
	printk(KERN_ERR "%s: trying to free invalid coherent area: %p\n",
	       __func__, cpu_addr);
	dump_stack();
}
EXPORT_SYMBOL(dma_free_coherent);

/*
 * Initialise the consistent memory allocation.
 */
static int __init consistent_init(void)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int ret = 0;

	spin_lock(&init_mm.page_table_lock);

	do {
		pgd = pgd_offset(&init_mm, CONSISTENT_BASE);
		pmd = pmd_alloc(&init_mm, pgd, CONSISTENT_BASE);
		if (!pmd) {
			printk(KERN_ERR "%s: no pmd tables\n", __func__);
			ret = -ENOMEM;
			break;
		}
		WARN_ON(!pmd_none(*pmd));

		pte = pte_alloc_kernel(&init_mm, pmd, CONSISTENT_BASE);
		if (!pte) {
			printk(KERN_ERR "%s: no pte tables\n", __func__);
			ret = -ENOMEM;
			break;
		}

		consistent_pte = pte;
	} while (0);

	spin_unlock(&init_mm.page_table_lock);

	return ret;
}

core_initcall(consistent_init);

/*
 * Make an area consistent for devices.
 */
void consistent_sync(void *vaddr, size_t size, int direction)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end   = start + size;

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		dmac_inv_range(start, end);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		dmac_clean_range(start, end);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		dmac_flush_range(start, end);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL(consistent_sync);
