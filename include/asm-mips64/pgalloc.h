/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2001 by Ralf Baechle at alii
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <linux/config.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <asm/page.h>

#define check_pgt_cache()	do { } while (0)

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
	pte_t *pte)
{
	set_pmd(pmd, __pmd(__pa(pte)));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
	struct page *pte)
{
	set_pmd(pmd, __pmd((PAGE_OFFSET + page_to_pfn(pte)) << PAGE_SHIFT));
}

#define pgd_populate(mm, pgd, pmd)		set_pgd(pgd, __pgd(pmd))

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret, *init;

	ret = (pgd_t *) __get_free_pages(GFP_KERNEL, 1);
	if (ret) {
		init = pgd_offset(&init_mm, 0);
		pgd_init((unsigned long)ret);
		memcpy(ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}

	return ret;
}

static inline void pgd_free(pgd_t *pgd)
{
	free_pages((unsigned long)pgd, PGD_ORDER);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
	unsigned long address)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_pages(GFP_KERNEL|__GFP_REPEAT,
	                                 PTE_ORDER);
	if (pte)
		clear_page(pte);

	return pte;
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
	unsigned long address)
{
	struct page *pte;

	pte = alloc_pages(GFP_KERNEL | __GFP_REPEAT, PTE_ORDER);
	if (pte)
		clear_highpage(pte);

	return pte;
}

static inline void pte_free_kernel(pte_t *pte)
{
	free_pages((unsigned long)pte, PTE_ORDER);
}

static inline void pte_free(struct page *pte)
{
	__free_pages(pte, PTE_ORDER);
}

#define __pte_free_tlb(tlb,pte)		tlb_remove_page((tlb),(pte))
#define __pmd_free_tlb(tlb,x)		do { } while (0)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pmd_t *pmd;

	pmd = (pmd_t *) __get_free_pages(GFP_KERNEL|__GFP_REPEAT, PMD_ORDER);
	if (pmd)
		pmd_init((unsigned long)pmd, (unsigned long)invalid_pte_table);
	return pmd;
}

static inline void pmd_free(pmd_t *pmd)
{
	free_pages((unsigned long)pmd, PMD_ORDER);
}

extern pte_t kptbl[(PAGE_SIZE << PGD_ORDER)/sizeof(pte_t)];
extern pmd_t kpmdtbl[PTRS_PER_PMD];

#endif /* _ASM_PGALLOC_H */
