#ifndef _PPC64_PGALLOC_H
#define _PPC64_PGALLOC_H

#include <linux/threads.h>
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
	int count = 0;
	pmd_t *pmd;

	do {
		pmd = (pmd_t *)__get_free_page(GFP_KERNEL);
		if (pmd)
			clear_page(pmd);
		else {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
		}
	} while (!pmd && (count++ < 10));

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
	int count = 0;
	pte_t *pte;

	do {
		pte = (pte_t *)__get_free_page(GFP_KERNEL);
		if (pte)
			clear_page(pte);
		else {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
		}
	} while (!pte && (count++ < 10));

	return pte;
}

#define pte_alloc_one(mm, address) \
	virt_to_page(pte_alloc_one_kernel((mm), (address)))

static inline void
pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

#define pte_free(pte_page)	pte_free_kernel(page_address(pte_page))
#define __pte_free_tlb(tlb, pte)	pte_free(pte)

#define check_pgt_cache()	do { } while (0)

#endif /* _PPC64_PGALLOC_H */
