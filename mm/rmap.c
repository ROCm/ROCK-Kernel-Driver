/*
 * mm/rmap.c - physical to virtual reverse mappings
 *
 * Copyright 2001, Rik van Riel <riel@conectiva.com.br>
 * Released under the General Public License (GPL).
 *
 * Simple, low overhead reverse mapping scheme.
 * Please try to keep this thing as modular as possible.
 */

/*
 * Locking:
 * - the page->mapcount field is protected by the PG_maplock bit,
 *   which nests within the mm->page_table_lock,
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
#include <linux/rmap.h>

#include <asm/tlbflush.h>

static inline void clear_page_anon(struct page *page)
{
	page->mapping = NULL;
	ClearPageAnon(page);
}

/**
 ** VM stuff below this comment
 **/

/*
 * At what user virtual address is pgoff expected in file-backed vma?
 */
static inline
unsigned long vma_address(struct vm_area_struct *vma, pgoff_t pgoff)
{
	unsigned long address;

	address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	return (address >= vma->vm_start && address < vma->vm_end)?
		address: -EFAULT;
}

/**
 ** Subfunctions of page_referenced: page_referenced_one called
 ** repeatedly from either page_referenced_anon or page_referenced_file.
 **/

static int page_referenced_one(struct page *page,
	struct mm_struct *mm, unsigned long address,
	unsigned int *mapcount, int *failed)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int referenced = 0;

	if (!spin_trylock(&mm->page_table_lock)) {
		/*
		 * For debug we're currently warning if not all found,
		 * but in this case that's expected: suppress warning.
		 */
		(*failed)++;
		return 0;
	}

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

	if (ptep_test_and_clear_young(pte))
		referenced++;

	(*mapcount)--;

out_unmap:
	pte_unmap(pte);

out_unlock:
	spin_unlock(&mm->page_table_lock);
	return referenced;
}

