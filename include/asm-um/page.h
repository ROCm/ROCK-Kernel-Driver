/*
 *  Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __UM_PAGE_H
#define __UM_PAGE_H

struct page;

#include "asm/arch/page.h"

#undef __pa
#undef __va
#undef pfn_to_page
#undef page_to_pfn
#undef virt_to_page
#undef pfn_valid
#undef virt_addr_valid
#undef VALID_PAGE
#undef PAGE_OFFSET
#undef KERNELBASE

extern unsigned long uml_physmem;

#define PAGE_OFFSET (uml_physmem)
#define KERNELBASE PAGE_OFFSET

#define __va_space (8*1024*1024)

extern unsigned long to_phys(void *virt);
extern void *to_virt(unsigned long phys);

#define __pa(virt) to_phys((void *) virt)
#define __va(phys) to_virt((unsigned long) phys)

#define page_to_pfn(page) ((page) - mem_map)
#define pfn_to_page(pfn) (mem_map + (pfn))

#define phys_to_pfn(p) ((p) >> PAGE_SHIFT)
#define pfn_to_phys(pfn) ((pfn) << PAGE_SHIFT)

#define pfn_valid(pfn) ((pfn) < max_mapnr)
#define virt_addr_valid(v) pfn_valid(phys_to_pfn(__pa(v)))
  
extern struct page *arch_validate(struct page *page, int mask, int order);
#define HAVE_ARCH_VALIDATE

extern void arch_free_page(struct page *page, int order);
#define HAVE_ARCH_FREE_PAGE

#endif
