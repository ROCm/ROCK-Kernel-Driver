#ifndef _PPC64_PGALLOC_H
#define _PPC64_PGALLOC_H

#include <linux/mm.h>
#include <asm/processor.h>

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

static inline pgd_t *
pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL);
	if (pgd != NULL)
		clear_page(pgd);
	return pgd;
}

static inline void
pgd_free(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

#define pgd_populate(MM, PGD, PMD)	pgd_set(PGD, PMD)

static inline pmd_t *
pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	pmd_t *pmd;

	pmd = (pmd_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT);
	if (pmd)
		clear_page(pmd);
	return pmd;
}

static inline void
pmd_free(pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

#define __pmd_free_tlb(tlb, pmd)	pmd_free(pmd)

#define pmd_populate_kernel(mm, pmd, pte) pmd_set(pmd, pte)
#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))

static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte;

	pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT);
	if (pte)
		clear_page(pte);
	return pte;
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
	free_page((unsigned long)pte);
}

#define pte_free(pte_page)	pte_free_kernel(page_address(pte_page))
#define __pte_free_tlb(tlb, pte)	pte_free(pte)

#define check_pgt_cache()	do { } while (0)

#endif /* _PPC64_PGALLOC_H */
