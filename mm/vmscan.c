/*
 *  linux/mm/vmscan.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Zone aware kswapd started 02/00, Kanoj Sarcar (kanoj@sgi.com).
 *  Multiqueue VM started 5.8.00, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/suspend.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>		/* for try_to_release_page() */
#include <linux/mm_inline.h>
#include <linux/pagevec.h>
#include <linux/backing-dev.h>
#include <linux/rmap-locking.h>

#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/topology.h>

#include <linux/swapops.h>

/*
 * The "priority" of VM scanning is how much of the queues we
 * will scan in one go. A value of 12 for DEF_PRIORITY implies
 * that we'll scan 1/4096th of the queues ("queue_length >> 12")
 * during a normal aging round.
 */
#define DEF_PRIORITY 12

#ifdef ARCH_HAS_PREFETCH
#define prefetch_prev_lru_page(_page, _base, _field)			\
	do {								\
		if ((_page)->lru.prev != _base) {			\
			struct page *prev;				\
									\
			prev = list_entry(_page->lru.prev,		\
					struct page, lru);		\
			prefetch(&prev->_field);			\
		}							\
	} while (0)
#else
#define prefetch_prev_lru_page(_page, _base, _field) do { } while (0)
#endif

#ifdef ARCH_HAS_PREFETCHW
#define prefetchw_prev_lru_page(_page, _base, _field)			\
	do {								\
		if ((_page)->lru.prev != _base) {			\
			struct page *prev;				\
									\
			prev = list_entry(_page->lru.prev,		\
					struct page, lru);		\
			prefetchw(&prev->_field);			\
		}							\
	} while (0)
#else
#define prefetchw_prev_lru_page(_page, _base, _field) do { } while (0)
#endif

#ifndef CONFIG_QUOTA
#define shrink_dqcache_memory(ratio, gfp_mask) do { } while (0)
#endif

/* Must be called with page's pte_chain_lock held. */
static inline int page_mapping_inuse(struct page * page)
{
	struct address_space *mapping = page->mapping;

	/* Page is in somebody's page tables. */
	if (page_mapped(page))
		return 1;

	/* XXX: does this happen ? */
	if (!mapping)
		return 0;

	/* File is mmap'd by somebody. */
	if (!list_empty(&mapping->i_mmap) || !list_empty(&mapping->i_mmap_shared))
		return 1;

	return 0;
}

static inline int is_page_cache_freeable(struct page *page)
{
	return page_count(page) - !!PagePrivate(page) == 2;
}


/*
 * shrink_list returns the number of reclaimed pages
 */
