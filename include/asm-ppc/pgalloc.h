/*
 * BK Id: %F% %I% %G% %U% %#%
 */
#ifdef __KERNEL__
#ifndef _PPC_PGALLOC_H
#define _PPC_PGALLOC_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/highmem.h>
#include <asm/processor.h>

extern void __bad_pte(pmd_t *pmd);

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret;

	if ((ret = (pgd_t *)__get_free_page(GFP_KERNEL)) != NULL)
		clear_page(ret);
	return ret;
}

extern __inline__ void pgd_free(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

/*
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
#define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)                     do { } while (0)
#define pgd_populate(mm, pmd, pte)      BUG()

static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte;
	extern int mem_init_done;
	extern void *early_get_page(void);
	int timeout = 0;

	if (mem_init_done) {
		while ((pte = (pte_t *) __get_free_page(GFP_KERNEL)) == NULL
		       && ++timeout < 10) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ);
		}
	} else
		pte = (pte_t *) early_get_page();
	if (pte != NULL)
		clear_page(pte);
	return pte;
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;
	int timeout = 0;
#ifdef CONFIG_HIGHPTE
	int flags = GFP_KERNEL | __GFP_HIGHMEM;
#else
	int flags = GFP_KERNEL;
#endif

	while ((pte = alloc_pages(flags, 0)) == NULL) {
		if (++timeout >= 10)
			return NULL;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ);
	}
	clear_highpage(pte);
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

#define pmd_populate_kernel(mm, pmd, pte)	\
		(pmd_val(*(pmd)) = __pa(pte))
#define pmd_populate(mm, pmd, pte)	\
		(pmd_val(*(pmd)) = ((pte) - mem_map) << PAGE_SHIFT)

extern int do_check_pgt_cache(int, int);

#endif /* _PPC_PGALLOC_H */
#endif /* __KERNEL__ */
