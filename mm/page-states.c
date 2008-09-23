/*
 * mm/page-states.c
 *
 * (C) Copyright IBM Corp. 2005, 2007
 *
 * Guest page hinting functions.
 *
 * Authors: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *          Hubertus Franke <frankeh@watson.ibm.com>
 *          Himanshu Raj <rhim@cc.gatech.edu>
 */

#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/buffer_head.h>
#include <linux/pagevec.h>
#include <linux/page-states.h>
#include <linux/swap.h>

#include "internal.h"

/*
 * Check if there is anything in the page flags or the mapping
 * that prevents the page from changing its state to volatile.
 */
static inline int check_bits(struct page *page)
{
	struct address_space *mapping;

	/*
	 * There are several conditions that prevent a page from becoming
	 * volatile. The first check is for the page bits.
	 */
	if (PageDirty(page) || PageReserved(page) || PageWriteback(page) ||
	    PageLocked(page) || PagePrivate(page) || PageDiscarded(page) ||
	    !PageUptodate(page) || !PageLRU(page) ||
	    (PageAnon(page) && !PageSwapCache(page)))
		return 0;

	/*
	 * Special case shared memory: page is PageSwapCache but not
	 * PageAnon. page_unmap_all failes for swapped shared memory
	 * pages.
	 */
	if (PageSwapCache(page) && !PageAnon(page))
		return 0;

	/*
	 * If the page has been truncated there is no point in making
	 * it volatile. It will be freed soon. And if the mapping ever
	 * had locked pages all pages of the mapping will stay stable.
	 */
	mapping = page_mapping(page);
	return mapping && !mapping->mlocked;
}

/*
 * Check the reference counter of the page against the number of
 * mappings. The caller passes an offset, that is the number of
 * extra, known references. The page cache itself is one extra
 * reference. If the caller acquired an additional reference then
 * the offset would be 2. If the page map counter is equal to the
 * page count minus the offset then there is no other, unknown
 * user of the page in the system.
 */
static inline int check_counts(struct page *page, unsigned int offset)
{
	return page_mapcount(page) + offset == page_count(page);
}

/*
 * Attempts to change the state of a page to volatile.
 * If there is something preventing the state change the page stays
 * in its current state.
 */
void __page_make_volatile(struct page *page, int offset)
{
	preempt_disable();
	if (!page_test_set_state_change(page)) {
		if (check_bits(page) && check_counts(page, offset))
			page_set_volatile(page, PageWritable(page));
		page_clear_state_change(page);
	}
	preempt_enable();
}
EXPORT_SYMBOL(__page_make_volatile);

/*
 * Attempts to change the state of a vector of pages to volatile.
 * If there is something preventing the state change the page stays
 * int its current state.
 */
void __pagevec_make_volatile(struct pagevec *pvec)
{
	struct page *page;
	int i = pagevec_count(pvec);

	while (--i >= 0) {
		/*
		 * If we can't get the state change bit just give up.
		 * The worst that can happen is that the page will stay
		 * in the stable state although it might be volatile.
		 */
		page = pvec->pages[i];
		if (!page_test_set_state_change(page)) {
			if (check_bits(page) && check_counts(page, 1))
				page_set_volatile(page, PageWritable(page));
			page_clear_state_change(page);
		}
	}
}
EXPORT_SYMBOL(__pagevec_make_volatile);

/*
 * Attempts to change the state of a page to stable. The host could
 * have removed a volatile page, the page_set_stable_if_present call
 * can fail.
 *
 * returns "0" on success and "1" on failure
 */
int __page_make_stable(struct page *page)
{
	/*
	 * Postpone state change to stable until the state change bit is
	 * cleared. As long as the state change bit is set another cpu
	 * is in page_make_volatile for this page. That makes sure that
	 * no caller of make_stable "overtakes" a make_volatile leaving
	 * the page in volatile where stable is required.
	 * The caller of make_stable need to make sure that no caller
	 * of make_volatile can make the page volatile right after
	 * make_stable has finished.
	 */
	while (page_state_change(page))
		cpu_relax();
	return page_set_stable_if_present(page);
}
EXPORT_SYMBOL(__page_make_stable);