static /* inline */ int
shrink_list(struct list_head *page_list, unsigned int gfp_mask,
		int *max_scan, int *nr_mapped)
{
	struct address_space *mapping;
	LIST_HEAD(ret_pages);
	struct pagevec freed_pvec;
	int pgactivate = 0;
	int ret = 0;

	pagevec_init(&freed_pvec);
	while (!list_empty(page_list)) {
		struct page *page;
		int may_enter_fs;

		page = list_entry(page_list->prev, struct page, lru);
		list_del(&page->lru);

		if (TestSetPageLocked(page))
			goto keep;

		/* Double the slab pressure for mapped and swapcache pages */
		if (page_mapped(page) || PageSwapCache(page))
			(*nr_mapped)++;

		BUG_ON(PageActive(page));
		may_enter_fs = (gfp_mask & __GFP_FS) ||
				(PageSwapCache(page) && (gfp_mask & __GFP_IO));

		/*
		 * If the page is mapped into pagetables then wait on it, to
		 * throttle this allocator to the rate at which we can clear
		 * MAP_SHARED data.  This will also throttle against swapcache
		 * writes.
		 */
		if (PageWriteback(page)) {
			if (may_enter_fs) {
				if (page->pte.direct ||
					page->mapping->backing_dev_info ==
						current->backing_dev_info) {
					wait_on_page_writeback(page);
				}
			}
			goto keep_locked;
		}

		pte_chain_lock(page);
		if (page_referenced(page) && page_mapping_inuse(page)) {
			/* In active use or really unfreeable.  Activate it. */
			pte_chain_unlock(page);
			goto activate_locked;
		}

		mapping = page->mapping;

		/*
		 * Anonymous process memory without backing store. Try to
		 * allocate it some swap space here.
		 *
		 * XXX: implement swap clustering ?
		 */
		if (page_mapped(page) && !mapping && !PagePrivate(page)) {
			pte_chain_unlock(page);
			if (!add_to_swap(page))
				goto activate_locked;
			pte_chain_lock(page);
			mapping = page->mapping;
		}

		/*
		 * The page is mapped into the page tables of one or more
		 * processes. Try to unmap it here.
		 */
		if (page_mapped(page) && mapping) {
			switch (try_to_unmap(page)) {
			case SWAP_ERROR:
			case SWAP_FAIL:
				pte_chain_unlock(page);
				goto activate_locked;
			case SWAP_AGAIN:
				pte_chain_unlock(page);
				goto keep_locked;
			case SWAP_SUCCESS:
				; /* try to free the page below */
			}
		}
		pte_chain_unlock(page);

		/*
		 * FIXME: this is CPU-inefficient for shared mappings.
		 * try_to_unmap() will set the page dirty and ->vm_writeback
		 * will write it.  So we're back to page-at-a-time writepage
		 * in LRU order.
		 */
		/*
		 * If the page is dirty, only perform writeback if that write
		 * will be non-blocking.  To prevent this allocation from being
		 * stalled by pagecache activity.  But note that there may be
		 * stalls if we need to run get_block().  We could test
		 * PagePrivate for that.
		 *
		 * If this process is currently in generic_file_write() against
		 * this page's queue, we can perform writeback even if that
		 * will block.
		 *
		 * If the page is swapcache, write it back even if that would
		 * block, for some throttling. This happens by accident, because
		 * swap_backing_dev_info is bust: it doesn't reflect the
		 * congestion state of the swapdevs.  Easy to fix, if needed.
		 * See swapfile.c:page_queue_congested().
		 */
		if (PageDirty(page)) {
			int (*writeback)(struct page *,
					struct writeback_control *);
			struct backing_dev_info *bdi;
			const int cluster_size = SWAP_CLUSTER_MAX;
			struct writeback_control wbc = {
				.nr_to_write = cluster_size,
			};

			if (!is_page_cache_freeable(page))
				goto keep_locked;
			if (!mapping)
				goto keep_locked;
			if (!may_enter_fs)
				goto keep_locked;
			bdi = mapping->backing_dev_info;
			if (bdi != current->backing_dev_info &&
					bdi_write_congested(bdi))
				goto keep_locked;

			writeback = mapping->a_ops->vm_writeback;
			if (writeback == NULL)
				writeback = generic_vm_writeback;
			(*writeback)(page, &wbc);
			*max_scan -= (cluster_size - wbc.nr_to_write);
			goto keep;
		}

		/*
		 * If the page has buffers, try to free the buffer mappings
		 * associated with this page. If we succeed we try to free
		 * the page as well.
		 *
		 * We do this even if the page is PageDirty().
		 * try_to_release_page() does not perform I/O, but it is
		 * possible for a page to have PageDirty set, but it is actually
		 * clean (all its buffers are clean).  This happens if the
		 * buffers were written out directly, with submit_bh(). ext3
		 * will do this, as well as the blockdev mapping. 
		 * try_to_release_page() will discover that cleanness and will
		 * drop the buffers and mark the page clean - it can be freed.
		 *
		 * Rarely, pages can have buffers and no ->mapping.  These are
		 * the pages which were not successfully invalidated in
		 * truncate_complete_page().  We try to drop those buffers here
		 * and if that worked, and the page is no longer mapped into
		 * process address space (page_count == 0) it can be freed.
		 * Otherwise, leave the page on the LRU so it is swappable.
		 */
		if (PagePrivate(page)) {
			if (!try_to_release_page(page, gfp_mask))
				goto keep_locked;
			if (!mapping && page_count(page) == 1)
				goto free_it;
		}

		if (!mapping)
			goto keep_locked;	/* truncate got there first */

		write_lock(&mapping->page_lock);

		/*
		 * The non-racy check for busy page.  It is critical to check
		 * PageDirty _after_ making sure that the page is freeable and
		 * not in use by anybody. 	(pagecache + us == 2)
		 */
		if (page_count(page) != 2 || PageDirty(page)) {
			write_unlock(&mapping->page_lock);
			goto keep_locked;
		}

		if (PageSwapCache(page)) {
			swp_entry_t swap = { .val = page->index };
			__delete_from_swap_cache(page);
			write_unlock(&mapping->page_lock);
			swap_free(swap);
		} else {
			__remove_from_page_cache(page);
			write_unlock(&mapping->page_lock);
		}
		__put_page(page);	/* The pagecache ref */
free_it:
		unlock_page(page);
		ret++;
		if (!pagevec_add(&freed_pvec, page))
			__pagevec_release_nonlru(&freed_pvec);
		continue;

activate_locked:
		SetPageActive(page);
		pgactivate++;
keep_locked:
		unlock_page(page);
keep:
		list_add(&page->lru, &ret_pages);
		BUG_ON(PageLRU(page));
	}
	list_splice(&ret_pages, page_list);
	if (pagevec_count(&freed_pvec))
		__pagevec_release_nonlru(&freed_pvec);
	mod_page_state(pgsteal, ret);
	if (current->flags & PF_KSWAPD)
		mod_page_state(kswapd_steal, ret);
	mod_page_state(pgactivate, pgactivate);
	return ret;
}

