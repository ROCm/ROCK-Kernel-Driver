/*
 * mm/page-writeback.c.
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains functions related to writing back dirty pages at the
 * address_space level.
 *
 * 10Apr2002	akpm@zip.com.au
 *		Initial version
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/backing-dev.h>

/*
 * The maximum number of pages to writeout in a single bdflush/kupdate
 * operation.  We do this so we don't hold I_LOCK against an inode for
 * enormous amounts of time, which would block a userspace task which has
 * been forced to throttle against that inode.  Also, the code reevaluates
 * the dirty each time it has written this many pages.
 */
#define MAX_WRITEBACK_PAGES	1024

/*
 * After a CPU has dirtied this many pages, balance_dirty_pages_ratelimited
 * will look to see if it needs to force writeback or throttling.  Probably
 * should be scaled by memory size.
 */
#define RATELIMIT_PAGES		1000

/*
 * When balance_dirty_pages decides that the caller needs to perform some
 * non-background writeback, this is how many pages it will attempt to write.
 * It should be somewhat larger than RATELIMIT_PAGES to ensure that reasonably
 * large amounts of I/O are submitted.
 */
#define SYNC_WRITEBACK_PAGES	1500


/* The following parameters are exported via /proc/sys/vm */

/*
 * Dirty memory thresholds, in percentages
 */

/*
 * Start background writeback (via pdflush) at this level
 */
int dirty_background_ratio = 40;

/*
 * The generator of dirty data starts async writeback at this level
 */
int dirty_async_ratio = 50;

/*
 * The generator of dirty data performs sync writeout at this level
 */
int dirty_sync_ratio = 60;

/*
 * The interval between `kupdate'-style writebacks, in centiseconds
 * (hundredths of a second)
 */
int dirty_writeback_centisecs = 5 * 100;

/*
 * The longest amount of time for which data is allowed to remain dirty
 */
int dirty_expire_centisecs = 30 * 100;

/* End of sysctl-exported parameters */


static void background_writeout(unsigned long _min_pages);

/*
 * balance_dirty_pages() must be called by processes which are
 * generating dirty data.  It looks at the number of dirty pages
 * in the machine and either:
 *
 * - Starts background writeback or
 * - Causes the caller to perform async writeback or
 * - Causes the caller to perform synchronous writeback, then
 *   tells a pdflush thread to perform more writeback or
 * - Does nothing at all.
 *
 * balance_dirty_pages() can sleep.
 *
 * FIXME: WB_SYNC_LAST doesn't actually work.  It waits on the last dirty
 * inode on the superblock list.  It should wait when nr_to_write is
 * exhausted.  Doesn't seem to matter.
 */
void balance_dirty_pages(struct address_space *mapping)
{
	const int tot = nr_free_pagecache_pages();
	struct page_state ps;
	int background_thresh, async_thresh, sync_thresh;
	unsigned long dirty_and_writeback;

	get_page_state(&ps);
	dirty_and_writeback = ps.nr_dirty + ps.nr_writeback;

	background_thresh = (dirty_background_ratio * tot) / 100;
	async_thresh = (dirty_async_ratio * tot) / 100;
	sync_thresh = (dirty_sync_ratio * tot) / 100;

	if (dirty_and_writeback > sync_thresh) {
		int nr_to_write = SYNC_WRITEBACK_PAGES;

		writeback_unlocked_inodes(&nr_to_write, WB_SYNC_LAST, NULL);
		get_page_state(&ps);
	} else if (dirty_and_writeback > async_thresh) {
		int nr_to_write = SYNC_WRITEBACK_PAGES;

		writeback_unlocked_inodes(&nr_to_write, WB_SYNC_NONE, NULL);
		get_page_state(&ps);
	}

	if (!writeback_in_progress(mapping->backing_dev_info) &&
				ps.nr_dirty > background_thresh)
		pdflush_operation(background_writeout, 0);
}

/**
 * balance_dirty_pages_ratelimited - balance dirty memory state
 * @mapping - address_space which was dirtied
 *
 * Processes which are dirtying memory should call in here once for each page
 * which was newly dirtied.  The function will periodically check the system's
 * dirty state and will initiate writeback if needed.
 *
 * balance_dirty_pages_ratelimited() may sleep.
 */
void balance_dirty_pages_ratelimited(struct address_space *mapping)
{
	static struct rate_limit_struct {
		int count;
	} ____cacheline_aligned ratelimits[NR_CPUS];
	int cpu;

	cpu = get_cpu();
	if (ratelimits[cpu].count++ >= RATELIMIT_PAGES) {
		ratelimits[cpu].count = 0;
		put_cpu();
		balance_dirty_pages(mapping);
		return;
	}
	put_cpu();
}

