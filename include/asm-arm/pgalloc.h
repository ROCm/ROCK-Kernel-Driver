/*
 *  linux/include/asm-arm/pgalloc.h
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_PGALLOC_H
#define _ASMARM_PGALLOC_H

#include <linux/config.h>

#include <asm/processor.h>

/*
 * Get the cache handling stuff now.
 */
#include <asm/proc/cache.h>

/*
 * ARM processors do not cache TLB tables in RAM.
 */
#define flush_tlb_pgtables(mm,start,end)	do { } while (0)

/*
 * Page table cache stuff
 */
#ifndef CONFIG_NO_PGT_CACHE

#ifdef CONFIG_SMP
#error Pgtable caches have to be per-CPU, so that no locking is needed.
#endif	/* CONFIG_SMP */

extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
} quicklists;

#define pgd_quicklist		(quicklists.pgd_cache)
#define pmd_quicklist		((unsigned long *)0)
#define pte_quicklist		(quicklists.pte_cache)
#define pgtable_cache_size	(quicklists.pgtable_cache_sz)

/* used for quicklists */
#define __pgd_next(pgd) (((unsigned long *)pgd)[1])
#define __pte_next(pte)	(((unsigned long *)pte)[0])

extern __inline__ pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if ((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)__pgd_next(ret);
		ret[1] = ret[2];
		clean_dcache_entry(ret + 1);
		pgtable_cache_size--;
	}
	return (pgd_t *)ret;
}

extern __inline__ void free_pgd_fast(pgd_t *pgd)
{
	__pgd_next(pgd) = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

/* We don't use pmd cache, so this is a dummy routine */
#define get_pmd_fast()		((pmd_t *)0)

extern __inline__ void free_pmd_fast(pmd_t *pmd)
{
}

extern __inline__ pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)__pte_next(ret);
		ret[0] = ret[1];
		clean_dcache_entry(ret);
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

extern __inline__ void free_pte_fast(pte_t *pte)
{
	__pte_next(pte) = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

#else	/* CONFIG_NO_PGT_CACHE */

#define pgd_quicklist		((unsigned long *)0)
#define pmd_quicklist		((unsigned long *)0)
#define pte_quicklist		((unsigned long *)0)

#define get_pgd_fast()		((pgd_t *)0)
#define get_pmd_fast()		((pmd_t *)0)
#define get_pte_fast()		((pte_t *)0)

#define free_pgd_fast(pgd)	free_pgd_slow(pgd)
#define free_pmd_fast(pmd)	free_pmd_slow(pmd)
#define free_pte_fast(pte)	free_pte_slow(pte)

#endif	/* CONFIG_NO_PGT_CACHE */

extern pgd_t *get_pgd_slow(void);
extern void free_pgd_slow(pgd_t *pgd);

#define free_pmd_slow(pmd)	do { } while (0)

extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long addr_preadjusted);
extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long addr_preadjusted);
extern void free_pte_slow(pte_t *pte);

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */
#define pte_free_kernel(pte)	free_pte_fast(pte)
#define pte_free(pte)		free_pte_fast(pte)

#ifndef pte_alloc_kernel
extern __inline__ pte_t * pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_pte_fast();

		if (!page)
			return get_pte_kernel_slow(pmd, address);
		set_pmd(pmd, mk_kernel_pmd(page));
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}
#endif

extern __inline__ pte_t *pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
	if (pmd_none(*pmd)) {
		pte_t *page = (pte_t *) get_pte_fast();

		if (!page)
			return get_pte_slow(pmd, address);
		set_pmd(pmd, mk_user_pmd(page));
		return page + address;
	}
	if (pmd_bad(*pmd)) {
		__handle_bad_pmd(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

#define pmd_free_kernel		pmd_free
#define pmd_free(pmd)		do { } while (0)

#define pmd_alloc_kernel	pmd_alloc
extern __inline__ pmd_t *pmd_alloc(pgd_t *pgd, unsigned long address)
{
	return (pmd_t *) pgd;
}

#define pgd_free(pgd)		free_pgd_fast(pgd)

extern __inline__ pgd_t *pgd_alloc(void)
{
	pgd_t *pgd;

	pgd = get_pgd_fast();
	if (!pgd)
		pgd = get_pgd_slow();

	return pgd;
}

extern int do_check_pgt_cache(int, int);

extern __inline__ void set_pgdir(unsigned long address, pgd_t entry)
{
	struct task_struct * p;

	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->mm)
			continue;
		*pgd_offset(p->mm,address) = entry;
	}
	read_unlock(&tasklist_lock);

#ifndef CONFIG_NO_PGT_CACHE
	{
		pgd_t *pgd;
		for (pgd = (pgd_t *)pgd_quicklist; pgd;
		     pgd = (pgd_t *)__pgd_next(pgd))
			pgd[address >> PGDIR_SHIFT] = entry;
	}
#endif
}

#endif
