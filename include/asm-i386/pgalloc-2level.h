#ifndef _I386_PGALLOC_2LEVEL_H
#define _I386_PGALLOC_2LEVEL_H

/*
 * traditional i386 two-level paging, page table allocation routines:
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
#define pmd_alloc_one_fast()		({ BUG(); ((pmd_t *)1); })
#define pmd_alloc_one()			({ BUG(); ((pmd_t *)2); })
#define pmd_free_slow(x)		do { } while (0)
#define pmd_free_fast(x)		do { } while (0)
#define pmd_free(x)			do { } while (0)
#define pgd_populate(pmd, pte)		BUG()

#endif /* _I386_PGALLOC_2LEVEL_H */
