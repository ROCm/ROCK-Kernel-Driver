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

static inline void age_page_up(struct page *page)
{
	unsigned age = page->age + PAGE_AGE_ADV;
	if (age > PAGE_AGE_MAX)
		age = PAGE_AGE_MAX;
	page->age = age;
}

static inline void age_page_down(struct page * page)
{
	page->age /= 2;
}

/*
 * The swap-out function returns 1 if it successfully
 * scanned all the pages it was asked to (`count').
 * It returns zero if it couldn't do anything,
 *
 * rss may decrease because pages are shared, but this
 * doesn't count as having freed a page.
 */

/*
 * Estimate whether a zone has enough inactive or free pages..
 */
static unsigned int zone_inactive_plenty(zone_t *zone)
{
	unsigned int inactive;

	if (!zone->size)
		return 0;
		
	inactive = zone->inactive_dirty_pages;
	inactive += zone->inactive_clean_pages;
	inactive += zone->free_pages;

	return (inactive > (zone->size / 3));
}

static unsigned int zone_free_plenty(zone_t *zone)
{
	unsigned int free;

	free = zone->free_pages;
	free += zone->inactive_clean_pages;

	return free > zone->pages_high*2;
}

/* mm->page_table_lock is held. mmap_sem is not held */
static void try_to_swap_out(struct mm_struct * mm, struct vm_area_struct* vma, unsigned long address, pte_t * page_table, struct page *page)
{
	pte_t pte;
	swp_entry_t entry;

	/* 
	 * If we are doing a zone-specific scan, do not
	 * touch pages from zones which don't have a 
	 * shortage.
	 */
	if (zone_inactive_plenty(page->zone))
		return;

	/* Don't look at this pte if it's been accessed recently. */
	if (ptep_test_and_clear_young(page_table)) {
		mark_page_accessed(page);
		return;
	}

	if (TryLockPage(page))
		return;

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
		if (!PageReferenced(page))
			deactivate_page(page);
		UnlockPage(page);
		page_cache_release(page);
		return;
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
	return;
}

/* mm->page_table_lock is held. mmap_sem is not held */
static int swap_out_pmd(struct mm_struct * mm, struct vm_area_struct * vma, pmd_t *dir, unsigned long address, unsigned long end, int count)
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
				try_to_swap_out(mm, vma, address, pte, page);
				if (!--count)
					break;
			}
		}
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
	mm->swap_address = address + PAGE_SIZE;
	return count;
}

/* mm->page_table_lock is held. mmap_sem is not held */
static inline int swap_out_pgd(struct mm_struct * mm, struct vm_area_struct * vma, pgd_t *dir, unsigned long address, unsigned long end, int count)
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
		count = swap_out_pmd(mm, vma, pmd, address, end, count);
		if (!count)
			break;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return count;
}

/* mm->page_table_lock is held. mmap_sem is not held */
static int swap_out_vma(struct mm_struct * mm, struct vm_area_struct * vma, unsigned long address, int count)
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
		count = swap_out_pgd(mm, vma, pgdir, address, end, count);
		if (!count)
			break;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	} while (address && (address < end));
	return count;
}

/*
 * Returns non-zero if we scanned all `count' pages
 */
