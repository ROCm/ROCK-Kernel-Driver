/*
 *  mm/objrmap.c
 *
 *  Provides methods for unmapping all sort of mapped pages
 *  using the vma objects, the brainer part of objrmap is the
 *  tracking of the vma to analyze for every given mapped page.
 *  The anon_vma methods are tracking anonymous pages,
 *  and the inode methods are tracking pages belonging
 *  to an inode.
 *
 *  anonymous methods by Andrea Arcangeli <andrea@suse.de> 2004
 *  inode methods by Dave McCracken <dmccr@us.ibm.com> 2003, 2004
 */

/*
 * try_to_unmap/page_referenced/page_add_rmap/page_remove_rmap
 * inherit from the rmap design mm/rmap.c under
 * Copyright 2001, Rik van Riel <riel@conectiva.com.br>
 * Released under the General Public License (GPL).
 */

/*
 * nonlinear pagetable walking elaborated from mm/memory.c under
 * Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/objrmap.h>
#include <linux/init.h>
#include <asm/tlbflush.h>

kmem_cache_t * anon_vma_cachep;

//#define OBJRMAP_DEBUG /* can be enabled only for debugging */

static inline void validate_anon_vma_find_vma(struct vm_area_struct * find_vma)
{
#ifdef OBJRMAP_DEBUG
	struct vm_area_struct * vma;
	anon_vma_t * anon_vma = find_vma->anon_vma;
	unsigned long mapcount = 0;
	int found = 0;

	list_for_each_entry(vma, &anon_vma->anon_vma_head, anon_vma_node) {
		mapcount += 1;
		BUG_ON(mapcount > 100000);
		if (vma == find_vma)
			found = 1;
	}
	BUG_ON(!found);
#endif
}

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
static pte_t *
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
 * page_referenced_one - referenced check for object-based rmap
 * @vma: the vma to look in.
 * @page: the page we're working on.
 *
 * Find a pte entry for a page/vma pair, then check and clear the referenced
 * bit.
 *
 * This is strictly a helper function for page_referenced_inode.
 */
static int
page_referenced_one(struct vm_area_struct *vma, struct page *page)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t *pte;
	int referenced = 0;

	/*
	 * Tracking the referenced info is too expensive
	 * for nonlinear mappings.
	 */
	if (vma->vm_flags & VM_NONLINEAR)
		goto out;

	if (unlikely(!spin_trylock(&mm->page_table_lock)))
		goto out;

	pte = find_pte(vma, page, NULL);
	if (pte) {
		if (pte_young(*pte) && ptep_test_and_clear_young(pte))
			referenced++;
		pte_unmap(pte);
	}

	spin_unlock(&mm->page_table_lock);
 out:
	return referenced;
}

/**
 * page_referenced_inode - referenced check for object-based rmap
 * @page: the page we're checking references on.
 *
 * For an object-based mapped page, find all the places it is mapped and
 * check/clear the referenced flag.  This is done by following the page->as.mapping
 * pointer, then walking the chain of vmas it holds.  It returns the number
 * of references it found.
 *
 * This function is only called from page_referenced for object-based pages.
 *
 * The semaphore address_space->i_shared_sem is tried.  If it can't be gotten,
 * assume a reference count of 1.
 */
static int
page_referenced_inode(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct vm_area_struct *vma;
	int referenced = 0;

	BUG_ON(PageSwapCache(page));

	if (unlikely(down_trylock(&mapping->i_shared_sem)))
		goto out;

	list_for_each_entry(vma, &mapping->i_mmap, shared)
		referenced += page_referenced_one(vma, page);

	list_for_each_entry(vma, &mapping->i_mmap_shared, shared)
		referenced += page_referenced_one(vma, page);

	up(&mapping->i_shared_sem);
 out:
	return referenced;
}

static int page_referenced_anon(struct page *page)
{
	int referenced;
	struct vm_area_struct * vma;
	anon_vma_t * anon_vma = (anon_vma_t *) page->mapping;

	referenced = 0;
	spin_lock(&anon_vma->anon_vma_lock);
	BUG_ON(list_empty(&anon_vma->anon_vma_head));
	list_for_each_entry(vma, &anon_vma->anon_vma_head, anon_vma_node)
		referenced += page_referenced_one(vma, page);
	spin_unlock(&anon_vma->anon_vma_lock);

	return referenced;
}

