/*
 * mm/rmap.c - physical to virtual reverse mappings
 *
 * Copyright 2001, Rik van Riel <riel@conectiva.com.br>
 * Released under the General Public License (GPL).
 *
 * Simple, low overhead reverse mapping scheme.
 * Please try to keep this thing as modular as possible.
 *
 * Provides methods for unmapping each kind of mapped page:
 * the anon methods track anonymous pages, and
 * the file methods track pages belonging to an inode.
 *
 * Original design by Rik van Riel <riel@conectiva.com.br> 2001
 * File methods by Dave McCracken <dmccr@us.ibm.com> 2003, 2004
 * Anonymous methods by Andrea Arcangeli <andrea@suse.de> 2004
 * Contributions by Hugh Dickins <hugh@veritas.com> 2003, 2004
 */

/*
 * Locking: see "Lock ordering" summary in filemap.c.
 * In swapout, page_map_lock is held on entry to page_referenced and
 * try_to_unmap, so they trylock for i_mmap_lock and page_table_lock.
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rmap.h>

#include <asm/tlbflush.h>

//#define RMAP_DEBUG /* can be enabled only for debugging */

kmem_cache_t *anon_vma_cachep;

static inline void validate_anon_vma(struct vm_area_struct *find_vma)
{
#ifdef RMAP_DEBUG
	struct anon_vma *anon_vma = find_vma->anon_vma;
	struct vm_area_struct *vma;
	unsigned int mapcount = 0;
	int found = 0;

	list_for_each_entry(vma, &anon_vma->head, anon_vma_node) {
		mapcount++;
		BUG_ON(mapcount > 100000);
		if (vma == find_vma)
			found = 1;
	}
	BUG_ON(!found);
#endif
}

/* This must be called under the mmap_sem. */
int anon_vma_prepare(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = vma->anon_vma;

	might_sleep();
	if (unlikely(!anon_vma)) {
		struct mm_struct *mm = vma->vm_mm;
		struct anon_vma *allocated = NULL;

		anon_vma = find_mergeable_anon_vma(vma);
		if (!anon_vma) {
			anon_vma = anon_vma_alloc();
			if (unlikely(!anon_vma))
				return -ENOMEM;
			allocated = anon_vma;
		}

		/* page_table_lock to protect against threads */
		spin_lock(&mm->page_table_lock);
		if (likely(!vma->anon_vma)) {
			if (!allocated)
				spin_lock(&anon_vma->lock);
			vma->anon_vma = anon_vma;
			list_add(&vma->anon_vma_node, &anon_vma->head);
			if (!allocated)
				spin_unlock(&anon_vma->lock);
			allocated = NULL;
		}
		spin_unlock(&mm->page_table_lock);
		if (unlikely(allocated))
			anon_vma_free(allocated);
	}
	return 0;
}

void __anon_vma_merge(struct vm_area_struct *vma, struct vm_area_struct *next)
{
	if (!vma->anon_vma) {
		BUG_ON(!next->anon_vma);
		vma->anon_vma = next->anon_vma;
		list_add(&vma->anon_vma_node, &next->anon_vma_node);
	} else {
		/* if they're both non-null they must be the same */
		BUG_ON(vma->anon_vma != next->anon_vma);
	}
	list_del(&next->anon_vma_node);
}

void __anon_vma_link(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = vma->anon_vma;

	if (anon_vma) {
		list_add(&vma->anon_vma_node, &anon_vma->head);
		validate_anon_vma(vma);
	}
}

void anon_vma_link(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = vma->anon_vma;

	if (anon_vma) {
		spin_lock(&anon_vma->lock);
		list_add(&vma->anon_vma_node, &anon_vma->head);
		validate_anon_vma(vma);
		spin_unlock(&anon_vma->lock);
	}
}

