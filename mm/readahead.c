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
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>

void default_unplug_io_fn(struct backing_dev_info *bdi, struct page *page)
{
}
EXPORT_SYMBOL(default_unplug_io_fn);

struct backing_dev_info default_backing_dev_info = {
	.ra_pages	= (VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE,
	.state		= 0,
	.unplug_io_fn	= default_unplug_io_fn,
};
EXPORT_SYMBOL_GPL(default_backing_dev_info);

/*
 * Initialise a struct file's readahead state
 */
void
file_ra_state_init(struct file_ra_state *ra, struct address_space *mapping)
{
	memset(ra, 0, sizeof(*ra));
	ra->ra_pages = mapping->backing_dev_info->ra_pages;
	ra->average = ra->ra_pages / 2;
}
EXPORT_SYMBOL(file_ra_state_init);

/*
 * Return max readahead size for this inode in number-of-pages.
 */
static inline unsigned long get_max_readahead(struct file_ra_state *ra)
{
	return ra->ra_pages;
}

static inline unsigned long get_min_readahead(struct file_ra_state *ra)
{
	return (VM_MIN_READAHEAD * 1024) / PAGE_CACHE_SIZE;
}

#define list_to_page(head) (list_entry((head)->prev, struct page, lru))

/**
 * read_cache_pages - populate an address space with some pages, and
 * 			start reads against them.
 * @mapping: the address_space
 * @pages: The address of a list_head which contains the target pages.  These
 *   pages have their ->index populated and are otherwise uninitialised.
 * @filler: callback routine for filling a single page.
 * @data: private data for the callback routine.
 *
 * Hides the details of the LRU cache etc from the filesystems.
 */
int read_cache_pages(struct address_space *mapping, struct list_head *pages,
		 int (*filler)(void *, struct page *), void *data)
{
	struct page *page;
	struct pagevec lru_pvec;
	int ret = 0;

	pagevec_init(&lru_pvec, 0);

	while (!list_empty(pages)) {
		page = list_to_page(pages);
		list_del(&page->lru);
		if (add_to_page_cache(page, mapping, page->index, GFP_KERNEL)) {
			page_cache_release(page);
			continue;
		}
		ret = filler(data, page);
		if (!pagevec_add(&lru_pvec, page))
			__pagevec_lru_add(&lru_pvec);
		if (ret) {
			while (!list_empty(pages)) {
				struct page *victim;

				victim = list_to_page(pages);
				list_del(&victim->lru);
				page_cache_release(victim);
			}
			break;
		}
	}
	pagevec_lru_add(&lru_pvec);
	return ret;
}

EXPORT_SYMBOL(read_cache_pages);

static int read_pages(struct address_space *mapping, struct file *filp,
		struct list_head *pages, unsigned nr_pages)
{
	unsigned page_idx;
	struct pagevec lru_pvec;
	int ret = 0;

	if (mapping->a_ops->readpages) {
		ret = mapping->a_ops->readpages(filp, mapping, pages, nr_pages);
		goto out;
	}