/**
 * page_referenced - test if the page was referenced
 * @page: the page to test
 *
 * Quick test_and_clear_referenced for all mappings to a page,
 * returns the number of processes which referenced the page.
 *
 * Caller needs to hold the page_map_lock.
 */
int fastcall page_referenced(struct page * page)
{
	int referenced = 0;

	if (!page_mapped(page))
		goto out;

	/*
	 * We need an object to reach the ptes, all mapped
	 * pages must provide some method in their mapping.
	 * Subtle: this checks for page->as.anon_vma/vma too ;).
	 */
	BUG_ON(!page->mapping);

	if (page_test_and_clear_young(page))
		referenced++;

	if (TestClearPageReferenced(page))
		referenced++;

	if (!PageAnon(page))
		referenced += page_referenced_inode(page);
	else
		referenced += page_referenced_anon(page);

 out:
	return referenced;
}

/* this needs the page->flags PG_map_lock held */
static inline void anon_vma_page_link(struct page * page, struct vm_area_struct * vma,
				      unsigned long address)
{
	unsigned long index = ((address - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	BUG_ON(!vma->anon_vma);
	if (page->mapcount == 1) {
		page->index = index;
		BUG_ON(page->mapping);
		page->mapping = (struct address_space *) vma->anon_vma;
	} else {
		BUG_ON(vma->anon_vma != (anon_vma_t *) page->mapping || index != page->index);
	}
}

/**
 * page_add_rmap - add reverse mapping entry to a page
 * @page: the page to add the mapping to
 * @vma: the vma that is covering the page
 *
 * Add a new pte reverse mapping to a page.
 */
void fastcall page_add_rmap(struct page *page, struct vm_area_struct * vma,
			    unsigned long address, int anon)
{
	int last_anon;

	if (PageReserved(page))
		return;

	page_map_lock(page);

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
	last_anon = PageAnon(page);
	if (anon && !last_anon)
		SetPageAnon(page);
	BUG_ON(!anon && last_anon);

	if (!page->mapcount++)
		inc_page_state(nr_mapped);

	if (PageAnon(page))
		anon_vma_page_link(page, vma, address);
	else {
		/*
		 * If this is an object-based page, just count it.
		 * We can find the mappings by walking the object
		 * vma chain for that object.
		 */
		BUG_ON(PageSwapCache(page));
		BUG_ON(!page->mapping);
	}

	page_map_unlock(page);
}

/* this needs the page->flags PG_map_lock held */
static inline void anon_vma_page_unlink(struct page * page)
{
	BUG_ON(!page->mapping);
	/*
	 * Cleanup if this anon page is gone
	 * as far as the vm is concerned.
	 */
	if (!page->mapcount) {
		page->mapping = NULL;
		ClearPageAnon(page);
	}
}

/**
 * page_remove_rmap - take down reverse mapping to a page
 * @page: page to remove mapping from
 *
 * Removes the reverse mapping from the pte_chain of the page,
 * after that the caller can clear the page table entry and free
 * the page.
 */
void fastcall page_remove_rmap(struct page *page)
{
	if (PageReserved(page))
		return;

	page_map_lock(page);

	if (!page_mapped(page))
		goto out_unlock;

	if (!--page->mapcount) {
		dec_page_state(nr_mapped);
		if (page_test_and_clear_dirty(page))
			set_page_dirty(page);
	}

	if (PageAnon(page))
		anon_vma_page_unlink(page);
	else {
		/*
		 * If this is an object-based page, just uncount it.
		 * We can find the mappings by walking the object vma
		 * chain for that object.
		 */
		BUG_ON(PageSwapCache(page));
		/*
		 * This maybe a page cache removed from pagecache
		 * before all ptes have been unmapped, warn in such
		 * a case.
		 */
		WARN_ON(!page->mapping);
	}
  
 out_unlock:
	page_map_unlock(page);
}

static void
unmap_pte_page(struct page * page, struct vm_area_struct * vma,
	       unsigned long address, pte_t * pte)
{
	pte_t pteval;

	flush_cache_page(vma, address);
	pteval = ptep_clear_flush(vma, address, pte);

	if (PageSwapCache(page)) {
		/*
		 * Store the swap location in the pte.
		 * See handle_pte_fault() ...
		 */
		swp_entry_t entry = { .val = page->private };
		swap_duplicate(entry);
		set_pte(pte, swp_entry_to_pte(entry));

		BUG_ON(pte_file(*pte));
		BUG_ON(!PageAnon(page));
		BUG_ON(!page->mapping);
		BUG_ON(!page->mapcount);
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
			set_pte(pte, pgoff_to_pte(page->index));
			BUG_ON(!pte_file(*pte));
		}

		BUG_ON(!page->mapping);
		BUG_ON(!page->mapcount);
		BUG_ON(PageAnon(page));
	}

	if (pte_dirty(pteval))
		set_page_dirty(page);

	vma->vm_mm->rss--;
	if (!--page->mapcount && PageAnon(page))
		anon_vma_page_unlink(page);
	page_cache_release(page);
}

static void
try_to_unmap_nonlinear_pte(struct vm_area_struct * vma,
			   pmd_t * pmd, unsigned long address, unsigned long size)
{
	unsigned long offset;
	pte_t *ptep;

	if (pmd_none(*pmd))
		return;
	if (unlikely(pmd_bad(*pmd))) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return;
	}
	ptep = pte_offset_map(pmd, address);
	offset = address & ~PMD_MASK;
	if (offset + size > PMD_SIZE)
		size = PMD_SIZE - offset;
	size &= PAGE_MASK;
	for (offset=0; offset < size; ptep++, offset += PAGE_SIZE) {
		pte_t pte = *ptep;
		if (pte_none(pte))
			continue;
		if (pte_present(pte)) {
			unsigned long pfn = pte_pfn(pte);
			struct page * page;

			if (!pfn_valid(pfn))
				continue;
			page = pfn_to_page(pfn);
			if (PageReserved(page))
				continue;
			if (pte_young(pte) && ptep_test_and_clear_young(ptep))
				continue;
			/*
			 * any other page in the nonlinear mapping will not wait
			 * on us since only one cpu can take the i_shared_sem
			 * and reach this point.
			 */
			page_map_lock(page);
			/* check that we're not in between set_pte and page_add_rmap */
			if (page_mapped(page)) {
				unmap_pte_page(page, vma, address + offset, ptep);
				if (!page_mapped(page) && page_test_and_clear_dirty(page))
					set_page_dirty(page);
			}
			page_map_unlock(page);
		}
	}
	pte_unmap(ptep-1);
}

