#ifndef _PGTABLE_NOPMD_H
#define _PGTABLE_NOPMD_H

#ifndef __ASSEMBLY__

/*
 * Having the pmd type consist of a pgd gets the size right, and allows
 * us to conceptually access the pgd entry that this pmd is folded into
 * without casting.
 */
typedef struct { pgd_t pgd; } pmd_t;

#define PMD_SHIFT	PGDIR_SHIFT
#define PTRS_PER_PMD	1
#define PMD_SIZE  	(1UL << PMD_SHIFT)
#define PMD_MASK  	(~(PMD_SIZE-1))

/*
 * The "pgd_xxx()" functions here are trivial for a folded two-level
 * setup: the pmd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 */
static inline int pgd_none(pgd_t pgd)		{ return 0; }
static inline int pgd_bad(pgd_t pgd)		{ return 0; }
static inline int pgd_present(pgd_t pgd)	{ return 1; }
static inline void pgd_clear(pgd_t *pgd)	{ }
#define pmd_ERROR(pmd)				(pgd_ERROR((pmd).pgd))

#define pgd_populate(mm, pmd, pte)		do { } while (0)
#define pgd_populate_kernel(mm, pmd, pte)	do { } while (0)

/*
 * (pmds are folded into pgds so this doesn't get actually called,
 * but the define is needed for a generic inline function.)
 */
#define set_pgd(pgdptr, pgdval)			set_pmd((pmd_t *)(pgdptr), (pmd_t) { pgdval })

static inline pmd_t * pmd_offset(pgd_t * pgd, unsigned long address)
{
	return (pmd_t *)pgd;
}

#define pmd_val(x)				(pgd_val((x).pgd))
#define __pmd(x)				((pmd_t) { __pgd(x) } )

#define pgd_page(pgd)				(pmd_page((pmd_t){ pgd }))
#define pgd_page_kernel(pgd)			(pmd_page_kernel((pmd_t){ pgd }))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
#define pmd_alloc_one(mm, address)		NULL
#define pmd_free(x)				do { } while (0)
#define __pmd_free_tlb(tlb, x)			do { } while (0)

#endif /* __ASSEMBLY__ */

#endif /* _PGTABLE_NOPMD_H */