	pagevec_init(&lru_pvec, 0);
	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = list_to_page(pages);
		list_del(&page->lru);
		if (!add_to_page_cache(page, mapping,
					page->index, GFP_KERNEL)) {
			mapping->a_ops->readpage(filp, page);
			if (!pagevec_add(&lru_pvec, page))
				__pagevec_lru_add(&lru_pvec);
		} else {
			page_cache_release(page);
		}
	}
	pagevec_lru_add(&lru_pvec);
out:
	return ret;
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
 *              Has the magical value -1UL if readahead has been disabled.
 * prev_page:   The page which the readahead algorithm most-recently inspected.
 *              prev_page is mainly an optimisation: if page_cache_readahead
 *		sees that it is again being called for a page which it just
 *		looked at, it can return immediately without making any state
 *		changes.
 * ahead_start,
 * ahead_size:  Together, these form the "ahead window".
 * ra_pages:	The externally controlled max readahead for this fd.
 *
 * When readahead is in the "maximally shrunk" state (next_size == -1UL),
 * readahead is disabled.  In this state, prev_page and size are used, inside
 * handle_ra_miss(), to detect the resumption of sequential I/O.  Once there
 * has been a decent run of sequential I/O (defined by get_min_readahead),
 * readahead is reenabled.
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
 * After enough misses, readahead is fully disabled. (next_size = -1UL).
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
 *
 * Returns the number of pages which actually had IO started against them.
 */
static inline int
__do_page_cache_readahead(struct address_space *mapping, struct file *filp,
			unsigned long offset, unsigned long nr_to_read)
{
	struct inode *inode = mapping->host;
	struct page *page;
	unsigned long end_index;	/* The last page we want to read */
	LIST_HEAD(page_pool);
	int page_idx;
	int ret = 0;
	loff_t isize = i_size_read(inode);

	if (isize == 0)
		goto out;

 	end_index = ((isize - 1) >> PAGE_CACHE_SHIFT);

	/*
	 * Preallocate as many pages as we will need.
	 */
	spin_lock_irq(&mapping->tree_lock);
	for (page_idx = 0; page_idx < nr_to_read; page_idx++) {
		unsigned long page_offset = offset + page_idx;
		
		if (page_offset > end_index)
			break;

		page = radix_tree_lookup(&mapping->page_tree, page_offset);
		if (page)
			continue;

		spin_unlock_irq(&mapping->tree_lock);
		page = page_cache_alloc_cold(mapping);
		spin_lock_irq(&mapping->tree_lock);
		if (!page)
			break;
		page->index = page_offset;
		list_add(&page->lru, &page_pool);
		ret++;
	}
	spin_unlock_irq(&mapping->tree_lock);

	/*
	 * Now start the IO.  We ignore I/O errors - if the page is not
	 * uptodate then the caller will launch readpage again, and
	 * will then handle the error.
	 */
	if (ret)
		read_pages(mapping, filp, &page_pool, ret);
	BUG_ON(!list_empty(&page_pool));
out:
	return ret;
}

/*
 * Chunk the readahead into 2 megabyte units, so that we don't pin too much
 * memory at once.
 */
int force_page_cache_readahead(struct address_space *mapping, struct file *filp,
		unsigned long offset, unsigned long nr_to_read)
{
	int ret = 0;

	if (unlikely(!mapping->a_ops->readpage && !mapping->a_ops->readpages))
		return -EINVAL;

	while (nr_to_read) {
		int err;

		unsigned long this_chunk = (2 * 1024 * 1024) / PAGE_CACHE_SIZE;

		if (this_chunk > nr_to_read)
			this_chunk = nr_to_read;
		err = __do_page_cache_readahead(mapping, filp,
						offset, this_chunk);
		if (err < 0) {
			ret = err;
			break;
		}
		ret += err;
		offset += this_chunk;
		nr_to_read -= this_chunk;
	}
	return ret;
}

/*
 * This version skips the IO if the queue is read-congested, and will tell the
 * block layer to abandon the readahead if request allocation would block.
 *
 * force_page_cache_readahead() will ignore queue congestion and will block on
 * request queues.
 */
int do_page_cache_readahead(struct address_space *mapping, struct file *filp,
			unsigned long offset, unsigned long nr_to_read)
{
	if (!bdi_read_congested(mapping->backing_dev_info))
		return __do_page_cache_readahead(mapping, filp,
						offset, nr_to_read);
	return 0;
}

/*
 * Check how effective readahead is being.  If the amount of started IO is
 * less than expected then the file is partly or fully in pagecache and
 * readahead isn't helping.  Shrink the window.
 *
 * But don't shrink it too much - the application may read the same page
 * occasionally.
 */
static inline void
check_ra_success(struct file_ra_state *ra, pgoff_t attempt,
			pgoff_t actual, pgoff_t orig_next_size)
{
	if (actual == 0) {
		if (orig_next_size > 1) {
			ra->next_size = orig_next_size - 1;
			if (ra->ahead_size)
				ra->ahead_size = ra->next_size;
		} else {
			ra->next_size = -1UL;
			ra->size = 0;
		}
	}
}

/*
 * page_cache_readahead is the main function.  If performs the adaptive
 * readahead window size management and submits the readahead I/O.
 */
void
page_cache_readahead(struct address_space *mapping, struct file_ra_state *ra,
			struct file *filp, unsigned long offset)
{
	unsigned max;
	unsigned orig_next_size;
	unsigned actual;
	int first_access=0;
	unsigned long average;

	/*
	 * Here we detect the case where the application is performing
	 * sub-page sized reads.  We avoid doing extra work and bogusly
	 * perturbing the readahead window expansion logic.
	 * If next_size is zero, this is the very first read for this
	 * file handle, or the window is maximally shrunk.
	 */
	if (offset == ra->prev_page) {
		if (ra->next_size != 0)
			goto out;
	}

	if (ra->next_size == -1UL)
		goto out;	/* Maximally shrunk */

	max = get_max_readahead(ra);
	if (max == 0)
		goto out;	/* No readahead */

	orig_next_size = ra->next_size;

	if (ra->next_size == 0) {
		/*
		 * Special case - first read.
		 * We'll assume it's a whole-file read, and
		 * grow the window fast.
		 */
		first_access=1;
		ra->next_size = max / 2;
		ra->prev_page = offset;
		ra->serial_cnt++;
		goto do_io;
	}

