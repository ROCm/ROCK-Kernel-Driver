/*
 * mm/rmap.c - physical to virtual reverse mappings
 *
 * Copyright 2001, Rik van Riel <riel@conectiva.com.br>
 * Released under the General Public License (GPL).
 *
 *
 * Simple, low overhead pte-based reverse mapping scheme.
 * This is kept modular because we may want to experiment
 * with object-based reverse mapping schemes. Please try
 * to keep this thing as modular as possible.
 */

/*
 * Locking:
 * - the page->pte.chain is protected by the PG_chainlock bit,
 *   which nests within the the mm->page_table_lock,
 *   which nests within the page lock.
 * - because swapout locking is opposite to the locking order
 *   in the page fault path, the swapout path uses trylocks
 *   on the mm->page_table_lock
 */
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rmap-locking.h>
#include <linux/cache.h>
#include <linux/percpu.h>

#include <asm/pgalloc.h>
#include <asm/rmap.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

/* #define DEBUG_RMAP */

/*
 * Shared pages have a chain of pte_chain structures, used to locate
 * all the mappings to this page. We only need a pointer to the pte
 * here, the page struct for the page table page contains the process
 * it belongs to and the offset within that process.
 *
 * We use an array of pte pointers in this structure to minimise cache misses
 * while traversing reverse maps.
 */
#define NRPTE ((L1_CACHE_BYTES - sizeof(unsigned long))/sizeof(pte_addr_t))

/*
 * next_and_idx encodes both the address of the next pte_chain and the
 * offset of the highest-index used pte in ptes[].
 */
struct pte_chain {
	unsigned long next_and_idx;
	pte_addr_t ptes[NRPTE];
} ____cacheline_aligned;

kmem_cache_t	*pte_chain_cache;

static inline struct pte_chain *pte_chain_next(struct pte_chain *pte_chain)
{
	return (struct pte_chain *)(pte_chain->next_and_idx & ~NRPTE);
}

static inline struct pte_chain *pte_chain_ptr(unsigned long pte_chain_addr)
{
	return (struct pte_chain *)(pte_chain_addr & ~NRPTE);
}

static inline int pte_chain_idx(struct pte_chain *pte_chain)
{
	return pte_chain->next_and_idx & NRPTE;
}

static inline unsigned long
pte_chain_encode(struct pte_chain *pte_chain, int idx)
{
	return (unsigned long)pte_chain | idx;
}

/*
 * pte_chain list management policy:
 *
 * - If a page has a pte_chain list then it is shared by at least two processes,
 *   because a single sharing uses PageDirect. (Well, this isn't true yet,
 *   coz this code doesn't collapse singletons back to PageDirect on the remove
 *   path).
 * - A pte_chain list has free space only in the head member - all succeeding
 *   members are 100% full.
 * - If the head element has free space, it occurs in its leading slots.
 * - All free space in the pte_chain is at the start of the head member.
 * - Insertion into the pte_chain puts a pte pointer in the last free slot of
 *   the head member.
 * - Removal from a pte chain moves the head pte of the head member onto the
 *   victim pte and frees the head member if it became empty.
 */

/**
 ** VM stuff below this comment
 **/

/**
 * find_pte - Find a pte pointer given a vma and a struct page.
 * @vma: the vma to search
 * @page: the page to find
 *
 * Determine if this page is mapped in this vma.  If it is, map and rethrn
 * the pte pointer associated with it.  Return null if the page is not
 * mapped in this vma for any reason.
 *
 * This is strictly an internal helper function for the object-based rmap
 * functions.
 * 
 * It is the caller's responsibility to unmap the pte if it is returned.
 */
static inline pte_t *
find_pte(struct vm_area_struct *vma, struct page *page, unsigned long *addr)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long loffset;
	unsigned long address;

	loffset = (page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT));
	address = vma->vm_start + ((loffset - vma->vm_pgoff) << PAGE_SHIFT);
	if (address < vma->vm_start || address >= vma->vm_end)
		goto out;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;

	pmd = pmd_offset(pgd, address);
	if (!pmd_present(*pmd))
		goto out;

	pte = pte_offset_map(pmd, address);
	if (!pte_present(*pte))
		goto out_unmap;

	if (page_to_pfn(page) != pte_pfn(*pte))
		goto out_unmap;

	if (addr)
		*addr = address;

	return pte;

out_unmap:
	pte_unmap(pte);
out:
	return NULL;
}

