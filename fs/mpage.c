/*
 * fs/mpage.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains functions related to preparing and submitting BIOs which contain
 * multiple pagecache pages.
 *
 * 15May2002	akpm@zip.com.au
 *		Initial version
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/prefetch.h>
#include <linux/mpage.h>

/*
 * The largest-sized BIO which this code will assemble, in bytes.  Set this
 * to PAGE_CACHE_SIZE if your drivers are broken.
 */
#define MPAGE_BIO_MAX_SIZE BIO_MAX_SIZE

/*
 * I/O completion handler for multipage BIOs.
 *
 * The mpage code never puts partial pages into a BIO (except for end-of-file).
 * If a page does not map to a contiguous run of blocks then it simply falls
 * back to block_read_full_page().
 *
 * Why is this?  If a page's completion depends on a number of different BIOs
 * which can complete in any order (or at the same time) then determining the
 * status of that page is hard.  See end_buffer_async_read() for the details.
 * There is no point in duplicating all that complexity.
 */
static void mpage_end_io_read(struct bio *bio)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (uptodate) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	} while (bvec >= bio->bi_io_vec);
	bio_put(bio);
}

struct bio *mpage_bio_submit(int rw, struct bio *bio)
{
	bio->bi_vcnt = bio->bi_idx;
	bio->bi_idx = 0;
	bio->bi_end_io = mpage_end_io_read;
	submit_bio(rw, bio);
	return NULL;
}

static struct bio *
mpage_alloc(struct block_device *bdev,
		sector_t first_sector, int nr_vecs, int gfp_flags)
{
	struct bio *bio;

	bio = bio_alloc(gfp_flags, nr_vecs);
	if (bio) {
		bio->bi_bdev = bdev;
		bio->bi_vcnt = nr_vecs;
		bio->bi_idx = 0;
		bio->bi_size = 0;
		bio->bi_sector = first_sector;
		bio->bi_io_vec[0].bv_page = NULL;
	}
	return bio;
}

/**
 * mpage_readpages - populate an address space with some pages, and
 *                       start reads against them.
 *
 * @mapping: the address_space
 * @pages: The address of a list_head which contains the target pages.  These
 *   pages have their ->index populated and are otherwise uninitialised.
 *
 *   The page at @pages->prev has the lowest file offset, and reads should be
 *   issued in @pages->prev to @pages->next order.
 *
 * @nr_pages: The number of pages at *@pages
 * @get_block: The filesystem's block mapper function.
 *
 * This function walks the pages and the blocks within each page, building and
 * emitting large BIOs.
 *
 * If anything unusual happens, such as:
 *
 * - encountering a page which has buffers
 * - encountering a page which has a non-hole after a hole
 * - encountering a page with non-contiguous blocks
 *
 * then this code just gives up and calls the buffer_head-based read function.
 * It does handle a page which has holes at the end - that is a common case:
 * the end-of-file on blocksize < PAGE_CACHE_SIZE setups.
 *
 * BH_Boundary explanation:
 *
 * There is a problem.  The mpage read code assembles several pages, gets all
 * their disk mappings, and then submits them all.  That's fine, but obtaining
 * the disk mappings may require I/O.  Reads of indirect blocks, for example.
 *
 * So an mpage read of the first 16 blocks of an ext2 file will cause I/O to be
 * submitted in the following order:
 * 	12 0 1 2 3 4 5 6 7 8 9 10 11 13 14 15 16
 * because the indirect block has to be read to get the mappings of blocks
 * 13,14,15,16.  Obviously, this impacts performance.
 * 
 * So what we do it to allow the filesystem's get_block() function to set
 * BH_Boundary when it maps block 11.  BH_Boundary says: mapping of the block
 * after this one will require I/O against a block which is probably close to
 * this one.  So you should push what I/O you have currently accumulated.
 *
 * This all causes the disk requests to be issued in the correct order.
 */
