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
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/suspend.h>
#include <linux/buffer_head.h>		/* for try_to_release_page() */
#include <linux/pagevec.h>

#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <linux/swapops.h>

/*
 * The "priority" of VM scanning is how much of the queues we
 * will scan in one go. A value of 6 for DEF_PRIORITY implies
 * that we'll scan 1/64th of the queues ("queue_length >> 6")
 * during a normal aging round.
 */
#define DEF_PRIORITY (6)

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

/* Must be called with page's pte_chain_lock held. */
static inline int page_mapping_inuse(struct page * page)
{
	struct address_space *mapping = page->mapping;

	/* Page is in somebody's page tables. */
	if (page->pte.chain)
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

static /* inline */ int
shrink_list(struct list_head *page_list, int nr_pages, zone_t *classzone,
		unsigned int gfp_mask, int priority, int *max_scan)
{
	struct address_space *mapping;
	LIST_HEAD(ret_pages);
	struct pagevec freed_pvec;
	const int nr_pages_in = nr_pages;
	int pgactivate = 0;

	pagevec_init(&freed_pvec);
	while (!list_empty(page_list)) {
		struct page *page;
		int may_enter_fs;

		page = list_entry(page_list->prev, struct page, lru);
		list_del(&page->lru);
		if (!memclass(page_zone(page), classzone))
			goto keep;

		if (TestSetPageLocked(page))
			goto keep;

		BUG_ON(PageActive(page));
		may_enter_fs = (gfp_mask & __GFP_FS) ||
				(PageSwapCache(page) && (gfp_mask & __GFP_IO));
		if (PageWriteback(page)) {
			if (may_enter_fs)
				wait_on_page_writeback(page);  /* throttling */
			else
				goto keep_locked;
		}

		pte_chain_lock(page);
		if (page_referenced(page) && page_mapping_inuse(page)) {
			/* In active use or really unfreeable.  Activate it. */
			pte_chain_unlock(page);
			goto activate_locked;
		}

		/*
		 * Anonymous process memory without backing store. Try to
		 * allocate it some swap space here.
		 *
		 * XXX: implement swap clustering ?
		 */
		if (page->pte.chain && !page->mapping && !PagePrivate(page)) {
			pte_chain_unlock(page);
			if (!add_to_swap(page))
				goto activate_locked;
			pte_chain_lock(page);
		}

		/*
		 * The page is mapped into the page tables of one or more
		 * processes. Try to unmap it here.
		 */
		if (page->pte.chain) {
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
		mapping = page->mapping;

		if (PageDirty(page) && is_page_cache_freeable(page) &&
					mapping && may_enter_fs) {
			int (*writeback)(struct page *, int *);
			const int cluster_size = SWAP_CLUSTER_MAX;
			int nr_to_write = cluster_size;

			writeback = mapping->a_ops->vm_writeback;
			if (writeback == NULL)
				writeback = generic_vm_writeback;
			(*writeback)(page, &nr_to_write);
			*max_scan -= (cluster_size - nr_to_write);
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
			if (!try_to_release_page(page, 0))
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
		nr_pages--;
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
	KERNEL_STAT_ADD(pgsteal, nr_pages_in - nr_pages);
	KERNEL_STAT_ADD(pgactivate, pgactivate);
	return nr_pages;
}

/*
 * pagemap_lru_lock is heavily contented.  We relieve it by quickly privatising
 * a batch of pages and working on them outside the lock.  Any pages which were
 * not freed will be added back to the LRU.
 *
 * shrink_cache() is passed the number of pages to try to free, and returns
 * the number which are yet-to-free.
 *
 * For pagecache intensive workloads, the first loop here is the hottest spot
 * in the kernel (apart from the copy_*_user functions).
 */
static /* inline */ int
shrink_cache(int nr_pages, zone_t *classzone,
		unsigned int gfp_mask, int priority, int max_scan)
{
	LIST_HEAD(page_list);
	struct pagevec pvec;
	int nr_to_process;

	/*
	 * Try to ensure that we free `nr_pages' pages in one pass of the loop.
	 */
	nr_to_process = nr_pages;
	if (nr_to_process < SWAP_CLUSTER_MAX)
		nr_to_process = SWAP_CLUSTER_MAX;

	pagevec_init(&pvec);

	spin_lock(&pagemap_lru_lock);
	while (max_scan > 0 && nr_pages > 0) {
		struct page *page;
		int n = 0;

		while (n < nr_to_process && !list_empty(&inactive_list)) {
			page = list_entry(inactive_list.prev, struct page, lru);

			prefetchw_prev_lru_page(page, &inactive_list, flags);

			if (!TestClearPageLRU(page))
				BUG();
			list_del(&page->lru);
			if (page_count(page) == 0) {
				/* It is currently in pagevec_release() */
				SetPageLRU(page);
				list_add(&page->lru, &inactive_list);
				continue;
			}
			list_add(&page->lru, &page_list);
			page_cache_get(page);
			n++;
		}
		spin_unlock(&pagemap_lru_lock);

		if (list_empty(&page_list))
			goto done;

		max_scan -= n;
		mod_page_state(nr_inactive, -n);
		KERNEL_STAT_ADD(pgscan, n);
		nr_pages = shrink_list(&page_list, nr_pages, classzone,
					gfp_mask, priority, &max_scan);

		if (nr_pages <= 0 && list_empty(&page_list))
			goto done;

		spin_lock(&pagemap_lru_lock);
		/*
		 * Put back any unfreeable pages.
		 */
		while (!list_empty(&page_list)) {
			page = list_entry(page_list.prev, struct page, lru);
			if (TestSetPageLRU(page))
				BUG();
			list_del(&page->lru);
			if (PageActive(page))
				__add_page_to_active_list(page);
			else
				add_page_to_inactive_list(page);
			if (!pagevec_add(&pvec, page)) {
				spin_unlock(&pagemap_lru_lock);
				__pagevec_release(&pvec);
				spin_lock(&pagemap_lru_lock);
			}
		}
  	}
	spin_unlock(&pagemap_lru_lock);
done:
	pagevec_release(&pvec);
	return nr_pages;	
}

/*
 * This moves pages from the active list to the inactive list.
 *
 * We move them the other way if the page is referenced by one or more
 * processes, from rmap.
 *
 * If the pages are mostly unmapped, the processing is fast and it is
 * appropriate to hold pagemap_lru_lock across the whole operation.  But if
 * the pages are mapped, the processing is slow (page_referenced()) so we
 * should drop pagemap_lru_lock around each page.  It's impossible to balance
 * this, so instead we remove the pages from the LRU while processing them.
 * It is safe to rely on PG_active against the non-LRU pages in here because
 * nobody will play with that bit on a non-LRU page.
 *
 * The downside is that we have to touch page->count against each page.
 * But we had to alter page->flags anyway.
 */
static /* inline */ void refill_inactive(const int nr_pages_in)
{
	int pgdeactivate = 0;
	int nr_pages = nr_pages_in;
	LIST_HEAD(l_hold);	/* The pages which were snipped off */
	LIST_HEAD(l_inactive);	/* Pages to go onto the inactive_list */
	LIST_HEAD(l_active);	/* Pages to go onto the active_list */
	struct page *page;
	struct pagevec pvec;

	spin_lock(&pagemap_lru_lock);
	while (nr_pages && !list_empty(&active_list)) {
		page = list_entry(active_list.prev, struct page, lru);
		prefetchw_prev_lru_page(page, &active_list, flags);
		if (!TestClearPageLRU(page))
			BUG();
		page_cache_get(page);
		list_move(&page->lru, &l_hold);
		nr_pages--;
	}
	spin_unlock(&pagemap_lru_lock);

	while (!list_empty(&l_hold)) {
		page = list_entry(l_hold.prev, struct page, lru);
		list_del(&page->lru);
		if (page->pte.chain) {
			if (test_and_set_bit(PG_chainlock, &page->flags)) {
				list_add(&page->lru, &l_active);
				continue;
			}
			if (page->pte.chain && page_referenced(page)) {
				pte_chain_unlock(page);
				list_add(&page->lru, &l_active);
				continue;
			}
			pte_chain_unlock(page);
		}
		list_add(&page->lru, &l_inactive);
		pgdeactivate++;
	}

	pagevec_init(&pvec);
	spin_lock(&pagemap_lru_lock);
	while (!list_empty(&l_inactive)) {
		page = list_entry(l_inactive.prev, struct page, lru);
		prefetchw_prev_lru_page(page, &l_inactive, flags);
		if (TestSetPageLRU(page))
			BUG();
		if (!TestClearPageActive(page))
			BUG();
		list_move(&page->lru, &inactive_list);
		if (!pagevec_add(&pvec, page)) {
			spin_unlock(&pagemap_lru_lock);
			__pagevec_release(&pvec);
			spin_lock(&pagemap_lru_lock);
		}
	}
	while (!list_empty(&l_active)) {
		page = list_entry(l_active.prev, struct page, lru);
		prefetchw_prev_lru_page(page, &l_active, flags);
		if (TestSetPageLRU(page))
			BUG();
		BUG_ON(!PageActive(page));
		list_move(&page->lru, &active_list);
		if (!pagevec_add(&pvec, page)) {
			spin_unlock(&pagemap_lru_lock);
			__pagevec_release(&pvec);
			spin_lock(&pagemap_lru_lock);
		}
	}
	spin_unlock(&pagemap_lru_lock);
	pagevec_release(&pvec);

	mod_page_state(nr_active, -pgdeactivate);
	mod_page_state(nr_inactive, pgdeactivate);
	KERNEL_STAT_ADD(pgscan, nr_pages_in - nr_pages);
	KERNEL_STAT_ADD(pgdeactivate, pgdeactivate);
}

static /* inline */ int
shrink_caches(zone_t *classzone, int priority,
		unsigned int gfp_mask, int nr_pages)
{
	unsigned long ratio;
	struct page_state ps;
	int max_scan;
	static atomic_t nr_to_refill = ATOMIC_INIT(0);

	if (kmem_cache_reap(gfp_mask) >= nr_pages)
  		return 0;

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
	get_page_state(&ps);
	ratio = (unsigned long)nr_pages * ps.nr_active /
				((ps.nr_inactive | 1) * 2);
	atomic_add(ratio+1, &nr_to_refill);
	if (atomic_read(&nr_to_refill) > SWAP_CLUSTER_MAX) {
		atomic_sub(SWAP_CLUSTER_MAX, &nr_to_refill);
		refill_inactive(SWAP_CLUSTER_MAX);
	}

	max_scan = ps.nr_inactive / priority;
	nr_pages = shrink_cache(nr_pages, classzone,
				gfp_mask, priority, max_scan);

	if (nr_pages <= 0)
		return 0;

	wakeup_bdflush();

	shrink_dcache_memory(priority, gfp_mask);

	/* After shrinking the dcache, get rid of unused inodes too .. */
	shrink_icache_memory(1, gfp_mask);
#ifdef CONFIG_QUOTA
	shrink_dqcache_memory(DEF_PRIORITY, gfp_mask);
#endif

	return nr_pages;
}

int try_to_free_pages(zone_t *classzone, unsigned int gfp_mask, unsigned int order)
{
	int priority = DEF_PRIORITY;
	int nr_pages = SWAP_CLUSTER_MAX;

	KERNEL_STAT_INC(pageoutrun);

	do {
		nr_pages = shrink_caches(classzone, priority, gfp_mask, nr_pages);
		if (nr_pages <= 0)
			return 1;
	} while (--priority);

	/*
	 * Hmm.. Cache shrink failed - time to kill something?
	 * Mhwahahhaha! This is the part I really like. Giggle.
	 */
	out_of_memory();
	return 0;
}

DECLARE_WAIT_QUEUE_HEAD(kswapd_wait);

static int check_classzone_need_balance(zone_t * classzone)
{
	zone_t * first_classzone;

	first_classzone = classzone->zone_pgdat->node_zones;
	while (classzone >= first_classzone) {
		if (classzone->free_pages > classzone->pages_high)
			return 0;
		classzone--;
	}
	return 1;
}

static int kswapd_balance_pgdat(pg_data_t * pgdat)
{
	int need_more_balance = 0, i;
	zone_t * zone;

	for (i = pgdat->nr_zones-1; i >= 0; i--) {
		zone = pgdat->node_zones + i;
		cond_resched();
		if (!zone->need_balance)
			continue;
		if (!try_to_free_pages(zone, GFP_KSWAPD, 0)) {
			zone->need_balance = 0;
			__set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ);
			continue;
		}
		if (check_classzone_need_balance(zone))
			need_more_balance = 1;
		else
			zone->need_balance = 0;
	}

	return need_more_balance;
}

static void kswapd_balance(void)
{
	int need_more_balance;
	pg_data_t * pgdat;

	do {
		need_more_balance = 0;
		pgdat = pgdat_list;
		do
			need_more_balance |= kswapd_balance_pgdat(pgdat);
		while ((pgdat = pgdat->pgdat_next));
	} while (need_more_balance);
}

static int kswapd_can_sleep_pgdat(pg_data_t * pgdat)
{
	zone_t * zone;
	int i;

	for (i = pgdat->nr_zones-1; i >= 0; i--) {
		zone = pgdat->node_zones + i;
		if (!zone->need_balance)
			continue;
		return 0;
	}

	return 1;
}

static int kswapd_can_sleep(void)
{
	pg_data_t * pgdat;

	pgdat = pgdat_list;
	do {
		if (kswapd_can_sleep_pgdat(pgdat))
			continue;
		return 0;
	} while ((pgdat = pgdat->pgdat_next));

	return 1;
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
int kswapd(void *unused)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	daemonize();
	strcpy(tsk->comm, "kswapd");
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
	tsk->flags |= PF_MEMALLOC;

	/*
	 * Kswapd main loop.
	 */
	for (;;) {
		if (current->flags & PF_FREEZE)
			refrigerator(PF_IOTHREAD);
		__set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&kswapd_wait, &wait);

		mb();
		if (kswapd_can_sleep())
			schedule();

		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&kswapd_wait, &wait);

		/*
		 * If we actually get into a low-memory situation,
		 * the processes needing more memory will wake us
		 * up on a more timely basis.
		 */
		kswapd_balance();
		blk_run_queues();
	}
}

static int __init kswapd_init(void)
{
	printk("Starting kswapd\n");
	swap_setup();
	kernel_thread(kswapd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	return 0;
}

module_init(kswapd_init)
