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
		pte_t pte;
		/*
		 * zero means we don't have anything to do,
		 * >1 means that it is still in use. Only
		 * a count of 1 means that it is free but
		 * needs to be unmapped
		 */
		if (pkmap_count[i] != 1)
			continue;
		pkmap_count[i] = 0;
		pte = ptep_get_and_clear(pkmap_page_table+i);
		if (pte_none(pte))
			BUG();
		page = pte_page(pte);
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

	spin_lock(&kmap_lock);
	vaddr = (unsigned long) page->virtual;
	if (!vaddr)
		BUG();
	nr = PKMAP_NR(vaddr);

	/*
	 * A count must never go down to zero
	 * without a TLB flush!
	 */
	switch (--pkmap_count[nr]) {
	case 0:
		BUG();
	case 1:
		wake_up(&pkmap_map_wait);
	}
	spin_unlock(&kmap_lock);
}

/*
 * Simple bounce buffer support for highmem pages.
 * This will be moved to the block layer in 2.5.
 */

static inline void copy_from_high_bh (struct buffer_head *to,
			 struct buffer_head *from)
{
	struct page *p_from;
	char *vfrom;
	unsigned long flags;

	p_from = from->b_page;

	/*
	 * Since this can be executed from IRQ context, reentrance
	 * on the same CPU must be avoided:
	 */
	__save_flags(flags);
	__cli();
	vfrom = kmap_atomic(p_from, KM_BOUNCE_WRITE);
	memcpy(to->b_data, vfrom + bh_offset(from), to->b_size);
	kunmap_atomic(vfrom, KM_BOUNCE_WRITE);
	__restore_flags(flags);
}

static inline void copy_to_high_bh_irq (struct buffer_head *to,
			 struct buffer_head *from)
{
	struct page *p_to;
	char *vto;
	unsigned long flags;

	p_to = to->b_page;
	__save_flags(flags);
	__cli();
	vto = kmap_atomic(p_to, KM_BOUNCE_READ);
	memcpy(vto + bh_offset(to), from->b_data, to->b_size);
	kunmap_atomic(vto, KM_BOUNCE_READ);
	__restore_flags(flags);
}

static inline void bounce_end_io (struct buffer_head *bh, int uptodate)
{
	struct buffer_head *bh_orig = (struct buffer_head *)(bh->b_private);

	bh_orig->b_end_io(bh_orig, uptodate);
	__free_page(bh->b_page);
	kmem_cache_free(bh_cachep, bh);
}

static void bounce_end_io_write (struct buffer_head *bh, int uptodate)
{
	bounce_end_io(bh, uptodate);
}

static void bounce_end_io_read (struct buffer_head *bh, int uptodate)
{
	struct buffer_head *bh_orig = (struct buffer_head *)(bh->b_private);

	if (uptodate)
		copy_to_high_bh_irq(bh_orig, bh);
	bounce_end_io(bh, uptodate);
}

struct buffer_head * create_bounce(int rw, struct buffer_head * bh_orig)
{
	struct page *page;
	struct buffer_head *bh;

	if (!PageHighMem(bh_orig->b_page))
		return bh_orig;

repeat_bh:
	bh = kmem_cache_alloc(bh_cachep, SLAB_BUFFER);
	if (!bh) {
		wakeup_bdflush(1);  /* Sets task->state to TASK_RUNNING */
		goto repeat_bh;
	}
	/*
	 * This is wasteful for 1k buffers, but this is a stopgap measure
	 * and we are being ineffective anyway. This approach simplifies
	 * things immensly. On boxes with more than 4GB RAM this should
	 * not be an issue anyway.
	 */
repeat_page:
	page = alloc_page(GFP_BUFFER);
	if (!page) {
		wakeup_bdflush(1);  /* Sets task->state to TASK_RUNNING */
		goto repeat_page;
	}
	set_bh_page(bh, page, 0);

	bh->b_next = NULL;
	bh->b_blocknr = bh_orig->b_blocknr;
	bh->b_size = bh_orig->b_size;
	bh->b_list = -1;
	bh->b_dev = bh_orig->b_dev;
	bh->b_count = bh_orig->b_count;
	bh->b_rdev = bh_orig->b_rdev;
	bh->b_state = bh_orig->b_state;
	bh->b_flushtime = jiffies;
	bh->b_next_free = NULL;
	bh->b_prev_free = NULL;
	/* bh->b_this_page */
	bh->b_reqnext = NULL;
	bh->b_pprev = NULL;
	/* bh->b_page */
	if (rw == WRITE) {
		bh->b_end_io = bounce_end_io_write;
		copy_from_high_bh(bh, bh_orig);
	} else
		bh->b_end_io = bounce_end_io_read;
	bh->b_private = (void *)bh_orig;
	bh->b_rsector = bh_orig->b_rsector;
	memset(&bh->b_wait, -1, sizeof(bh->b_wait));

	return bh;
}

