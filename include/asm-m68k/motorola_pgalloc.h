#ifndef _MOTOROLA_PGALLOC_H
#define _MOTOROLA_PGALLOC_H

#include <asm/tlbflush.h>

extern struct pgtable_cache_struct {
	unsigned long *pmd_cache;
	unsigned long *pte_cache;
/* This counts in units of pointer tables, of which can be eight per page. */
	unsigned long pgtable_cache_sz;
} quicklists;

#define pgd_quicklist ((unsigned long *)0)
#define pmd_quicklist (quicklists.pmd_cache)
#define pte_quicklist (quicklists.pte_cache)
/* This isn't accurate because of fragmentation of allocated pages for
   pointer tables, but that should not be a problem. */
#define pgtable_cache_size ((quicklists.pgtable_cache_sz+7)/8)

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long offset);
extern pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long offset);

extern pmd_t *get_pointer_table(void);
extern int free_pointer_table(pmd_t *);


extern inline pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	ret = pte_quicklist;
	if (ret) {
		pte_quicklist = (unsigned long *)*ret;
		ret[0] = 0;
		quicklists.pgtable_cache_sz -= 8;
	}
	return (pte_t *)ret;
}
#define pte_alloc_one_fast(mm,addr)  get_pte_fast()

static inline pte_t *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
 	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (pte) {
	         clear_page(pte);
		 __flush_page_to_ram((unsigned long)pte);
		 flush_tlb_kernel_page((unsigned long)pte);
		 nocache_page((unsigned long)pte);
		}

	return pte;
}


extern __inline__ pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
        return get_pointer_table();
}


extern inline void free_pte_fast(pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long)pte_quicklist;
	pte_quicklist = (unsigned long *)pte;
	quicklists.pgtable_cache_sz += 8;
}

extern inline void free_pte_slow(pte_t *pte)
{
	cache_page((unsigned long)pte);
	free_page((unsigned long) pte);
}

extern inline pmd_t *get_pmd_fast(void)
{
	unsigned long *ret;

	ret = pmd_quicklist;
	if (ret) {
		pmd_quicklist = (unsigned long *)*ret;
		ret[0] = 0;
		quicklists.pgtable_cache_sz--;
	}
	return (pmd_t *)ret;
}
#define pmd_alloc_one_fast(mm,addr) get_pmd_fast()

extern inline void free_pmd_fast(pmd_t *pmd)
{
	*(unsigned long *)pmd = (unsigned long)pmd_quicklist;
	pmd_quicklist = (unsigned long *) pmd;
	quicklists.pgtable_cache_sz++;
}

extern inline int free_pmd_slow(pmd_t *pmd)
{
	return free_pointer_table(pmd);
}

/* The pgd cache is folded into the pmd cache, so these are dummy routines. */
extern inline pgd_t *get_pgd_fast(void)
{
	return (pgd_t *)0;
}

extern inline void free_pgd_fast(pgd_t *pgd)
{
}

extern inline void free_pgd_slow(pgd_t *pgd)
{
}

extern void __bad_pte(pmd_t *pmd);
extern void __bad_pmd(pgd_t *pgd);

extern inline void pte_free(pte_t *pte)
{
	free_pte_fast(pte);
}

extern inline void pmd_free(pmd_t *pmd)
{
	free_pmd_fast(pmd);
}


extern inline void pte_free_kernel(pte_t *pte)
{
	free_pte_fast(pte);
}

extern inline pte_t *pte_alloc_kernel(pmd_t *pmd, unsigned long address)
{
	return pte_alloc(&init_mm,pmd, address);
}

extern inline void pmd_free_kernel(pmd_t *pmd)
{
	free_pmd_fast(pmd);
}

extern inline pmd_t *pmd_alloc_kernel(pgd_t *pgd, unsigned long address)
{
	return pmd_alloc(&init_mm,pgd, address);
}

extern inline void pgd_free(pgd_t *pgd)
{
	free_pmd_fast((pmd_t *)pgd);
}

extern inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)get_pmd_fast();
	if (!pgd)
		pgd = (pgd_t *)get_pointer_table();
	return pgd;
}


#define pmd_populate(MM, PMD, PTE)	pmd_set(PMD, PTE)
#define pgd_populate(MM, PGD, PMD)	pgd_set(PGD, PMD)


extern int do_check_pgt_cache(int, int);

extern inline void set_pgdir(unsigned long address, pgd_t entry)
{
}


#endif /* _MOTOROLA_PGALLOC_H */