void anon_vma_unlink(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma = vma->anon_vma;
	int empty;

	if (!anon_vma)
		return;

	spin_lock(&anon_vma->lock);
	validate_anon_vma(vma);
	list_del(&vma->anon_vma_node);

	/* We must garbage collect the anon_vma if it's empty */
	empty = list_empty(&anon_vma->head);
	spin_unlock(&anon_vma->lock);

	if (empty)
		anon_vma_free(anon_vma);
}

static void anon_vma_ctor(void *data, kmem_cache_t *cachep, unsigned long flags)
{
	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
						SLAB_CTOR_CONSTRUCTOR) {
		struct anon_vma *anon_vma = data;

		spin_lock_init(&anon_vma->lock);
		INIT_LIST_HEAD(&anon_vma->head);
	}
}

void __init anon_vma_init(void)
{
	anon_vma_cachep = kmem_cache_create("anon_vma",
		sizeof(struct anon_vma), 0, SLAB_PANIC, anon_vma_ctor, NULL);
}

/* this needs the page->flags PG_maplock held */
static inline void clear_page_anon(struct page *page)
{
	BUG_ON(!page->mapping);
	page->mapping = NULL;
	ClearPageAnon(page);
}

/*
 * At what user virtual address is page expected in vma?
 */
static inline unsigned long
vma_address(struct page *page, struct vm_area_struct *vma)
{
	pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	unsigned long address;

	address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	if (unlikely(address < vma->vm_start || address >= vma->vm_end)) {
		/* page should be within any vma from prio_tree_next */
		BUG_ON(!PageAnon(page));
		return -EFAULT;
	}
	return address;
}

/*
 * Subfunctions of page_referenced: page_referenced_one called
 * repeatedly from either page_referenced_anon or page_referenced_file.
 */
static int page_referenced_one(struct page *page,
	struct vm_area_struct *vma, unsigned int *mapcount)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int referenced = 0;

	if (!mm->rss)
		goto out;
	address = vma_address(page, vma);
	if (address == -EFAULT)
		goto out;

	if (!spin_trylock(&mm->page_table_lock))
		goto out;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out_unlock;

	pmd = pmd_offset(pgd, address);
	if (!pmd_present(*pmd))
		goto out_unlock;

	pte = pte_offset_map(pmd, address);
	if (!pte_present(*pte))
		goto out_unmap;

	if (page_to_pfn(page) != pte_pfn(*pte))
		goto out_unmap;

	if (ptep_clear_flush_young(vma, address, pte))
		referenced++;

	(*mapcount)--;

out_unmap:
	pte_unmap(pte);
out_unlock:
	spin_unlock(&mm->page_table_lock);
out:
	return referenced;
}

static inline int page_referenced_anon(struct page *page)
{
	unsigned int mapcount = page->mapcount;
	struct anon_vma *anon_vma = (struct anon_vma *) page->mapping;
	struct vm_area_struct *vma;
	int referenced = 0;

	spin_lock(&anon_vma->lock);
	BUG_ON(list_empty(&anon_vma->head));
	list_for_each_entry(vma, &anon_vma->head, anon_vma_node) {
		referenced += page_referenced_one(page, vma, &mapcount);
		if (!mapcount)
			break;
	}
	spin_unlock(&anon_vma->lock);
	return referenced;
}

/**
 * page_referenced_file - referenced check for object-based rmap
 * @page: the page we're checking references on.
 *
 * For an object-based mapped page, find all the places it is mapped and
 * check/clear the referenced flag.  This is done by following the page->mapping
 * pointer, then walking the chain of vmas it holds.  It returns the number
 * of references it found.
 *
 * This function is only called from page_referenced for object-based pages.
 *
 * The spinlock address_space->i_mmap_lock is tried.  If it can't be gotten,
 * assume a reference count of 0, so try_to_unmap will then have a go.
 */
