/*
 *  linux/mm/swap_state.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *
 *  Rewritten to use page cache, (C) 1998 Stephen Tweedie
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>

#include <asm/pgtable.h>

static struct backing_dev_info swap_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed	= 1,	/* Does not contribute to dirty memory */
};

extern struct address_space_operations swap_aops;

struct address_space swapper_space = {
	.page_tree	= RADIX_TREE_INIT(GFP_ATOMIC),
	.tree_lock	= SPIN_LOCK_UNLOCKED,
	.clean_pages	= LIST_HEAD_INIT(swapper_space.clean_pages),
	.io_pages	= LIST_HEAD_INIT(swapper_space.io_pages),
	.locked_pages	= LIST_HEAD_INIT(swapper_space.locked_pages),
	.a_ops		= &swap_aops,
	.backing_dev_info = &swap_backing_dev_info,
	.i_mmap		= LIST_HEAD_INIT(swapper_space.i_mmap),
	.i_mmap_shared	= LIST_HEAD_INIT(swapper_space.i_mmap_shared),
	.i_shared_sem	= __MUTEX_INITIALIZER(swapper_space.i_shared_sem),
	.truncate_count  = ATOMIC_INIT(0),
	.private_lock	= SPIN_LOCK_UNLOCKED,
	.private_list	= LIST_HEAD_INIT(swapper_space.private_list),
};

#define INC_CACHE_INFO(x)	do { swap_cache_info.x++; } while (0)

static struct {
	unsigned long add_total;
	unsigned long del_total;
	unsigned long find_success;
	unsigned long find_total;
	unsigned long noent_race;
	unsigned long exist_race;
} swap_cache_info;

void show_swap_cache_info(void)
{
	printk("Swap cache: add %lu, delete %lu, find %lu/%lu, race %lu+%lu\n",
		swap_cache_info.add_total, swap_cache_info.del_total,
		swap_cache_info.find_success, swap_cache_info.find_total,
		swap_cache_info.noent_race, swap_cache_info.exist_race);
}

static int add_to_swap_cache(struct page *page, swp_entry_t entry)
{
	int error;

	if (page->mapping)
		BUG();
	if (!swap_duplicate(entry)) {
		INC_CACHE_INFO(noent_race);
		return -ENOENT;
	}
	error = add_to_page_cache(page, &swapper_space, entry.val, GFP_KERNEL);
	/*
	 * Anon pages are already on the LRU, we don't run lru_cache_add here.
	 */
	if (error != 0) {
		swap_free(entry);
		if (error == -EEXIST)
			INC_CACHE_INFO(exist_race);
		return error;
	}
	if (!PageLocked(page))
		BUG();
	if (!PageSwapCache(page))
		BUG();
	INC_CACHE_INFO(add_total);
	return 0;
}

/*
 * This must be called only on pages that have
 * been verified to be in the swap cache.
 */
void __delete_from_swap_cache(struct page *page)
{
	BUG_ON(!PageLocked(page));
	BUG_ON(!PageSwapCache(page));
	BUG_ON(PageWriteback(page));
	__remove_from_page_cache(page);
	INC_CACHE_INFO(del_total);
}

/**
 * add_to_swap - allocate swap space for a page
 * @page: page we want to move to swap
 *
 * Allocate swap space for the page and add the page to the
 * swap cache.  Caller needs to hold the page lock. 
 */
int add_to_swap(struct page * page)
{
	swp_entry_t entry;
	int pf_flags;
	int err;

	if (!PageLocked(page))
		BUG();

	for (;;) {
		entry = get_swap_page();
		if (!entry.val)
			return 0;

		/* Radix-tree node allocations are performing
		 * GFP_ATOMIC allocations under PF_MEMALLOC.  
		 * They can completely exhaust the page allocator.  
		 *
		 * So PF_MEMALLOC is dropped here.  This causes the slab 
		 * allocations to fail earlier, so radix-tree nodes will 
		 * then be allocated from the mempool reserves.
		 *
		 * We're still using __GFP_HIGH for radix-tree node
		 * allocations, so some of the emergency pools are available,
		 * just not all of them.
		 */

		pf_flags = current->flags;
		current->flags &= ~PF_MEMALLOC;

		/*
		 * Add it to the swap cache and mark it dirty
		 */
		err = add_to_page_cache(page, &swapper_space,
					entry.val, GFP_ATOMIC);

		if (pf_flags & PF_MEMALLOC)
			current->flags |= PF_MEMALLOC;

		switch (err) {
		case 0:				/* Success */
			SetPageUptodate(page);
			__clear_page_dirty(page);
			set_page_dirty(page);
			INC_CACHE_INFO(add_total);
			return 1;
		case -EEXIST:
			/* Raced with "speculative" read_swap_cache_async */
			INC_CACHE_INFO(exist_race);
			swap_free(entry);
			continue;
		default:
			/* -ENOMEM radix-tree allocation failure */
			swap_free(entry);
			return 0;
		}
	}
}

/*
 * This must be called only on pages that have
 * been verified to be in the swap cache and locked.
 * It will never put the page into the free list,
 * the caller has a reference on the page.
 */
void delete_from_swap_cache(struct page *page)
{
	swp_entry_t entry;

	BUG_ON(!PageLocked(page));
	BUG_ON(PageWriteback(page));
	BUG_ON(PagePrivate(page));
  
	entry.val = page->index;

	spin_lock_irq(&swapper_space.tree_lock);
	__delete_from_swap_cache(page);
	spin_unlock_irq(&swapper_space.tree_lock);

	swap_free(entry);
	page_cache_release(page);
}

