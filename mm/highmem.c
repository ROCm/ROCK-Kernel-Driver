/*
 * High memory handling common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * 64-bit physical space. With current x86 CPUs this
 * means up to 64 Gigabytes physical RAM.
 *
 * Rewrote high memory support to move the page cache into
 * high memory. Implemented permanent (schedulable) kmaps
 * based on Linus' idea.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/compiler.h>

#include <linux/kernel_stat.h>

/*
 * Virtual_count is not a pure "count".
 *  0 means that it is not mapped, and has not been mapped
 *    since a TLB flush - it is usable.
 *  1 means that there are no users, but it has been mapped
 *    since the last TLB flush - so we can't use it.
 *  n means that there are (n-1) current users of it.
 */
static int pkmap_count[LAST_PKMAP];
static unsigned int last_pkmap_nr;
static spinlock_t kmap_lock = SPIN_LOCK_UNLOCKED;

pte_t * pkmap_page_table;

static DECLARE_WAIT_QUEUE_HEAD(pkmap_map_wait);

static void flush_all_zero_pkmaps(void)
{
	int i;

	flush_cache_all();

	for (i = 0; i < LAST_PKMAP; i++) {
		struct page *page;

		/*
		 * zero means we don't have anything to do,
		 * >1 means that it is still in use. Only
		 * a count of 1 means that it is free but
		 * needs to be unmapped
		 */
		if (pkmap_count[i] != 1)
			continue;
		pkmap_count[i] = 0;

		/* sanity check */
		if (pte_none(pkmap_page_table[i]))
			BUG();

		/*
		 * Don't need an atomic fetch-and-clear op here;
		 * no-one has the page mapped, and cannot get at
		 * its virtual address (and hence PTE) without first
		 * getting the kmap_lock (which is held here).
		 * So no dangers, even with speculative execution.
		 */
		page = pte_page(pkmap_page_table[i]);
		pte_clear(&pkmap_page_table[i]);

		page->virtual = NULL;
	}
	flush_tlb_all();
}

static inline unsigned long map_new_virtual(struct page *page)
{
	unsigned long vaddr;
	int count;

start:
	count = LAST_PKMAP;
	/* Find an empty entry */
	for (;;) {
		last_pkmap_nr = (last_pkmap_nr + 1) & LAST_PKMAP_MASK;
		if (!last_pkmap_nr) {
			flush_all_zero_pkmaps();
			count = LAST_PKMAP;
		}
		if (!pkmap_count[last_pkmap_nr])
			break;	/* Found a usable entry */
		if (--count)
			continue;

		/*
		 * Sleep for somebody else to unmap their entries
		 */
		{
			DECLARE_WAITQUEUE(wait, current);

			current->state = TASK_UNINTERRUPTIBLE;
			add_wait_queue(&pkmap_map_wait, &wait);
			spin_unlock(&kmap_lock);
			schedule();
			remove_wait_queue(&pkmap_map_wait, &wait);
			spin_lock(&kmap_lock);

			/* Somebody else might have mapped it while we slept */
			if (page->virtual)
				return (unsigned long) page->virtual;

			/* Re-start */
			goto start;
		}
	}
	vaddr = PKMAP_ADDR(last_pkmap_nr);
	set_pte(&(pkmap_page_table[last_pkmap_nr]), mk_pte(page, kmap_prot));

	pkmap_count[last_pkmap_nr] = 1;
	page->virtual = (void *) vaddr;

	return vaddr;
}

void *kmap_high(struct page *page)
{
	unsigned long vaddr;

	/*
	 * For highmem pages, we can't trust "virtual" until
	 * after we have the lock.
	 *
	 * We cannot call this from interrupts, as it may block
	 */
	spin_lock(&kmap_lock);
	vaddr = (unsigned long) page->virtual;
	if (!vaddr)
		vaddr = map_new_virtual(page);
	pkmap_count[PKMAP_NR(vaddr)]++;
	if (pkmap_count[PKMAP_NR(vaddr)] < 2)
		BUG();
	spin_unlock(&kmap_lock);
	return (void*) vaddr;
}

