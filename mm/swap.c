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
#include <linux/mm_inline.h>
#include <linux/buffer_head.h>
#include <linux/prefetch.h>
#include <linux/percpu.h>

/* How many pages do we try to swap or page in/out together? */
int page_cluster;

/*
 * Writeback is about to end against a page whic has been marked for immediate
 * reclaim.  If it still appears to be reclaimable, move it to the tail of the
 * inactive list.  The page still has PageWriteback set, which will pin it.
 *
 * We don't expect many pages to come through here, so don't bother batching
 * things up.
 *
 * To avoid placing the page at the tail of the LRU while PG_writeback is still
 * set, this function will clear PG_writeback before performing the page
 * motion.  Do that inside the lru lock because once PG_writeback is cleared
 * we may not touch the page.
 *
 * Returns zero if it cleared PG_writeback.
 */
int rotate_reclaimable_page(struct page *page)
{
	struct zone *zone;
	unsigned long flags;

	if (PageLocked(page))
		return 1;
	if (PageDirty(page))
		return 1;
	if (PageActive(page))
		return 1;
	if (!PageLRU(page))
		return 1;

	zone = page_zone(page);
	spin_lock_irqsave(&zone->lru_lock, flags);
	if (PageLRU(page) && !PageActive(page)) {
		list_del(&page->lru);
		list_add_tail(&page->lru, &zone->inactive_list);
	}
	if (!TestClearPageWriteback(page))
		BUG();
	spin_unlock_irqrestore(&zone->lru_lock, flags);
	return 0;
}

/*
 * FIXME: speed this up?
 */
void activate_page(struct page *page)
{
	struct zone *zone = page_zone(page);

	spin_lock_irq(&zone->lru_lock);
	if (PageLRU(page) && !PageActive(page)) {
		del_page_from_inactive_list(zone, page);
		SetPageActive(page);
		add_page_to_active_list(zone, page);
		inc_page_state(pgactivate);
	}
	spin_unlock_irq(&zone->lru_lock);
}

/*
 * Mark a page as having seen activity.
 *
 * inactive,unreferenced	->	inactive,referenced
 * inactive,referenced		->	active,unreferenced
 * active,unreferenced		->	active,referenced
 */
void mark_page_accessed(struct page *page)
{
	if (!PageActive(page) && PageReferenced(page) && PageLRU(page)) {
		activate_page(page);
		ClearPageReferenced(page);
	} else if (!PageReferenced(page)) {
		SetPageReferenced(page);
	}
}

/**
 * lru_cache_add: add a page to the page lists
 * @page: the page to add
 */
static DEFINE_PER_CPU(struct pagevec, lru_add_pvecs) = { 0, };
static DEFINE_PER_CPU(struct pagevec, lru_add_active_pvecs) = { 0, };

void lru_cache_add(struct page *page)
{
	struct pagevec *pvec = &per_cpu(lru_add_pvecs, get_cpu());

	page_cache_get(page);
	if (!pagevec_add(pvec, page))
		__pagevec_lru_add(pvec);
	put_cpu();
}

void lru_cache_add_active(struct page *page)
{
	struct pagevec *pvec = &per_cpu(lru_add_active_pvecs, get_cpu());

	page_cache_get(page);
	if (!pagevec_add(pvec, page))
		__pagevec_lru_add_active(pvec);
	put_cpu();
}

void lru_add_drain(void)
{
	int cpu = get_cpu();
	struct pagevec *pvec = &per_cpu(lru_add_pvecs, cpu);

	if (pagevec_count(pvec))
		__pagevec_lru_add(pvec);
	pvec = &per_cpu(lru_add_active_pvecs, cpu);
	if (pagevec_count(pvec))
		__pagevec_lru_add_active(pvec);
	put_cpu();
}

/*
 * This path almost never happens for VM activity - pages are normally
 * freed via pagevecs.  But it gets used by networking.
 */
void __page_cache_release(struct page *page)
{
	unsigned long flags;
	struct zone *zone = page_zone(page);

	spin_lock_irqsave(&zone->lru_lock, flags);
	if (TestClearPageLRU(page))
		del_page_from_lru(zone, page);
	if (page_count(page) != 0)
		page = NULL;
	spin_unlock_irqrestore(&zone->lru_lock, flags);
	if (page)
		free_hot_page(page);
}

