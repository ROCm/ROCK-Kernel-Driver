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
#include <linux/swapctl.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

#include <asm/pgtable.h>

static int swap_writepage(struct page *page)
{
	rw_swap_page(WRITE, page, 0);
	return 0;
}

static struct address_space_operations swap_aops = {
	writepage: swap_writepage,
	sync_page: block_sync_page,
};

struct address_space swapper_space = {
	LIST_HEAD_INIT(swapper_space.clean_pages),
	LIST_HEAD_INIT(swapper_space.dirty_pages),
	LIST_HEAD_INIT(swapper_space.locked_pages),
	0,				/* nrpages	*/
	&swap_aops,
};

#ifdef SWAP_CACHE_INFO
unsigned long swap_cache_add_total;
unsigned long swap_cache_del_total;
unsigned long swap_cache_find_total;
unsigned long swap_cache_find_success;

void show_swap_cache_info(void)
{
	printk("Swap cache: add %ld, delete %ld, find %ld/%ld\n",
		swap_cache_add_total, 
		swap_cache_del_total,
		swap_cache_find_success, swap_cache_find_total);
}
#endif

void add_to_swap_cache(struct page *page, swp_entry_t entry)
{
	unsigned long flags;

#ifdef SWAP_CACHE_INFO
	swap_cache_add_total++;
#endif
	if (!PageLocked(page))
		BUG();
	if (PageTestandSetSwapCache(page))
		BUG();
	if (page->mapping)
		BUG();
	flags = page->flags & ~((1 << PG_error) | (1 << PG_arch_1));
	page->flags = flags | (1 << PG_uptodate);
	add_to_page_cache_locked(page, &swapper_space, entry.val);
}

static inline void remove_from_swap_cache(struct page *page)
{
	struct address_space *mapping = page->mapping;

	if (mapping != &swapper_space)
		BUG();
	if (!PageSwapCache(page) || !PageLocked(page))
		PAGE_BUG(page);

	PageClearSwapCache(page);
	ClearPageDirty(page);
	__remove_inode_page(page);
}

/*
 * This must be called only on pages that have
 * been verified to be in the swap cache.
 */
void __delete_from_swap_cache(struct page *page)
{
	swp_entry_t entry;

	entry.val = page->index;

#ifdef SWAP_CACHE_INFO
	swap_cache_del_total++;
#endif
	remove_from_swap_cache(page);
	swap_free(entry);
}

/*
 * This will never put the page into the free list, the caller has
 * a reference on the page.
 */
void delete_from_swap_cache_nolock(struct page *page)
{
	if (!PageLocked(page))
		BUG();

	if (block_flushpage(page, 0))
		lru_cache_del(page);

	spin_lock(&pagecache_lock);
	ClearPageDirty(page);
	__delete_from_swap_cache(page);
	spin_unlock(&pagecache_lock);
	page_cache_release(page);
}

/*
 * This must be called only on pages that have
 * been verified to be in the swap cache and locked.
 */
void delete_from_swap_cache(struct page *page)
{
	lock_page(page);
	delete_from_swap_cache_nolock(page);
	UnlockPage(page);
}

/* 
 * Perform a free_page(), also freeing any swap cache associated with
 * this page if it is the last user of the page. Can not do a lock_page,
 * as we are holding the page_table_lock spinlock.
 */
void free_page_and_swap_cache(struct page *page)
{
	/* 
	 * If we are the only user, then try to free up the swap cache. 
	 */
	if (PageSwapCache(page) && !TryLockPage(page)) {
		if (!is_page_shared(page)) {
			delete_from_swap_cache_nolock(page);
		}
		UnlockPage(page);
	}
	page_cache_release(page);
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

#ifdef SWAP_CACHE_INFO
	swap_cache_find_total++;
#endif
	while (1) {
		/*
		 * Right now the pagecache is 32-bit only.  But it's a 32 bit index. =)
		 */
repeat:
		found = find_lock_page(&swapper_space, entry.val);
		if (!found)
			return 0;
		/*
		 * Though the "found" page was in the swap cache an instant
		 * earlier, it might have been removed by refill_inactive etc.
		 * Re search ... Since find_lock_page grabs a reference on
		 * the page, it can not be reused for anything else, namely
		 * it can not be associated with another swaphandle, so it
		 * is enough to check whether the page is still in the scache.
		 */
		if (!PageSwapCache(found)) {
			UnlockPage(found);
			page_cache_release(found);
			goto repeat;
		}
		if (found->mapping != &swapper_space)
			goto out_bad;
#ifdef SWAP_CACHE_INFO
		swap_cache_find_success++;
#endif
		UnlockPage(found);
		return found;
	}

out_bad:
	printk (KERN_ERR "VM: Found a non-swapper swap page!\n");
	UnlockPage(found);
	page_cache_release(found);
	return 0;
}

/* 
 * Locate a page of swap in physical memory, reserving swap cache space
 * and reading the disk if it is not already cached.  If wait==0, we are
 * only doing readahead, so don't worry if the page is already locked.
 *
 * A failure return means that either the page allocation failed or that
 * the swap entry is no longer in use.
 */

struct page * read_swap_cache_async(swp_entry_t entry, int wait)
{
	struct page *found_page = 0, *new_page;
	unsigned long new_page_addr;
	
	/*
	 * Make sure the swap entry is still in use.
	 */
	if (!swap_duplicate(entry))	/* Account for the swap cache */
		goto out;
	/*
	 * Look for the page in the swap cache.
	 */
	found_page = lookup_swap_cache(entry);
	if (found_page)
		goto out_free_swap;

	new_page_addr = __get_free_page(GFP_USER);
	if (!new_page_addr)
		goto out_free_swap;	/* Out of memory */
	new_page = virt_to_page(new_page_addr);

	/*
	 * Check the swap cache again, in case we stalled above.
	 */
	found_page = lookup_swap_cache(entry);
	if (found_page)
		goto out_free_page;
	/* 
	 * Add it to the swap cache and read its contents.
	 */
	lock_page(new_page);
	add_to_swap_cache(new_page, entry);
	rw_swap_page(READ, new_page, wait);
	return new_page;

out_free_page:
	page_cache_release(new_page);
out_free_swap:
	swap_free(entry);
out:
	return found_page;
}
