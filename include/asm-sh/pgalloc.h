#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <asm/processor.h>
#include <linux/threads.h>
#include <linux/slab.h>

#define pgd_quicklist (current_cpu_data.pgd_quick)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist (current_cpu_data.pte_quick)
#define pgtable_cache_size (current_cpu_data.pgtable_cache_sz)

#include <asm/pgalloc-2level.h>

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static __inline__ pgd_t *get_pgd_slow(void)
{
	unsigned int pgd_size = (USER_PTRS_PER_PGD * sizeof(pgd_t));
	pgd_t *ret = (pgd_t *)kmalloc(pgd_size, GFP_KERNEL);

	if (ret)
		memset(ret, 0, pgd_size);

	return ret;
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

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);
extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long address_preadjusted);

static __inline__ pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

static __inline__ void free_pte_fast(pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

static __inline__ void free_pte_slow(pte_t *pte)
{
	free_page((unsigned long)pte);
}

#define pte_free_kernel(pte)    free_pte_slow(pte)
#define pte_free(pte)	   free_pte_slow(pte)
#define pgd_free(pgd)	   free_pgd_slow(pgd)
#define pgd_alloc()	     get_pgd_fast()

static __inline__ pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	if (!pmd)
		BUG();
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_pte_fast();
		
		if (!page)
			return get_pte_kernel_slow(pmd, address);
		set_pmd(pmd, __pmd(_KERNPG_TABLE + __pa(page)));
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

static __inline__ pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

	if (pmd_none(*pmd))
		goto getnew;
	if (pmd_bad(*pmd))
		goto fix;
	return (pte_t *)pmd_page(*pmd) + address;
getnew:
{
	unsigned long page = (unsigned long) get_pte_fast();
	
	if (!page)
		return get_pte_slow(pmd, address);
	set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(page)));
	return (pte_t *)page + address;
}
fix:
	__handle_bad_pmd(pmd);
	return NULL;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
static __inline__ void pmd_free(pmd_t * pmd)
{
}

#define pmd_free_kernel		pmd_free
#define pmd_alloc_kernel	pmd_alloc

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
