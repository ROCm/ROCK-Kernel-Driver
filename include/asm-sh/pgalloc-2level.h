#ifndef __ASM_SH_PGALLOC_2LEVEL_H
#define __ASM_SH_PGALLOC_2LEVEL_H

/*
 * traditional two-level paging, page table allocation routines:
 */

static __inline__ pmd_t *get_pmd_fast(void)
{
	return (pmd_t *)0;
}

static __inline__ void free_pmd_fast(pmd_t *pmd) { }
static __inline__ void free_pmd_slow(pmd_t *pmd) { }

static __inline__ pmd_t * pmd_alloc(pgd_t *pgd, unsigned long address)
{
	if (!pgd)
		BUG();
	return (pmd_t *) pgd;
}

#endif /* __ASM_SH_PGALLOC_2LEVEL_H */
