#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <asm/processor.h>
#include <linux/threads.h>
#include <linux/slab.h>

#define pgd_quicklist (current_cpu_data.pgd_quick)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (current_cpu_data.pte_quick)
#define pgtable_cache_size (current_cpu_data.pgtable_cache_sz)

#define pmd_populate(mm, pmd, pte) \
		set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(pte)))

/*
 * Allocate and free page tables.
 */

static __inline__ pgd_t *get_pgd_slow(void)
{
	unsigned int pgd_size = (USER_PTRS_PER_PGD * sizeof(pgd_t));
	pgd_t *pgd = (pgd_t *)kmalloc(pgd_size, GFP_KERNEL);

	if (pgd)
		memset(pgd, 0, pgd_size);

	return pgd;
}

static __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if ((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	} else
		ret = (unsigned long *)get_pgd_slow();
	return (pgd_t *)ret;
}

static __inline__ void free_pgd_fast(pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

static __inline__ void free_pgd_slow(pgd_t *pgd)
{
	kfree(pgd);
}

static inline pte_t *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pte)
		clear_page(pte);
	return pte;
}

static inline pte_t *pte_alloc_one_fast(struct mm_struct *mm, unsigned long address)
{
	unsigned long *ret;

	if ((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

static __inline__ void pte_free_fast(pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

static __inline__ void pte_free_slow(pte_t *pte)
{
	free_page((unsigned long)pte);
}

#define pte_free(pte)		pte_free_slow(pte)
#define pgd_free(pgd)		free_pgd_slow(pgd)
#define pgd_alloc(mm)		get_pgd_fast()

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
static __inline__ void pmd_free(pmd_t * pmd)
{
}

#define pmd_alloc_one_fast(mm, addr)	({ BUG(); ((pmd_t *)1); })
#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free_slow(x)		do { } while (0)
#define pmd_free_fast(x)		do { } while (0)
#define pmd_free(x)			do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()

extern int do_check_pgt_cache(int, int);

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 */

extern void flush_tlb(void);
extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_range(struct mm_struct *mm, unsigned long start,
			    unsigned long end);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long page);

static __inline__ void flush_tlb_pgtables(struct mm_struct *mm,
					  unsigned long start, unsigned long end)
{
}

#endif /* __ASM_SH_PGALLOC_H */
