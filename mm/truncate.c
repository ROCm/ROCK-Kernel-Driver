/*
 * mm/truncate.c - code for taking down pages from address_spaces
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 10Sep2002	akpm@zip.com.au
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/buffer_head.h>	/* grr. try_to_release_page,
				   block_invalidatepage */


static int do_invalidatepage(struct page *page, unsigned long offset)
{
	int (*invalidatepage)(struct page *, unsigned long);
	invalidatepage = page->mapping->a_ops->invalidatepage;
	if (invalidatepage == NULL)
		invalidatepage = block_invalidatepage;
	return (*invalidatepage)(page, offset);
}

static inline void truncate_partial_page(struct page *page, unsigned partial)
{
	memclear_highpage_flush(page, partial, PAGE_CACHE_SIZE-partial);
	if (PagePrivate(page))
		do_invalidatepage(page, partial);
}

/*
 * If truncate cannot remove the fs-private metadata from the page, the page
 * becomes anonymous.  It will be left on the LRU and may even be mapped into
 * user pagetables if we're racing with filemap_nopage().
 */
static void truncate_complete_page(struct page *page)
{
	if (PagePrivate(page))
		do_invalidatepage(page, 0);

	clear_page_dirty(page);
	ClearPageUptodate(page);
	remove_from_page_cache(page);
	page_cache_release(page);
}

/**
 * truncate_inode_pages - truncate *all* the pages from an offset
 * @mapping: mapping to truncate
 * @lstart: offset from which to truncate
 *
 * Truncate the page cache at a set offset, removing the pages that are beyond
 * that offset (and zeroing out partial pages).
 *
 * Truncate takes two passes - the first pass is nonblocking.  It will not
 * block on page locks and it will not block on writeback.  The second pass
 * will wait.  This is to prevent as much IO as possible in the affected region.
 * The first pass will remove most pages, so the search cost of the second pass
 * is low.
 *
 * Called under (and serialised by) inode->i_sem.
 */
void truncate_inode_pages(struct address_space *mapping, loff_t lstart)
{
	const pgoff_t start = (lstart + PAGE_CACHE_SIZE-1) >> PAGE_CACHE_SHIFT;
	const unsigned partial = lstart & (PAGE_CACHE_SIZE - 1);
	struct pagevec pvec;
	pgoff_t next;
	int i;

	pagevec_init(&pvec);
	next = start;
	while (pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			next = page->index + 1;
			if (TestSetPageLocked(page))
				continue;
			if (PageWriteback(page)) {
				unlock_page(page);
				continue;
			}
			truncate_complete_page(page);
			unlock_page(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	}

	if (partial) {
		struct page *page = find_lock_page(mapping, start - 1);
		if (page) {
			wait_on_page_writeback(page);
			truncate_partial_page(page, partial);
			unlock_page(page);
			page_cache_release(page);
		}
	}

	next = start;
	for ( ; ; ) {
		if (!pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
			if (next == start)
				break;
			next = start;
			continue;
		}
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			lock_page(page);
			wait_on_page_writeback(page);
			next = page->index + 1;
			truncate_complete_page(page);
			unlock_page(page);
		}
		pagevec_release(&pvec);
	}
	if (lstart == 0 && mapping->nrpages)
		printk("%s: I goofed!\n", __FUNCTION__);
}

/**
 * invalidate_inode_pages - Invalidate all the unlocked pages of one inode
 * @inode: the inode which pages we want to invalidate
 *
 * This function only removes the unlocked pages, if you want to
 * remove all the pages of one inode, you must call truncate_inode_pages.
 *
 * invalidate_inode_pages() will not block on IO activity. It will not
 * invalidate pages which are dirty, locked, under writeback or mapped into
 * pagetables.
 */
void invalidate_inode_pages(struct address_space *mapping)
{
	struct pagevec pvec;
	pgoff_t next = 0;
	int i;

	pagevec_init(&pvec);
	while (pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			if (TestSetPageLocked(page)) {
				next++;
				continue;
			}
			next = page->index + 1;
			if (PageDirty(page) || PageWriteback(page))
				goto unlock;
			if (PagePrivate(page) && !try_to_release_page(page, 0))
				goto unlock;
			if (page_mapped(page))
				goto unlock;
			truncate_complete_page(page);
unlock:
			unlock_page(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	}
}

/**
 * invalidate_inode_pages2 - remove all unmapped pages from an address_space
 * @mapping - the address_space
 *
 * invalidate_inode_pages2() is like truncate_inode_pages(), except for the case
 * where the page is seen to be mapped into process pagetables.  In that case,
 * the page is marked clean but is left attached to its address_space.
 *
 * FIXME: invalidate_inode_pages2() is probably trivially livelockable.
 */
void invalidate_inode_pages2(struct address_space *mapping)
{
	struct pagevec pvec;
	pgoff_t next = 0;
	int i;

	pagevec_init(&pvec);
	while (!pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			lock_page(page);
			if (page->mapping) {	/* truncate race? */
				wait_on_page_writeback(page);
				next = page->index + 1;
				if (page_mapped(page))
					clear_page_dirty(page);
				else
					truncate_complete_page(page);
			}
			unlock_page(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	}
}