	if (offset == ra->prev_page + 1) {
		if (ra->serial_cnt <= (max * 2))
			ra->serial_cnt++;
	} else {
		/*
		 * to avoid rounding errors, ensure that 'average'
		 * tends towards the value of ra->serial_cnt.
		 */
		average = ra->average;
		if (average < ra->serial_cnt) {
			average++;
		}
		ra->average = (average + ra->serial_cnt) / 2;
		ra->serial_cnt = 1;
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
		 * A miss - lseek, pagefault, pread, etc.  Shrink the readahead
		 * window.
		 */
		ra->next_size -= 2;
	}

	if ((long)ra->next_size > (long)max)
		ra->next_size = max;
	if ((long)ra->next_size <= 0L) {
		ra->next_size = -1UL;
		ra->size = 0;
		goto out;		/* Readahead is off */
	}

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
		if (!first_access) {
			 /* Heuristic: there is a high probability
			  * that around  ra->average number of
			  * pages shall be accessed in the next
			  * current window.
			  */
			average = ra->average;
			if (ra->serial_cnt > average)
				average = (ra->serial_cnt + ra->average + 1) / 2;

			ra->next_size = min(average , (unsigned long)max);
		}
		ra->start = offset;
		ra->size = ra->next_size;
		ra->ahead_start = 0;		/* Invalidate these */
		ra->ahead_size = 0;
		actual = do_page_cache_readahead(mapping, filp, offset,
						 ra->size);
		if(!first_access) {
			/*
			 * do not adjust the readahead window size the first
			 * time, the ahead window might get closed if all
			 * the pages are already in the cache.
			 */
			check_ra_success(ra, ra->size, actual, orig_next_size);
		}
	} else {
		/*
		 * This read request is within the current window.  It may be
		 * time to submit I/O for the ahead window while the
		 * application is about to step into the ahead window.
		 */
		if (ra->ahead_start == 0) {
			/*
			 * If the average io-size is more than maximum
			 * readahead size of the file the io pattern is
			 * sequential. Hence  bring in the readahead window
			 * immediately.
			 * If the average io-size is less than maximum
			 * readahead size of the file the io pattern is
			 * random. Hence don't bother to readahead.
			 */
			average = ra->average;
			if (ra->serial_cnt > average)
				average = (ra->serial_cnt + ra->average + 1) / 2;

			if (average > max) {
				ra->ahead_start = ra->start + ra->size;
				ra->ahead_size = ra->next_size;
				actual = do_page_cache_readahead(mapping, filp,
					ra->ahead_start, ra->ahead_size);
				check_ra_success(ra, ra->ahead_size,
						actual, orig_next_size);
			}
		}
	}
out:
	return;
}


/*
 * handle_ra_miss() is called when it is known that a page which should have
 * been present in the pagecache (we just did some readahead there) was in fact
 * not found.  This will happen if it was evicted by the VM (readahead
 * thrashing) or if the readahead window is maximally shrunk.
 *
 * If the window has been maximally shrunk (next_size == -1UL) then look to see
 * if we are getting misses against sequential file offsets.  If so, and this
 * persists then resume readahead.
 *
 * Otherwise we're thrashing, so shrink the readahead window by three pages.
 * This is because it is grown by two pages on a readahead hit.  Theory being
 * that the readahead window size will stabilise around the maximum level at
 * which there is no thrashing.
 */
void handle_ra_miss(struct address_space *mapping,
		struct file_ra_state *ra, pgoff_t offset)
{
	if (ra->next_size == -1UL) {
		const unsigned long max = get_max_readahead(ra);

		if (offset != ra->prev_page + 1) {
			ra->size = ra->size?ra->size-1:0; /* Not sequential */
		} else {
			ra->size++;			/* A sequential read */
			if (ra->size >= max) {		/* Resume readahead */
				ra->start = offset - max;
				ra->next_size = max;
				ra->size = max;
				ra->ahead_start = 0;
				ra->ahead_size = 0;
				ra->average = max / 2;
			}
		}
		ra->prev_page = offset;
	} else {
		const unsigned long min = get_min_readahead(ra);

		ra->next_size -= 3;
		if (ra->next_size < min)
			ra->next_size = min;
	}
}

/*
 * Given a desired number of PAGE_CACHE_SIZE readahead pages, return a
 * sensible upper limit.
 */
unsigned long max_sane_readahead(unsigned long nr)
{
	unsigned long active;
	unsigned long inactive;
	unsigned long free;

	get_zone_counts(&active, &inactive, &free);
	return min(nr, (inactive + free) / 2);
}