static int swap_out_mm(struct mm_struct * mm, int count)
{
	unsigned long address;
	struct vm_area_struct* vma;

	if (!count)
		return 1;
	/*
	 * Go through process' page directory.
	 */

	/*
	 * Find the proper vm-area after freezing the vma chain 
	 * and ptes.
	 */
	spin_lock(&mm->page_table_lock);
	address = mm->swap_address;
	vma = find_vma(mm, address);
	if (vma) {
		if (address < vma->vm_start)
			address = vma->vm_start;

		for (;;) {
			count = swap_out_vma(mm, vma, address, count);
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

out_unlock:
	spin_unlock(&mm->page_table_lock);
	return !count;
}

#define SWAP_MM_SHIFT	4
#define SWAP_SHIFT	5
#define SWAP_MIN	8

static inline int swap_amount(struct mm_struct *mm)
{
	int nr = mm->rss >> SWAP_SHIFT;
	if (nr < SWAP_MIN) {
		nr = SWAP_MIN;
		if (nr > mm->rss)
			nr = mm->rss;
	}
	return nr;
}

/* Placeholder for swap_out(): may be updated by fork.c:mmput() */
struct mm_struct *swap_mm = &init_mm;

static void swap_out(unsigned int priority, int gfp_mask)
{
	int counter;
	int retval = 0;
	struct mm_struct *mm = current->mm;

	/* Always start by trying to penalize the process that is allocating memory */
	if (mm)
		retval = swap_out_mm(mm, swap_amount(mm));

	/* Then, look at the other mm's */
	counter = (mmlist_nr << SWAP_MM_SHIFT) >> priority;
	do {
		spin_lock(&mmlist_lock);
		mm = swap_mm;
		if (mm == &init_mm) {
			mm = list_entry(mm->mmlist.next, struct mm_struct, mmlist);
			if (mm == &init_mm)
				goto empty;
		}
		/* Set pointer for next call to next in the list */
		swap_mm = list_entry(mm->mmlist.next, struct mm_struct, mmlist);

		/* Make sure the mm doesn't disappear when we drop the lock.. */
		atomic_inc(&mm->mm_users);
		spin_unlock(&mmlist_lock);

		/* Walk about 6% of the address space each time */
		retval |= swap_out_mm(mm, swap_amount(mm));
		mmput(mm);
	} while (--counter >= 0);
	return;

empty:
	spin_unlock(&mmlist_lock);
}


/**
 * reclaim_page -	reclaims one page from the inactive_clean list
 * @zone: reclaim a page from this zone
 *
 * The pages on the inactive_clean can be instantly reclaimed.
 * The tests look impressive, but most of the time we'll grab
 * the first page of the list and exit successfully.
 */
struct page * reclaim_page(zone_t * zone)
{
	struct page * page = NULL;
	struct list_head * page_lru;
	int maxscan;

	/*
	 * We only need the pagemap_lru_lock if we don't reclaim the page,
	 * but we have to grab the pagecache_lock before the pagemap_lru_lock
	 * to avoid deadlocks and most of the time we'll succeed anyway.
	 */
	spin_lock(&pagecache_lock);
	spin_lock(&pagemap_lru_lock);
	maxscan = zone->inactive_clean_pages;
	while ((page_lru = zone->inactive_clean_list.prev) !=
			&zone->inactive_clean_list && maxscan--) {
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (!PageInactiveClean(page)) {
			printk("VM: reclaim_page, wrong page on list.\n");
			list_del(page_lru);
			page->zone->inactive_clean_pages--;
			continue;
		}

		/* Page is referenced? Clear and move to the head of the list.. */
		if (PageTestandClearReferenced(page)) {
			list_del(page_lru);
			list_add(page_lru, &zone->inactive_clean_list);
		}

		/* The page is dirty, or locked, move to inactive_dirty list. */
		if (page->buffers || PageDirty(page) || TryLockPage(page)) {
			del_page_from_inactive_clean_list(page);
			add_page_to_inactive_dirty_list(page);
			continue;
		}

		/* Page is in use?  Move it to the active list. */
		if (page_count(page) > 1) {
			UnlockPage(page);
			del_page_from_inactive_clean_list(page);
			add_page_to_active_list(page);
			continue;
		}

		/* OK, remove the page from the caches. */
		if (PageSwapCache(page)) {
			__delete_from_swap_cache(page);
			goto found_page;
		}

		if (page->mapping) {
			__remove_inode_page(page);
			goto found_page;
		}

		/* We should never ever get here. */
		printk(KERN_ERR "VM: reclaim_page, found unknown page\n");
		list_del(page_lru);
		zone->inactive_clean_pages--;
		UnlockPage(page);
	}
	/* Reset page pointer, maybe we encountered an unfreeable page. */
	page = NULL;
	goto out;

found_page:
	memory_pressure++;
	del_page_from_inactive_clean_list(page);
	UnlockPage(page);
	page->age = PAGE_AGE_START;
	if (page_count(page) != 1)
		printk("VM: reclaim_page, found page with count %d!\n",
				page_count(page));
out:
	spin_unlock(&pagemap_lru_lock);
	spin_unlock(&pagecache_lock);
	return page;
}

/**
 * page_launder - clean dirty inactive pages, move to inactive_clean list
 * @gfp_mask: what operations we are allowed to do
 * @sync: are we allowed to do synchronous IO in emergencies ?
 *
 * When this function is called, we are most likely low on free +
 * inactive_clean pages. Since we want to refill those pages as
 * soon as possible, we'll make two loops over the inactive list,
 * one to move the already cleaned pages to the inactive_clean lists
 * and one to (often asynchronously) clean the dirty inactive pages.
 *
 * In situations where kswapd cannot keep up, user processes will
 * end up calling this function. Since the user process needs to
 * have a page before it can continue with its allocation, we'll
 * do synchronous page flushing in that case.
 *
 * This code used to be heavily inspired by the FreeBSD source code. 
 * Thanks go out to Matthew Dillon.
 */
#define CAN_DO_FS		(gfp_mask & __GFP_FS)
int page_launder(int gfp_mask, int sync)
{
	int maxscan, cleaned_pages;
	struct list_head * page_lru;
	struct page * page;

	cleaned_pages = 0;

	/* Will we wait on IO? */
	if (!sync)
		gfp_mask &= ~__GFP_WAIT;

	spin_lock(&pagemap_lru_lock);
	maxscan = nr_inactive_dirty_pages >> DEF_PRIORITY;
	while ((page_lru = inactive_dirty_list.prev) != &inactive_dirty_list &&
				maxscan-- > 0) {
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (!PageInactiveDirty(page)) {
			printk("VM: page_launder, wrong page on list.\n");
			list_del(page_lru);
			nr_inactive_dirty_pages--;
			page->zone->inactive_dirty_pages--;
			continue;
		}

		/* Page is referenced? Clear and move to the head of the list.. */
		if (PageTestandClearReferenced(page)) {
			list_del(page_lru);
			list_add(page_lru, &inactive_dirty_list);
		}

		/* Page is in use?  Move it to the active list. */
		if ((!page->buffers && page_count(page) > 1) || page_ramdisk(page)) {
			del_page_from_inactive_dirty_list(page);
			add_page_to_active_list(page);
			continue;
		}

		/* 
		 * If this zone has plenty of pages free,
		 * don't spend time on cleaning it.
		 */
		if (zone_free_plenty(page->zone)) {
			list_del(page_lru);
			list_add(page_lru, &inactive_dirty_list);
			continue;
		}

		/*
		 * The page is locked. IO in progress?
		 * Move it to the back of the list.
		 */
		if (TryLockPage(page)) {
			list_del(page_lru);
			list_add(page_lru, &inactive_dirty_list);
			continue;
		}

		/*
		 * Dirty swap-cache page? Write it out if
		 * last copy..
		 */
		if (PageDirty(page)) {
			int (*writepage)(struct page *);

			/* Can a page get here without page->mapping? */
			if (!page->mapping)
				goto page_active;
			writepage = page->mapping->a_ops->writepage;
			if (!writepage)
				goto page_active;

			/* Can't do it? Move it to the back of the list */
			if (!CAN_DO_FS) {
				list_del(page_lru);
				list_add(page_lru, &inactive_dirty_list);
				UnlockPage(page);
				continue;
			}

			/* OK, do a physical asynchronous write to swap.  */
			ClearPageDirty(page);
			page_cache_get(page);
			spin_unlock(&pagemap_lru_lock);

			writepage(page);
			page_cache_release(page);

			/* And re-start the thing.. */
			spin_lock(&pagemap_lru_lock);
			continue;
		}

		/*
		 * If the page has buffers, try to free the buffer mappings
		 * associated with this page. If we succeed we either free
		 * the page (in case it was a buffercache only page) or we
		 * move the page to the inactive_clean list.
		 *
		 * On the first round, we should free all previously cleaned
		 * buffer pages
		 */
		if (page->buffers) {
			int clearedbuf;
			int freed_page = 0;

			/*
			 * Since we might be doing disk IO, we have to
			 * drop the spinlock and take an extra reference
			 * on the page so it doesn't go away from under us.
			 */
			del_page_from_inactive_dirty_list(page);
			page_cache_get(page);
			spin_unlock(&pagemap_lru_lock);

			/* Try to free the page buffers. */
			clearedbuf = try_to_free_buffers(page, gfp_mask);

			/*
			 * Re-take the spinlock. Note that we cannot
			 * unlock the page yet since we're still
			 * accessing the page_struct here...
			 */
			spin_lock(&pagemap_lru_lock);

			/* The buffers were not freed. */
			if (!clearedbuf) {
				add_page_to_inactive_dirty_list(page);

			/* The page was only in the buffer cache. */
			} else if (!page->mapping) {
				atomic_dec(&buffermem_pages);
				freed_page = 1;
				cleaned_pages++;

			/* The page has more users besides the cache and us. */
			} else if (page_count(page) > 2) {
				add_page_to_active_list(page);

			/* OK, we "created" a freeable page. */
			} else /* page->mapping && page_count(page) == 2 */ {
				add_page_to_inactive_clean_list(page);
				cleaned_pages++;
			}

			/*
			 * Unlock the page and drop the extra reference.
			 * We can only do it here because we are accessing
			 * the page struct above.
			 */
			UnlockPage(page);
			page_cache_release(page);

			continue;
		} else if (page->mapping && !PageDirty(page)) {
			/*
			 * If a page had an extra reference in
			 * deactivate_page(), we will find it here.
			 * Now the page is really freeable, so we
			 * move it to the inactive_clean list.
			 */
			del_page_from_inactive_dirty_list(page);
			add_page_to_inactive_clean_list(page);
			UnlockPage(page);
			cleaned_pages++;
		} else {
page_active:
			/*
			 * OK, we don't know what to do with the page.
			 * It's no use keeping it here, so we move it to
			 * the active list.
			 */
			del_page_from_inactive_dirty_list(page);
			add_page_to_active_list(page);
			UnlockPage(page);
		}
	}
	spin_unlock(&pagemap_lru_lock);

	/* Return the number of pages moved to the inactive_clean list. */
	return cleaned_pages;
}

/**
 * refill_inactive_scan - scan the active list and find pages to deactivate
 * @priority: the priority at which to scan
 *
 * This function will scan a portion of the active list to find
 * unused pages, those pages will then be moved to the inactive list.
 */
static int refill_inactive_scan(unsigned int priority)
{
	struct list_head * page_lru;
	struct page * page;
	int maxscan = nr_active_pages >> priority;
	int page_active = 0;
	int nr_deactivated = 0;

	/* Take the lock while messing with the list... */
	spin_lock(&pagemap_lru_lock);
	while (maxscan-- > 0 && (page_lru = active_list.prev) != &active_list) {
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (!PageActive(page)) {
			printk("VM: refill_inactive, wrong page on list.\n");
			list_del(page_lru);
			nr_active_pages--;
			continue;
		}

		/*
		 * Do not deactivate pages from zones which 
		 * have plenty inactive pages.
		 */

		if (zone_inactive_plenty(page->zone)) {
			page_active = 1;
			goto skip_page;
		}

		/* Do aging on the pages. */
		if (PageTestandClearReferenced(page)) {
			age_page_up(page);
			page_active = 1;
		} else {
			age_page_down(page);
			/*
			 * Since we don't hold a reference on the page
			 * ourselves, we have to do our test a bit more
			 * strict then deactivate_page(). This is needed
			 * since otherwise the system could hang shuffling
			 * unfreeable pages from the active list to the
			 * inactive_dirty list and back again...
			 *
			 * SUBTLE: we can have buffer pages with count 1.
			 */
			if (page_count(page) <=	(page->buffers ? 2 : 1)) {
				deactivate_page_nolock(page);
				page_active = 0;
			} else {
				page_active = 1;
			}
		}
		/*
		 * If the page is still on the active list, move it
		 * to the other end of the list. Otherwise we exit if
		 * we have done enough work.
		 */
		if (page_active || PageActive(page)) {
skip_page:
			list_del(page_lru);
			list_add(page_lru, &active_list);
		} else {
			nr_deactivated++;
		}
	}
	spin_unlock(&pagemap_lru_lock);

	return nr_deactivated;
}

/*
 * Check if there are zones with a severe shortage of free pages,
 * or if all zones have a minor shortage.
 */
int free_shortage(void)
{
	pg_data_t *pgdat;
	unsigned int global_free = 0;
	unsigned int global_target = freepages.high;

	/* Are we low on free pages anywhere? */
	pgdat = pgdat_list;
	do {
		int i;
		for(i = 0; i < MAX_NR_ZONES; i++) {
			zone_t *zone = pgdat->node_zones+ i;
			unsigned int free;

			if (!zone->size)
				continue;

			free = zone->free_pages;
			free += zone->inactive_clean_pages;

			/* Local shortage? */
			if (free < zone->pages_low)
				return 1;

			global_free += free;
		}
		pgdat = pgdat->node_next;
	} while (pgdat);

	/* Global shortage? */
	return global_free < global_target;
}

/*
 * Are we low on inactive pages globally or in any zone?
 */
int inactive_shortage(void)
{
	pg_data_t *pgdat;
	unsigned int global_target = freepages.high + inactive_target;
	unsigned int global_inactive = 0;

	pgdat = pgdat_list;
	do {
		int i;
		for(i = 0; i < MAX_NR_ZONES; i++) {
			zone_t *zone = pgdat->node_zones + i;
			unsigned int inactive;

			if (!zone->size)
				continue;

			inactive  = zone->inactive_dirty_pages;
			inactive += zone->inactive_clean_pages;
			inactive += zone->free_pages;

			/* Local shortage? */
			if (inactive < zone->pages_high)
				return 1;

			global_inactive += inactive;
		}
		pgdat = pgdat->node_next;
	} while (pgdat);

	/* Global shortage? */
	return global_inactive < global_target;
}

/*
 * Loop until we are no longer under an inactive or free
 * shortage. Return 1 on success, 0 if we failed to get
 * there even after "maxtry" loops.
 */
#define INACTIVE_SHORTAGE 1
#define FREE_SHORTAGE 2
#define GENERAL_SHORTAGE 4
static int do_try_to_free_pages(unsigned int gfp_mask, int user)
{
	int shortage = 0;
	int maxtry;

	/* Always walk at least the active queue when called */
	refill_inactive_scan(DEF_PRIORITY);

	maxtry = 1 << DEF_PRIORITY;
	do {
		/*
		 * If needed, we move pages from the active list
		 * to the inactive list.
		 */
		if (shortage & INACTIVE_SHORTAGE) {
			/* Walk the VM space for a bit.. */
			swap_out(DEF_PRIORITY, gfp_mask);

			/* ..and refill the inactive list */
			refill_inactive_scan(DEF_PRIORITY);
		}

		/*
		 * If we're low on free pages, move pages from the
		 * inactive_dirty list to the inactive_clean list.
		 *
		 * Usually bdflush will have pre-cleaned the pages
		 * before we get around to moving them to the other
		 * list, so this is a relatively cheap operation.
		 */
		if (shortage & FREE_SHORTAGE)
			page_launder(gfp_mask, user);

		/* 	
		 * Reclaim unused slab cache if we were short on memory.
		 */
		if (shortage & GENERAL_SHORTAGE) {
			shrink_dcache_memory(DEF_PRIORITY, gfp_mask);
			shrink_icache_memory(DEF_PRIORITY, gfp_mask);

			kmem_cache_reap(gfp_mask);
		}

		if (current->need_resched) {
			 __set_current_state(TASK_RUNNING);
			schedule();
		}

		shortage = 0;
		if (inactive_shortage())
			shortage |= INACTIVE_SHORTAGE | GENERAL_SHORTAGE;
		if (free_shortage())
			shortage |= FREE_SHORTAGE | GENERAL_SHORTAGE;

		if (--maxtry <= 0)
			break;
	} while (shortage);

	/* Return success if we're not "totally short" */
	return shortage != (FREE_SHORTAGE | INACTIVE_SHORTAGE | GENERAL_SHORTAGE);
}

DECLARE_WAIT_QUEUE_HEAD(kswapd_wait);
DECLARE_WAIT_QUEUE_HEAD(kswapd_done);

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
		static long recalc = 0;

		/* Once a second ... */
		if (time_after(jiffies, recalc + HZ)) {
			recalc = jiffies;

			/* Recalculate VM statistics. */
			recalculate_vm_stats();
		}

		if (!do_try_to_free_pages(GFP_KSWAPD, 1)) {
			if (out_of_memory())
				oom_kill();
			continue;
		}

		run_task_queue(&tq_disk);
		interruptible_sleep_on_timeout(&kswapd_wait, HZ);
	}
}