void kunmap_high(struct page *page)
{
	unsigned long vaddr;
	unsigned long nr;
	int need_wakeup;

	spin_lock(&kmap_lock);
	vaddr = (unsigned long) page->virtual;
	if (!vaddr)
		BUG();
	nr = PKMAP_NR(vaddr);

	/*
	 * A count must never go down to zero
	 * without a TLB flush!
	 */
	need_wakeup = 0;
	switch (--pkmap_count[nr]) {
	case 0:
		BUG();
	case 1:
		/*
		 * Avoid an unnecessary wake_up() function call.
		 * The common case is pkmap_count[] == 1, but
		 * no waiters.
		 * The tasks queued in the wait-queue are guarded
		 * by both the lock in the wait-queue-head and by
		 * the kmap_lock.  As the kmap_lock is held here,
		 * no need for the wait-queue-head's lock.  Simply
		 * test if the queue is empty.
		 */
		need_wakeup = waitqueue_active(&pkmap_map_wait);
	}
	spin_unlock(&kmap_lock);

	/* do wake-up, if needed, race-free outside of the spin lock */
	if (need_wakeup)
		wake_up(&pkmap_map_wait);
}

#define POOL_SIZE 64

/*
 * This lock gets no contention at all, normally.
 */
static spinlock_t emergency_lock = SPIN_LOCK_UNLOCKED;

int nr_emergency_pages;
static LIST_HEAD(emergency_pages);

int nr_emergency_bhs;
static LIST_HEAD(emergency_bhs);

/*
 * Simple bounce buffer support for highmem pages. Depending on the
 * queue gfp mask set, *to may or may not be a highmem page. kmap it
 * always, it will do the Right Thing
 */
static inline void copy_to_high_bio_irq(struct bio *to, struct bio *from)
{
	unsigned char *vto, *vfrom;
	unsigned long flags;
	struct bio_vec *tovec, *fromvec;
	int i;

	bio_for_each_segment(tovec, to, i) {
		fromvec = &from->bi_io_vec[i];

		/*
		 * not bounced
		 */
		if (tovec->bv_page == fromvec->bv_page)
			continue;

		vfrom = page_address(fromvec->bv_page) + fromvec->bv_offset;

		__save_flags(flags);
		__cli();
		vto = kmap_atomic(tovec->bv_page, KM_BOUNCE_READ);
		memcpy(vto + tovec->bv_offset, vfrom, to->bi_size);
		kunmap_atomic(vto, KM_BOUNCE_READ);
		__restore_flags(flags);
	}
}

static __init int init_emergency_pool(void)
{
	struct sysinfo i;
        si_meminfo(&i);
        si_swapinfo(&i);
        
        if (!i.totalhigh)
        	return 0;

	spin_lock_irq(&emergency_lock);
	while (nr_emergency_pages < POOL_SIZE) {
		struct page * page = alloc_page(GFP_ATOMIC);
		if (!page) {
			printk("couldn't refill highmem emergency pages");
			break;
		}
		list_add(&page->list, &emergency_pages);
		nr_emergency_pages++;
	}
	spin_unlock_irq(&emergency_lock);
	printk("allocated %d pages reserved for the highmem bounces\n", nr_emergency_pages);
	return 0;
}

__initcall(init_emergency_pool);

static inline int bounce_end_io (struct bio *bio, int nr_sectors)
{
	struct bio *bio_orig = bio->bi_private;
	struct page *page = bio_page(bio);
	unsigned long flags;
	int ret;

	if (test_bit(BIO_UPTODATE, &bio->bi_flags))
		set_bit(BIO_UPTODATE, &bio_orig->bi_flags);

	ret = bio_orig->bi_end_io(bio_orig, nr_sectors);

	spin_lock_irqsave(&emergency_lock, flags);
	if (nr_emergency_pages >= POOL_SIZE) {
		spin_unlock_irqrestore(&emergency_lock, flags);
		__free_page(page);
	} else {
		/*
		 * We are abusing page->list to manage
		 * the highmem emergency pool:
		 */
		list_add(&page->list, &emergency_pages);
		nr_emergency_pages++;
		spin_unlock_irqrestore(&emergency_lock, flags);
	}

	bio_put(bio);
	return ret;
}