/*
 * writeback at least _min_pages, and keep writing until the amount of dirty
 * memory is less than the background threshold, or until we're all clean.
 */
static void background_writeout(unsigned long _min_pages)
{
	const int tot = nr_free_pagecache_pages();
	const int background_thresh = (dirty_background_ratio * tot) / 100;
	long min_pages = _min_pages;
	int nr_to_write;

	do {
		struct page_state ps;

		get_page_state(&ps);
		if (ps.nr_dirty < background_thresh && min_pages <= 0)
			break;
		nr_to_write = MAX_WRITEBACK_PAGES;
		writeback_unlocked_inodes(&nr_to_write, WB_SYNC_NONE, NULL);
		min_pages -= MAX_WRITEBACK_PAGES - nr_to_write;
	} while (nr_to_write <= 0);
	blk_run_queues();
}

/*
 * Start heavy writeback of everything.
 */
void wakeup_bdflush(void)
{
	struct page_state ps;

	get_page_state(&ps);
	pdflush_operation(background_writeout, ps.nr_dirty);
}

static struct timer_list wb_timer;

/*
 * Periodic writeback of "old" data.
 *
 * Define "old": the first time one of an inode's pages is dirtied, we mark the
 * dirtying-time in the inode's address_space.  So this periodic writeback code
 * just walks the superblock inode list, writing back any inodes which are
 * older than a specific point in time.
 *
 * Try to run once per dirty_writeback_centisecs.  But if a writeback event
 * takes longer than a dirty_writeback_centisecs interval, then leave a
 * one-second gap.
 *
 * older_than_this takes precedence over nr_to_write.  So we'll only write back
 * all dirty pages if they are all attached to "old" mappings.
 */
static void wb_kupdate(unsigned long arg)
{
	unsigned long oldest_jif;
	unsigned long start_jif;
	unsigned long next_jif;
	struct page_state ps;
	int nr_to_write;

	sync_supers();
	get_page_state(&ps);

	oldest_jif = jiffies - (dirty_expire_centisecs * HZ) / 100;
	start_jif = jiffies;
	next_jif = start_jif + (dirty_writeback_centisecs * HZ) / 100;
	nr_to_write = ps.nr_dirty;
	writeback_unlocked_inodes(&nr_to_write, WB_SYNC_NONE, &oldest_jif);
	blk_run_queues();
	yield();

	if (time_before(next_jif, jiffies + HZ))
		next_jif = jiffies + HZ;
	mod_timer(&wb_timer, next_jif);
}

static void wb_timer_fn(unsigned long unused)
{
	if (pdflush_operation(wb_kupdate, 0) < 0)
		mod_timer(&wb_timer, jiffies + HZ); /* delay 1 second */

}

static int __init wb_timer_init(void)
{
	init_timer(&wb_timer);
	wb_timer.expires = jiffies + (dirty_writeback_centisecs * HZ) / 100;
	wb_timer.data = 0;
	wb_timer.function = wb_timer_fn;
	add_timer(&wb_timer);
	return 0;
}
module_init(wb_timer_init);

/*
 * A library function, which implements the vm_writeback a_op.  It's fairly
 * lame at this time.  The idea is: the VM wants to liberate this page,
 * so we pass the page to the address_space and give the fs the opportunity
 * to write out lots of pages around this one.  It allows extent-based
 * filesytems to do intelligent things.  It lets delayed-allocate filesystems
 * perform better file layout.  It lets the address_space opportunistically
 * write back disk-contiguous pages which are in other zones.
 *
 * FIXME: the VM wants to start I/O against *this* page.  Because its zone
 * is under pressure.  But this function may start writeout against a
 * totally different set of pages.  Unlikely to be a huge problem, but if it
 * is, we could just writepage the page if it is still (PageDirty &&
 * !PageWriteback) (See below).
 *
 * Another option is to just reposition page->mapping->dirty_pages so we
 * *know* that the page will be written.  That will work fine, but seems
 * unpleasant.  (If the page is not for-sure on ->dirty_pages we're dead).
 * Plus it assumes that the address_space is performing writeback in
 * ->dirty_pages order.
 *
 * So.  The proper fix is to leave the page locked-and-dirty and to pass
 * it all the way down.
 */