/**
 * __page_check_writable() - check page state for new writable pte
 *
 * @page: the page the new writable pte refers to
 * @pte: the new writable pte
 */
void __page_check_writable(struct page *page, pte_t pte, unsigned int offset)
{
	int count_ok = 0;

	preempt_disable();
	while (page_test_set_state_change(page))
		cpu_relax();

	if (!TestSetPageWritable(page)) {
		count_ok = check_counts(page, offset);
		if (check_bits(page) && count_ok)
			page_set_volatile(page, 1);
		else
			/*
			 * If two processes create a write mapping at the
			 * same time check_counts will return false or if
			 * the page is currently isolated from the LRU
			 * check_bits will return false but the page might
			 * be in volatile state.
			 * We have to take care about the dirty bit so the
			 * only option left is to make the page stable but
			 * we can try to make it volatile a bit later.
			 */
			page_set_stable_if_present(page);
	}
	page_clear_state_change(page);
	if (!count_ok)
		page_make_volatile(page, 1);
	preempt_enable();
}
EXPORT_SYMBOL(__page_check_writable);

/**
 * __page_reset_writable() - clear the PageWritable bit
 *
 * @page: the page
 */
void __page_reset_writable(struct page *page)
{
	preempt_disable();
	if (!page_test_set_state_change(page)) {
		ClearPageWritable(page);
		page_clear_state_change(page);
	}
	preempt_enable();
}
EXPORT_SYMBOL(__page_reset_writable);

/**
 * __page_discard() - remove a discarded page from the cache
 *
 * @page: the page
 *
 * The page passed to this function needs to be locked.
 */
static void __page_discard(struct page *page)
{
	struct address_space *mapping;
	struct zone *zone;

	/* Paranoia checks. */
	VM_BUG_ON(PageWriteback(page));
	VM_BUG_ON(PageDirty(page));
	VM_BUG_ON(PagePrivate(page));

	/* Set the discarded bit early. */
	if (TestSetPageDiscarded(page))
		return;

	/* Unmap the page from all page tables. */
	page_unmap_all(page);

	/* Check if really all mappers of this page are gone. */
	VM_BUG_ON(page_mapcount(page) != 0);

	/*
	 * Remove the page from LRU if it is currently added.
	 * The users of isolate_lru_pages need to check the
	 * discarded bit before readding the page to the LRU.
	 */
	zone = page_zone(page);
	spin_lock_irq(&zone->lru_lock);
	if (PageLRU(page)) {
		/* Unlink page from lru. */
		__ClearPageLRU(page);
		del_page_from_lru(zone, page);
	}
	spin_unlock_irq(&zone->lru_lock);

	/* Remove page from page cache/swap cache. */
 	mapping = page->mapping;
	if (PageSwapCache(page)) {
		swp_entry_t entry = { .val = page_private(page) };
		spin_lock_irq(&swapper_space.tree_lock);
		__delete_from_swap_cache_nocheck(page);
		spin_unlock_irq(&swapper_space.tree_lock);
		swap_free(entry);
		page_cache_release(page);
	} else {
		spin_lock_irq(&mapping->tree_lock);
		__remove_from_page_cache_nocheck(page);
		spin_unlock_irq(&mapping->tree_lock);
 		__put_page(page);
	}
}

/**
 * page_discard() - remove a discarded page from the cache
 *
 * @page: the page
 *
 * Before calling this function an additional page reference needs to
 * be acquired. This reference is released by the function.
 */
void page_discard(struct page *page)
{
	lock_page(page);
	__page_discard(page);
	unlock_page(page);
	page_cache_release(page);
}
EXPORT_SYMBOL(page_discard);
