/*
 * mm/readahead.c - address_space-level file readahead.
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 09Apr2002	akpm@zip.com.au
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>

struct backing_dev_info default_backing_dev_info = {
	ra_pages:	(VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE,
	state:		0,
};

/*
 * Return max readahead size for this inode in number-of-pages.
 */
static inline unsigned long get_max_readahead(struct file *file)
{
	return file->f_ra.ra_pages;
}

static inline unsigned long get_min_readahead(struct file *file)
{
	return (VM_MIN_READAHEAD * 1024) / PAGE_CACHE_SIZE;
}

static int
read_pages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	unsigned page_idx;

	if (mapping->a_ops->readpages)
		return mapping->a_ops->readpages(mapping, pages, nr_pages);

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = list_entry(pages->prev, struct page, list);
		list_del(&page->list);
		if (!add_to_page_cache(page, mapping, page->index))
			mapping->a_ops->readpage(file, page);
		page_cache_release(page);
	}
	return 0;
}

/*
 * Readahead design.
 *
 * The fields in struct file_ra_state represent the most-recently-executed
 * readahead attempt:
 *
 * start:	Page index at which we started the readahead
 * size:	Number of pages in that read
 *              Together, these form the "current window".
 *              Together, start and size represent the `readahead window'.
 * next_size:   The number of pages to read on the next readahead miss.
 * prev_page:   The page which the readahead algorithm most-recently inspected.
 *              prev_page is mainly an optimisation: if page_cache_readahead
 *		sees that it is again being called for a page which it just
 *		looked at, it can return immediately without making any state
 *		changes.
 * ahead_start,
 * ahead_size:  Together, these form the "ahead window".
 *
 * The readahead code manages two windows - the "current" and the "ahead"
 * windows.  The intent is that while the application is walking the pages
 * in the current window, I/O is underway on the ahead window.  When the
 * current window is fully traversed, it is replaced by the ahead window
 * and the ahead window is invalidated.  When this copying happens, the
 * new current window's pages are probably still locked.  When I/O has
 * completed, we submit a new batch of I/O, creating a new ahead window.
 *
 * So:
 *
 *   ----|----------------|----------------|-----
 *       ^start           ^start+size
 *                        ^ahead_start     ^ahead_start+ahead_size
 *
 *         ^ When this page is read, we submit I/O for the
 *           ahead window.
 *
 * A `readahead hit' occurs when a read request is made against a page which is
 * inside the current window.  Hits are good, and the window size (next_size)
 * is grown aggressively when hits occur.  Two pages are added to the next
 * window size on each hit, which will end up doubling the next window size by
 * the time I/O is submitted for it.
 *
 * If readahead hits are more sparse (say, the application is only reading
 * every second page) then the window will build more slowly.
 *
 * On a readahead miss (the application seeked away) the readahead window is
 * shrunk by 25%.  We don't want to drop it too aggressively, because it is a
 * good assumption that an application which has built a good readahead window
 * will continue to perform linear reads.  Either at the new file position, or
 * at the old one after another seek.
 *
 * There is a special-case: if the first page which the application tries to
 * read happens to be the first page of the file, it is assumed that a linear
 * read is about to happen and the window is immediately set to half of the
 * device maximum.
 * 
 * A page request at (start + size) is not a miss at all - it's just a part of
 * sequential file reading.
 *
 * This function is to be called for every page which is read, rather than when
 * it is time to perform readahead.  This is so the readahead algorithm can
 * centrally work out the access patterns.  This could be costly with many tiny
 * read()s, so we specifically optimise for that case with prev_page.
 */

/*
 * do_page_cache_readahead actually reads a chunk of disk.  It allocates all
 * the pages first, then submits them all for I/O. This avoids the very bad
 * behaviour which would occur if page allocations are causing VM writeback.
 * We really don't want to intermingle reads and writes like that.
 */
void do_page_cache_readahead(struct file *file,
			unsigned long offset, unsigned long nr_to_read)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct inode *inode = mapping->host;
	struct page *page;
	unsigned long end_index;	/* The last page we want to read */
	LIST_HEAD(page_pool);
	int page_idx;
	int nr_to_really_read = 0;

	if (inode->i_size == 0)
		return;

 	end_index = ((inode->i_size - 1) >> PAGE_CACHE_SHIFT);

	/*
	 * Preallocate as many pages as we will need.
	 */
	read_lock(&mapping->page_lock);
	for (page_idx = 0; page_idx < nr_to_read; page_idx++) {
		unsigned long page_offset = offset + page_idx;
		
		if (page_offset > end_index)
			break;

		page = radix_tree_lookup(&mapping->page_tree, page_offset);
		if (page)
			continue;

		read_unlock(&mapping->page_lock);
		page = page_cache_alloc(mapping);
		read_lock(&mapping->page_lock);
		if (!page)
			break;
		page->index = page_offset;
		list_add(&page->list, &page_pool);
		nr_to_really_read++;
	}
	read_unlock(&mapping->page_lock);

	/*
	 * Now start the IO.  We ignore I/O errors - if the page is not
	 * uptodate then the caller will launch readpage again, and
	 * will then handle the error.
	 */
	read_pages(file, mapping, &page_pool, nr_to_really_read);
	blk_run_queues();
	BUG_ON(!list_empty(&page_pool));
	return;
}