/*
 * zone->lru_lock is heavily contented.  We relieve it by quickly privatising
 * a batch of pages and working on them outside the lock.  Any pages which were
 * not freed will be added back to the LRU.
 *
 * shrink_cache() is passed the number of pages to try to free, and returns
 * the number of pages which were reclaimed.
 *
 * For pagecache intensive workloads, the first loop here is the hottest spot
 * in the kernel (apart from the copy_*_user functions).
 */
static /* inline */ int
shrink_cache(const int nr_pages, struct zone *zone,
		unsigned int gfp_mask, int max_scan, int *nr_mapped)
{
	LIST_HEAD(page_list);
	struct pagevec pvec;
	int nr_to_process;
	int ret = 0;

	/*
	 * Try to ensure that we free `nr_pages' pages in one pass of the loop.
	 */
	nr_to_process = nr_pages;
	if (nr_to_process < SWAP_CLUSTER_MAX)
		nr_to_process = SWAP_CLUSTER_MAX;

	pagevec_init(&pvec);

	lru_add_drain();
	spin_lock_irq(&zone->lru_lock);
	while (max_scan > 0 && ret < nr_pages) {
		struct page *page;
		int nr_taken = 0;
		int nr_scan = 0;
		int nr_freed;

		while (nr_scan++ < nr_to_process &&
				!list_empty(&zone->inactive_list)) {
			page = list_entry(zone->inactive_list.prev,
						struct page, lru);

			prefetchw_prev_lru_page(page,
						&zone->inactive_list, flags);

			if (!TestClearPageLRU(page))
				BUG();
			list_del(&page->lru);
			if (page_count(page) == 0) {
				/* It is currently in pagevec_release() */
				SetPageLRU(page);
				list_add(&page->lru, &zone->inactive_list);
				continue;
			}
			list_add(&page->lru, &page_list);
			page_cache_get(page);
			nr_taken++;
		}
		zone->nr_inactive -= nr_taken;
		spin_unlock_irq(&zone->lru_lock);

		if (nr_taken == 0)
			goto done;

		max_scan -= nr_scan;
		mod_page_state(pgscan, nr_scan);
		nr_freed = shrink_list(&page_list, gfp_mask,
					&max_scan, nr_mapped);
		ret += nr_freed;
		if (nr_freed <= 0 && list_empty(&page_list))
			goto done;

		spin_lock_irq(&zone->lru_lock);
		/*
		 * Put back any unfreeable pages.
		 */
		while (!list_empty(&page_list)) {
			page = list_entry(page_list.prev, struct page, lru);
			if (TestSetPageLRU(page))
				BUG();
			list_del(&page->lru);
			if (PageActive(page))
				add_page_to_active_list(zone, page);
			else
				add_page_to_inactive_list(zone, page);
			if (!pagevec_add(&pvec, page)) {
				spin_unlock_irq(&zone->lru_lock);
				__pagevec_release(&pvec);
				spin_lock_irq(&zone->lru_lock);
			}
		}
  	}
	spin_unlock_irq(&zone->lru_lock);
done:
	pagevec_release(&pvec);
	return ret;
}

