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
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
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

#if 0
static void vm_region_dump(struct vm_region *head, char *fn)
{
	struct vm_region *c;

	printk("Consistent Allocation Map (%s):\n", fn);
	list_for_each_entry(c, &head->vm_list, vm_list) {
		printk(" %p:  %08lx - %08lx   (0x%08x)\n", c,
		       c->vm_start, c->vm_end, c->vm_end - c->vm_start);
	}
}
#else
#define vm_region_dump(head,fn)	do { } while(0)
#endif

static int vm_region_alloc(struct vm_region *head, struct vm_region *new, size_t size)
{
	unsigned long addr = head->vm_start, end = head->vm_end - size;
	struct vm_region *c;

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if ((addr + size) < addr)
			goto out;
		if ((addr + size) <= c->vm_start)
			goto found;
		addr = c->vm_end;
		if (addr > end)
			goto out;
	}

 found:
	/*
	 * Insert this entry _before_ the one we found.
	 */
	list_add_tail(&new->vm_list, &c->vm_list);
	new->vm_start = addr;
	new->vm_end = addr + size;

	return 0;

 out:
	return -ENOMEM;
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

/*
 * This allocates one page of cache-coherent memory space and returns
 * both the virtual and a "dma" address to that space.
 */
void *consistent_alloc(int gfp, size_t size, dma_addr_t *handle,
		       unsigned long cache_flags)
{
	struct page *page;
	struct vm_region *c;
	unsigned long order, flags;
	void *ret = NULL;
	int res;

	if (!consistent_pte) {
		printk(KERN_ERR "consistent_alloc: not initialised\n");
		dump_stack();
		return NULL;
	}

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page)
		goto no_page;

	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	{
		unsigned long kaddr = (unsigned long)page_address(page);
		dmac_inv_range(kaddr, kaddr + size);
	}

	/*
	 * Our housekeeping doesn't need to come from DMA,
	 * but it must not come from highmem.
	 */
	c = kmalloc(sizeof(struct vm_region),
		    gfp & ~(__GFP_DMA | __GFP_HIGHMEM));
	if (!c)
		goto no_remap;

	/*
	 * Attempt to allocate a virtual address in the
	 * consistent mapping region.
	 */
	spin_lock_irqsave(&consistent_lock, flags);
	vm_region_dump(&consistent_head, "before alloc");

	res = vm_region_alloc(&consistent_head, c, size);

	vm_region_dump(&consistent_head, "after alloc");
	spin_unlock_irqrestore(&consistent_lock, flags);

	if (!res) {
		pte_t *pte = consistent_pte + CONSISTENT_OFFSET(c->vm_start);
		struct page *end = page + (1 << order);
		pgprot_t prot = __pgprot(L_PTE_PRESENT | L_PTE_YOUNG |
					 L_PTE_DIRTY | L_PTE_WRITE |
					 cache_flags);

		/*
		 * Set the "dma handle"
		 */
		*handle = page_to_bus(page);

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

		ret = (void *)c->vm_start;
	}

 no_remap:
	if (ret == NULL) {
		kfree(c);
		__free_pages(page, order);
	}
 no_page:
	return ret;
}

/*
 * Since we have the DMA mask available to us here, we could try to do
 * a normal allocation, and only fall back to a "DMA" allocation if the
 * resulting bus address does not satisfy the dma_mask requirements.
 */
void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, int gfp)
{
	if (dev == NULL || *dev->dma_mask != 0xffffffff)
		gfp |= GFP_DMA;

	return consistent_alloc(gfp, size, handle, 0);
}

EXPORT_SYMBOL(dma_alloc_coherent);

/*
 * free a page as defined by the above mapping.
 */
void consistent_free(void *vaddr, size_t size, dma_addr_t handle)
{
	struct vm_region *c;
	unsigned long flags;
	pte_t *ptep;

	size = PAGE_ALIGN(size);

	spin_lock_irqsave(&consistent_lock, flags);
	vm_region_dump(&consistent_head, "before free");

	c = vm_region_find(&consistent_head, (unsigned long)vaddr);
	if (!c)
		goto no_area;

	if ((c->vm_end - c->vm_start) != size) {
		printk(KERN_ERR "consistent_free: wrong size (%ld != %d)\n",
		       c->vm_end - c->vm_start, size);
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

		printk(KERN_CRIT "consistent_free: bad page in kernel page "
		       "table\n");
	} while (size -= PAGE_SIZE);

	flush_tlb_kernel_range(c->vm_start, c->vm_end);

	list_del(&c->vm_list);

	vm_region_dump(&consistent_head, "after free");
	spin_unlock_irqrestore(&consistent_lock, flags);

	kfree(c);
	return;

 no_area:
	spin_unlock_irqrestore(&consistent_lock, flags);
	printk(KERN_ERR "consistent_free: trying to free "
	       "invalid area: %p\n", vaddr);
	dump_stack();
}

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
			printk(KERN_ERR "consistent_init: no pmd tables\n");
			ret = -ENOMEM;
			break;
		}
		WARN_ON(!pmd_none(*pmd));

		pte = pte_alloc_kernel(&init_mm, pmd, CONSISTENT_BASE);
		if (!pte) {
			printk(KERN_ERR "consistent_init: no pte tables\n");
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