void wakeup_kswapd(void)
{
	if (waitqueue_active(&kswapd_wait))
		wake_up_interruptible(&kswapd_wait);
}

/*
 * Called by non-kswapd processes when they want more
 * memory but are unable to sleep on kswapd because
 * they might be holding some IO locks ...
 */
int try_to_free_pages(unsigned int gfp_mask)
{
	int ret = 1;

	if (gfp_mask & __GFP_WAIT) {
		current->flags |= PF_MEMALLOC;
		ret = do_try_to_free_pages(gfp_mask, 1);
		current->flags &= ~PF_MEMALLOC;
	}

	return ret;
}

DECLARE_WAIT_QUEUE_HEAD(kreclaimd_wait);
/*
 * Kreclaimd will move pages from the inactive_clean list to the
 * free list, in order to keep atomic allocations possible under
 * all circumstances.
 */
int kreclaimd(void *unused)
{
	struct task_struct *tsk = current;
	pg_data_t *pgdat;

	daemonize();
	strcpy(tsk->comm, "kreclaimd");
	sigfillset(&tsk->blocked);
	current->flags |= PF_MEMALLOC;

	while (1) {

		/*
		 * We sleep until someone wakes us up from
		 * page_alloc.c::__alloc_pages().
		 */
		interruptible_sleep_on(&kreclaimd_wait);

		/*
		 * Move some pages from the inactive_clean lists to
		 * the free lists, if it is needed.
		 */
		pgdat = pgdat_list;
		do {
			int i;
			for(i = 0; i < MAX_NR_ZONES; i++) {
				zone_t *zone = pgdat->node_zones + i;
				if (!zone->size)
					continue;

				while (zone->free_pages < zone->pages_low) {
					struct page * page;
					page = reclaim_page(zone);
					if (!page)
						break;
					__free_page(page);
				}
			}
			pgdat = pgdat->node_next;
		} while (pgdat);
	}
}


static int __init kswapd_init(void)
{
	printk("Starting kswapd v1.8\n");
	swap_setup();
	kernel_thread(kswapd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	kernel_thread(kreclaimd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	return 0;
}

module_init(kswapd_init)
