#ifndef _PPC64_PGALLOC_H
#define _PPC64_PGALLOC_H

#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/processor.h>

extern kmem_cache_t *zero_cache;

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

static inline pgd_t *
pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(zero_cache, GFP_KERNEL);
}

static inline void
pgd_free(pgd_t *pgd)
{
	kmem_cache_free(zero_cache, pgd);
}

#define pgd_populate(MM, PGD, PMD)	pgd_set(PGD, PMD)

static inline pmd_t *
pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(zero_cache, GFP_KERNEL|__GFP_REPEAT);
}

static inline void
pmd_free(pmd_t *pmd)
{
	kmem_cache_free(zero_cache, pmd);
}

#define __pmd_free_tlb(tlb, pmd)	pmd_free(pmd)

#define pmd_populate_kernel(mm, pmd, pte) pmd_set(pmd, pte)
#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))

static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(zero_cache, GFP_KERNEL|__GFP_REPEAT);
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte = pte_alloc_one_kernel(mm, address);

	if (pte)
		return virt_to_page(pte);

	return NULL;
}

static inline void
pte_free_kernel(pte_t *pte)
{
	kmem_cache_free(zero_cache, pte);
}

#define pte_free(pte_page)	pte_free_kernel(page_address(pte_page))
#define __pte_free_tlb(tlb, pte)	pte_free(pte)

#define check_pgt_cache()	do { } while (0)

#endif /* _PPC64_PGALLOC_H */