static inline int page_referenced_file(struct page *page)
{
	unsigned int mapcount = page->mapcount;
	struct address_space *mapping = page->mapping;
	pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	struct vm_area_struct *vma = NULL;
	struct prio_tree_iter iter;
	int referenced = 0;

	if (!spin_trylock(&mapping->i_mmap_lock))
		return 0;

	while ((vma = vma_prio_tree_next(vma, &mapping->i_mmap,
					&iter, pgoff, pgoff)) != NULL) {
		if ((vma->vm_flags & (VM_LOCKED|VM_MAYSHARE))
				  == (VM_LOCKED|VM_MAYSHARE)) {
			referenced++;
			break;
		}
		referenced += page_referenced_one(page, vma, &mapcount);
		if (!mapcount)
			break;
	}

	spin_unlock(&mapping->i_mmap_lock);
	return referenced;
}

/**
 * page_referenced - test if the page was referenced
 * @page: the page to test
 *
 * Quick test_and_clear_referenced for all mappings to a page,
 * returns the number of ptes which referenced the page.
 * Caller needs to hold the rmap lock.
 */
int page_referenced(struct page *page)
{
	int referenced = 0;

	if (page_test_and_clear_young(page))
		referenced++;

	if (TestClearPageReferenced(page))
		referenced++;

	if (page->mapcount && page->mapping) {
		if (PageAnon(page))
			referenced += page_referenced_anon(page);
		else
			referenced += page_referenced_file(page);
	}
	return referenced;
}

/**
 * page_add_anon_rmap - add pte mapping to an anonymous page
 * @page:	the page to add the mapping to
 * @vma:	the vm area in which the mapping is added
 * @address:	the user virtual address mapped
 *
 * The caller needs to hold the mm->page_table_lock.
 */
void page_add_anon_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address)
{
	struct anon_vma *anon_vma = vma->anon_vma;
	pgoff_t index;

	BUG_ON(PageReserved(page));
	BUG_ON(!anon_vma);

	index = (address - vma->vm_start) >> PAGE_SHIFT;
	index += vma->vm_pgoff;
	index >>= PAGE_CACHE_SHIFT - PAGE_SHIFT;

	/*
	 * Setting and clearing PG_anon must always happen inside
	 * page_map_lock to avoid races between mapping and
	 * unmapping on different processes of the same
	 * shared cow swapcache page. And while we take the
	 * page_map_lock PG_anon cannot change from under us.
	 * Actually PG_anon cannot change under fork either
	 * since fork holds a reference on the page so it cannot
	 * be unmapped under fork and in turn copy_page_range is
	 * allowed to read PG_anon outside the page_map_lock.
	 */
	page_map_lock(page);
	if (!page->mapcount) {
		BUG_ON(PageAnon(page));
		BUG_ON(page->mapping);
		SetPageAnon(page);
		page->index = index;
		page->mapping = (struct address_space *) anon_vma;
		inc_page_state(nr_mapped);
	} else {
		BUG_ON(!PageAnon(page));
		BUG_ON(page->index != index);
		BUG_ON(page->mapping != (struct address_space *) anon_vma);
	}
	page->mapcount++;
	page_map_unlock(page);
}

/**
 * page_add_file_rmap - add pte mapping to a file page
 * @page: the page to add the mapping to
 *
 * The caller needs to hold the mm->page_table_lock.
 */
void page_add_file_rmap(struct page *page)
{
	BUG_ON(PageAnon(page));
	if (!pfn_valid(page_to_pfn(page)) || PageReserved(page))
		return;

	page_map_lock(page);
	if (!page->mapcount)
		inc_page_state(nr_mapped);
	page->mapcount++;
	page_map_unlock(page);
}

/**
 * page_remove_rmap - take down pte mapping from a page
 * @page: page to remove mapping from
 *
 * Caller needs to hold the mm->page_table_lock.
 */
void page_remove_rmap(struct page *page)
{
	BUG_ON(PageReserved(page));
	BUG_ON(!page->mapcount);

	page_map_lock(page);
	page->mapcount--;
	if (!page->mapcount) {
		if (page_test_and_clear_dirty(page))
			set_page_dirty(page);
		if (PageAnon(page))
			clear_page_anon(page);
		dec_page_state(nr_mapped);
	}
	page_map_unlock(page);
}