int generic_vm_writeback(struct page *page, int *nr_to_write)
{
	struct inode *inode = page->mapping->host;

	/*
	 * We don't own this inode, and we don't want the address_space
	 * vanishing while writeback is walking its pages.
	 */
	inode = igrab(inode);
	unlock_page(page);

	if (inode) {
		do_writepages(inode->i_mapping, nr_to_write);

		/*
		 * This iput() will internally call ext2_discard_prealloc(),
		 * which is rather bogus.  But there is no other way of
		 * dropping our ref to the inode.  However, there's no harm
		 * in dropping the prealloc, because there probably isn't any.
		 * Just a waste of cycles.
		 */
		iput(inode);
#if 0
		if (!PageWriteback(page) && PageDirty(page)) {
			lock_page(page);
			if (!PageWriteback(page) && TestClearPageDirty(page))
				page->mapping->a_ops->writepage(page);
			else
				unlock_page(page);
		}
#endif
	}
	return 0;
}
EXPORT_SYMBOL(generic_vm_writeback);

/**
 * generic_writepages - walk the list of dirty pages of the given
 * address space and writepage() all of them.
 * 
 * @mapping: address space structure to write
 * @nr_to_write: subtract the number of written pages from *@nr_to_write
 *
 * This is a library function, which implements the writepages()
 * address_space_operation.
 *
 * (The next two paragraphs refer to code which isn't here yet, but they
 *  explain the presence of address_space.io_pages)
 *
 * Pages can be moved from clean_pages or locked_pages onto dirty_pages
 * at any time - it's not possible to lock against that.  So pages which
 * have already been added to a BIO may magically reappear on the dirty_pages
 * list.  And generic_writepages() will again try to lock those pages.
 * But I/O has not yet been started against the page.  Thus deadlock.
 *
 * To avoid this, the entire contents of the dirty_pages list are moved
 * onto io_pages up-front.  We then walk io_pages, locking the
 * pages and submitting them for I/O, moving them to locked_pages.
 *
 * This has the added benefit of preventing a livelock which would otherwise
 * occur if pages are being dirtied faster than we can write them out.
 *
 * If a page is already under I/O, generic_writepages() skips it, even
 * if it's dirty.  This is desirable behaviour for memory-cleaning writeback,
 * but it is INCORRECT for data-integrity system calls such as fsync().  fsync()
 * and msync() need to guarentee that all the data which was dirty at the time
 * the call was made get new I/O started against them.  The way to do this is
 * to run filemap_fdatawait() before calling filemap_fdatawrite().
 *
 * It's fairly rare for PageWriteback pages to be on ->dirty_pages.  It
 * means that someone redirtied the page while it was under I/O.
 */
int generic_writepages(struct address_space *mapping, int *nr_to_write)
{
	int (*writepage)(struct page *) = mapping->a_ops->writepage;
	int ret = 0;
	int done = 0;
	int err;

	write_lock(&mapping->page_lock);

	list_splice(&mapping->dirty_pages, &mapping->io_pages);
	INIT_LIST_HEAD(&mapping->dirty_pages);

        while (!list_empty(&mapping->io_pages) && !done) {
		struct page *page = list_entry(mapping->io_pages.prev,
					struct page, list);
		list_del(&page->list);
		if (PageWriteback(page)) {
			if (PageDirty(page)) {
				list_add(&page->list, &mapping->dirty_pages);
				continue;
			}
			list_add(&page->list, &mapping->locked_pages);
			continue;
		}
		if (!PageDirty(page)) {
			list_add(&page->list, &mapping->clean_pages);
			continue;
		}
		list_add(&page->list, &mapping->locked_pages);
		page_cache_get(page);
		write_unlock(&mapping->page_lock);
		lock_page(page);

		/* It may have been removed from swapcache: check ->mapping */
		if (page->mapping && !PageWriteback(page) &&
					TestClearPageDirty(page)) {
			/* FIXME: batch this up */
			if (!PageActive(page) && PageLRU(page)) {
				spin_lock(&pagemap_lru_lock);
				if (!PageActive(page) && PageLRU(page)) {
					list_del(&page->lru);
					list_add(&page->lru, &inactive_list);
				}
				spin_unlock(&pagemap_lru_lock);
			}
			err = writepage(page);
			if (!ret)
				ret = err;
			if (nr_to_write && --(*nr_to_write) <= 0)
				done = 1;
		} else {
			unlock_page(page);
		}

		page_cache_release(page);
		write_lock(&mapping->page_lock);
	}
	if (!list_empty(&mapping->io_pages)) {
		/*
		 * Put the rest back, in the correct order.
		 */
		list_splice(&mapping->io_pages, mapping->dirty_pages.prev);
		INIT_LIST_HEAD(&mapping->io_pages);
	}
	write_unlock(&mapping->page_lock);
	return ret;
}
EXPORT_SYMBOL(generic_writepages);

int do_writepages(struct address_space *mapping, int *nr_to_write)
{
	if (mapping->a_ops->writepages)
		return mapping->a_ops->writepages(mapping, nr_to_write);
	return generic_writepages(mapping, nr_to_write);
}