/**
 * page_referenced_obj_one - referenced check for object-based rmap
 * @vma: the vma to look in.
 * @page: the page we're working on.
 *
 * Find a pte entry for a page/vma pair, then check and clear the referenced
 * bit.
 *
 * This is strictly a helper function for page_referenced_obj.
 */
static int
page_referenced_obj_one(struct vm_area_struct *vma, struct page *page)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t *pte;
	int referenced = 0;

	if (!spin_trylock(&mm->page_table_lock))
		return 1;

	pte = find_pte(vma, page, NULL);
	if (pte) {
		if (ptep_test_and_clear_young(pte))
			referenced++;
		pte_unmap(pte);
	}

	spin_unlock(&mm->page_table_lock);
	return referenced;
}

/**
 * page_referenced_obj_one - referenced check for object-based rmap
 * @page: the page we're checking references on.
 *
 * For an object-based mapped page, find all the places it is mapped and
 * check/clear the referenced flag.  This is done by following the page->mapping
 * pointer, then walking the chain of vmas it holds.  It returns the number
 * of references it found.
 *
 * This function is only called from page_referenced for object-based pages.
 *
 * The semaphore address_space->i_shared_sem is tried.  If it can't be gotten,
 * assume a reference count of 1.
 */
static int
page_referenced_obj(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct vm_area_struct *vma;
	int referenced = 0;

	if (!page->pte.mapcount)
		return 0;

	if (!mapping)
		BUG();

	if (PageSwapCache(page))
		BUG();

	if (down_trylock(&mapping->i_shared_sem))
		return 1;
	
	list_for_each_entry(vma, &mapping->i_mmap, shared)
		referenced += page_referenced_obj_one(vma, page);

	list_for_each_entry(vma, &mapping->i_mmap_shared, shared)
		referenced += page_referenced_obj_one(vma, page);

	up(&mapping->i_shared_sem);

	return referenced;
}

/**
 * page_referenced - test if the page was referenced
 * @page: the page to test
 *
 * Quick test_and_clear_referenced for all mappings to a page,
 * returns the number of processes which referenced the page.
 * Caller needs to hold the pte_chain_lock.
 *
 * If the page has a single-entry pte_chain, collapse that back to a PageDirect
 * representation.  This way, it's only done under memory pressure.
 */
int fastcall page_referenced(struct page * page)
{
	struct pte_chain *pc;
	int referenced = 0;

	if (page_test_and_clear_young(page))
		mark_page_accessed(page);

	if (TestClearPageReferenced(page))
		referenced++;

	if (!PageAnon(page)) {
		referenced += page_referenced_obj(page);
		goto out;
	}
	if (PageDirect(page)) {
		pte_t *pte = rmap_ptep_map(page->pte.direct);
		if (ptep_test_and_clear_young(pte))
			referenced++;
		rmap_ptep_unmap(pte);
	} else {
		int nr_chains = 0;

		/* Check all the page tables mapping this page. */
		for (pc = page->pte.chain; pc; pc = pte_chain_next(pc)) {
			int i;

			for (i = pte_chain_idx(pc); i < NRPTE; i++) {
				pte_addr_t pte_paddr = pc->ptes[i];
				pte_t *p;

				p = rmap_ptep_map(pte_paddr);
				if (ptep_test_and_clear_young(p))
					referenced++;
				rmap_ptep_unmap(p);
				nr_chains++;
			}
		}
		if (nr_chains == 1) {
			pc = page->pte.chain;
			page->pte.direct = pc->ptes[NRPTE-1];
			SetPageDirect(page);
			pc->ptes[NRPTE-1] = 0;
			__pte_chain_free(pc);
		}
	}
out:
	return referenced;
}

/**
 * page_add_rmap - add reverse mapping entry to a page
 * @page: the page to add the mapping to
 * @ptep: the page table entry mapping this page
 *
 * Add a new pte reverse mapping to a page.
 * The caller needs to hold the mm->page_table_lock.
 */
