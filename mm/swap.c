/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file contains the default values for the opereation of the
 * Linux VM subsystem. Fine-tuning documentation can be found in
 * linux/Documentation/sysctl/vm.txt.
 * Started 18.12.91
 * Swap aging added 23.2.95, Stephen Tweedie.
 * Buffermem limits added 12.3.98, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/init.h>
#include <linux/prefetch.h>

/* How many pages do we try to swap or page in/out together? */
int page_cluster;

/*
 * Move an inactive page to the active list.
 */
static inline void activate_page_nolock(struct page * page)
{
	if (PageLRU(page) && !PageActive(page)) {
		del_page_from_inactive_list(page);
		add_page_to_active_list(page);
		KERNEL_STAT_INC(pgactivate);
	}
}

/*
 * FIXME: speed this up?
 */
void activate_page(struct page * page)
{
	spin_lock_irq(&_pagemap_lru_lock);
	activate_page_nolock(page);
	spin_unlock_irq(&_pagemap_lru_lock);
}

/**
 * lru_cache_add: add a page to the page lists
 * @page: the page to add
 */
void lru_cache_add(struct page * page)
{
	if (!PageLRU(page)) {
		spin_lock_irq(&_pagemap_lru_lock);
		if (!TestSetPageLRU(page))
			add_page_to_inactive_list(page);
		spin_unlock_irq(&_pagemap_lru_lock);
	}
}

/**
 * __lru_cache_del: remove a page from the page lists
 * @page: the page to add
 *
 * This function is for when the caller already holds
 * the pagemap_lru_lock.
 */
void __lru_cache_del(struct page * page)
{
	if (TestClearPageLRU(page)) {
		if (PageActive(page))
			del_page_from_active_list(page);
		else
			del_page_from_inactive_list(page);
	}
}

/**
 * lru_cache_del: remove a page from the page lists
 * @page: the page to remove
 */
void lru_cache_del(struct page * page)
{
	spin_lock_irq(&_pagemap_lru_lock);
	__lru_cache_del(page);
	spin_unlock_irq(&_pagemap_lru_lock);
}

/*
 * Batched page_cache_release().  Decrement the reference count on all the
 * pagevec's pages.  If it fell to zero then remove the page from the LRU and
 * free it.
 *
 * Avoid taking pagemap_lru_lock if possible, but if it is taken, retain it
 * for the remainder of the operation.
 *
 * The locking in this function is against shrink_cache(): we recheck the
 * page count inside the lock to see whether shrink_cache grabbed the page
 * via the LRU.  If it did, give up: shrink_cache will free it.
 *
 * This function reinitialises the caller's pagevec.
 */
void __pagevec_release(struct pagevec *pvec)
{
	int i;
	int lock_held = 0;
	struct pagevec pages_to_free;

	pagevec_init(&pages_to_free);
	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		if (!put_page_testzero(page))
			continue;

		if (!lock_held) {
			spin_lock_irq(&_pagemap_lru_lock);
			lock_held = 1;
		}

		if (TestClearPageLRU(page)) {
			if (PageActive(page))
				del_page_from_active_list(page);
			else
				del_page_from_inactive_list(page);
		}
		if (page_count(page) == 0)
			pagevec_add(&pages_to_free, page);
	}
	if (lock_held)
		spin_unlock_irq(&_pagemap_lru_lock);

	pagevec_free(&pages_to_free);
	pagevec_init(pvec);
}

/*
 * pagevec_release() for pages which are known to not be on the LRU
 *
 * This function reinitialises the caller's pagevec.
 */
void __pagevec_release_nonlru(struct pagevec *pvec)
{
	int i;
	struct pagevec pages_to_free;

	pagevec_init(&pages_to_free);
	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		BUG_ON(PageLRU(page));
		if (put_page_testzero(page))
			pagevec_add(&pages_to_free, page);
	}
	pagevec_free(&pages_to_free);
	pagevec_init(pvec);
}

/*
 * Move all the inactive pages to the head of the inactive list
 * and release them.  Reinitialises the caller's pagevec.
 */
void pagevec_deactivate_inactive(struct pagevec *pvec)
{
	int i;
	int lock_held = 0;

	if (pagevec_count(pvec) == 0)
		return;
	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		if (!lock_held) {
			if (PageActive(page) || !PageLRU(page))
				continue;
			spin_lock_irq(&_pagemap_lru_lock);
			lock_held = 1;
		}
		if (!PageActive(page) && PageLRU(page))
			list_move(&page->lru, &inactive_list);
	}
	if (lock_held)
		spin_unlock_irq(&_pagemap_lru_lock);
	__pagevec_release(pvec);
}

/*
 * Add the passed pages to the inactive_list, then drop the caller's refcount
 * on them.  Reinitialises the caller's pagevec.
 */
void __pagevec_lru_add(struct pagevec *pvec)
{
	int i;

	spin_lock_irq(&_pagemap_lru_lock);
	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		if (TestSetPageLRU(page))
			BUG();
		add_page_to_inactive_list(page);
	}
	spin_unlock_irq(&_pagemap_lru_lock);
	pagevec_release(pvec);
}

/*
 * Remove the passed pages from the LRU, then drop the caller's refcount on
 * them.  Reinitialises the caller's pagevec.
 */
void __pagevec_lru_del(struct pagevec *pvec)
{
	int i;

	spin_lock_irq(&_pagemap_lru_lock);
	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		if (!TestClearPageLRU(page))
			BUG();
		if (PageActive(page))
			del_page_from_active_list(page);
		else
			del_page_from_inactive_list(page);
	}
	spin_unlock_irq(&_pagemap_lru_lock);
	pagevec_release(pvec);
}

/*
 * Perform any setup for the swap system
 */
void __init swap_setup(void)
{
	unsigned long megs = num_physpages >> (20 - PAGE_SHIFT);

	/* Use a smaller cluster for small-memory machines */
	if (megs < 16)
		page_cluster = 2;
	else
		page_cluster = 3;
	/*
	 * Right now other parts of the system means that we
	 * _really_ don't want to cluster much more
	 */
}
