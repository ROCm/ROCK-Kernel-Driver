/*
 *  linux/include/asm-arm/proc-armv/pgalloc.h
 *
 *  Copyright (C) 2001 Russell King
 *
 * Page table allocation/freeing primitives for 32-bit ARM processors.
 */

/* unfortunately, this includes linux/mm.h and the rest of the universe. */
#include <linux/slab.h>

extern kmem_cache_t *pte_cache;

/*
 * Allocate one PTE table.
 *
 * Note that we keep the processor copy of the PTE entries separate
 * from the Linux copy.  The processor copies are offset by -PTRS_PER_PTE
 * words from the Linux copy.
 */
static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte;

	pte = kmem_cache_alloc(pte_cache, GFP_KERNEL);
	if (pte)
		pte += PTRS_PER_PTE;
	return pte;
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte;

	pte = kmem_cache_alloc(pte_cache, GFP_KERNEL);
	if (pte)
		pte += PTRS_PER_PTE;
	return (struct page *)pte;
}

/*
 * Free one PTE table.
 */
static inline void pte_free_kernel(pte_t *pte)
{
	if (pte) {
		pte -= PTRS_PER_PTE;
		kmem_cache_free(pte_cache, pte);
	}
}

static inline void pte_free(struct page *pte)
{
	pte_t *_pte = (pte_t *)pte;
	if (pte) {
		_pte -= PTRS_PER_PTE;
		kmem_cache_free(pte_cache, _pte);
	}
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 *
 * If 'mm' is the init tasks mm, then we are doing a vmalloc, and we
 * need to set stuff up correctly for it.
 */
#define pmd_populate_kernel(mm,pmdp,pte)			\
	do {							\
		BUG_ON(mm != &init_mm);				\
		set_pmd(pmdp, __mk_pmd(pte, _PAGE_KERNEL_TABLE));\
	} while (0)

#define pmd_populate(mm,pmdp,pte)				\
	do {							\
		BUG_ON(mm == &init_mm);				\
		set_pmd(pmdp, __mk_pmd(pte, _PAGE_USER_TABLE));	\
	} while (0)