struct pte_chain * fastcall
page_add_rmap(struct page *page, pte_t *ptep, struct pte_chain *pte_chain)
{
	pte_addr_t pte_paddr = ptep_to_paddr(ptep);
	struct pte_chain *cur_pte_chain;

	if (PageReserved(page))
		return pte_chain;

	pte_chain_lock(page);

	/*
	 * If this is an object-based page, just count it.  We can
 	 * find the mappings by walking the object vma chain for that object.
	 */
	if (!PageAnon(page)) {
		if (!page->mapping)
			BUG();
		if (PageSwapCache(page))
			BUG();
		if (!page->pte.mapcount)
			inc_page_state(nr_mapped);
		page->pte.mapcount++;
		goto out;
	}

	if (page->pte.direct == 0) {
		page->pte.direct = pte_paddr;
		SetPageDirect(page);
		inc_page_state(nr_mapped);
		goto out;
	}

	if (PageDirect(page)) {
		/* Convert a direct pointer into a pte_chain */
		ClearPageDirect(page);
		pte_chain->ptes[NRPTE-1] = page->pte.direct;
		pte_chain->ptes[NRPTE-2] = pte_paddr;
		pte_chain->next_and_idx = pte_chain_encode(NULL, NRPTE-2);
		page->pte.direct = 0;
		page->pte.chain = pte_chain;
		pte_chain = NULL;	/* We consumed it */
		goto out;
	}

	cur_pte_chain = page->pte.chain;
	if (cur_pte_chain->ptes[0]) {	/* It's full */
		pte_chain->next_and_idx = pte_chain_encode(cur_pte_chain,
								NRPTE - 1);
		page->pte.chain = pte_chain;
		pte_chain->ptes[NRPTE-1] = pte_paddr;
		pte_chain = NULL;	/* We consumed it */
		goto out;
	}
	cur_pte_chain->ptes[pte_chain_idx(cur_pte_chain) - 1] = pte_paddr;
	cur_pte_chain->next_and_idx--;
out:
	pte_chain_unlock(page);
	return pte_chain;
}

/**
 * page_remove_rmap - take down reverse mapping to a page
 * @page: page to remove mapping from
 * @ptep: page table entry to remove
 *
 * Removes the reverse mapping from the pte_chain of the page,
 * after that the caller can clear the page table entry and free
 * the page.
 * Caller needs to hold the mm->page_table_lock.
 */
void fastcall page_remove_rmap(struct page *page, pte_t *ptep)
{
	pte_addr_t pte_paddr = ptep_to_paddr(ptep);
	struct pte_chain *pc;

	if (!pfn_valid(page_to_pfn(page)) || PageReserved(page))
		return;

	pte_chain_lock(page);

	if (!page_mapped(page))
		goto out_unlock;

	/*
	 * If this is an object-based page, just uncount it.  We can
	 * find the mappings by walking the object vma chain for that object.
	 */
	if (!PageAnon(page)) {
		if (!page->mapping)
			BUG();
		if (PageSwapCache(page))
			BUG();
		if (!page->pte.mapcount)
			BUG();
		page->pte.mapcount--;
		if (!page->pte.mapcount)
			dec_page_state(nr_mapped);
		goto out_unlock;
	}
  
	if (PageDirect(page)) {
		if (page->pte.direct == pte_paddr) {
			page->pte.direct = 0;
			ClearPageDirect(page);
			goto out;
		}
	} else {
		struct pte_chain *start = page->pte.chain;
		struct pte_chain *next;
		int victim_i = pte_chain_idx(start);

		for (pc = start; pc; pc = next) {
			int i;

			next = pte_chain_next(pc);
			if (next)
				prefetch(next);
			for (i = pte_chain_idx(pc); i < NRPTE; i++) {
				pte_addr_t pa = pc->ptes[i];

				if (pa != pte_paddr)
					continue;
				pc->ptes[i] = start->ptes[victim_i];
				start->ptes[victim_i] = 0;
				if (victim_i == NRPTE-1) {
					/* Emptied a pte_chain */
					page->pte.chain = pte_chain_next(start);
					__pte_chain_free(start);
				} else {
					start->next_and_idx++;
				}
				goto out;
			}
		}
	}
out:
	if (page->pte.direct == 0 && page_test_and_clear_dirty(page))
		set_page_dirty(page);
	if (!page_mapped(page))
		dec_page_state(nr_mapped);
out_unlock:
	pte_chain_unlock(page);
	return;
}

/**
 * try_to_unmap_obj - unmap a page using the object-based rmap method
 * @page: the page to unmap
 *
 * Determine whether a page is mapped in a given vma and unmap it if it's found.
 *
 * This function is strictly a helper function for try_to_unmap_obj.
 */
