#ifdef __KERNEL__
#ifndef _PPC_PGALLOC_H
#define _PPC_PGALLOC_H

#include <linux/config.h>
#include <linux/threads.h>
#include <asm/processor.h>

/*
 * This is handled very differently on the PPC since out page tables
 * are all 0's and I want to be able to use these zero'd pages elsewhere
 * as well - it gives us quite a speedup.
 *
 * Note that the SMP/UP versions are the same but we don't need a
 * per cpu list of zero pages because we do the zero-ing with the cache
 * off and the access routines are lock-free but the pgt cache stuff
 * is per-cpu since it isn't done with any lock-free access routines
 * (although I think we need arch-specific routines so I can do lock-free).
 *
 * I need to generalize this so we can use it for other arch's as well.
 * -- Cort
 */
#ifdef CONFIG_SMP
#define quicklists	cpu_data[smp_processor_id()]
#else
extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
} quicklists;
#endif

#define pgd_quicklist 		(quicklists.pgd_cache)
#define pmd_quicklist 		((unsigned long *)0)
#define pte_quicklist 		(quicklists.pte_cache)
#define pgtable_cache_size 	(quicklists.pgtable_cache_sz)

extern unsigned long *zero_cache;    /* head linked list of pre-zero'd pages */
extern atomic_t zero_sz;	     /* # currently pre-zero'd pages */
extern atomic_t zeropage_hits;	     /* # zero'd pages request that we've done */
extern atomic_t zeropage_calls;      /* # zero'd pages request that've been made */
extern atomic_t zerototal;	     /* # pages zero'd over time */

#define zero_quicklist     	(zero_cache)
#define zero_cache_sz  	 	(zero_sz)
#define zero_cache_calls 	(zeropage_calls)
#define zero_cache_hits  	(zeropage_hits)
#define zero_cache_total 	(zerototal)

/* return a pre-zero'd page from the list, return NULL if none available -- Cort */
extern unsigned long get_zero_page_fast(void);

extern void __bad_pte(pmd_t *pmd);

/* We don't use pmd cache, so this is a dummy routine */
extern __inline__ pmd_t *get_pmd_fast(void)
{
	return (pmd_t *)0;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
}

extern __inline__ void free_pmd_slow(pmd_t *pmd)
{
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free(pmd_t * pmd)
{
}

extern inline pmd_t * pmd_alloc(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

#define pmd_free_kernel		pmd_free
#define pmd_alloc_kernel	pmd_alloc
#define pte_alloc_kernel	pte_alloc

extern __inline__ pgd_t *get_pgd_slow(void)
{
	pgd_t *ret, *init;
	/*if ( (ret = (pgd_t *)get_zero_page_fast()) == NULL )*/
	if ( (ret = (pgd_t *)__get_free_page(GFP_KERNEL)) != NULL )
		memset (ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
	if (ret) {
		init = pgd_offset(&init_mm, 0);
		memcpy (ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}
	return ret;
}

extern __inline__ pgd_t *get_pgd_fast(void)
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

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
        *(unsigned long **)pgd = pgd_quicklist;
        pgd_quicklist = (unsigned long *) pgd;
        pgtable_cache_size++;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern __inline__ pte_t *get_pte_fast(void)
{
        unsigned long *ret;

        if ((ret = pte_quicklist) != NULL) {
                pte_quicklist = (unsigned long *)(*ret);
                ret[0] = 0;
                pgtable_cache_size--;
	}
        return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
        *(unsigned long **)pte = pte_quicklist;
        pte_quicklist = (unsigned long *) pte;
        pgtable_cache_size++;
}

extern __inline__ void free_pte_slow(pte_t *pte)
{
	free_page((unsigned long)pte);
}

#define pte_free_kernel(pte)    free_pte_fast(pte)
#define pte_free(pte)           free_pte_fast(pte)
#define pgd_free(pgd)           free_pgd_fast(pgd)
#define pgd_alloc()             get_pgd_fast()

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t * page = (pte_t *) get_pte_fast();
		
		if (!page)
			return get_pte_slow(pmd, address);
		pmd_val(*pmd) = (unsigned long) page;
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern int do_check_pgt_cache(int, int);

#endif /* _PPC_PGALLOC_H */
#endif /* __KERNEL__ */