/*
 * Subfunctions of try_to_unmap: try_to_unmap_one called
 * repeatedly from either try_to_unmap_anon or try_to_unmap_file.
 */
static int try_to_unmap_one(struct page *page, struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	pte_t pteval;
	int ret = SWAP_AGAIN;

	if (!mm->rss)
		goto out;
	address = vma_address(page, vma);
	if (address == -EFAULT)
		goto out;

	/*
	 * We need the page_table_lock to protect us from page faults,
	 * munmap, fork, etc...
	 */
	if (!spin_trylock(&mm->page_table_lock))
		goto out;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out_unlock;

	pmd = pmd_offset(pgd, address);
	if (!pmd_present(*pmd))
		goto out_unlock;

	pte = pte_offset_map(pmd, address);
	if (!pte_present(*pte))
		goto out_unmap;

	if (page_to_pfn(page) != pte_pfn(*pte))
		goto out_unmap;

	/*
	 * If the page is mlock()d, we cannot swap it out.
	 * If it's recently referenced (perhaps page_referenced
	 * skipped over this mm) then we should reactivate it.
	 */
	if ((vma->vm_flags & (VM_LOCKED|VM_RESERVED)) ||
			ptep_clear_flush_young(vma, address, pte)) {
		ret = SWAP_FAIL;
		goto out_unmap;
	}

	/*
	 * Don't pull an anonymous page out from under get_user_pages.
	 * GUP carefully breaks COW and raises page count (while holding
	 * page_table_lock, as we have here) to make sure that the page
	 * cannot be freed.  If we unmap that page here, a user write
	 * access to the virtual address will bring back the page, but
	 * its raised count will (ironically) be taken to mean it's not
	 * an exclusive swap page, do_wp_page will replace it by a copy
	 * page, and the user never get to see the data GUP was holding
	 * the original page for.
	 *
	 * This test is also useful for when swapoff (unuse_process) has
	 * to drop page lock: its reference to the page stops existing
	 * ptes from being unmapped, so swapoff can make progress.
	 */
	if (PageSwapCache(page) &&
	    page_count(page) != page->mapcount + 2) {
		ret = SWAP_FAIL;
		goto out_unmap;
	}

	/* Nuke the page table entry. */
	flush_cache_page(vma, address);
	pteval = ptep_clear_flush(vma, address, pte);

	/* Move the dirty bit to the physical page now the pte is gone. */
	if (pte_dirty(pteval))
		set_page_dirty(page);

	if (PageAnon(page)) {
		swp_entry_t entry = { .val = page->private };
		/*
		 * Store the swap location in the pte.
		 * See handle_pte_fault() ...
		 */
		BUG_ON(!PageSwapCache(page));
		swap_duplicate(entry);
		set_pte(pte, swp_entry_to_pte(entry));
		BUG_ON(pte_file(*pte));
	}

	mm->rss--;
	BUG_ON(!page->mapcount);
	page->mapcount--;
	page_cache_release(page);

out_unmap:
	pte_unmap(pte);
out_unlock:
	spin_unlock(&mm->page_table_lock);
out:
	return ret;
}

/*
 * objrmap doesn't work for nonlinear VMAs because the assumption that
 * offset-into-file correlates with offset-into-virtual-addresses does not hold.
 * Consequently, given a particular page and its ->index, we cannot locate the
 * ptes which are mapping that page without an exhaustive linear search.
 *
 * So what this code does is a mini "virtual scan" of each nonlinear VMA which
 * maps the file to which the target page belongs.  The ->vm_private_data field
 * holds the current cursor into that scan.  Successive searches will circulate
 * around the vma's virtual address space.
 *
 * So as more replacement pressure is applied to the pages in a nonlinear VMA,
 * more scanning pressure is placed against them as well.   Eventually pages
 * will become fully unmapped and are eligible for eviction.
 *
 * For very sparsely populated VMAs this is a little inefficient - chances are
 * there there won't be many ptes located within the scan cluster.  In this case
 * maybe we could scan further - to the end of the pte page, perhaps.
 */
