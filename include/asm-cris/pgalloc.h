#ifndef _CRIS_PGALLOC_H
#define _CRIS_PGALLOC_H

#include <linux/config.h>
#include <asm/page.h>
#include <linux/threads.h>

/* bunch of protos */

extern pgd_t *get_pgd_slow(void);
extern void free_pgd_slow(pgd_t *pgd);
extern __inline__ void free_pmd_slow(pmd_t *pmd) { }
extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);
extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long address_preadjusted);

/* first the non-cached versions */

extern __inline__ void free_pte_slow(pte_t *pte)
{
        free_page((unsigned long)pte);
}

extern __inline__ pgd_t *get_pgd_slow(void)
{
        pgd_t *ret = (pgd_t *)__get_free_page(GFP_KERNEL);

        if (ret) {
                memset(ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
                memcpy(ret + USER_PTRS_PER_PGD, swapper_pg_dir + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
        }
        return ret;
}

extern __inline__ void free_pgd_slow(pgd_t *pgd)
{
        free_page((unsigned long)pgd);
}

/*
 * Now for the page table cache versions
 */

#ifndef CONFIG_NO_PGT_CACHE

#ifdef CONFIG_SMP
#error Pgtable caches have to be per-CPU, so that no locking is needed.
#endif  /* CONFIG_SMP */

extern struct pgtable_cache_struct {
        unsigned long *pgd_cache;
        unsigned long *pte_cache;
        unsigned long pgtable_cache_sz;
} quicklists;

#define pgd_quicklist           (quicklists.pgd_cache)
#define pmd_quicklist           ((unsigned long *)0)
#define pte_quicklist           (quicklists.pte_cache)
#define pgtable_cache_size      (quicklists.pgtable_cache_sz)

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
        *(unsigned long *)pgd = (unsigned long) pgd_quicklist;
        pgd_quicklist = (unsigned long *) pgd;
        pgtable_cache_size++;
}

/* We don't use pmd cache, so this is a dummy routine */

extern __inline__ pmd_t *get_pmd_fast(void)
{
        return (pmd_t *)0;
}

extern __inline__ void free_pmd_fast(pmd_t *pmd) { }

extern __inline__ pte_t *get_pte_fast(void)
{
        unsigned long *ret;

        if((ret = (unsigned long *)pte_quicklist) != NULL) {
                pte_quicklist = (unsigned long *)(*ret);
                ret[0] = ret[1];
                pgtable_cache_size--;
        }
        return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
        *(unsigned long *)pte = (unsigned long) pte_quicklist;
        pte_quicklist = (unsigned long *) pte;
        pgtable_cache_size++;
}

#else   /* CONFIG_NO_PGT_CACHE */

#define pgd_quicklist           ((unsigned long *)0)
#define pmd_quicklist           ((unsigned long *)0)
#define pte_quicklist           ((unsigned long *)0)

#define get_pgd_fast()          ((pgd_t *)0)
#define get_pmd_fast()          ((pmd_t *)0)
#define get_pte_fast()          ((pte_t *)0)

#define free_pgd_fast(pgd)      free_pgd_slow(pgd)
#define free_pmd_fast(pmd)      free_pmd_slow(pmd)
#define free_pte_fast(pte)      free_pte_slow(pte)

#endif  /* CONFIG_NO_PGT_CACHE */

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

#define pte_free_kernel(pte)    free_pte_slow(pte)
#define pte_free(pte)           free_pte_slow(pte)

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
        if (!pmd)
                BUG();
        address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
        if (pmd_none(*pmd)) {
                pte_t * page = (pte_t *) get_pte_fast();
                if (!page)
                        return get_pte_kernel_slow(pmd, address);
		pmd_set_kernel(pmd, page);
                return page + address;
        }
        if (pmd_bad(*pmd)) {
                __handle_bad_pmd_kernel(pmd);
                return NULL;
        }
        return (pte_t *) pmd_page(*pmd) + address;
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
        address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

        if (pmd_none(*pmd))
                goto getnew;
        if (pmd_bad(*pmd))
                goto fix;
        return (pte_t *)pmd_page(*pmd) + address;
 getnew:
	{
		pte_t * page = (pte_t *) get_pte_fast();
		if (!page)
			return get_pte_slow(pmd, address);
		pmd_set(pmd, page);
		return page + address;
	}
 fix:
        __handle_bad_pmd(pmd);
        return NULL;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#define pmd_free(pmd)      free_pmd_slow(pmd)
#define pmd_free_kernel    pmd_free
#define pmd_alloc_kernel   pmd_alloc

extern inline pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
        if (!pgd)
                BUG();
        return (pmd_t *) pgd;
}

/* pgd handling */

#define pgd_free(pgd)      free_pgd_slow(pgd)
#define pgd_alloc(mm)      get_pgd_fast()

/* other stuff */

extern int do_check_pgt_cache(int, int);

#endif