/*
 * This moves pages from the active list to the inactive list.
 *
 * We move them the other way if the page is referenced by one or more
 * processes, from rmap.
 *
 * If the pages are mostly unmapped, the processing is fast and it is
 * appropriate to hold zone->lru_lock across the whole operation.  But if
 * the pages are mapped, the processing is slow (page_referenced()) so we
 * should drop zone->lru_lock around each page.  It's impossible to balance
 * this, so instead we remove the pages from the LRU while processing them.
 * It is safe to rely on PG_active against the non-LRU pages in here because
 * nobody will play with that bit on a non-LRU page.
 *
 * The downside is that we have to touch page->count against each page.
 * But we had to alter page->flags anyway.
 */
static /* inline */ void
refill_inactive_zone(struct zone *zone, const int nr_pages_in)
{
	int pgdeactivate = 0;
	int nr_pages = nr_pages_in;
	LIST_HEAD(l_hold);	/* The pages which were snipped off */
	LIST_HEAD(l_inactive);	/* Pages to go onto the inactive_list */
	LIST_HEAD(l_active);	/* Pages to go onto the active_list */
	struct page *page;
	struct pagevec pvec;

	lru_add_drain();
	spin_lock_irq(&zone->lru_lock);
	while (nr_pages && !list_empty(&zone->active_list)) {
		page = list_entry(zone->active_list.prev, struct page, lru);
		prefetchw_prev_lru_page(page, &zone->active_list, flags);
		if (!TestClearPageLRU(page))
			BUG();
		list_del(&page->lru);
		if (page_count(page) == 0) {
			/* It is currently in pagevec_release() */
			SetPageLRU(page);
			list_add(&page->lru, &zone->active_list);
			continue;
		}
		page_cache_get(page);
		list_add(&page->lru, &l_hold);
		nr_pages--;
	}
	spin_unlock_irq(&zone->lru_lock);

	while (!list_empty(&l_hold)) {
		page = list_entry(l_hold.prev, struct page, lru);
		list_del(&page->lru);
		if (page_mapped(page)) {
			pte_chain_lock(page);
			if (page_mapped(page) && page_referenced(page)) {
				pte_chain_unlock(page);
				list_add(&page->lru, &l_active);
				continue;
			}
			pte_chain_unlock(page);
		}
		/*
		 * FIXME: need to consider page_count(page) here if/when we
		 * reap orphaned pages via the LRU (Daniel's locking stuff)
		 */
		if (total_swap_pages == 0 && !page->mapping &&
						!PagePrivate(page)) {
			list_add(&page->lru, &l_active);
			continue;
		}
		list_add(&page->lru, &l_inactive);
		pgdeactivate++;
	}

	pagevec_init(&pvec);
	spin_lock_irq(&zone->lru_lock);
	while (!list_empty(&l_inactive)) {
		page = list_entry(l_inactive.prev, struct page, lru);
		prefetchw_prev_lru_page(page, &l_inactive, flags);
		if (TestSetPageLRU(page))
			BUG();
		if (!TestClearPageActive(page))
			BUG();
		list_move(&page->lru, &zone->inactive_list);
		if (!pagevec_add(&pvec, page)) {
			spin_unlock_irq(&zone->lru_lock);
			if (buffer_heads_over_limit)
				pagevec_strip(&pvec);
			__pagevec_release(&pvec);
			spin_lock_irq(&zone->lru_lock);
		}
	}
	if (buffer_heads_over_limit) {
		spin_unlock_irq(&zone->lru_lock);
		pagevec_strip(&pvec);
		spin_lock_irq(&zone->lru_lock);
	}
	while (!list_empty(&l_active)) {
		page = list_entry(l_active.prev, struct page, lru);
		prefetchw_prev_lru_page(page, &l_active, flags);
		if (TestSetPageLRU(page))
			BUG();
		BUG_ON(!PageActive(page));
		list_move(&page->lru, &zone->active_list);
		if (!pagevec_add(&pvec, page)) {
			spin_unlock_irq(&zone->lru_lock);
			__pagevec_release(&pvec);
			spin_lock_irq(&zone->lru_lock);
		}
	}
	zone->nr_active -= pgdeactivate;
	zone->nr_inactive += pgdeactivate;
	spin_unlock_irq(&zone->lru_lock);
	pagevec_release(&pvec);

	mod_page_state(pgrefill, nr_pages_in - nr_pages);
	mod_page_state(pgdeactivate, pgdeactivate);
}

