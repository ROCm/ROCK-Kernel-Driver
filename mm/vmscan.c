/*
 *  linux/mm/vmscan.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Version: $Id: vmscan.c,v 1.5 1998/02/23 22:14:28 sct Exp $
 *  Zone aware kswapd started 02/00, Kanoj Sarcar (kanoj@sgi.com).
 *  Multiqueue VM started 5.8.00, Rik van Riel.
 */

#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/file.h>

#include <asm/pgalloc.h>

/*
 * The "priority" of VM scanning is how much of the queues we
 * will scan in one go. A value of 6 for DEF_PRIORITY implies
 * that we'll scan 1/64th of the queues ("queue_length >> 6")
 * during a normal aging round.
 */
#define DEF_PRIORITY (6)

/*
 * The swap-out function returns 1 if it successfully
 * scanned all the pages it was asked to (`count').
 * It returns zero if it couldn't do anything,
 *
 * rss may decrease because pages are shared, but this
 * doesn't count as having freed a page.
 */

/* mm->page_table_lock is held. mmap_sem is not held */
static inline int try_to_swap_out(struct mm_struct * mm, struct vm_area_struct* vma, unsigned long address, pte_t * page_table, struct page *page, zone_t * classzone)
{
	pte_t pte;
	swp_entry_t entry;

	/* Don't look at this pte if it's been accessed recently. */
	if (ptep_test_and_clear_young(page_table)) {
		flush_tlb_page(vma, address);
		SetPageReferenced(page);
		return 0;
	}

	if (!memclass(page->zone, classzone))
		return 0;

	if (TryLockPage(page))
		return 0;

	/* From this point on, the odds are that we're going to
	 * nuke this pte, so read and clear the pte.  This hook
	 * is needed on CPUs which update the accessed and dirty
	 * bits in hardware.
	 */
	flush_cache_page(vma, address);
	pte = ptep_get_and_clear(page_table);
	flush_tlb_page(vma, address);

	/*
	 * Is the page already in the swap cache? If so, then
	 * we can just drop our reference to it without doing
	 * any IO - it's already up-to-date on disk.
	 */
	if (PageSwapCache(page)) {
		entry.val = page->index;
		if (pte_dirty(pte))
			set_page_dirty(page);
set_swap_pte:
		swap_duplicate(entry);
		set_pte(page_table, swp_entry_to_pte(entry));
drop_pte:
		mm->rss--;
		UnlockPage(page);
		{
			int freeable = page_count(page) - !!page->buffers <= 2;
			if (freeable)
				deactivate_page(page);
			page_cache_release(page);
			return freeable;
		}
	}

	/*
	 * Is it a clean page? Then it must be recoverable
	 * by just paging it in again, and we can just drop
	 * it..  or if it's dirty but has backing store,
	 * just mark the page dirty and drop it.
	 *
	 * However, this won't actually free any real
	 * memory, as the page will just be in the page cache
	 * somewhere, and as such we should just continue
	 * our scan.
	 *
	 * Basically, this just makes it possible for us to do
	 * some real work in the future in "refill_inactive()".
	 */
	if (page->mapping) {
		if (pte_dirty(pte))
			set_page_dirty(page);
		goto drop_pte;
	}
	/*
	 * Check PageDirty as well as pte_dirty: page may
	 * have been brought back from swap by swapoff.
	 */
	if (!pte_dirty(pte) && !PageDirty(page))
		goto drop_pte;

	/*
	 * This is a dirty, swappable page.  First of all,
	 * get a suitable swap entry for it, and make sure
	 * we have the swap cache set up to associate the
	 * page with that swap entry.
	 */
	entry = get_swap_page();
	if (!entry.val)
		goto out_unlock_restore; /* No swap space left */

	/* Add it to the swap cache and mark it dirty */
	add_to_swap_cache(page, entry);
	set_page_dirty(page);
	goto set_swap_pte;

out_unlock_restore:
	set_pte(page_table, pte);
	UnlockPage(page);
	return 0;
}