#define CLUSTER_SIZE	min(32*PAGE_SIZE, PMD_SIZE)
#define CLUSTER_MASK	(~(CLUSTER_SIZE - 1))

static int try_to_unmap_cluster(unsigned long cursor,
	unsigned int *mapcount, struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	pte_t pteval;
	struct page *page;
	unsigned long address;
	unsigned long end;
	unsigned long pfn;

	/*
	 * We need the page_table_lock to protect us from page faults,
	 * munmap, fork, etc...
	 */
	if (!spin_trylock(&mm->page_table_lock))
		return SWAP_FAIL;

	address = (vma->vm_start + cursor) & CLUSTER_MASK;
	end = address + CLUSTER_SIZE;
	if (address < vma->vm_start)
		address = vma->vm_start;
	if (end > vma->vm_end)
		end = vma->vm_end;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out_unlock;

	pmd = pmd_offset(pgd, address);
	if (!pmd_present(*pmd))
		goto out_unlock;

	for (pte = pte_offset_map(pmd, address);
			address < end; pte++, address += PAGE_SIZE) {

		if (!pte_present(*pte))
			continue;

		pfn = pte_pfn(*pte);
		if (!pfn_valid(pfn))
			continue;

		page = pfn_to_page(pfn);
		BUG_ON(PageAnon(page));
		if (PageReserved(page))
			continue;

		if (ptep_clear_flush_young(vma, address, pte))
			continue;

		/* Nuke the page table entry. */
		flush_cache_page(vma, address);
		pteval = ptep_clear_flush(vma, address, pte);

		/* If nonlinear, store the file page offset in the pte. */
		if (page->index != linear_page_index(vma, address))
			set_pte(pte, pgoff_to_pte(page->index));

		/* Move the dirty bit to the physical page now the pte is gone. */
		if (pte_dirty(pteval))
			set_page_dirty(page);

		page_remove_rmap(page);
		page_cache_release(page);
		mm->rss--;
		(*mapcount)--;
	}

	pte_unmap(pte);

out_unlock:
	spin_unlock(&mm->page_table_lock);
	return SWAP_AGAIN;
}

static inline int try_to_unmap_anon(struct page *page)
{
	struct anon_vma *anon_vma = (struct anon_vma *) page->mapping;
	struct vm_area_struct *vma;
	int ret = SWAP_AGAIN;

	spin_lock(&anon_vma->lock);
	BUG_ON(list_empty(&anon_vma->head));
	list_for_each_entry(vma, &anon_vma->head, anon_vma_node) {
		ret = try_to_unmap_one(page, vma);
		if (ret == SWAP_FAIL || !page->mapcount)
			break;
	}
	spin_unlock(&anon_vma->lock);
	return ret;
}

/**
 * try_to_unmap_file - unmap file page using the object-based rmap method
 * @page: the page to unmap
 *
 * Find all the mappings of a page using the mapping pointer and the vma chains
 * contained in the address_space struct it points to.
 *
 * This function is only called from try_to_unmap for object-based pages.
 *
 * The spinlock address_space->i_mmap_lock is tried.  If it can't be gotten,
 * return a temporary error.
 */