/*
 * Try to reclaim `nr_pages' from this zone.  Returns the number of reclaimed
 * pages.  This is a basic per-zone page freer.  Used by both kswapd and
 * direct reclaim.
 */
static /* inline */ int
shrink_zone(struct zone *zone, int max_scan, unsigned int gfp_mask,
		const int nr_pages, int *nr_mapped)
{
	unsigned long ratio;

	/*
	 * Try to keep the active list 2/3 of the size of the cache.  And
	 * make sure that refill_inactive is given a decent number of pages.
	 *
	 * The "ratio+1" here is important.  With pagecache-intensive workloads
	 * the inactive list is huge, and `ratio' evaluates to zero all the
	 * time.  Which pins the active list memory.  So we add one to `ratio'
	 * just to make sure that the kernel will slowly sift through the
	 * active list.
	 */
	ratio = (unsigned long)nr_pages * zone->nr_active /
				((zone->nr_inactive | 1) * 2);
	atomic_add(ratio+1, &zone->refill_counter);
	while (atomic_read(&zone->refill_counter) > SWAP_CLUSTER_MAX) {
		atomic_sub(SWAP_CLUSTER_MAX, &zone->refill_counter);
		refill_inactive_zone(zone, SWAP_CLUSTER_MAX);
	}
	return shrink_cache(nr_pages, zone, gfp_mask, max_scan, nr_mapped);
}

/*
 * FIXME: don't do this for ZONE_HIGHMEM
 */
/*
 * Here we assume it costs one seek to replace a lru page and that it also
 * takes a seek to recreate a cache object.  With this in mind we age equal
 * percentages of the lru and ageable caches.  This should balance the seeks
 * generated by these structures.
 *
 * NOTE: for now I do this for all zones.  If we find this is too aggressive
 * on large boxes we may want to exclude ZONE_HIGHMEM.
 *
 * If we're encountering mapped pages on the LRU then increase the pressure on
 * slab to avoid swapping.
 */
static void shrink_slab(int total_scanned, int gfp_mask)
{
	int shrink_ratio;
	int pages = nr_used_zone_pages();

	shrink_ratio = (pages / (total_scanned + 1)) + 1;
	shrink_dcache_memory(shrink_ratio, gfp_mask);
	shrink_icache_memory(shrink_ratio, gfp_mask);
	shrink_dqcache_memory(shrink_ratio, gfp_mask);
}

/*
 * This is the direct reclaim path, for page-allocating processes.  We only
 * try to reclaim pages from zones which will satisfy the caller's allocation
 * request.
 */
static int
shrink_caches(struct zone *classzone, int priority,
		int *total_scanned, int gfp_mask, const int nr_pages)
{
	struct zone *first_classzone;
	struct zone *zone;
	int nr_mapped = 0;
	int ret = 0;

	first_classzone = classzone->zone_pgdat->node_zones;
	for (zone = classzone; zone >= first_classzone; zone--) {
		int max_scan;
		int to_reclaim;

		to_reclaim = zone->pages_high - zone->free_pages;
		if (to_reclaim < 0)
			continue;	/* zone has enough memory */

		to_reclaim = min(to_reclaim, SWAP_CLUSTER_MAX);
		to_reclaim = max(to_reclaim, nr_pages);

		/*
		 * If we cannot reclaim `nr_pages' pages by scanning twice
		 * that many pages then fall back to the next zone.
		 */
		max_scan = zone->nr_inactive >> priority;
		if (max_scan < to_reclaim * 2)
			max_scan = to_reclaim * 2;
		ret += shrink_zone(zone, max_scan, gfp_mask,
				to_reclaim, &nr_mapped);
		*total_scanned += max_scan;
		*total_scanned += nr_mapped;
		if (ret >= nr_pages)
			break;
	}
	return ret;
}

/*
 * This is the main entry point to direct page reclaim.
 *
 * If a full scan of the inactive list fails to free enough memory then we
 * are "out of memory" and something needs to be killed.
 *
 * If the caller is !__GFP_FS then the probability of a failure is reasonably
 * high - the zone may be full of dirty or under-writeback pages, which this
 * caller can't do much about.  So for !__GFP_FS callers, we just perform a
 * small LRU walk and if that didn't work out, fail the allocation back to the
 * caller.  GFP_NOFS allocators need to know how to deal with it.  Kicking
 * bdflush, waiting and retrying will work.
 *
 * This is a fairly lame algorithm - it can result in excessive CPU burning and
 * excessive rotation of the inactive list, which is _supposed_ to be an LRU,
 * yes?
 */