/*
 * page_cache_readahead is the main function.  If performs the adaptive
 * readahead window size management and submits the readahead I/O.
 */
void page_cache_readahead(struct file *file, unsigned long offset)
{
	struct file_ra_state *ra = &file->f_ra;
	unsigned long max;
	unsigned long min;

	/*
	 * Here we detect the case where the application is performing
	 * sub-page sized reads.  We avoid doing extra work and bogusly
	 * perturbing the readahead window expansion logic.
	 * If next_size is zero, this is the very first read for this
	 * file handle.
	 */
	if (offset == ra->prev_page) {
		if (ra->next_size != 0)
			goto out;
	}

	max = get_max_readahead(file);
	if (max == 0)
		goto out;	/* No readahead */
	min = get_min_readahead(file);

	if (ra->next_size == 0 && offset == 0) {
		/*
		 * Special case - first read from first page.
		 * We'll assume it's a whole-file read, and
		 * grow the window fast.
		 */
		ra->next_size = max / 2;
		goto do_io;
	}

	ra->prev_page = offset;

	if (offset >= ra->start && offset <= (ra->start + ra->size)) {
		/*
		 * A readahead hit.  Either inside the window, or one
		 * page beyond the end.  Expand the next readahead size.
		 */
		ra->next_size += 2;
	} else {
		/*
		 * A miss - lseek, pread, etc.  Shrink the readahead
		 * window by 25%.
		 */
		ra->next_size -= ra->next_size / 4;
		if (ra->next_size < min)
			ra->next_size = min;
	}

	if (ra->next_size > max)
		ra->next_size = max;
	if (ra->next_size < min)
		ra->next_size = min;

	/*
	 * Is this request outside the current window?
	 */
	if (offset < ra->start || offset >= (ra->start + ra->size)) {
		/*
		 * A miss against the current window.  Have we merely
		 * advanced into the ahead window?
		 */
		if (offset == ra->ahead_start) {
			/*
			 * Yes, we have.  The ahead window now becomes
			 * the current window.
			 */
			ra->start = ra->ahead_start;
			ra->size = ra->ahead_size;
			ra->prev_page = ra->start;
			ra->ahead_start = 0;
			ra->ahead_size = 0;
			/*
			 * Control now returns, probably to sleep until I/O
			 * completes against the first ahead page.
			 * When the second page in the old ahead window is
			 * requested, control will return here and more I/O
			 * will be submitted to build the new ahead window.
			 */
			goto out;
		}
do_io:
		/*
		 * This is the "unusual" path.  We come here during
		 * startup or after an lseek.  We invalidate the
		 * ahead window and get some I/O underway for the new
		 * current window.
		 */
		ra->start = offset;
		ra->size = ra->next_size;
		ra->ahead_start = 0;		/* Invalidate these */
		ra->ahead_size = 0;

		do_page_cache_readahead(file, offset, ra->size);
	} else {
		/*
		 * This read request is within the current window.  It
		 * is time to submit I/O for the ahead window while
		 * the application is crunching through the current
		 * window.
		 */
		if (ra->ahead_start == 0) {
			ra->ahead_start = ra->start + ra->size;
			ra->ahead_size = ra->next_size;
			do_page_cache_readahead(file,
					ra->ahead_start, ra->ahead_size);
		}
	}
out:
	return;
}

/*
 * For mmap reads (typically executables) the access pattern is fairly random,
 * but somewhat ascending.  So readaround favours pages beyond the target one.
 * We also boost the window size, as it can easily shrink due to misses.
 */
void page_cache_readaround(struct file *file, unsigned long offset)
{
	const unsigned long min = get_min_readahead(file) * 2;
	unsigned long target;
	unsigned long backward;

	if (file->f_ra.next_size < min)
		file->f_ra.next_size = min;

	target = offset;
	backward = file->f_ra.next_size / 4;

	if (backward > target)
		target = 0;
	else
		target -= backward;
	page_cache_readahead(file, target);
}

/*
 * handle_ra_thrashing() is called when it is known that a page which should
 * have been present (it's inside the readahead window) was in fact evicted by
 * the VM.
 *
 * We shrink the readahead window by three pages.  This is because we grow it
 * by two pages on a readahead hit.  Theory being that the readahead window
 * size will stabilise around the maximum level at which there isn't any
 * thrashing.
 */
void handle_ra_thrashing(struct file *file)
{
	const unsigned long min = get_min_readahead(file);

	file->f_ra.next_size -= 3;
	if (file->f_ra.next_size < min)
		file->f_ra.next_size = min;
}
