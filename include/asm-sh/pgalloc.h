#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <asm/processor.h>
#include <linux/threads.h>
#include <linux/slab.h>
#include <linux/mm.h>

#define pgd_quicklist ((unsigned long *)0)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist ((unsigned long *)0)
#define pgtable_cache_size 0L

#define pmd_populate_kernel(mm, pmd, pte) \
		set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(pte)))

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE + page_to_phys(pte)));
}

/*
 * Allocate and free page tables.
 */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	unsigned int pgd_size = (USER_PTRS_PER_PGD * sizeof(pgd_t));
	pgd_t *pgd = (pgd_t *)kmalloc(pgd_size, GFP_KERNEL);

	if (pgd)
		memset(pgd, 0, pgd_size);

	return pgd;
}

static inline void pgd_free(pgd_t *pgd)
{
	kfree(pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	int count = 0;
	pte_t *pte;

   	do {
		pte = (pte_t *) __get_free_page(GFP_KERNEL | __GFP_REPEAT);
		if (pte)
			clear_page(pte);
		else {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
		}
	} while (!pte && (count++ < 10));

	return pte;
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	int count = 0;
	struct page *pte;

   	do {
		pte = alloc_pages(GFP_KERNEL, 0);
		if (pte)
			clear_page(page_address(pte));
		else {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
		}
	} while (!pte && (count++ < 10));

	return pte;
}

static inline void pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
}

#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)			do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()

#if defined(CONFIG_CPU_SH4)
#define PG_mapped	PG_arch_1

/*
 * For SH-4, we have our own implementation for ptep_get_and_clear
 */
static inline pte_t ptep_get_and_clear(pte_t *ptep)
{
	pte_t pte = *ptep;

	pte_clear(ptep);
	if (!pte_not_present(pte)) {
		struct page *page;
		unsigned long pfn = pte_pfn(pte);
		if (pfn_valid(pfn)) {
			page = pfn_to_page(pfn);
			if (!page->mapping
			    || list_empty(&page->mapping->i_mmap_shared))
				__clear_bit(PG_mapped, &page->flags);
		}
	}
	return pte;
}
#else
static inline pte_t ptep_get_and_clear(pte_t *ptep)
{
	pte_t pte = *ptep;
	pte_clear(ptep);
	return pte;
}
#endif

/*
 * Following functions are same as generic ones.
 */
static inline int ptep_test_and_clear_young(pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!pte_young(pte))
		return 0;
	set_pte(ptep, pte_mkold(pte));
	return 1;
}

static inline int ptep_test_and_clear_dirty(pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!pte_dirty(pte))
		return 0;
	set_pte(ptep, pte_mkclean(pte));
	return 1;
}

static inline void ptep_set_wrprotect(pte_t *ptep)
{
	pte_t old_pte = *ptep;
	set_pte(ptep, pte_wrprotect(old_pte));
}

static inline void ptep_mkdirty(pte_t *ptep)
{
	pte_t old_pte = *ptep;
	set_pte(ptep, pte_mkdirty(old_pte));
}

#define check_pgt_cache()	do { } while (0)

#endif /* __ASM_SH_PGALLOC_H */
