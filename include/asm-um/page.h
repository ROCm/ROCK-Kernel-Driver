#ifndef __UM_PAGE_H
#define __UM_PAGE_H

struct page;

#include "asm/arch/page.h"

#undef BUG
#undef PAGE_BUG
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

#define PAGE_OFFSET (uml_physmem)
#define KERNELBASE PAGE_OFFSET

#ifndef __ASSEMBLY__

extern void stop(void);

#define BUG() do { \
	panic("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
} while (0)

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

#endif /* __ASSEMBLY__ */

#define __va_space (8*1024*1024)

extern unsigned long region_pa(void *virt);
extern void *region_va(unsigned long phys);

#define __pa(virt) region_pa((void *) (virt))
#define __va(phys) region_va((unsigned long) (phys))

extern struct page *phys_to_page(unsigned long phys);

#define pfn_to_page(pfn) (phys_to_page(pfn << PAGE_SHIFT))
#define page_to_pfn(page) (page_to_phys(page) >> PAGE_SHIFT)
#define virt_to_page(v) (phys_to_page(__pa(v)))

extern struct page *page_mem_map(struct page *page);

#define pfn_valid(pfn) (page_mem_map(pfn_to_page(pfn)) != NULL)
#define virt_addr_valid(v) pfn_valid(__pa(v) >> PAGE_SHIFT)

extern struct page *arch_validate(struct page *page, int mask, int order);
#define HAVE_ARCH_VALIDATE

#endif
