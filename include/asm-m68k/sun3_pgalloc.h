/* sun3_pgalloc.h --
 * reorganization around 2.3.39, routines moved from sun3_pgtable.h 
 *
 * moved 1/26/2000 Sam Creasey
 */

#ifndef _SUN3_PGALLOC_H
#define _SUN3_PGALLOC_H

/* Pagetable caches. */
//todo: should implement for at least ptes. --m
#define pgd_quicklist ((unsigned long *) 0)
#define pmd_quicklist ((unsigned long *) 0)
#define pte_quicklist ((unsigned long *) 0)
#define pgtable_cache_size (0L)

/* Allocation and deallocation of various flavours of pagetables. */
extern inline int free_pmd_fast (pmd_t *pmdp) { return 0; }
extern inline int free_pmd_slow (pmd_t *pmdp) { return 0; }
extern inline pmd_t *get_pmd_fast (void) { return (pmd_t *) 0; }

//todo: implement the following properly.
#define get_pte_fast() ((pte_t *) 0)
#define get_pte_slow pte_alloc
#define free_pte_fast(pte)
#define free_pte_slow pte_free

/* FIXME - when we get this compiling */
/* erm, now that it's compiling, what do we do with it? */
#define _KERNPG_TABLE 0

extern inline void pte_free_kernel(pte_t * pte)
{
        free_page((unsigned long) pte);
}

extern const char bad_pmd_string[];

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
        address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
        if (pmd_none(*pmd)) {
                pte_t * page = (pte_t *) get_free_page(GFP_KERNEL);
                if (pmd_none(*pmd)) {
                        if (page) {
                                pmd_val(*pmd) = _KERNPG_TABLE + __pa(page);
                                return page + address;
                        }
                        pmd_val(*pmd) = _KERNPG_TABLE + __pa((unsigned long)BAD_PAGETABLE);
                        return NULL;
                }
                free_page((unsigned long) page);
        }
        if (pmd_bad(*pmd)) {
                printk(bad_pmd_string, pmd_val(*pmd));
		printk("at kernel pgd off %08x\n", (unsigned int)pmd);
                pmd_val(*pmd) = _KERNPG_TABLE + __pa((unsigned long)BAD_PAGETABLE);
                return NULL;
        }
        return (pte_t *) __pmd_page(*pmd) + address;
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free_kernel(pmd_t * pmd)
{
//        pmd_val(*pmd) = 0;
}

extern inline pmd_t * pmd_alloc_kernel(pgd_t * pgd, unsigned long address)
{
        return (pmd_t *) pgd;
}

#define pmd_alloc_one_fast(mm, address) ({ BUG(); ((pmd_t *)1); })
#define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); })

extern inline void pte_free(pte_t * pte)
{
        free_page((unsigned long) pte);
}

static inline pte_t *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	unsigned long page = __get_free_page(GFP_KERNEL);

	if (!page)
		return NULL;
		
	memset((void *)page, 0, PAGE_SIZE);
//	pmd_val(*pmd) = SUN3_PMD_MAGIC + __pa(page);
/*	pmd_val(*pmd) = __pa(page); */
	return (pte_t *) (page);
}

#define pte_alloc_one_fast(mm,addr) pte_alloc_one(mm,addr)

#define pmd_populate(mm, pmd, pte) (pmd_val(*pmd) = __pa((unsigned long)pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern inline void pmd_free(pmd_t * pmd)
{
        pmd_val(*pmd) = 0;
}

extern inline void pgd_free(pgd_t * pgd)
{
        free_page((unsigned long) pgd);
}

extern inline pgd_t * pgd_alloc(struct mm_struct *mm)
{
     pgd_t *new_pgd;

     new_pgd = (pgd_t *)get_free_page(GFP_KERNEL);
     memcpy(new_pgd, swapper_pg_dir, PAGE_SIZE);
     memset(new_pgd, 0, (PAGE_OFFSET >> PGDIR_SHIFT));
     return new_pgd;
}

#define pgd_populate(mm, pmd, pte) BUG()

/* FIXME: the sun3 doesn't have a page table cache! 
   (but the motorola routine should just return 0) */

extern int do_check_pgt_cache(int, int);

extern inline void set_pgdir(unsigned long address, pgd_t entry)
{
}

/* Reserved PMEGs. */
extern char sun3_reserved_pmeg[SUN3_PMEGS_NUM];
extern unsigned long pmeg_vaddr[SUN3_PMEGS_NUM];
extern unsigned char pmeg_alloc[SUN3_PMEGS_NUM];
extern unsigned char pmeg_ctx[SUN3_PMEGS_NUM];



#endif /* SUN3_PGALLOC_H */
