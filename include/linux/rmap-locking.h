/*
 * include/linux/rmap-locking.h
 *
 * Locking primitives for exclusive access to a page's reverse-mapping
 * pte chain.
 */

#include <linux/slab.h>

struct pte_chain;
extern kmem_cache_t *pte_chain_cache;

#define pte_chain_lock(page)	bit_spin_lock(PG_chainlock, &page->flags)
#define pte_chain_unlock(page)	bit_spin_unlock(PG_chainlock, &page->flags)

struct pte_chain *pte_chain_alloc(int gfp_flags);
void __pte_chain_free(struct pte_chain *pte_chain);

static inline void pte_chain_free(struct pte_chain *pte_chain)
{
	if (pte_chain)
		__pte_chain_free(pte_chain);
}
