/*  $Id: modutil.c,v 1.11 2001/12/05 06:05:35 davem Exp $
 *  arch/sparc64/mm/modutil.c
 *
 *  Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Based upon code written by Linus Torvalds and others.
 */

#warning "major untested changes to this file  --hch (2002/08/05)"
 
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/system.h>

static struct vm_struct * modvmlist = NULL;

void module_unmap (void * addr)
{
	struct vm_struct **p, *tmp;
	int i;

	if (!addr)
		return;
	if ((PAGE_SIZE-1) & (unsigned long) addr) {
		printk("Trying to unmap module with bad address (%p)\n", addr);
		return;
	}

	for (p = &modvmlist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
		}
	}
	printk("Trying to unmap nonexistent module vm area (%p)\n", addr);
	return;

found:

	unmap_vm_area(tmp);
	
	for (i = 0; i < tmp->nr_pages; i++) {
		if (unlikely(!tmp->pages[i]))
			BUG();
		__free_page(tmp->pages[i]);
	}

	kfree(tmp->pages);
	kfree(tmp);
}


void * module_map (unsigned long size)
{
	struct vm_struct **p, *tmp, *area;
	struct vm_struct *area;
	struct page **pages;
	void * addr;
	unsigned int nr_pages, array_size, i;


	size = PAGE_ALIGN(size);
	if (!size || size > MODULES_LEN) return NULL;
		
	addr = (void *) MODULES_VADDR;
	for (p = &modvmlist; (tmp = *p) ; p = &tmp->next) {
		if (size + (unsigned long) addr < (unsigned long) tmp->addr)
			break;
		addr = (void *) (tmp->size + (unsigned long) tmp->addr);
	}
	if ((unsigned long) addr + size >= MODULES_END) return NULL;
	
	area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area) return NULL;
	area->size = size + PAGE_SIZE;
	area->addr = addr;
	area->next = *p;
	area->pages = NULL;
	area->nr_pages = 0;
	area->phys_addr = 0;
	*p = area;

	nr_pages = (size+PAGE_SIZE) >> PAGE_SHIFT;
	array_size = (nr_pages * sizeof(struct page *));

	area->nr_pages = nr_pages;
	area->pages = pages = kmalloc(array_size, (gfp_mask & ~__GFP_HIGHMEM));
	if (!area->pages)
		return NULL;
	memset(area->pages, 0, array_size);

	for (i = 0; i < area->nr_pages; i++) {
		area->pages[i] = alloc_page(gfp_mask);
		if (unlikely(!area->pages[i]))
			goto fail;
	}
	
	if (map_vm_area(area, prot, &pages))
		goto fail;
	return area->addr;

fail:
	vfree(area->addr);
	return NULL;
}
}
