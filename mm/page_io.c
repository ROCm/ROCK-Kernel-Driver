/*
 *  linux/mm/page_io.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, 
 *  Asynchronous swapping added 30.12.95. Stephen Tweedie
 *  Removed race in async swapping. 14.4.1996. Bruno Haible
 *  Add swap of shared pages through the page cache. 20.2.1998. Stephen Tweedie
 *  Always use brw_page, life becomes simpler. 12 May 1998 Eric Biederman
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/swapops.h>
#include <linux/buffer_head.h>	/* for block_sync_page() */
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <asm/pgtable.h>

static struct bio *
get_swap_bio(int gfp_flags, struct page *page, bio_end_io_t end_io)
{
	struct bio *bio;

	bio = bio_alloc(gfp_flags, 1);
	if (bio) {
		struct swap_info_struct *sis;
		swp_entry_t entry;

		BUG_ON(!PageSwapCache(page));
		entry.val = page->index;
		sis = get_swap_info_struct(swp_type(entry));

		bio->bi_sector = map_swap_page(sis, swp_offset(entry)) *
					(PAGE_SIZE >> 9);
		bio->bi_bdev = sis->bdev;
		bio->bi_io_vec[0].bv_page = page;
		bio->bi_io_vec[0].bv_len = PAGE_SIZE;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_idx = 0;
		bio->bi_size = PAGE_SIZE;
		bio->bi_end_io = end_io;
	}
	return bio;
}

static int end_swap_bio_write(struct bio *bio, unsigned int bytes_done, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (bio->bi_size)
		return 1;

	if (!uptodate)
		SetPageError(page);
	end_page_writeback(page);
	bio_put(bio);
	return 0;
}

static int end_swap_bio_read(struct bio *bio, unsigned int bytes_done, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (bio->bi_size)
		return 1;

	if (!uptodate) {
		SetPageError(page);
		ClearPageUptodate(page);
	} else {
		SetPageUptodate(page);
	}
	unlock_page(page);
	bio_put(bio);
	return 0;
}

/*
 * We may have stale swap cache pages in memory: notice
 * them here and get rid of the unnecessary final write.
 */
int swap_writepage(struct page *page, struct writeback_control *wbc)
{
	struct bio *bio;
	int ret = 0;

	if (remove_exclusive_swap_page(page)) {
		unlock_page(page);
		goto out;
	}
	bio = get_swap_bio(GFP_NOIO, page, end_swap_bio_write);
	if (bio == NULL) {
		set_page_dirty(page);
		unlock_page(page);
		ret = -ENOMEM;
		goto out;
	}
	inc_page_state(pswpout);
	set_page_writeback(page);
	unlock_page(page);
	submit_bio(WRITE, bio);
out:
	return ret;
}

int swap_readpage(struct file *file, struct page *page)
{
	struct bio *bio;
	int ret = 0;

	BUG_ON(!PageLocked(page));
	ClearPageUptodate(page);
	bio = get_swap_bio(GFP_KERNEL, page, end_swap_bio_read);
	if (bio == NULL) {
		unlock_page(page);
		ret = -ENOMEM;
		goto out;
	}
	inc_page_state(pswpin);
	submit_bio(READ, bio);
out:
	return ret;
}

struct address_space_operations swap_aops = {
	.writepage	= swap_writepage,
	.readpage	= swap_readpage,
	.sync_page	= block_sync_page,
	.set_page_dirty	= __set_page_dirty_nobuffers,
};

#if defined(CONFIG_SOFTWARE_SUSPEND) || defined(CONFIG_PM_DISK)

/*
 * A scruffy utility function to read or write an arbitrary swap page
 * and wait on the I/O.  The caller must have a ref on the page.
 */
int rw_swap_page_sync(int rw, swp_entry_t entry, struct page *page)
{
	int ret;
	struct writeback_control swap_wbc = {
		.sync_mode = WB_SYNC_ALL,
	};

	lock_page(page);

	BUG_ON(page->mapping);
	ret = add_to_page_cache(page, &swapper_space,
				entry.val, GFP_NOIO|__GFP_NOFAIL);
	if (ret) {
		unlock_page(page);
		goto out;
	}

	/*
	 * get one more reference to make page non-exclusive so
	 * remove_exclusive_swap_page won't mess with it.
	 */
	page_cache_get(page);

	if (rw == READ) {
		ret = swap_readpage(NULL, page);
		wait_on_page_locked(page);
	} else {
		ret = swap_writepage(page, &swap_wbc);
		wait_on_page_writeback(page);
	}

	lock_page(page);
	remove_from_page_cache(page);
	unlock_page(page);
	page_cache_release(page);
	page_cache_release(page);	/* For add_to_page_cache() */

	if (ret == 0 && (!PageUptodate(page) || PageError(page)))
		ret = -EIO;
out:
	return ret;
}
#endif