int
try_to_free_pages(struct zone *classzone,
		unsigned int gfp_mask, unsigned int order)
{
	int priority;
	const int nr_pages = SWAP_CLUSTER_MAX;
	int nr_reclaimed = 0;

	inc_page_state(pageoutrun);

	for (priority = DEF_PRIORITY; priority; priority--) {
		int total_scanned = 0;

		nr_reclaimed += shrink_caches(classzone, priority,
					&total_scanned, gfp_mask, nr_pages);
		if (nr_reclaimed >= nr_pages)
			return 1;
		if (total_scanned == 0)
			return 1;	/* All zones had enough free memory */
		if (!(gfp_mask & __GFP_FS))
			break;		/* Let the caller handle it */
		/*
		 * Try to write back as many pages as we just scanned.  Not
		 * sure if that makes sense, but it's an attempt to avoid
		 * creating IO storms unnecessarily
		 */
		wakeup_bdflush(total_scanned);

		/* Take a nap, wait for some writeback to complete */
		blk_congestion_wait(WRITE, HZ/4);
		shrink_slab(total_scanned, gfp_mask);
	}
	if (gfp_mask & __GFP_FS)
		out_of_memory();
	return 0;
}

/*
 * kswapd will work across all this node's zones until they are all at
 * pages_high.
 */
static void kswapd_balance_pgdat(pg_data_t *pgdat)
{
	int priority = DEF_PRIORITY;
	int i;

	for (priority = DEF_PRIORITY; priority; priority--) {
		int success = 1;

		for (i = 0; i < pgdat->nr_zones; i++) {
			struct zone *zone = pgdat->node_zones + i;
			int nr_mapped = 0;
			int max_scan;
			int to_reclaim;

			to_reclaim = zone->pages_high - zone->free_pages;
			if (to_reclaim <= 0)
				continue;
			success = 0;
			max_scan = zone->nr_inactive >> priority;
			if (max_scan < to_reclaim * 2)
				max_scan = to_reclaim * 2;
			shrink_zone(zone, max_scan, GFP_KSWAPD,
					to_reclaim, &nr_mapped);
			shrink_slab(max_scan + nr_mapped, GFP_KSWAPD);
		}
		if (success)
			break;	/* All zones are at pages_high */
		blk_congestion_wait(WRITE, HZ/4);
	}
}

/*
 * The background pageout daemon, started as a kernel thread
 * from the init process. 
 *
 * This basically trickles out pages so that we have _some_
 * free memory available even if there is no other activity
 * that frees anything up. This is needed for things like routing
 * etc, where we otherwise might have all activity going on in
 * asynchronous contexts that cannot page things out.
 *
 * If there are applications that are active memory-allocators
 * (most normal use), this basically shouldn't matter.
 */
int kswapd(void *p)
{
	pg_data_t *pgdat = (pg_data_t*)p;
	struct task_struct *tsk = current;
	DEFINE_WAIT(wait);

	daemonize();
	set_cpus_allowed(tsk, __node_to_cpu_mask(pgdat->node_id));
	sprintf(tsk->comm, "kswapd%d", pgdat->node_id);
	sigfillset(&tsk->blocked);
	
	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	tsk->flags |= PF_MEMALLOC|PF_KSWAPD;

	for ( ; ; ) {
		if (current->flags & PF_FREEZE)
			refrigerator(PF_IOTHREAD);
		prepare_to_wait(&pgdat->kswapd_wait, &wait, TASK_INTERRUPTIBLE);
		schedule();
		finish_wait(&pgdat->kswapd_wait, &wait);
		kswapd_balance_pgdat(pgdat);
		blk_run_queues();
	}
}

static int __init kswapd_init(void)
{
	pg_data_t *pgdat;
	printk("Starting kswapd\n");
	swap_setup();
	for_each_pgdat(pgdat)
		kernel_thread(kswapd, pgdat, CLONE_KERNEL);
	return 0;
}

module_init(kswapd_init)
