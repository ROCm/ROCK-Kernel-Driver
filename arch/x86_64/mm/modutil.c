/*  arch/x86_64/mm/modutil.c
 *
 *  Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Based upon code written by Linus Torvalds and others.
 * 
 *  Blatantly copied from sparc64 for x86-64 by Andi Kleen. 
 *  Should use direct mapping with 2MB pages. This would need extension
 *  of the kernel mapping.
 */
 
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/err.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>

/* FIXME: If module_region == mod->init_region, trim exception
   table entries. */
void module_free(struct module *mod, void *module_region)
{
	struct vm_struct **p, *tmp;
	int i;
	unsigned long addr = (unsigned long)module_region;

	if (!addr)
		return;
	if ((PAGE_SIZE-1) & addr) {
		printk("Trying to unmap module with bad address (%lx)\n", addr);
		return;
	}
	write_lock(&vmlist_lock); 
	for (p = &vmlist ; (tmp = *p) ; p = &tmp->next) {
		if ((unsigned long)tmp->addr == addr) {
			*p = tmp->next;
			write_unlock(&vmlist_lock); 
			goto found;
		}
	}
	write_unlock(&vmlist_lock); 
	printk("Trying to unmap nonexistent module vm area (%lx)\n", addr);
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

void * module_alloc (unsigned long size)
{
	struct vm_struct **p, *tmp, *area;
	struct page **pages;
	void * addr;
	unsigned int nr_pages, array_size, i;

	if (!size)
		return NULL;
	size = PAGE_ALIGN(size);
	if (size > MODULES_LEN)
		return ERR_PTR(-ENOMEM);
		
	addr = (void *) MODULES_VADDR;
	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		if (size + (unsigned long) addr < (unsigned long) tmp->addr)
			break;
		addr = (void *) (tmp->size + (unsigned long) tmp->addr);
	}
	if ((unsigned long) addr + size >= MODULES_END)
		return ERR_PTR(-ENOMEM);
	
	area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return ERR_PTR(-ENOMEM);
	area->size = size + PAGE_SIZE;
	area->addr = addr;
	area->next = *p;
	area->pages = NULL;
	area->nr_pages = 0;
	area->phys_addr = 0;
	*p = area;

	nr_pages = size >> PAGE_SHIFT;
	array_size = (nr_pages * sizeof(struct page *));

	area->nr_pages = nr_pages;
	area->pages = pages = kmalloc(array_size, GFP_KERNEL);
	if (!area->pages)
		goto fail;

	memset(area->pages, 0, array_size);

	for (i = 0; i < area->nr_pages; i++) {
		area->pages[i] = alloc_page(GFP_KERNEL);
		if (unlikely(!area->pages[i]))
			goto fail;
	}
	
	if (map_vm_area(area, PAGE_KERNEL_EXECUTABLE, &pages)) {
		unmap_vm_area(area);
		goto fail;
	}

	memset(area->addr, 0, size);
	return area->addr;

fail:
	if (area->pages) {
		for (i = 0; i < area->nr_pages; i++) {
			if (area->pages[i])
				__free_page(area->pages[i]);
		}
		kfree(area->pages);
	}
	kfree(area);

	return ERR_PTR(-ENOMEM);
}

