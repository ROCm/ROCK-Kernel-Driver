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
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/suspend.h>
#include <linux/buffer_head.h>		/* for try_to_release_page() */

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

static inline int is_page_cache_freeable(struct page * page)
{
	return page_count(page) - !!PagePrivate(page) == 1;
}

/* Must be called with page's pte_chain_lock held. */
static inline int page_mapping_inuse(struct page * page)
{
	struct address_space *mapping = page->mapping;

	/* Page is in somebody's page tables. */
	if (page->pte_chain)
		return 1;

	/* XXX: does this happen ? */
	if (!mapping)
		return 0;

	/* File is mmap'd by somebody. */
	if (!list_empty(&mapping->i_mmap) || !list_empty(&mapping->i_mmap_shared))
		return 1;

	return 0;
}

static int
shrink_cache(int nr_pages, zone_t *classzone,
		unsigned int gfp_mask, int priority, int max_scan)
{
	struct list_head * entry;
	struct address_space *mapping;

	spin_lock(&pagemap_lru_lock);
	while (--max_scan >= 0 &&
			(entry = inactive_list.prev) != &inactive_list) {
		struct page *page;
		int may_enter_fs;

		if (need_resched()) {
			spin_unlock(&pagemap_lru_lock);
			__set_current_state(TASK_RUNNING);
			schedule();
			spin_lock(&pagemap_lru_lock);
			continue;
		}

		page = list_entry(entry, struct page, lru);

		if (unlikely(!PageLRU(page)))
			BUG();
		if (unlikely(PageActive(page)))
			BUG();

		list_del(entry);
		list_add(entry, &inactive_list);

		/*
		 * Zero page counts can happen because we unlink the pages
		 * _after_ decrementing the usage count..
		 */
		if (unlikely(!page_count(page)))
			continue;

		if (!memclass(page_zone(page), classzone))
			continue;

		/*
		 * swap activity never enters the filesystem and is safe
		 * for GFP_NOFS allocations.
		 */
		may_enter_fs = (gfp_mask & __GFP_FS) ||
				(PageSwapCache(page) && (gfp_mask & __GFP_IO));

		/*
		 * IO in progress? Leave it at the back of the list.
		 */
		if (unlikely(PageWriteback(page))) {
			if (may_enter_fs) {
				page_cache_get(page);
				spin_unlock(&pagemap_lru_lock);
				wait_on_page_writeback(page);
				page_cache_release(page);
				spin_lock(&pagemap_lru_lock);
			}
			continue;
		}

		if (TestSetPageLocked(page))
			continue;

		if (PageWriteback(page)) {	/* The non-racy check */
			unlock_page(page);
			continue;
		}

		/*
		 * The page is in active use or really unfreeable. Move to
		 * the active list.
		 */
		pte_chain_lock(page);
		if (page_referenced(page) && page_mapping_inuse(page)) {
			del_page_from_inactive_list(page);
			add_page_to_active_list(page);
			pte_chain_unlock(page);
			unlock_page(page);
			continue;
		}

		/*
		 * Anonymous process memory without backing store. Try to
		 * allocate it some swap space here.
		 *
		 * XXX: implement swap clustering ?
		 */
		if (page->pte_chain && !page->mapping && !PagePrivate(page)) {
			page_cache_get(page);
			pte_chain_unlock(page);
			spin_unlock(&pagemap_lru_lock);
			if (!add_to_swap(page)) {
				activate_page(page);
				unlock_page(page);
				page_cache_release(page);
				spin_lock(&pagemap_lru_lock);
				continue;
			}
			page_cache_release(page);
			spin_lock(&pagemap_lru_lock);
			pte_chain_lock(page);
		}

		/*
		 * The page is mapped into the page tables of one or more
		 * processes. Try to unmap it here.
		 */
		if (page->pte_chain) {
			switch (try_to_unmap(page)) {
				case SWAP_ERROR:
				case SWAP_FAIL:
					goto page_active;
				case SWAP_AGAIN:
					pte_chain_unlock(page);
					unlock_page(page);
					continue;
				case SWAP_SUCCESS:
					; /* try to free the page below */
			}
		}
		pte_chain_unlock(page);
		mapping = page->mapping;

		if (PageDirty(page) && is_page_cache_freeable(page) &&
				page->mapping && may_enter_fs) {
			/*
			 * It is not critical here to write it only if
			 * the page is unmapped beause any direct writer
			 * like O_DIRECT would set the page's dirty bitflag
			 * on the physical page after having successfully
			 * pinned it and after the I/O to the page is finished,
			 * so the direct writes to the page cannot get lost.
			 */
			int (*writeback)(struct page *, int *);
			const int nr_pages = SWAP_CLUSTER_MAX;
			int nr_to_write = nr_pages;

			writeback = mapping->a_ops->vm_writeback;
			if (writeback == NULL)
				writeback = generic_vm_writeback;
			page_cache_get(page);
			spin_unlock(&pagemap_lru_lock);
			(*writeback)(page, &nr_to_write);
			max_scan -= (nr_pages - nr_to_write);
			page_cache_release(page);
			spin_lock(&pagemap_lru_lock);
			continue;
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
		 */
		if (PagePrivate(page)) {
			spin_unlock(&pagemap_lru_lock);

			/* avoid to free a locked page */
			page_cache_get(page);

			if (try_to_release_page(page, gfp_mask)) {
				if (!mapping) {
					/* effectively free the page here */
					unlock_page(page);
					page_cache_release(page);

					spin_lock(&pagemap_lru_lock);
					if (--nr_pages)
						continue;
					break;
				} else {
					/*
					 * The page is still in pagecache so undo the stuff
					 * before the try_to_release_page since we've not
					 * finished and we can now try the next step.
					 */
					page_cache_release(page);

					spin_lock(&pagemap_lru_lock);
				}
			} else {
				/* failed to drop the buffers so stop here */
				unlock_page(page);
				page_cache_release(page);

				spin_lock(&pagemap_lru_lock);
				continue;
			}
		}

		/*
		 * This is the non-racy check for busy page.
		 */
		if (mapping) {
			write_lock(&mapping->page_lock);
			if (is_page_cache_freeable(page))
				goto page_freeable;
			write_unlock(&mapping->page_lock);
		}
		unlock_page(page);
		continue;
page_freeable:
		/*
		 * It is critical to check PageDirty _after_ we made sure
		 * the page is freeable* so not in use by anybody.
		 */
		if (PageDirty(page)) {
			write_unlock(&mapping->page_lock);
			unlock_page(page);
			continue;
		}

		/* point of no return */
		if (likely(!PageSwapCache(page))) {
			__remove_inode_page(page);
			write_unlock(&mapping->page_lock);
		} else {
			swp_entry_t swap;
			swap.val = page->index;
			__delete_from_swap_cache(page);
			write_unlock(&mapping->page_lock);
			swap_free(swap);
		}

		__lru_cache_del(page);
		unlock_page(page);

		/* effectively free the page here */
		page_cache_release(page);
		if (--nr_pages)
			continue;
		goto out;
page_active:
		/*
		 * OK, we don't know what to do with the page.
		 * It's no use keeping it here, so we move it to
		 * the active list.
		 */
		del_page_from_inactive_list(page);
		add_page_to_active_list(page);
		pte_chain_unlock(page);
		unlock_page(page);
	}
out:	spin_unlock(&pagemap_lru_lock);
	return nr_pages;
}

/*
 * This moves pages from the active list to
 * the inactive list.
 *
 * We move them the other way if the page is 
 * referenced by one or more processes, from rmap
 */
static void refill_inactive(int nr_pages)
{
	struct list_head * entry;

	spin_lock(&pagemap_lru_lock);
	entry = active_list.prev;
	while (nr_pages-- && entry != &active_list) {
		struct page * page;

		page = list_entry(entry, struct page, lru);
		entry = entry->prev;

		pte_chain_lock(page);
		if (page->pte_chain && page_referenced(page)) {
			list_del(&page->lru);
			list_add(&page->lru, &active_list);
			pte_chain_unlock(page);
			continue;
		}
		del_page_from_active_list(page);
		add_page_to_inactive_list(page);
		pte_chain_unlock(page);
	}
	spin_unlock(&pagemap_lru_lock);
}

static int FASTCALL(shrink_caches(zone_t * classzone, int priority, unsigned int gfp_mask, int nr_pages));
static int shrink_caches(zone_t * classzone, int priority, unsigned int gfp_mask, int nr_pages)
{
	int chunk_size = nr_pages;
	unsigned long ratio;
	struct page_state ps;
	int max_scan;

	nr_pages -= kmem_cache_reap(gfp_mask);
	if (nr_pages <= 0)
		return 0;

	nr_pages = chunk_size;

	/*
	 * Try to keep the active list 2/3 of the size of the cache
	 */
	get_page_state(&ps);
	ratio = (unsigned long)nr_pages * ps.nr_active /
				((ps.nr_inactive | 1) * 2);
	refill_inactive(ratio);
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
		while ((pgdat = pgdat->node_next));
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
	} while ((pgdat = pgdat->node_next));

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
