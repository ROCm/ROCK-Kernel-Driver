#ifndef _ASM_X86_PAGE_64_H
#define _ASM_X86_PAGE_64_H

#include <asm/page_64_types.h>

#ifndef __ASSEMBLY__

/* duplicated to the one in bootmem.h */
extern unsigned long max_pfn;
#ifndef CONFIG_XEN
extern unsigned long phys_base;
#else
/* This would be nice, but the symbol name is too generic:
#define phys_base 0
*/
extern const unsigned long phys_base;
#endif

static inline unsigned long __phys_addr_nodebug(unsigned long x)
{
	unsigned long y = x - __START_KERNEL_map;

	/* use the carry flag to determine if x was < __START_KERNEL_map */
	x = y + ((x > y) ? phys_base : (__START_KERNEL_map - PAGE_OFFSET));

	return x;
}

#ifdef CONFIG_DEBUG_VIRTUAL
extern unsigned long __phys_addr(unsigned long);
extern unsigned long __phys_addr_symbol(unsigned long);
#else
#define __phys_addr(x)		__phys_addr_nodebug(x)
#define __phys_addr_symbol(x) \
	((unsigned long)(x) - __START_KERNEL_map + phys_base)
#endif

#define __phys_reloc_hide(x)	(x)

#ifdef CONFIG_FLATMEM
/*
 * While max_pfn is not exported, max_mapnr never gets initialized for non-Xen
 * other than for hotplugged memory.
 */
#ifndef CONFIG_XEN
#define pfn_valid(pfn)          ((pfn) < max_pfn)
#else
#define pfn_valid(pfn)          ((pfn) < max_mapnr)
#endif
#endif

void clear_page(void *page);
void copy_page(void *to, void *from);

#endif	/* !__ASSEMBLY__ */

#endif /* _ASM_X86_PAGE_64_H */
