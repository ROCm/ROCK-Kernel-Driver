#ifndef _I386_RMAP_H
#define _I386_RMAP_H

/* nothing to see, move along */
#include <asm-generic/rmap.h>

#ifdef CONFIG_HIGHPTE
static inline pte_t *rmap_ptep_map(pte_addr_t pte_paddr)
{
	unsigned long pfn = (unsigned long)(pte_paddr >> PAGE_SHIFT);
	unsigned long off = ((unsigned long)pte_paddr) & ~PAGE_MASK;
	return (pte_t *)((char *)kmap_atomic(pfn_to_page(pfn), KM_PTE2) + off);
}

static inline void rmap_ptep_unmap(pte_t *pte)
{
	kunmap_atomic(pte, KM_PTE2);
}
#endif

#endif