int move_to_swap_cache(struct page *page, swp_entry_t entry)
{
	struct address_space *mapping = page->mapping;
	int err;

	spin_lock_irq(&swapper_space.tree_lock);
	spin_lock(&mapping->tree_lock);

	err = radix_tree_insert(&swapper_space.page_tree, entry.val, page);
	if (!err) {
		__remove_from_page_cache(page);
		___add_to_page_cache(page, &swapper_space, entry.val);
	}

	spin_unlock(&mapping->tree_lock);
	spin_unlock_irq(&swapper_space.tree_lock);

	if (!err) {
		if (!swap_duplicate(entry))
			BUG();
		BUG_ON(PageDirty(page));
		set_page_dirty(page);
		INC_CACHE_INFO(add_total);
	} else if (err == -EEXIST)
		INC_CACHE_INFO(exist_race);
	return err;
}

int move_from_swap_cache(struct page *page, unsigned long index,
		struct address_space *mapping)
{
	swp_entry_t entry;
	int err;

	BUG_ON(!PageLocked(page));
	BUG_ON(PageWriteback(page));
	BUG_ON(PagePrivate(page));

	entry.val = page->index;

	spin_lock_irq(&swapper_space.tree_lock);
	spin_lock(&mapping->tree_lock);

	err = radix_tree_insert(&mapping->page_tree, index, page);
	if (!err) {
		__delete_from_swap_cache(page);
		___add_to_page_cache(page, mapping, index);
	}

	spin_unlock(&mapping->tree_lock);
	spin_unlock_irq(&swapper_space.tree_lock);

	if (!err) {
		swap_free(entry);
		__clear_page_dirty(page);
		set_page_dirty(page);
	}
	return err;
}


/* 
 * If we are the only user, then try to free up the swap cache. 
 * 
 * Its ok to check for PageSwapCache without the page lock
 * here because we are going to recheck again inside 
 * exclusive_swap_page() _with_ the lock. 
 * 					- Marcelo
 */
static inline void free_swap_cache(struct page *page)
{
	if (PageSwapCache(page) && !TestSetPageLocked(page)) {
		remove_exclusive_swap_page(page);
		unlock_page(page);
	}
}

/* 
 * Perform a free_page(), also freeing any swap cache associated with
 * this page if it is the last user of the page. Can not do a lock_page,
 * as we are holding the page_table_lock spinlock.
 */
void free_page_and_swap_cache(struct page *page)
{
	free_swap_cache(page);
	page_cache_release(page);
}

/*
 * Passed an array of pages, drop them all from swapcache and then release
 * them.  They are removed from the LRU and freed if this is their last use.
 */
void free_pages_and_swap_cache(struct page **pages, int nr)
{
	int chunk = 16;
	struct page **pagep = pages;

	lru_add_drain();
	while (nr) {
		int todo = min(chunk, nr);
		int i;

		for (i = 0; i < todo; i++)
			free_swap_cache(pagep[i]);
		release_pages(pagep, todo, 0);
		pagep += todo;
		nr -= todo;
	}
}

/*
 * Lookup a swap entry in the swap cache. A found page will be returned
 * unlocked and with its refcount incremented - we rely on the kernel
 * lock getting page table operations atomic even if we drop the page
 * lock before returning.
 */
struct page * lookup_swap_cache(swp_entry_t entry)
{
	struct page *found;

	found = find_get_page(&swapper_space, entry.val);
	/*
	 * Unsafe to assert PageSwapCache and mapping on page found:
	 * if SMP nothing prevents swapoff from deleting this page from
	 * the swap cache at this moment.  find_lock_page would prevent
	 * that, but no need to change: we _have_ got the right page.
	 */
	INC_CACHE_INFO(find_total);
	if (found)
		INC_CACHE_INFO(find_success);
	return found;
}

/* 
 * Locate a page of swap in physical memory, reserving swap cache space
 * and reading the disk if it is not already cached.
 * A failure return means that either the page allocation failed or that
 * the swap entry is no longer in use.
 */
struct page * read_swap_cache_async(swp_entry_t entry)
{
	struct page *found_page, *new_page = NULL;
	int err;

	do {
		/*
		 * First check the swap cache.  Since this is normally
		 * called after lookup_swap_cache() failed, re-calling
		 * that would confuse statistics: use find_get_page()
		 * directly.
		 */
		found_page = find_get_page(&swapper_space, entry.val);
		if (found_page)
			break;

		/*
		 * Get a new page to read into from swap.
		 */
		if (!new_page) {
			new_page = alloc_page(GFP_HIGHUSER);
			if (!new_page)
				break;		/* Out of memory */
		}

		/*
		 * Associate the page with swap entry in the swap cache.
		 * May fail (-ENOENT) if swap entry has been freed since
		 * our caller observed it.  May fail (-EEXIST) if there
		 * is already a page associated with this entry in the
		 * swap cache: added by a racing read_swap_cache_async,
		 * or by try_to_swap_out (or shmem_writepage) re-using
		 * the just freed swap entry for an existing page.
		 * May fail (-ENOMEM) if radix-tree node allocation failed.
		 */
		err = add_to_swap_cache(new_page, entry);
		if (!err) {
			/*
			 * Initiate read into locked page and return.
			 */
			lru_cache_add_active(new_page);
			swap_readpage(NULL, new_page);
			return new_page;
		}
	} while (err != -ENOENT && err != -ENOMEM);

	if (new_page)
		page_cache_release(new_page);
	return found_page;
}