static int bounce_end_io_write(struct bio *bio, int nr_sectors)
{
	return bounce_end_io(bio, nr_sectors);
}

static int bounce_end_io_read (struct bio *bio, int nr_sectors)
{
	struct bio *bio_orig = bio->bi_private;

	if (test_bit(BIO_UPTODATE, &bio->bi_flags))
		copy_to_high_bio_irq(bio_orig, bio);

	return bounce_end_io(bio, nr_sectors);
}

struct page *alloc_bounce_page(int gfp_mask)
{
	struct list_head *tmp;
	struct page *page;

	page = alloc_page(gfp_mask);
	if (page)
		return page;
	/*
	 * No luck. First, kick the VM so it doesnt idle around while
	 * we are using up our emergency rations.
	 */
	wakeup_bdflush();

repeat_alloc:
	/*
	 * Try to allocate from the emergency pool.
	 */
	tmp = &emergency_pages;
	spin_lock_irq(&emergency_lock);
	if (!list_empty(tmp)) {
		page = list_entry(tmp->next, struct page, list);
		list_del(tmp->next);
		nr_emergency_pages--;
	}
	spin_unlock_irq(&emergency_lock);
	if (page)
		return page;

	/* we need to wait I/O completion */
	run_task_queue(&tq_disk);

	current->policy |= SCHED_YIELD;
	__set_current_state(TASK_RUNNING);
	schedule();
	goto repeat_alloc;
}

void create_bounce(unsigned long pfn, struct bio **bio_orig)
{
	struct page *page;
	struct bio *bio = NULL;
	int i, rw = bio_data_dir(*bio_orig);
	struct bio_vec *to, *from;

	BUG_ON((*bio_orig)->bi_idx);

	bio_for_each_segment(from, *bio_orig, i) {
		page = from->bv_page;

		/*
		 * is destination page below bounce pfn?
		 */
		if ((page - page->zone->zone_mem_map) + (page->zone->zone_start_paddr >> PAGE_SHIFT) < pfn)
			continue;

		/*
		 * irk, bounce it
		 */
		if (!bio)
			bio = bio_alloc(GFP_NOHIGHIO, (*bio_orig)->bi_vcnt);

		to = &bio->bi_io_vec[i];

		to->bv_page = alloc_bounce_page(GFP_NOHIGHIO);
		to->bv_len = from->bv_len;
		to->bv_offset = from->bv_offset;

		if (rw & WRITE) {
			char *vto, *vfrom;

			vto = page_address(to->bv_page) + to->bv_offset;
			vfrom = kmap(from->bv_page);
			memcpy(vto, vfrom + from->bv_offset, to->bv_len);
			kunmap(to->bv_page);
		}
	}

	/*
	 * no pages bounced
	 */
	if (!bio)
		return;

	/*
	 * at least one page was bounced, fill in possible non-highmem
	 * pages
	 */
	bio_for_each_segment(from, *bio_orig, i) {
		to = &bio->bi_io_vec[i];
		if (!to->bv_page) {
			to->bv_page = from->bv_page;
			to->bv_len = from->bv_len;
			to->bv_offset = to->bv_offset;
		}
	}

	bio->bi_dev = (*bio_orig)->bi_dev;
	bio->bi_sector = (*bio_orig)->bi_sector;
	bio->bi_rw = (*bio_orig)->bi_rw;

	bio->bi_vcnt = (*bio_orig)->bi_vcnt;
	bio->bi_idx = 0;
	bio->bi_size = (*bio_orig)->bi_size;

	if (rw & WRITE)
		bio->bi_end_io = bounce_end_io_write;
	else
		bio->bi_end_io = bounce_end_io_read;

	bio->bi_private = *bio_orig;
	*bio_orig = bio;
}