static inline int page_referenced_anon(struct page *page)
{
	return 1;	/* until next patch */
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
 * The semaphore address_space->i_shared_sem is tried.  If it can't be gotten,
 * assume a reference count of 0, so try_to_unmap will then have a go.
 */
static inline int page_referenced_file(struct page *page)
{
	unsigned int mapcount = page->mapcount;
	struct address_space *mapping = page->mapping;
	pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	struct vm_area_struct *vma;
	unsigned long address;
	int referenced = 0;
	int failed = 0;

	if (down_trylock(&mapping->i_shared_sem))
		return 0;

	list_for_each_entry(vma, &mapping->i_mmap, shared) {
		address = vma_address(vma, pgoff);
		if (address == -EFAULT)
			continue;
		if ((vma->vm_flags & (VM_LOCKED|VM_MAYSHARE))
				  == (VM_LOCKED|VM_MAYSHARE)) {
			referenced++;
			goto out;
		}
		if (vma->vm_mm->rss) {
			referenced += page_referenced_one(page,
				vma->vm_mm, address, &mapcount, &failed);
			if (!mapcount)
				goto out;
		}
	}

	list_for_each_entry(vma, &mapping->i_mmap_shared, shared) {
		if (unlikely(vma->vm_flags & VM_NONLINEAR)) {
			failed++;
			continue;
		}
		address = vma_address(vma, pgoff);
		if (address == -EFAULT)
			continue;
		if (vma->vm_flags & (VM_LOCKED|VM_RESERVED)) {
			referenced++;
			goto out;
		}
		if (vma->vm_mm->rss) {
			referenced += page_referenced_one(page,
				vma->vm_mm, address, &mapcount, &failed);
			if (!mapcount)
				goto out;
		}
	}

	WARN_ON(!failed);
out:
	up(&mapping->i_shared_sem);
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
int fastcall page_referenced(struct page *page)
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
 * @mm:		the mm in which the mapping is added
 * @address:	the user virtual address mapped
 *
 * The caller needs to hold the mm->page_table_lock.
 */
void fastcall page_add_anon_rmap(struct page *page,
	struct mm_struct *mm, unsigned long address)
{
	BUG_ON(PageReserved(page));

	page_map_lock(page);
	if (!page->mapcount) {
		BUG_ON(page->mapping);
		SetPageAnon(page);
		page->index = address & PAGE_MASK;
		page->mapping = (void *) mm;	/* until next patch */
		inc_page_state(nr_mapped);
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
void fastcall page_add_file_rmap(struct page *page)
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
void fastcall page_remove_rmap(struct page *page)
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

/**
 ** Subfunctions of try_to_unmap: try_to_unmap_one called
 ** repeatedly from either try_to_unmap_anon or try_to_unmap_file.
 **/

static int try_to_unmap_one(struct page *page,
	struct mm_struct *mm, unsigned long address,
	unsigned int *mapcount, struct vm_area_struct *vma)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	pte_t pteval;
	int ret = SWAP_AGAIN;

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

	(*mapcount)--;

	/*
	 * If the page is mlock()d, we cannot swap it out.
	 * If it's recently referenced (perhaps page_referenced
	 * skipped over this mm) then we should reactivate it.
	 */
	if ((vma->vm_flags & (VM_LOCKED|VM_RESERVED)) ||
			ptep_test_and_clear_young(pte)) {
		ret = SWAP_FAIL;
		goto out_unmap;
	}

	/* Nuke the page table entry. */
	flush_cache_page(vma, address);
	pteval = ptep_clear_flush(vma, address, pte);

	/* Move the dirty bit to the physical page now the pte is gone. */
	if (pte_dirty(pteval))
		set_page_dirty(page);

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
#define CLUSTER_SIZE	(32 * PAGE_SIZE)
#if     CLUSTER_SIZE  >	PMD_SIZE
#undef  CLUSTER_SIZE
#define CLUSTER_SIZE	PMD_SIZE
#endif
#define CLUSTER_MASK	(~(CLUSTER_SIZE - 1))

static int try_to_unmap_cluster(struct mm_struct *mm, unsigned long cursor,
	unsigned int *mapcount, struct vm_area_struct *vma)
{
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

		if (ptep_test_and_clear_young(pte))
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
	return SWAP_FAIL;	/* until next patch */
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
 * The semaphore address_space->i_shared_sem is tried.  If it can't be gotten,
 * return a temporary error.
 */
static inline int try_to_unmap_file(struct page *page)
{
	unsigned int mapcount = page->mapcount;
	struct address_space *mapping = page->mapping;
	pgoff_t pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
	struct vm_area_struct *vma;
	unsigned long address;
	int ret = SWAP_AGAIN;
	unsigned long cursor;
	unsigned long max_nl_cursor = 0;
	unsigned long max_nl_size = 0;

	if (down_trylock(&mapping->i_shared_sem))
		return ret;

	list_for_each_entry(vma, &mapping->i_mmap, shared) {
		if (vma->vm_mm->rss) {
			address = vma_address(vma, pgoff);
			if (address == -EFAULT)
				continue;
			ret = try_to_unmap_one(page,
				vma->vm_mm, address, &mapcount, vma);
			if (ret == SWAP_FAIL || !mapcount)
				goto out;
		}
	}

	list_for_each_entry(vma, &mapping->i_mmap_shared, shared) {
		if (unlikely(vma->vm_flags & VM_NONLINEAR)) {
			/*
			 * Defer unmapping nonlinear to the next loop,
			 * but take notes while we're here e.g. don't
			 * want to loop again when no nonlinear vmas.
			 */
			if (vma->vm_flags & (VM_LOCKED|VM_RESERVED))
				continue;
			cursor = (unsigned long) vma->vm_private_data;
			if (cursor > max_nl_cursor)
				max_nl_cursor = cursor;
			cursor = vma->vm_end - vma->vm_start;
			if (cursor > max_nl_size)
				max_nl_size = cursor;
			continue;
		}
		if (vma->vm_mm->rss) {
			address = vma_address(vma, pgoff);
			if (address == -EFAULT)
				continue;
			ret = try_to_unmap_one(page,
				vma->vm_mm, address, &mapcount, vma);
			if (ret == SWAP_FAIL || !mapcount)
				goto out;
		}
	}

	if (max_nl_size == 0)	/* no nonlinear vmas of this file */
		goto out;

	/*
	 * We don't try to search for this page in the nonlinear vmas,
	 * and page_referenced wouldn't have found it anyway.  Instead
	 * just walk the nonlinear vmas trying to age and unmap some.
	 * The mapcount of the page we came in with is irrelevant,
	 * but even so use it as a guide to how hard we should try?
	 */
	page_map_unlock(page);

	max_nl_size = (max_nl_size + CLUSTER_SIZE - 1) & CLUSTER_MASK;
	if (max_nl_cursor == 0)
		max_nl_cursor = CLUSTER_SIZE;

	do {
		list_for_each_entry(vma, &mapping->i_mmap_shared, shared) {
			if (VM_NONLINEAR != (vma->vm_flags &
			     (VM_NONLINEAR|VM_LOCKED|VM_RESERVED)))
				continue;
			cursor = (unsigned long) vma->vm_private_data;
			while (vma->vm_mm->rss &&
				cursor < max_nl_cursor &&
				cursor < vma->vm_end - vma->vm_start) {
				ret = try_to_unmap_cluster(vma->vm_mm,
						cursor, &mapcount, vma);
				if (ret == SWAP_FAIL)
					break;
				cursor += CLUSTER_SIZE;
				vma->vm_private_data = (void *) cursor;
				if ((int)mapcount <= 0)
					goto relock;
				cond_resched();
			}
			if (ret != SWAP_FAIL)
				vma->vm_private_data =
					(void *) max_nl_cursor;
			ret = SWAP_AGAIN;
		}
		max_nl_cursor += CLUSTER_SIZE;
	} while (max_nl_cursor <= max_nl_size);

	/*
	 * Don't loop forever (perhaps all the remaining pages are
	 * in locked vmas).  Reset cursor on all unreserved nonlinear
	 * vmas, now forgetting on which ones it had fallen behind.
	 */
	list_for_each_entry(vma, &mapping->i_mmap_shared, shared) {
		if ((vma->vm_flags & (VM_NONLINEAR|VM_RESERVED)) ==
				VM_NONLINEAR)
			vma->vm_private_data = 0;
	}
relock:
	page_map_lock(page);
out:
	up(&mapping->i_shared_sem);
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
int fastcall try_to_unmap(struct page *page)
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