static inline int try_to_unmap_file(struct page *page)
{
	struct address_space *mapping = page->mapping;
	pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	struct vm_area_struct *vma = NULL;
	struct prio_tree_iter iter;
	int ret = SWAP_AGAIN;
	unsigned long cursor;
	unsigned long max_nl_cursor = 0;
	unsigned long max_nl_size = 0;
	unsigned int mapcount;

	if (!spin_trylock(&mapping->i_mmap_lock))
		return ret;

	while ((vma = vma_prio_tree_next(vma, &mapping->i_mmap,
					&iter, pgoff, pgoff)) != NULL) {
		ret = try_to_unmap_one(page, vma);
		if (ret == SWAP_FAIL || !page->mapcount)
			goto out;
	}

	if (list_empty(&mapping->i_mmap_nonlinear))
		goto out;

	list_for_each_entry(vma, &mapping->i_mmap_nonlinear,
						shared.vm_set.list) {
		if (vma->vm_flags & (VM_LOCKED|VM_RESERVED))
			continue;
		cursor = (unsigned long) vma->vm_private_data;
		if (cursor > max_nl_cursor)
			max_nl_cursor = cursor;
		cursor = vma->vm_end - vma->vm_start;
		if (cursor > max_nl_size)
			max_nl_size = cursor;
	}

	if (max_nl_size == 0)	/* any nonlinears locked or reserved */
		goto out;

	/*
	 * We don't try to search for this page in the nonlinear vmas,
	 * and page_referenced wouldn't have found it anyway.  Instead
	 * just walk the nonlinear vmas trying to age and unmap some.
	 * The mapcount of the page we came in with is irrelevant,
	 * but even so use it as a guide to how hard we should try?
	 */
	mapcount = page->mapcount;
	page_map_unlock(page);
	cond_resched_lock(&mapping->i_mmap_lock);

	max_nl_size = (max_nl_size + CLUSTER_SIZE - 1) & CLUSTER_MASK;
	if (max_nl_cursor == 0)
		max_nl_cursor = CLUSTER_SIZE;

	do {
		list_for_each_entry(vma, &mapping->i_mmap_nonlinear,
						shared.vm_set.list) {
			if (vma->vm_flags & (VM_LOCKED|VM_RESERVED))
				continue;
			cursor = (unsigned long) vma->vm_private_data;
			while (vma->vm_mm->rss &&
				cursor < max_nl_cursor &&
				cursor < vma->vm_end - vma->vm_start) {
				ret = try_to_unmap_cluster(
						cursor, &mapcount, vma);
				if (ret == SWAP_FAIL)
					break;
				cursor += CLUSTER_SIZE;
				vma->vm_private_data = (void *) cursor;
				if ((int)mapcount <= 0)
					goto relock;
			}
			if (ret != SWAP_FAIL)
				vma->vm_private_data =
					(void *) max_nl_cursor;
			ret = SWAP_AGAIN;
		}
		cond_resched_lock(&mapping->i_mmap_lock);
		max_nl_cursor += CLUSTER_SIZE;
	} while (max_nl_cursor <= max_nl_size);

	/*
	 * Don't loop forever (perhaps all the remaining pages are
	 * in locked vmas).  Reset cursor on all unreserved nonlinear
	 * vmas, now forgetting on which ones it had fallen behind.
	 */
	list_for_each_entry(vma, &mapping->i_mmap_nonlinear,
						shared.vm_set.list) {
		if (!(vma->vm_flags & VM_RESERVED))
			vma->vm_private_data = NULL;
	}
relock:
	page_map_lock(page);
out:
	spin_unlock(&mapping->i_mmap_lock);
	return ret;
}

/**
 * try_to_unmap - try to remove all page table mappings to a page
 * @page: the page to get unmapped
 *
 * Tries to remove all the page table entries which are mapping this
 * page, used in the pageout path.  Caller must hold the page lock
 * and its rmap lock.  Return values are:
 *
 * SWAP_SUCCESS	- we succeeded in removing all mappings
 * SWAP_AGAIN	- we missed a trylock, try again later
 * SWAP_FAIL	- the page is unswappable
 */
int try_to_unmap(struct page *page)
{
	int ret;

	BUG_ON(PageReserved(page));
	BUG_ON(!PageLocked(page));
	BUG_ON(!page->mapcount);

	if (PageAnon(page))
		ret = try_to_unmap_anon(page);
	else
		ret = try_to_unmap_file(page);

	if (!page->mapcount) {
		if (page_test_and_clear_dirty(page))
			set_page_dirty(page);
		if (PageAnon(page))
			clear_page_anon(page);
		dec_page_state(nr_mapped);
		ret = SWAP_SUCCESS;
	}
	return ret;
}