static struct bio *
do_mpage_readpage(struct bio *bio, struct page *page, unsigned nr_pages,
			sector_t *last_block_in_bio, get_block_t get_block)
{
	struct inode *inode = page->mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocks_per_page = PAGE_CACHE_SIZE >> blkbits;
	const unsigned blocksize = 1 << blkbits;
	struct bio_vec *bvec;
	sector_t block_in_file;
	sector_t last_block;
	sector_t blocks[MAX_BUF_PER_PAGE];
	unsigned page_block;
	unsigned first_hole = blocks_per_page;
	struct block_device *bdev = NULL;
	struct buffer_head bh;

	if (page_has_buffers(page))
		goto confused;

	block_in_file = page->index << (PAGE_CACHE_SHIFT - blkbits);
	last_block = (inode->i_size + blocksize - 1) >> blkbits;

	for (page_block = 0; page_block < blocks_per_page;
				page_block++, block_in_file++) {
		bh.b_state = 0;
		if (block_in_file < last_block) {
			if (get_block(inode, block_in_file, &bh, 0))
				goto confused;
		}

		if (!buffer_mapped(&bh)) {
			if (first_hole == blocks_per_page)
				first_hole = page_block;
			continue;
		}
	
		if (first_hole != blocks_per_page)
			goto confused;		/* hole -> non-hole */

		/* Contiguous blocks? */
		if (page_block && blocks[page_block-1] != bh.b_blocknr-1)
			goto confused;
		blocks[page_block] = bh.b_blocknr;
		bdev = bh.b_bdev;
	}

	if (first_hole != blocks_per_page) {
		memset(kmap(page) + (first_hole << blkbits), 0,
				PAGE_CACHE_SIZE - (first_hole << blkbits));
		flush_dcache_page(page);
		kunmap(page);
		if (first_hole == 0) {
			SetPageUptodate(page);
			unlock_page(page);
			goto out;
		}
	}

	/*
	 * This page will go to BIO.  Do we need to send this BIO off first?
	 */
	if (bio && (bio->bi_idx == bio->bi_vcnt ||
			*last_block_in_bio != blocks[0] - 1))
		bio = mpage_bio_submit(READ, bio);

	if (bio == NULL) {
		unsigned nr_bvecs = MPAGE_BIO_MAX_SIZE / PAGE_CACHE_SIZE;

		if (nr_bvecs > nr_pages)
			nr_bvecs = nr_pages;
		bio = mpage_alloc(bdev, blocks[0] << (blkbits - 9),
					nr_bvecs, GFP_KERNEL);
		if (bio == NULL)
			goto confused;
	}

	bvec = &bio->bi_io_vec[bio->bi_idx++];
	bvec->bv_page = page;
	bvec->bv_len = (first_hole << blkbits);
	bvec->bv_offset = 0;
	bio->bi_size += bvec->bv_len;
	if (buffer_boundary(&bh) || (first_hole != blocks_per_page))
		bio = mpage_bio_submit(READ, bio);
	else
		*last_block_in_bio = blocks[blocks_per_page - 1];
out:
	return bio;

confused:
	if (bio)
		bio = mpage_bio_submit(READ, bio);
	block_read_full_page(page, get_block);
	goto out;
}

int
mpage_readpages(struct address_space *mapping, struct list_head *pages,
				unsigned nr_pages, get_block_t get_block)
{
	struct bio *bio = NULL;
	unsigned page_idx;
	sector_t last_block_in_bio = 0;

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = list_entry(pages->prev, struct page, list);

		prefetchw(&page->flags);
		list_del(&page->list);
		if (!add_to_page_cache_unique(page, mapping, page->index))
			bio = do_mpage_readpage(bio, page,
					nr_pages - page_idx,
					&last_block_in_bio, get_block);
		page_cache_release(page);
	}
	BUG_ON(!list_empty(pages));
	if (bio)
		mpage_bio_submit(READ, bio);
	return 0;
}
EXPORT_SYMBOL(mpage_readpages);

/*
 * This isn't called much at all
 */
int mpage_readpage(struct page *page, get_block_t get_block)
{
	struct bio *bio = NULL;
	sector_t last_block_in_bio = 0;

	bio = do_mpage_readpage(bio, page, 1,
			&last_block_in_bio, get_block);
	if (bio)
		mpage_bio_submit(READ, bio);
	return 0;
}
EXPORT_SYMBOL(mpage_readpage);