static inline int
try_to_unmap_obj_one(struct vm_area_struct *vma, struct page *page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *pte;
	pte_t pteval;
	int ret = SWAP_AGAIN;

	if (!spin_trylock(&mm->page_table_lock))
		return ret;

	pte = find_pte(vma, page, &address);
	if (!pte)
		goto out;

	if (vma->vm_flags & (VM_LOCKED|VM_RESERVED)) {
		ret =  SWAP_FAIL;
		goto out_unmap;
	}

	flush_cache_page(vma, address);
	pteval = ptep_get_and_clear(pte);
	flush_tlb_page(vma, address);

	if (pte_dirty(pteval))
		set_page_dirty(page);

	if (!page->pte.mapcount)
		BUG();

	mm->rss--;
	page->pte.mapcount--;
	page_cache_release(page);

out_unmap:
	pte_unmap(pte);

out:
	spin_unlock(&mm->page_table_lock);
	return ret;
}

/**
 * try_to_unmap_obj - unmap a page using the object-based rmap method
 * @page: the page to unmap
 *
 * Find all the mappings of a page using the mapping pointer and the vma chains
 * contained in the address_space struct it points to.
 *
 * This function is only called from try_to_unmap for object-based pages.
 *
 * The semaphore address_space->i_shared_sem is tried.  If it can't be gotten,
 * return a temporary error.
 */
static int
try_to_unmap_obj(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct vm_area_struct *vma;
	int ret = SWAP_AGAIN;

	if (!mapping)
		BUG();

	if (PageSwapCache(page))
		BUG();

	if (down_trylock(&mapping->i_shared_sem))
		return ret;
	
	list_for_each_entry(vma, &mapping->i_mmap, shared) {
		ret = try_to_unmap_obj_one(vma, page);
		if (ret == SWAP_FAIL || !page->pte.mapcount)
			goto out;
	}

	list_for_each_entry(vma, &mapping->i_mmap_shared, shared) {
		ret = try_to_unmap_obj_one(vma, page);
		if (ret == SWAP_FAIL || !page->pte.mapcount)
			goto out;
	}

out:
	up(&mapping->i_shared_sem);
	return ret;
}

/**
 * try_to_unmap_one - worker function for try_to_unmap
 * @page: page to unmap
 * @ptep: page table entry to unmap from page
 *
 * Internal helper function for try_to_unmap, called for each page
 * table entry mapping a page. Because locking order here is opposite
 * to the locking order used by the page fault path, we use trylocks.
 * Locking:
 *	    page lock			shrink_list(), trylock
 *		pte_chain_lock		shrink_list()
 *		    mm->page_table_lock	try_to_unmap_one(), trylock
 */
static int FASTCALL(try_to_unmap_one(struct page *, pte_addr_t));
static int fastcall try_to_unmap_one(struct page * page, pte_addr_t paddr)
{
	pte_t *ptep = rmap_ptep_map(paddr);
	unsigned long address = ptep_to_address(ptep);
	struct mm_struct * mm = ptep_to_mm(ptep);
	struct vm_area_struct * vma;
	pte_t pte;
	int ret;

	if (!mm)
		BUG();

	/*
	 * We need the page_table_lock to protect us from page faults,
	 * munmap, fork, etc...
	 */
	if (!spin_trylock(&mm->page_table_lock)) {
		rmap_ptep_unmap(ptep);
		return SWAP_AGAIN;
	}


	/* During mremap, it's possible pages are not in a VMA. */
	vma = find_vma(mm, address);
	if (!vma) {
		ret = SWAP_FAIL;
		goto out_unlock;
	}

	/* The page is mlock()d, we cannot swap it out. */
	if (vma->vm_flags & VM_LOCKED) {
		ret = SWAP_FAIL;
		goto out_unlock;
	}

	/* Nuke the page table entry. */
	flush_cache_page(vma, address);
	pte = ptep_clear_flush(vma, address, ptep);

	if (PageSwapCache(page)) {
		/*
		 * Store the swap location in the pte.
		 * See handle_pte_fault() ...
		 */
		swp_entry_t entry = { .val = page->index };
		swap_duplicate(entry);
		set_pte(ptep, swp_entry_to_pte(entry));
		BUG_ON(pte_file(*ptep));
	} else {
		unsigned long pgidx;
		/*
		 * If a nonlinear mapping then store the file page offset
		 * in the pte.
		 */
		pgidx = (address - vma->vm_start) >> PAGE_SHIFT;
		pgidx += vma->vm_pgoff;
		pgidx >>= PAGE_CACHE_SHIFT - PAGE_SHIFT;
		if (page->index != pgidx) {
			set_pte(ptep, pgoff_to_pte(page->index));
			BUG_ON(!pte_file(*ptep));
		}
	}

	/* Move the dirty bit to the physical page now the pte is gone. */
	if (pte_dirty(pte))
		set_page_dirty(page);

	mm->rss--;
	page_cache_release(page);
	ret = SWAP_SUCCESS;

out_unlock:
	rmap_ptep_unmap(ptep);
	spin_unlock(&mm->page_table_lock);
	return ret;
}

