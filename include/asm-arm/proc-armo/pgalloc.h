/*
 *  linux/include/asm-arm/proc-armo/pgalloc.h
 *
 *  Copyright (C) 2001-2002 Russell King
 *
 * Page table allocation/freeing primitives for 26-bit ARM processors.
 */

#include <linux/slab.h>

extern kmem_cache_t *pte_cache;

static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(pte_cache, GFP_KERNEL);
}

static inline void pte_free_kernel(pte_t *pte)
{
	if (pte)
		kmem_cache_free(pte_cache, pte);
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 *
 * If 'mm' is the init tasks mm, then we are doing a vmalloc, and we
 * need to set stuff up correctly for it.
 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
	set_pmd(pmdp, __mk_pmd(ptep, _PAGE_TABLE));
}

/*
 * We use the old 2.5.5-rmk1 hack for this.
 * This is not truly correct, but should be functional.
 */
#define pte_alloc_one(mm,addr)	((struct page *)pte_alloc_one_kernel(mm,addr))
#define pte_free(pte)		pte_free_kernel((pte_t *)pte)
#define pmd_populate(mm,pmdp,ptep) pmd_populate_kernel(mm,pmdp,(pte_t *)ptep)
