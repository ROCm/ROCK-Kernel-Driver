#ifndef __UM_PAGE_H
#define __UM_PAGE_H

struct page;

#include "asm/arch/page.h"
#include "asm/bug.h"

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

extern unsigned long region_pa(void *virt);
extern void *region_va(unsigned long phys);

#define __pa(virt) region_pa((void *) (virt))
#define __va(phys) region_va((unsigned long) (phys))

extern unsigned long page_to_pfn(struct page *page);
extern struct page *pfn_to_page(unsigned long pfn);

extern struct page *phys_to_page(unsigned long phys);

#define virt_to_page(v) (phys_to_page(__pa(v)))

extern struct page *page_mem_map(struct page *page);

#define pfn_valid(pfn) (page_mem_map(pfn_to_page(pfn)) != NULL)
#define virt_addr_valid(v) pfn_valid(__pa(v) >> PAGE_SHIFT)

extern struct page *arch_validate(struct page *page, int mask, int order);
#define HAVE_ARCH_VALIDATE

#endif
