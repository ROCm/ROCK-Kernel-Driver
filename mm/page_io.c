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
#include <linux/buffer_head.h>
#include <asm/pgtable.h>
#include <linux/swapops.h>

static int
swap_get_block(struct inode *inode, sector_t iblock,
		struct buffer_head *bh_result, int create)
{
	struct swap_info_struct *sis;
	swp_entry_t entry;

	entry.val = iblock;
	sis = get_swap_info_struct(swp_type(entry));
	bh_result->b_bdev = sis->bdev;
	bh_result->b_blocknr = map_swap_page(sis, swp_offset(entry));
	bh_result->b_size = PAGE_SIZE;
	set_buffer_mapped(bh_result);
	return 0;
}

static struct bio *
get_swap_bio(int gfp_flags, struct page *page, bio_end_io_t end_io)
{
	struct bio *bio;
	struct buffer_head bh;

	bio = bio_alloc(gfp_flags, 1);
	if (bio) {
		swap_get_block(NULL, page->index, &bh, 1);
		bio->bi_sector = bh.b_blocknr * (PAGE_SIZE >> 9);
		bio->bi_bdev = bh.b_bdev;
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

static void end_swap_bio_write(struct bio *bio)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (!uptodate)
		SetPageError(page);
	end_page_writeback(page);
	bio_put(bio);
}

static void end_swap_bio_read(struct bio *bio)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct page *page = bio->bi_io_vec[0].bv_page;

	if (!uptodate) {
		SetPageError(page);
		ClearPageUptodate(page);
	} else {
		SetPageUptodate(page);
	}
	unlock_page(page);
	bio_put(bio);
}

/*
 * We may have stale swap cache pages in memory: notice
 * them here and get rid of the unnecessary final write.
 */
int swap_writepage(struct page *page)
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
		ret = -ENOMEM;
		goto out;
	}
	kstat.pswpout++;
	SetPageWriteback(page);
	unlock_page(page);
	submit_bio(WRITE, bio);
out:
	return ret;
}

int swap_readpage(struct file *file, struct page *page)
{
	struct bio *bio;
	int ret = 0;

	ClearPageUptodate(page);
	bio = get_swap_bio(GFP_KERNEL, page, end_swap_bio_read);
	if (bio == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	kstat.pswpin++;
	submit_bio(READ, bio);
out:
	return ret;
}
/*
 * swapper_space doesn't have a real inode, so it gets a special vm_writeback()
 * so we don't need swap special cases in generic_vm_writeback().
 *
 * Swap pages are !PageLocked and PageWriteback while under writeout so that
 * memory allocators will throttle against them.
 */
static int swap_vm_writeback(struct page *page, int *nr_to_write)
{
	struct address_space *mapping = page->mapping;

	unlock_page(page);
	return generic_writepages(mapping, nr_to_write);
}

struct address_space_operations swap_aops = {
	vm_writeback:	swap_vm_writeback,
	writepage:	swap_writepage,
	readpage:	swap_readpage,
	sync_page:	block_sync_page,
	set_page_dirty:	__set_page_dirty_nobuffers,
};

/*
 * A scruffy utility function to read or write an arbitrary swap page
 * and wait on the I/O.
 */
int rw_swap_page_sync(int rw, swp_entry_t entry, struct page *page)
{
	int ret;

	lock_page(page);

	BUG_ON(page->mapping);
	page->mapping = &swapper_space;
	page->index = entry.val;

	if (rw == READ) {
		ret = swap_readpage(NULL, page);
		wait_on_page_locked(page);
	} else {
		ret = swap_writepage(page);
		wait_on_page_writeback(page);
	}
	page->mapping = NULL;
	if (ret == 0 && (!PageUptodate(page) || PageError(page)))
		ret = -EIO;
	return ret;
}
