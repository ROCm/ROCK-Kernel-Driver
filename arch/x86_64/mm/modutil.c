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

#include <asm/uaccess.h>
#include <asm/system.h>

static struct vm_struct * modvmlist = NULL;

void module_unmap (void * addr)
{
	struct vm_struct **p, *tmp;

	if (!addr)
		return;
	if ((PAGE_SIZE-1) & (unsigned long) addr) {
		printk("Trying to unmap module with bad address (%p)\n", addr);
		return;
	}
	for (p = &modvmlist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
			vmfree_area_pages(VMALLOC_VMADDR(tmp->addr), tmp->size);
			kfree(tmp);
			return;
		}
	}
	printk("Trying to unmap nonexistent module vm area (%p)\n", addr);
}

void * module_map (unsigned long size)
{
	void * addr;
	struct vm_struct **p, *tmp, *area;

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
	*p = area;

	if (vmalloc_area_pages(VMALLOC_VMADDR(addr), size, GFP_KERNEL, PAGE_KERNEL)) {
		module_unmap(addr);
		return NULL;
	}
	return addr;
}