/**
 * try_to_unmap - try to remove all page table mappings to a page
 * @page: the page to get unmapped
 *
 * Tries to remove all the page table entries which are mapping this
 * page, used in the pageout path.  Caller must hold the page lock
 * and its pte chain lock.  Return values are:
 *
 * SWAP_SUCCESS	- we succeeded in removing all mappings
 * SWAP_AGAIN	- we missed a trylock, try again later
 * SWAP_FAIL	- the page is unswappable
 */
int fastcall try_to_unmap(struct page * page)
{
	struct pte_chain *pc, *next_pc, *start;
	int ret = SWAP_SUCCESS;
	int victim_i;

	/* This page should not be on the pageout lists. */
	if (PageReserved(page))
		BUG();
	if (!PageLocked(page))
		BUG();
	/* We need backing store to swap out a page. */
	if (!page->mapping)
		BUG();

	/*
	 * If it's an object-based page, use the object vma chain to find all
	 * the mappings.
	 */
	if (!PageAnon(page)) {
		ret = try_to_unmap_obj(page);
		goto out;
	}

	if (PageDirect(page)) {
		ret = try_to_unmap_one(page, page->pte.direct);
		if (ret == SWAP_SUCCESS) {
			if (page_test_and_clear_dirty(page))
				set_page_dirty(page);
			page->pte.direct = 0;
			ClearPageDirect(page);
		}
		goto out;
	}		

	start = page->pte.chain;
	victim_i = pte_chain_idx(start);
	for (pc = start; pc; pc = next_pc) {
		int i;

		next_pc = pte_chain_next(pc);
		if (next_pc)
			prefetch(next_pc);
		for (i = pte_chain_idx(pc); i < NRPTE; i++) {
			pte_addr_t pte_paddr = pc->ptes[i];

			switch (try_to_unmap_one(page, pte_paddr)) {
			case SWAP_SUCCESS:
				/*
				 * Release a slot.  If we're releasing the
				 * first pte in the first pte_chain then
				 * pc->ptes[i] and start->ptes[victim_i] both
				 * refer to the same thing.  It works out.
				 */
				pc->ptes[i] = start->ptes[victim_i];
				start->ptes[victim_i] = 0;
				victim_i++;
				if (victim_i == NRPTE) {
					page->pte.chain = pte_chain_next(start);
					__pte_chain_free(start);
					start = page->pte.chain;
					victim_i = 0;
				} else {
					start->next_and_idx++;
				}
				if (page->pte.direct == 0 &&
				    page_test_and_clear_dirty(page))
					set_page_dirty(page);
				break;
			case SWAP_AGAIN:
				/* Skip this pte, remembering status. */
				ret = SWAP_AGAIN;
				continue;
			case SWAP_FAIL:
				ret = SWAP_FAIL;
				goto out;
			}
		}
	}
out:
	if (!page_mapped(page)) {
		dec_page_state(nr_mapped);
		ret = SWAP_SUCCESS;
	}
	return ret;
}

/**
 * page_convert_anon - Convert an object-based mapped page to pte_chain-based.
 * @page: the page to convert
 *
 * Find all the mappings for an object-based page and convert them
 * to 'anonymous', ie create a pte_chain and store all the pte pointers there.
 *
 * This function takes the address_space->i_shared_sem, sets the PageAnon flag,
 * then sets the mm->page_table_lock for each vma and calls page_add_rmap. This
 * means there is a period when PageAnon is set, but still has some mappings
 * with no pte_chain entry.  This is in fact safe, since page_remove_rmap will
 * simply not find it.  try_to_unmap might erroneously return success, but it
 * will never be called because the page_convert_anon() caller has locked the
 * page.
 *
 * page_referenced() may fail to scan all the appropriate pte's and may return
 * an inaccurate result.  This is so rare that it does not matter.
 */