static void
try_to_unmap_nonlinear_pmd(struct vm_area_struct * vma,
			   pgd_t * dir, unsigned long address, unsigned long end)
{
	pmd_t * pmd;

	if (pgd_none(*dir))
		return;
	if (unlikely(pgd_bad(*dir))) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	if (end > ((address + PGDIR_SIZE) & PGDIR_MASK))
		end = ((address + PGDIR_SIZE) & PGDIR_MASK);
	do {
		try_to_unmap_nonlinear_pte(vma, pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK; 
		pmd++;
	} while (address && (address < end));
}

static void
try_to_unmap_nonlinear(struct vm_area_struct *vma)
{
	pgd_t * dir;
	unsigned long address = vma->vm_start, end = vma->vm_end;

	dir = pgd_offset(vma->vm_mm, address);
	do {
		try_to_unmap_nonlinear_pmd(vma, dir, address, end);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
}

/**
 * try_to_unmap_one - unmap a page using the object-based rmap method
 * @page: the page to unmap
 *
 * Determine whether a page is mapped in a given vma and unmap it if it's found.
 *
 * This function is strictly a helper function for try_to_unmap_inode.
 */
static int
try_to_unmap_one(struct vm_area_struct *vma, struct page *page, int * young)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *pte;
	int ret;

	BUG_ON(vma->vm_flags & VM_RESERVED);
	if (unlikely(vma->vm_flags & VM_LOCKED))
		return SWAP_FAIL;

	ret = SWAP_AGAIN;
	if (unlikely(!spin_trylock(&mm->page_table_lock)))
		return ret;

	if (unlikely(vma->vm_flags & VM_NONLINEAR)) {
		/*
		 * If this was a false positive generated by a
		 * failed trylock in the referenced pass let's
		 * avoid to pay the big cost of the nonlinear
		 * swap, we'd better be sure we've to pay that
		 * cost before running it.
		 */
		if (!*young) {
			/*
			 * All it matters is that the page won't go
			 * away under us after we unlock.
			 */
			page_map_unlock(page);
			try_to_unmap_nonlinear(vma);
			page_map_lock(page);
		}
		goto out;
	}

	pte = find_pte(vma, page, &address);
	if (!pte)
		goto out;

	/*
	 * We use trylocks in the "reference" methods, if they fails
	 * we let the VM to go ahead unmapping to avoid locking
	 * congestions, so here we may be trying to unmap young
	 * ptes, if that happens we givup trying unmapping this page
	 * and we clear all other reference bits instead (basically
	 * downgrading to a page_referenced pass).
	 */
	if ((!pte_young(*pte) || !ptep_test_and_clear_young(pte)) && !*young)
		unmap_pte_page(page, vma, address, pte);
	else
		*young = 1;

	pte_unmap(pte);
 out:
	spin_unlock(&mm->page_table_lock);
	return ret;
}

/**
 * try_to_unmap_inode - unmap a page using the object-based rmap method
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
try_to_unmap_inode(struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct vm_area_struct *vma;
	int ret = SWAP_AGAIN, young = 0;

	BUG_ON(PageSwapCache(page));

	if (unlikely(down_trylock(&mapping->i_shared_sem)))
		return ret;
	
	list_for_each_entry(vma, &mapping->i_mmap, shared) {
		ret = try_to_unmap_one(vma, page, &young);
		if (ret == SWAP_FAIL || !page->mapcount)
			goto out;
	}

	list_for_each_entry(vma, &mapping->i_mmap_shared, shared) {
		ret = try_to_unmap_one(vma, page, &young);
		if (ret == SWAP_FAIL || !page->mapcount)
			goto out;
	}

out:
	up(&mapping->i_shared_sem);
	return ret;
}

static int
try_to_unmap_anon(struct page * page)
{
	int ret = SWAP_AGAIN, young = 0;
	struct vm_area_struct * vma;
	anon_vma_t * anon_vma = (anon_vma_t *) page->mapping;

	if (!PageSwapCache(page))
		return SWAP_AGAIN;

	spin_lock(&anon_vma->anon_vma_lock);
	BUG_ON(list_empty(&anon_vma->anon_vma_head));
	list_for_each_entry(vma, &anon_vma->anon_vma_head, anon_vma_node) {
		ret = try_to_unmap_one(vma, page, &young);
		if (ret == SWAP_FAIL || !page->mapcount)
			break;
	}
	spin_unlock(&anon_vma->anon_vma_lock);

	return ret;
}

/**
 * try_to_unmap - try to remove all page table mappings to a page
 * @page: the page to get unmapped
 *
 * Tries to remove all the page table entries which are mapping this
 * page, used in the pageout path.
 *
 * Caller must hold the page_map_lock.
 *
 * Return values are:
 *
 * SWAP_SUCCESS	- we succeeded in removing all mappings
 * SWAP_AGAIN	- we missed a trylock, try again later
 * SWAP_FAIL	- the page is unswappable
 */
int fastcall try_to_unmap(struct page * page)
{
	int ret = SWAP_SUCCESS;

	/* This page should not be on the pageout lists. */
	BUG_ON(PageReserved(page));
	BUG_ON(!PageLocked(page));

	/*
	 * We need an object to reach the ptes.
	 * Subtle: this checks for page->as.anon_vma too ;).
	 */
	BUG_ON(!page->mapping);

	if (!PageAnon(page))
		ret = try_to_unmap_inode(page);
	else
		ret = try_to_unmap_anon(page);

	if (!page_mapped(page)) {
		dec_page_state(nr_mapped);
		ret = SWAP_SUCCESS;
		if (page_test_and_clear_dirty(page))
			set_page_dirty(page);
	}

	return ret;
}

/*
 * No more VM stuff below this comment, only anon_vma helper
 * functions.
 */

/* This must be called under the mmap_sem. */
int fastcall anon_vma_prepare(struct vm_area_struct * vma)
{
	anon_vma_t * anon_vma = vma->anon_vma;

	might_sleep();
	if (!anon_vma) {
		anon_vma = anon_vma_alloc();
		if (!anon_vma)
			return -ENOMEM;
		vma->anon_vma = anon_vma;
		/* mmap_sem to protect against threads is enough */
		list_add(&vma->anon_vma_node, &anon_vma->anon_vma_head);
	}
	return 0;
}

void fastcall anon_vma_merge(struct vm_area_struct * vma,
			     struct vm_area_struct * vma_dying)
{
	anon_vma_t * anon_vma;

	anon_vma = vma_dying->anon_vma;
	if (!anon_vma)
		return;

	if (!vma->anon_vma) {
		/* this is serialized by the mmap_sem */
		vma->anon_vma = anon_vma;

		spin_lock(&anon_vma->anon_vma_lock);
		list_add(&vma->anon_vma_node, &vma_dying->anon_vma_node);
		list_del(&vma_dying->anon_vma_node);
		spin_unlock(&anon_vma->anon_vma_lock);
	} else {
		/* if they're both non-null they must be the same */
		BUG_ON(vma->anon_vma != anon_vma);

		spin_lock(&anon_vma->anon_vma_lock);
		list_del(&vma_dying->anon_vma_node);
		spin_unlock(&anon_vma->anon_vma_lock);
	}
}

void fastcall __anon_vma_link(struct vm_area_struct * vma)
{
	anon_vma_t * anon_vma = vma->anon_vma;

	if (anon_vma) {
		list_add(&vma->anon_vma_node, &anon_vma->anon_vma_head);
		validate_anon_vma_find_vma(vma);
	}
}

void fastcall anon_vma_link(struct vm_area_struct * vma)
{
	anon_vma_t * anon_vma = vma->anon_vma;

	if (anon_vma) {
		spin_lock(&anon_vma->anon_vma_lock);
		list_add(&vma->anon_vma_node, &anon_vma->anon_vma_head);
		validate_anon_vma_find_vma(vma);
		spin_unlock(&anon_vma->anon_vma_lock);
	}
}

void fastcall anon_vma_unlink(struct vm_area_struct * vma)
{
	anon_vma_t * anon_vma;
	int empty = 0;

	anon_vma = vma->anon_vma;
	if (!anon_vma)
		return;

	spin_lock(&anon_vma->anon_vma_lock);
	validate_anon_vma_find_vma(vma);
	list_del(&vma->anon_vma_node);
	/* We must garbage collect the anon_vma if it's empty */
	if (list_empty(&anon_vma->anon_vma_head))
		empty = 1;
	spin_unlock(&anon_vma->anon_vma_lock);

	if (empty)
		anon_vma_free(anon_vma);
}

static void
anon_vma_ctor(void *data, kmem_cache_t *cachep, unsigned long flags)
{
	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		anon_vma_t * anon_vma = (anon_vma_t *) data;

		spin_lock_init(&anon_vma->anon_vma_lock);
		INIT_LIST_HEAD(&anon_vma->anon_vma_head);
	}
}

void __init anon_vma_init(void)
{
	/* this is intentonally not hw aligned to avoid wasting ram */
	anon_vma_cachep = kmem_cache_create("anon_vma",
					    sizeof(anon_vma_t), 0, 0,
					    anon_vma_ctor, NULL);

	if(!anon_vma_cachep)
		panic("Cannot create anon_vma SLAB cache");
}