/* mm->page_table_lock is held. mmap_sem is not held */
static inline int swap_out_pmd(struct mm_struct * mm, struct vm_area_struct * vma, pmd_t *dir, unsigned long address, unsigned long end, int count, zone_t * classzone)
{
	pte_t * pte;
	unsigned long pmd_end;

	if (pmd_none(*dir))
		return count;
	if (pmd_bad(*dir)) {
		pmd_ERROR(*dir);
		pmd_clear(dir);
		return count;
	}
	
	pte = pte_offset(dir, address);
	
	pmd_end = (address + PMD_SIZE) & PMD_MASK;
	if (end > pmd_end)
		end = pmd_end;

	do {
		if (pte_present(*pte)) {
			struct page *page = pte_page(*pte);

			if (VALID_PAGE(page) && !PageReserved(page)) {
				count -= try_to_swap_out(mm, vma, address, pte, page, classzone);
				if (!count) {
					address += PAGE_SIZE;
					break;
				}
			}
		}
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
	mm->swap_address = address;
	return count;
}

/* mm->page_table_lock is held. mmap_sem is not held */
static inline int swap_out_pgd(struct mm_struct * mm, struct vm_area_struct * vma, pgd_t *dir, unsigned long address, unsigned long end, int count, zone_t * classzone)
{
	pmd_t * pmd;
	unsigned long pgd_end;

	if (pgd_none(*dir))
		return count;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return count;
	}

	pmd = pmd_offset(dir, address);

	pgd_end = (address + PGDIR_SIZE) & PGDIR_MASK;	
	if (pgd_end && (end > pgd_end))
		end = pgd_end;
	
	do {
		count = swap_out_pmd(mm, vma, pmd, address, end, count, classzone);
		if (!count)
			break;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return count;
}

/* mm->page_table_lock is held. mmap_sem is not held */
static inline int swap_out_vma(struct mm_struct * mm, struct vm_area_struct * vma, unsigned long address, int count, zone_t * classzone)
{
	pgd_t *pgdir;
	unsigned long end;

	/* Don't swap out areas which are locked down */
	if (vma->vm_flags & (VM_LOCKED|VM_RESERVED))
		return count;

	pgdir = pgd_offset(mm, address);

	end = vma->vm_end;
	if (address >= end)
		BUG();
	do {
		count = swap_out_pgd(mm, vma, pgdir, address, end, count, classzone);
		if (!count)
			break;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	} while (address && (address < end));
	return count;
}

/* Placeholder for swap_out(): may be updated by fork.c:mmput() */
struct mm_struct *swap_mm = &init_mm;

/*
 * Returns non-zero if we scanned all `count' pages
 */
static inline int swap_out_mm(struct mm_struct * mm, int count, int * race, zone_t * classzone)
{
	unsigned long address;
	struct vm_area_struct* vma;

	/*
	 * Find the proper vm-area after freezing the vma chain 
	 * and ptes.
	 */
	spin_lock(&mm->page_table_lock);
	*race = 1;
	if (swap_mm != mm)
		goto out_unlock;
	*race = 0;
	address = mm->swap_address;
	vma = find_vma(mm, address);
	if (vma) {
		if (address < vma->vm_start)
			address = vma->vm_start;

		for (;;) {
			count = swap_out_vma(mm, vma, address, count, classzone);
			if (!count)
				goto out_unlock;
			vma = vma->vm_next;
			if (!vma)
				break;
			address = vma->vm_start;
		}
	}
	/* Reset to 0 when we reach the end of address space */
	mm->swap_address = 0;

	spin_lock(&mmlist_lock);
	swap_mm = list_entry(mm->mmlist.next, struct mm_struct, mmlist);
	spin_unlock(&mmlist_lock);

out_unlock:
	spin_unlock(&mm->page_table_lock);

	return count;
}

static int FASTCALL(swap_out(unsigned int priority, zone_t * classzone, unsigned int gfp_mask, int nr_pages));
static int swap_out(unsigned int priority, zone_t * classzone, unsigned int gfp_mask, int nr_pages)
{
	int counter, race;
	struct mm_struct *mm;

	/* Then, look at the other mm's */
	counter = mmlist_nr / priority;
	do {
		if (current->need_resched)
			schedule();

		spin_lock(&mmlist_lock);
		mm = swap_mm;
		if (mm == &init_mm) {
			mm = list_entry(mm->mmlist.next, struct mm_struct, mmlist);
			if (mm == &init_mm)
				goto empty;
			swap_mm = mm;
		}

		/* Make sure the mm doesn't disappear when we drop the lock.. */
		atomic_inc(&mm->mm_users);
		spin_unlock(&mmlist_lock);

		nr_pages = swap_out_mm(mm, nr_pages, &race, classzone);

		mmput(mm);

		if (!nr_pages)
			return 1;
	} while (race || --counter >= 0);

	return 0;

empty:
	spin_unlock(&mmlist_lock);
	return 0;
}

static int FASTCALL(shrink_cache(struct list_head * lru, int * max_scan, int nr_pages, zone_t * classzone, unsigned int gfp_mask));
static int shrink_cache(struct list_head * lru, int * max_scan, int nr_pages, zone_t * classzone, unsigned int gfp_mask)
{
	LIST_HEAD(active_local_lru);
	LIST_HEAD(inactive_local_lru);
	struct list_head * entry;
	int __max_scan = *max_scan;

	spin_lock(&pagemap_lru_lock);
	while (__max_scan && (entry = lru->prev) != lru) {
		struct page * page;

		if (__builtin_expect(current->need_resched, 0)) {
			spin_unlock(&pagemap_lru_lock);
			schedule();
			spin_lock(&pagemap_lru_lock);
			continue;
		}

		page = list_entry(entry, struct page, lru);

		if (__builtin_expect(!PageInactive(page) && !PageActive(page), 0))
			BUG();

		if (PageTestandClearReferenced(page)) {
			if (PageInactive(page)) {
				del_page_from_inactive_list(page);
				add_page_to_active_list(page);
			} else if (PageActive(page)) {
				list_del(entry);
				list_add(entry, &active_list);
			} else
				BUG();
			continue;
		}

		deactivate_page_nolock(page);
		list_del(entry);
		list_add_tail(entry, &inactive_local_lru);

		if (__builtin_expect(!memclass(page->zone, classzone), 0))
			continue;

		__max_scan--;

		/* Racy check to avoid trylocking when not worthwhile */
		if (!page->buffers && page_count(page) != 1) {
			activate_page_nolock(page);
			list_del(entry);
			list_add_tail(entry, &active_local_lru);
			continue;
		}

		/*
		 * The page is locked. IO in progress?
		 * Move it to the back of the list.
		 */
		if (__builtin_expect(TryLockPage(page), 0))
			continue;

		if (PageDirty(page) && is_page_cache_freeable(page)) {
			/*
			 * It is not critical here to write it only if
			 * the page is unmapped beause any direct writer
			 * like O_DIRECT would set the PG_dirty bitflag
			 * on the phisical page after having successfully
			 * pinned it and after the I/O to the page is finished,
			 * so the direct writes to the page cannot get lost.
			 */
			int (*writepage)(struct page *);

			writepage = page->mapping->a_ops->writepage;
			if (gfp_mask & __GFP_FS && writepage) {
				spin_unlock(&pagemap_lru_lock);

				ClearPageDirty(page);
				writepage(page);

				spin_lock(&pagemap_lru_lock);
				continue;
			}
		}

		/*
		 * If the page has buffers, try to free the buffer mappings
		 * associated with this page. If we succeed we try to free
		 * the page as well.
		 */
		if (page->buffers) {
			spin_unlock(&pagemap_lru_lock);

			/* avoid to free a locked page */
			page_cache_get(page);

			if (try_to_free_buffers(page, gfp_mask)) {
				if (!page->mapping) {
					UnlockPage(page);

					/*
					 * Account we successfully freed a page
					 * of buffer cache.
					 */
					atomic_dec(&buffermem_pages);

					spin_lock(&pagemap_lru_lock);
					__lru_cache_del(page);

					/* effectively free the page here */
					page_cache_release(page);

					if (--nr_pages)
						continue;
					break;
				} else {
					/*
					 * The page is still in pagecache so undo the stuff
					 * before the try_to_free_buffers since we've not
					 * finished and we can now try the next step.
					 */
					page_cache_release(page);

					spin_lock(&pagemap_lru_lock);
				}
			} else {
				/* failed to drop the buffers so stop here */
				UnlockPage(page);
				page_cache_release(page);

				spin_lock(&pagemap_lru_lock);
				continue;
			}
		}

		if (__builtin_expect(!page->mapping, 0))
			BUG();

		if (__builtin_expect(!spin_trylock(&pagecache_lock), 0)) {
			/* we hold the page lock so the page cannot go away from under us */
			spin_unlock(&pagemap_lru_lock);

			spin_lock(&pagecache_lock);
			spin_lock(&pagemap_lru_lock);
		}

		/*
		 * this is the non-racy check, it is critical to check
		 * PageDirty _after_ we made sure the page is freeable
		 * so not in use by anybody.
		 */
		if (!is_page_cache_freeable(page) || PageDirty(page)) {
			spin_unlock(&pagecache_lock);
			UnlockPage(page);
			continue;
		}

		/* point of no return */
		if (__builtin_expect(!PageSwapCache(page), 1))
			__remove_inode_page(page);
		else
			__delete_from_swap_cache(page);
		spin_unlock(&pagecache_lock);

		__lru_cache_del(page);

		UnlockPage(page);

		/* effectively free the page here */
		page_cache_release(page);

		if (--nr_pages)
			continue;
		break;
	}

	list_splice(&inactive_local_lru, &inactive_list);
	list_splice(&active_local_lru, &active_list);
	spin_unlock(&pagemap_lru_lock);

	*max_scan = __max_scan;
	return nr_pages;
}

static int FASTCALL(shrink_caches(int priority, zone_t * classzone, unsigned int gfp_mask, int nr_pages));
static int shrink_caches(int priority, zone_t * classzone, unsigned int gfp_mask, int nr_pages)
{
	int max_scan = (nr_inactive_pages + nr_active_pages / priority) / priority;

	nr_pages -= kmem_cache_reap(gfp_mask);
	if (nr_pages <= 0)
		return 0;

	nr_pages = shrink_cache(&inactive_list, &max_scan, nr_pages, classzone, gfp_mask);
	if (nr_pages <= 0)
		return 0;

	nr_pages = shrink_cache(&active_list, &max_scan, nr_pages, classzone, gfp_mask);
	if (nr_pages <= 0)
		return 0;

	shrink_dcache_memory(priority, gfp_mask);
	shrink_icache_memory(priority, gfp_mask);

	return nr_pages;
}

int try_to_free_pages(zone_t * classzone, unsigned int gfp_mask, unsigned int order)
{
	int priority = DEF_PRIORITY;

	do {
		int nr_pages = SWAP_CLUSTER_MAX;
		nr_pages = shrink_caches(priority, classzone, gfp_mask, nr_pages);
		if (nr_pages <= 0)
			return 1;

		swap_out(priority, classzone, gfp_mask, SWAP_CLUSTER_MAX);
	} while (--priority);

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
		if (current->need_resched)
			schedule();
		if (!zone->need_balance)
			continue;
		if (!try_to_free_pages(zone, GFP_KSWAPD, 0)) {
			zone->need_balance = 0;
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
		run_task_queue(&tq_disk);
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