/*
 * Batched page_cache_release().  Decrement the reference count on all the
 * passed pages.  If it fell to zero then remove the page from the LRU and
 * free it.
 *
 * Avoid taking zone->lru_lock if possible, but if it is taken, retain it
 * for the remainder of the operation.
 *
 * The locking in this function is against shrink_cache(): we recheck the
 * page count inside the lock to see whether shrink_cache grabbed the page
 * via the LRU.  If it did, give up: shrink_cache will free it.
 */
void release_pages(struct page **pages, int nr, int cold)
{
	int i;
	struct pagevec pages_to_free;
	struct zone *zone = NULL;

	pagevec_init(&pages_to_free, cold);
	for (i = 0; i < nr; i++) {
		struct page *page = pages[i];
		struct zone *pagezone;

		if (PageReserved(page) || !put_page_testzero(page))
			continue;

		pagezone = page_zone(page);
		if (pagezone != zone) {
			if (zone)
				spin_unlock_irq(&zone->lru_lock);
			zone = pagezone;
			spin_lock_irq(&zone->lru_lock);
		}
		if (TestClearPageLRU(page))
			del_page_from_lru(zone, page);
		if (page_count(page) == 0) {
			if (!pagevec_add(&pages_to_free, page)) {
				spin_unlock_irq(&zone->lru_lock);
				__pagevec_free(&pages_to_free);
				pagevec_reinit(&pages_to_free);
				zone = NULL;	/* No lock is held */
			}
		}
	}
	if (zone)
		spin_unlock_irq(&zone->lru_lock);

	pagevec_free(&pages_to_free);
}

void __pagevec_release(struct pagevec *pvec)
{
	release_pages(pvec->pages, pagevec_count(pvec), pvec->cold);
	pagevec_reinit(pvec);
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

	pagevec_init(&pages_to_free, pvec->cold);
	pages_to_free.cold = pvec->cold;
	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		BUG_ON(PageLRU(page));
		if (put_page_testzero(page))
			pagevec_add(&pages_to_free, page);
	}
	pagevec_free(&pages_to_free);
	pagevec_reinit(pvec);
}

/*
 * Add the passed pages to the LRU, then drop the caller's refcount
 * on them.  Reinitialises the caller's pagevec.
 */
void __pagevec_lru_add(struct pagevec *pvec)
{
	int i;
	struct zone *zone = NULL;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		struct zone *pagezone = page_zone(page);

		if (pagezone != zone) {
			if (zone)
				spin_unlock_irq(&zone->lru_lock);
			zone = pagezone;
			spin_lock_irq(&zone->lru_lock);
		}
		if (TestSetPageLRU(page))
			BUG();
		add_page_to_inactive_list(zone, page);
	}
	if (zone)
		spin_unlock_irq(&zone->lru_lock);
	pagevec_release(pvec);
}

void __pagevec_lru_add_active(struct pagevec *pvec)
{
	int i;
	struct zone *zone = NULL;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		struct zone *pagezone = page_zone(page);

		if (pagezone != zone) {
			if (zone)
				spin_unlock_irq(&zone->lru_lock);
			zone = pagezone;
			spin_lock_irq(&zone->lru_lock);
		}
		if (TestSetPageLRU(page))
			BUG();
		if (TestSetPageActive(page))
			BUG();
		add_page_to_active_list(zone, page);
	}
	if (zone)
		spin_unlock_irq(&zone->lru_lock);
	pagevec_release(pvec);
}

/*
 * Try to drop buffers from the pages in a pagevec
 */
void pagevec_strip(struct pagevec *pvec)
{
	int i;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		if (PagePrivate(page) && !TestSetPageLocked(page)) {
			try_to_release_page(page, 0);
			unlock_page(page);
		}
	}
}

/**
 * pagevec_lookup - gang pagecache lookup
 * @pvec:	Where the resulting pages are placed
 * @mapping:	The address_space to search
 * @start:	The starting page index
 * @nr_pages:	The maximum number of pages
 *
 * pagevec_lookup() will search for and return a group of up to @nr_pages pages
 * in the mapping.  The pages are placed in @pvec.  pagevec_lookup() takes a
 * reference against the pages in @pvec.
 *
 * The search returns a group of mapping-contiguous pages with ascending
 * indexes.  There may be holes in the indices due to not-present pages.
 *
 * pagevec_lookup() returns the number of pages which were found.
 */
unsigned int pagevec_lookup(struct pagevec *pvec, struct address_space *mapping,
		pgoff_t start, unsigned int nr_pages)
{
	pvec->nr = find_get_pages(mapping, start, nr_pages, pvec->pages);
	return pagevec_count(pvec);
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