int page_convert_anon(struct page *page)
{
	struct address_space *mapping;
	struct vm_area_struct *vma;
	struct pte_chain *pte_chain = NULL;
	pte_t *pte;
	int err = 0;

	mapping = page->mapping;
	if (mapping == NULL)
		goto out;		/* truncate won the lock_page() race */

	down(&mapping->i_shared_sem);
	pte_chain_lock(page);

	/*
	 * Has someone else done it for us before we got the lock?
	 * If so, pte.direct or pte.chain has replaced pte.mapcount.
	 */
	if (PageAnon(page)) {
		pte_chain_unlock(page);
		goto out_unlock;
	}

	SetPageAnon(page);
	if (page->pte.mapcount == 0) {
		pte_chain_unlock(page);
		goto out_unlock;
	}
	/* This is gonna get incremented by page_add_rmap */
	dec_page_state(nr_mapped);
	page->pte.mapcount = 0;

	/*
	 * Now that the page is marked as anon, unlock it.  page_add_rmap will
	 * lock it as necessary.
	 */
	pte_chain_unlock(page);

	list_for_each_entry(vma, &mapping->i_mmap, shared) {
		if (!pte_chain) {
			pte_chain = pte_chain_alloc(GFP_KERNEL);
			if (!pte_chain) {
				err = -ENOMEM;
				goto out_unlock;
			}
		}
		spin_lock(&vma->vm_mm->page_table_lock);
		pte = find_pte(vma, page, NULL);
		if (pte) {
			/* Make sure this isn't a duplicate */
			page_remove_rmap(page, pte);
			pte_chain = page_add_rmap(page, pte, pte_chain);
			pte_unmap(pte);
		}
		spin_unlock(&vma->vm_mm->page_table_lock);
	}
	list_for_each_entry(vma, &mapping->i_mmap_shared, shared) {
		if (!pte_chain) {
			pte_chain = pte_chain_alloc(GFP_KERNEL);
			if (!pte_chain) {
				err = -ENOMEM;
				goto out_unlock;
			}
		}
		spin_lock(&vma->vm_mm->page_table_lock);
		pte = find_pte(vma, page, NULL);
		if (pte) {
			/* Make sure this isn't a duplicate */
			page_remove_rmap(page, pte);
			pte_chain = page_add_rmap(page, pte, pte_chain);
			pte_unmap(pte);
		}
		spin_unlock(&vma->vm_mm->page_table_lock);
	}

out_unlock:
	pte_chain_free(pte_chain);
	up(&mapping->i_shared_sem);
out:
	return err;
}

/**
 ** No more VM stuff below this comment, only pte_chain helper
 ** functions.
 **/

static void pte_chain_ctor(void *p, kmem_cache_t *cachep, unsigned long flags)
{
	struct pte_chain *pc = p;

	memset(pc, 0, sizeof(*pc));
}

DEFINE_PER_CPU(struct pte_chain *, local_pte_chain) = 0;

/**
 * __pte_chain_free - free pte_chain structure
 * @pte_chain: pte_chain struct to free
 */
void __pte_chain_free(struct pte_chain *pte_chain)
{
	struct pte_chain **pte_chainp;

	pte_chainp = &get_cpu_var(local_pte_chain);
	if (pte_chain->next_and_idx)
		pte_chain->next_and_idx = 0;
	if (*pte_chainp)
		kmem_cache_free(pte_chain_cache, *pte_chainp);
	*pte_chainp = pte_chain;
	put_cpu_var(local_pte_chain);
}

/*
 * pte_chain_alloc(): allocate a pte_chain structure for use by page_add_rmap().
 *
 * The caller of page_add_rmap() must perform the allocation because
 * page_add_rmap() is invariably called under spinlock.  Often, page_add_rmap()
 * will not actually use the pte_chain, because there is space available in one
 * of the existing pte_chains which are attached to the page.  So the case of
 * allocating and then freeing a single pte_chain is specially optimised here,
 * with a one-deep per-cpu cache.
 */
struct pte_chain *pte_chain_alloc(int gfp_flags)
{
	struct pte_chain *ret;
	struct pte_chain **pte_chainp;

	might_sleep_if(gfp_flags & __GFP_WAIT);

	pte_chainp = &get_cpu_var(local_pte_chain);
	if (*pte_chainp) {
		ret = *pte_chainp;
		*pte_chainp = NULL;
		put_cpu_var(local_pte_chain);
	} else {
		put_cpu_var(local_pte_chain);
		ret = kmem_cache_alloc(pte_chain_cache, gfp_flags);
	}
	return ret;
}

void __init pte_chain_init(void)
{
	// different because of the slab debug patch -arnd
	pte_chain_cache = kmem_cache_create(	"pte_chain",
						sizeof(struct pte_chain),
						sizeof(struct pte_chain),
						0,
						pte_chain_ctor,
						NULL);

	if (!pte_chain_cache)
		panic("failed to create pte_chain cache!\n");
}