/**
 * write_one_page - write out a single page and optionally wait on I/O
 *
 * @page - the page to write
 * @wait - if true, wait on writeout
 *
 * The page must be locked by the caller and will be unlocked upon return.
 *
 * write_one_page() returns a negative error code if I/O failed.
 */
int write_one_page(struct page *page, int wait)
{
	struct address_space *mapping = page->mapping;
	int ret = 0;

	BUG_ON(!PageLocked(page));

	if (wait && PageWriteback(page))
		wait_on_page_writeback(page);

	write_lock(&mapping->page_lock);
	list_del(&page->list);
	if (TestClearPageDirty(page)) {
		list_add(&page->list, &mapping->locked_pages);
		page_cache_get(page);
		write_unlock(&mapping->page_lock);
		ret = mapping->a_ops->writepage(page);
		if (ret == 0 && wait) {
			wait_on_page_writeback(page);
			if (PageError(page))
				ret = -EIO;
		}
		page_cache_release(page);
	} else {
		list_add(&page->list, &mapping->clean_pages);
		write_unlock(&mapping->page_lock);
		unlock_page(page);
	}
	return ret;
}
EXPORT_SYMBOL(write_one_page);

/*
 * Add a page to the dirty page list.
 *
 * It is a sad fact of life that this function is called from several places
 * deeply under spinlocking.  It may not sleep.
 *
 * If the page has buffers, the uptodate buffers are set dirty, to preserve
 * dirty-state coherency between the page and the buffers.  It the page does
 * not have buffers then when they are later attached they will all be set
 * dirty.
 *
 * The buffers are dirtied before the page is dirtied.  There's a small race
 * window in which a writepage caller may see the page cleanness but not the
 * buffer dirtiness.  That's fine.  If this code were to set the page dirty
 * before the buffers, a concurrent writepage caller could clear the page dirty
 * bit, see a bunch of clean buffers and we'd end up with dirty buffers/clean
 * page on the dirty page list.
 *
 * There is also a small window where the page is dirty, and not on dirty_pages.
 * Also a possibility that by the time the page is added to dirty_pages, it has
 * been set clean.  The page lists are somewhat approximate in this regard.
 * It's better to have clean pages accidentally attached to dirty_pages than to
 * leave dirty pages attached to clean_pages.
 *
 * We use private_lock to lock against try_to_free_buffers while using the
 * page's buffer list.  Also use this to protect against clean buffers being
 * added to the page after it was set dirty.
 *
 * FIXME: may need to call ->reservepage here as well.  That's rather up to the
 * address_space though.
 *
 * For now, we treat swapper_space specially.  It doesn't use the normal
 * block a_ops.
 *
 * FIXME: this should move over to fs/buffer.c - buffer_heads have no business in mm/
 */
#include <linux/buffer_head.h>
int __set_page_dirty_buffers(struct page *page)
{
	struct address_space * const mapping = page->mapping;
	int ret = 0;

	if (mapping == NULL) {
		SetPageDirty(page);
		goto out;
	}

	if (!PageUptodate(page))
		buffer_error();

	spin_lock(&mapping->private_lock);

	if (page_has_buffers(page)) {
		struct buffer_head *head = page_buffers(page);
		struct buffer_head *bh = head;

		do {
			if (buffer_uptodate(bh))
				set_buffer_dirty(bh);
			else
				buffer_error();
			bh = bh->b_this_page;
		} while (bh != head);
	}

	if (!TestSetPageDirty(page)) {
		write_lock(&mapping->page_lock);
		list_del(&page->list);
		list_add(&page->list, &mapping->dirty_pages);
		write_unlock(&mapping->page_lock);
		__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
	}
	
	spin_unlock(&mapping->private_lock);
out:
	return ret;
}
EXPORT_SYMBOL(__set_page_dirty_buffers);

/*
 * For address_spaces which do not use buffers.  Just set the page's dirty bit
 * and move it to the dirty_pages list.  Also perform space reservation if
 * required.
 *
 * __set_page_dirty_nobuffers() may return -ENOSPC.  But if it does, the page
 * is still safe, as long as it actually manages to find some blocks at
 * writeback time.
 *
 * This is also used when a single buffer is being dirtied: we want to set the
 * page dirty in that case, but not all the buffers.  This is a "bottom-up"
 * dirtying, whereas __set_page_dirty_buffers() is a "top-down" dirtying.
 */
int __set_page_dirty_nobuffers(struct page *page)
{
	int ret = 0;

	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page->mapping;

		if (mapping) {
			write_lock(&mapping->page_lock);
			list_del(&page->list);
			list_add(&page->list, &mapping->dirty_pages);
			write_unlock(&mapping->page_lock);
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
	}
	return ret;
}
EXPORT_SYMBOL(__set_page_dirty_nobuffers);
