#ifndef _LINUX_RMAP_H
#define _LINUX_RMAP_H
/*
 * Declarations for Reverse Mapping functions in mm/rmap.c
 * Its structures are declared within that file.
 */

#include <linux/config.h>
#include <linux/linkage.h>

#define rmap_lock(page) \
	bit_spin_lock(PG_maplock, (unsigned long *)&(page)->flags)
#define rmap_unlock(page) \
	bit_spin_unlock(PG_maplock, (unsigned long *)&(page)->flags)

#ifdef CONFIG_MMU

struct pte_chain;
struct pte_chain *pte_chain_alloc(int gfp_flags);
void __pte_chain_free(struct pte_chain *pte_chain);

static inline void pte_chain_free(struct pte_chain *pte_chain)
{
	if (pte_chain)
		__pte_chain_free(pte_chain);
}

struct pte_chain * fastcall
	page_add_rmap(struct page *, pte_t *, struct pte_chain *);
void fastcall page_remove_rmap(struct page *, pte_t *);

/*
 * Called from mm/vmscan.c to handle paging out
 */
int fastcall page_referenced(struct page *);
int fastcall try_to_unmap(struct page *);

#else	/* !CONFIG_MMU */

#define page_referenced(page)	TestClearPageReferenced(page)
#define try_to_unmap(page)	SWAP_FAIL

#endif	/* CONFIG_MMU */

/*
 * Return values of try_to_unmap
 */
#define SWAP_SUCCESS	0
#define SWAP_AGAIN	1
#define SWAP_FAIL	2

#endif	/* _LINUX_RMAP_H */
