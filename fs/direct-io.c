/*
 * fs/direct-io.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * O_DIRECT
 *
 * 04Jul2002	akpm@zip.com.au
 *		Initial version
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/buffer_head.h>
#include <linux/rwsem.h>
#include <asm/atomic.h>

/*
 * The largest-sized BIO which this code will assemble, in bytes.  Set this
 * to PAGE_SIZE if your drivers are broken.
 */
#define DIO_BIO_MAX_SIZE (16*1024)

/*
 * How many user pages to map in one call to get_user_pages().  This determines
 * the size of a structure on the stack.
 */
#define DIO_PAGES	64

struct dio {
	/* BIO submission state */
	struct bio *bio;		/* bio under assembly */
	struct bio_vec *bvec;		/* current bvec in that bio */
	struct inode *inode;
	int rw;
	unsigned blkbits;		/* doesn't change */
	sector_t block_in_file;		/* changes */
	unsigned blocks_available;	/* At block_in_file.  changes */
	sector_t final_block_in_request;/* doesn't change */
	unsigned first_block_in_page;	/* doesn't change, Used only once */
	int boundary;			/* prev block is at a boundary */
	int reap_counter;		/* rate limit reaping */
	get_blocks_t *get_blocks;	/* block mapping function */
	sector_t last_block_in_bio;	/* current final block in bio */
	sector_t next_block_in_bio;	/* next block to be added to bio */
	struct buffer_head map_bh;	/* last get_blocks() result */

	/* Page fetching state */
	int curr_page;			/* changes */
	int total_pages;		/* doesn't change */
	unsigned long curr_user_address;/* changes */

	/* Page queue */
	struct page *pages[DIO_PAGES];	/* page buffer */
	unsigned head;			/* next page to process */
	unsigned tail;			/* last valid page + 1 */
	int page_errors;		/* errno from get_user_pages() */

	/* BIO completion state */
	atomic_t bio_count;		/* nr bios in flight */
	spinlock_t bio_list_lock;	/* protects bio_list */
	struct bio *bio_list;		/* singly linked via bi_private */
	struct task_struct *waiter;	/* waiting task (NULL if none) */
};

/*
 * How many pages are in the queue?
 */
static inline unsigned dio_pages_present(struct dio *dio)
{
	return dio->tail - dio->head;
}

/*
 * Go grab and pin some userspace pages.   Typically we'll get 64 at a time.
 */
static int dio_refill_pages(struct dio *dio)
{
	int ret;
	int nr_pages;

	nr_pages = min(dio->total_pages - dio->curr_page, DIO_PAGES);
	down_read(&current->mm->mmap_sem);
	ret = get_user_pages(
		current,			/* Task for fault acounting */
		current->mm,			/* whose pages? */
		dio->curr_user_address,		/* Where from? */
		nr_pages,			/* How many pages? */
		dio->rw == READ,		/* Write to memory? */
		0,				/* force (?) */
		&dio->pages[0],
		NULL);				/* vmas */
	up_read(&current->mm->mmap_sem);

	if (ret < 0 && dio->blocks_available && (dio->rw == WRITE)) {
		/*
		 * A memory fault, but the filesystem has some outstanding
		 * mapped blocks.  We need to use those blocks up to avoid
		 * leaking stale data in the file.
		 */
		if (dio->page_errors == 0)
			dio->page_errors = ret;
		dio->pages[0] = ZERO_PAGE(dio->cur_user_address);
		dio->head = 0;
		dio->tail = 1;
		ret = 0;
		goto out;
	}

	if (ret >= 0) {
		dio->curr_user_address += ret * PAGE_SIZE;
		dio->curr_page += ret;
		dio->head = 0;
		dio->tail = ret;
		ret = 0;
	}
out:
	return ret;	
}

/*
 * Get another userspace page.  Returns an ERR_PTR on error.  Pages are
 * buffered inside the dio so that we can call get_user_pages() against a
 * decent number of pages, less frequently.  To provide nicer use of the
 * L1 cache.
 */
static struct page *dio_get_page(struct dio *dio)
{
	if (dio_pages_present(dio) == 0) {
		int ret;

		ret = dio_refill_pages(dio);
		if (ret)
			return ERR_PTR(ret);
		BUG_ON(dio_pages_present(dio) == 0);
	}
	return dio->pages[dio->head++];
}

/*
 * The BIO completion handler simply queues the BIO up for the process-context
 * handler.
 *
 * During I/O bi_private points at the dio.  After I/O, bi_private is used to
 * implement a singly-linked list of completed BIOs, at dio->bio_list.
 */
static int dio_bio_end_io(struct bio *bio, unsigned int bytes_done, int error)
{
	struct dio *dio = bio->bi_private;
	unsigned long flags;

	if (bio->bi_size)
		return 1;

	spin_lock_irqsave(&dio->bio_list_lock, flags);
	bio->bi_private = dio->bio_list;
	dio->bio_list = bio;
	if (dio->waiter)
		wake_up_process(dio->waiter);
	spin_unlock_irqrestore(&dio->bio_list_lock, flags);
	return 0;
}

static int
dio_bio_alloc(struct dio *dio, struct block_device *bdev,
		sector_t first_sector, int nr_vecs)
{
	struct bio *bio;

	bio = bio_alloc(GFP_KERNEL, nr_vecs);
	if (bio == NULL)
		return -ENOMEM;

	bio->bi_bdev = bdev;
	bio->bi_vcnt = nr_vecs;
	bio->bi_idx = 0;
	bio->bi_size = 0;
	bio->bi_sector = first_sector;
	bio->bi_io_vec[0].bv_page = NULL;
	bio->bi_end_io = dio_bio_end_io;

	dio->bio = bio;
	dio->bvec = NULL;		/* debug */
	return 0;
}

static void dio_bio_submit(struct dio *dio)
{
	struct bio *bio = dio->bio;

	bio->bi_vcnt = bio->bi_idx;
	bio->bi_idx = 0;
	bio->bi_private = dio;
	atomic_inc(&dio->bio_count);
	submit_bio(dio->rw, bio);

	dio->bio = NULL;
	dio->bvec = NULL;
	dio->boundary = 0;
}

/*
 * Release any resources in case of a failure
 */
static void dio_cleanup(struct dio *dio)
{
	while (dio_pages_present(dio))
		page_cache_release(dio_get_page(dio));
}

/*
 * Wait for the next BIO to complete.  Remove it and return it.
 */
static struct bio *dio_await_one(struct dio *dio)
{
	unsigned long flags;
	struct bio *bio;

	spin_lock_irqsave(&dio->bio_list_lock, flags);
	while (dio->bio_list == NULL) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (dio->bio_list == NULL) {
			dio->waiter = current;
			spin_unlock_irqrestore(&dio->bio_list_lock, flags);
			blk_run_queues();
			schedule();
			spin_lock_irqsave(&dio->bio_list_lock, flags);
			dio->waiter = NULL;
		}
		set_current_state(TASK_RUNNING);
	}
	bio = dio->bio_list;
	dio->bio_list = bio->bi_private;
	spin_unlock_irqrestore(&dio->bio_list_lock, flags);
	return bio;
}

/*
 * Process one completed BIO.  No locks are held.
 */
static int dio_bio_complete(struct dio *dio, struct bio *bio)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec;
	int page_no;

	for (page_no = 0; page_no < bio->bi_vcnt; page_no++) {
		struct page *page = bvec[page_no].bv_page;

		if (dio->rw == READ)
			set_page_dirty(page);
		page_cache_release(page);
	}
	atomic_dec(&dio->bio_count);
	bio_put(bio);
	return uptodate ? 0 : -EIO;
}

/*
 * Wait on and process all in-flight BIOs.
 */
static int dio_await_completion(struct dio *dio)
{
	int ret = 0;

	if (dio->bio)
		dio_bio_submit(dio);

	while (atomic_read(&dio->bio_count)) {
		struct bio *bio = dio_await_one(dio);
		int ret2;

		ret2 = dio_bio_complete(dio, bio);
		if (ret == 0)
			ret = ret2;
	}
	return ret;
}

/*
 * A really large O_DIRECT read or write can generate a lot of BIOs.  So
 * to keep the memory consumption sane we periodically reap any completed BIOs
 * during the BIO generation phase.
 *
 * This also helps to limit the peak amount of pinned userspace memory.
 */
static int dio_bio_reap(struct dio *dio)
{
	int ret = 0;

	if (dio->reap_counter++ >= 64) {
		while (dio->bio_list) {
			unsigned long flags;
			struct bio *bio;

			spin_lock_irqsave(&dio->bio_list_lock, flags);
			bio = dio->bio_list;
			dio->bio_list = bio->bi_private;
			spin_unlock_irqrestore(&dio->bio_list_lock, flags);
			ret = dio_bio_complete(dio, bio);
		}
		dio->reap_counter = 0;
	}
	return ret;
}

/*
 * Call into the fs to map some more disk blocks.  We record the current number
 * of available blocks at dio->blocks_available.  These are in units of the
 * fs blocksize, (1 << inode->i_blkbits).
 *
 * The fs is allowed to map lots of blocks at once.  If it wants to do that,
 * it uses the passed inode-relative block number as the file offset, as usual.
 *
 * get_blocks() is passed the number of i_blkbits-sized blocks which direct_io
 * has remaining to do.  The fs should not map more than this number of blocks.
 *
 * If the fs has mapped a lot of blocks, it should populate bh->b_size to
 * indicate how much contiguous disk space has been made available at
 * bh->b_blocknr.
 *
 * If *any* of the mapped blocks are new, then the fs must set buffer_new().
 * This isn't very efficient...
 *
 * In the case of filesystem holes: the fs may return an arbitrarily-large
 * hole by returning an appropriate value in b_size and by clearing
 * buffer_mapped().  This code _should_ handle that case correctly, but it has
 * only been tested against single-block holes (b_size == blocksize).
 */
static int get_more_blocks(struct dio *dio)
{
	int ret;
	struct buffer_head *map_bh = &dio->map_bh;

	if (dio->blocks_available)
		return 0;

	/*
	 * If there was a memory error and we've overwritten all the
	 * mapped blocks then we can now return that memory error
	 */
	if (dio->page_errors) {
		ret = dio->page_errors;
		goto out;
	}

	map_bh->b_state = 0;
	map_bh->b_size = 0;
	BUG_ON(dio->block_in_file >= dio->final_block_in_request);
	ret = (*dio->get_blocks)(dio->inode, dio->block_in_file,
			dio->final_block_in_request - dio->block_in_file,
			map_bh, dio->rw == WRITE);
	if (ret)
		goto out;

	if (buffer_mapped(map_bh)) {
		BUG_ON(map_bh->b_size == 0);
		BUG_ON((map_bh->b_size & ((1 << dio->blkbits) - 1)) != 0);

		dio->blocks_available = map_bh->b_size >> dio->blkbits;

		/* blockdevs do not set buffer_new */
		if (buffer_new(map_bh)) {
			sector_t block = map_bh->b_blocknr;
			unsigned i;

			for (i = 0; i < dio->blocks_available; i++)
				unmap_underlying_metadata(map_bh->b_bdev,
							block++);
		}
	} else {
		BUG_ON(dio->rw != READ);
		if (dio->bio)
			dio_bio_submit(dio);
	}
	dio->next_block_in_bio = map_bh->b_blocknr;
out:
	return ret;
}

/*
 * Check to see if we can continue to grow the BIO. If not, then send it.
 */
static void dio_prep_bio(struct dio *dio)
{
	if (dio->bio == NULL)
		return;

	if (dio->bio->bi_idx == dio->bio->bi_vcnt ||
			dio->boundary ||
			dio->last_block_in_bio != dio->next_block_in_bio - 1)
		dio_bio_submit(dio);
}

/*
 * There is no bio.  Make one now.
 */
static int dio_new_bio(struct dio *dio)
{
	sector_t sector;
	int ret;

	ret = dio_bio_reap(dio);
	if (ret)
		goto out;
	sector = dio->next_block_in_bio << (dio->blkbits - 9);
	ret = dio_bio_alloc(dio, dio->map_bh.b_bdev, sector,
				DIO_BIO_MAX_SIZE / PAGE_SIZE);
	dio->boundary = 0;
out:
	return ret;
}

/*
 * Walk the user pages, and the file, mapping blocks to disk and emitting BIOs.
 *
 * Direct IO against a blockdev is different from a file.  Because we can
 * happily perform page-sized but 512-byte aligned IOs.  It is important that
 * blockdev IO be able to have fine alignment and large sizes.
 *
 * So what we do is to permit the ->get_blocks function to populate bh.b_size
 * with the size of IO which is permitted at this offset and this i_blkbits.
 *
 * For best results, the blockdev should be set up with 512-byte i_blkbits and
 * it should set b_size to PAGE_SIZE or more inside get_blocks().  This gives
 * fine alignment but still allows this function to work in PAGE_SIZE units.
 */
int do_direct_IO(struct dio *dio)
{
	const unsigned blkbits = dio->blkbits;
	const unsigned blocks_per_page = PAGE_SIZE >> blkbits;
	struct page *page;
	unsigned block_in_page;
	int ret;

	/* The I/O can start at any block offset within the first page */
	block_in_page = dio->first_block_in_page;

	while (dio->block_in_file < dio->final_block_in_request) {
		int new_page;	/* Need to insert this page into the BIO? */

		page = dio_get_page(dio);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto out;
		}

		new_page = 1;
		while (block_in_page < blocks_per_page) {
			struct bio *bio;
			unsigned this_chunk_bytes;	/* # of bytes mapped */
			unsigned this_chunk_blocks;	/* # of blocks */
			unsigned u;

			ret = get_more_blocks(dio);
			if (ret)
				goto fail_release;

			/* Handle holes */
			if (!buffer_mapped(&dio->map_bh)) {
				char *kaddr = kmap_atomic(page, KM_USER0);
				memset(kaddr + (block_in_page << blkbits),
						0, 1 << blkbits);
				flush_dcache_page(page);
				kunmap_atomic(kaddr, KM_USER0);
				dio->block_in_file++;
				dio->next_block_in_bio++;
				block_in_page++;
				goto next_block;
			}

			dio_prep_bio(dio);
			if (dio->bio == NULL) {
				ret = dio_new_bio(dio);
				if (ret)
					goto fail_release;
				new_page = 1;
			}

			bio = dio->bio;
			if (new_page) {
				dio->bvec = &bio->bi_io_vec[bio->bi_idx];
				page_cache_get(page);
				dio->bvec->bv_page = page;
				dio->bvec->bv_len = 0;
				dio->bvec->bv_offset = block_in_page << blkbits;
				bio->bi_idx++;
				new_page = 0;
			}

			/* Work out how much disk we can add to this page */
			this_chunk_blocks = dio->blocks_available;
			u = (PAGE_SIZE - (dio->bvec->bv_offset + dio->bvec->bv_len)) >> blkbits;
			if (this_chunk_blocks > u)
				this_chunk_blocks = u;
			u = dio->final_block_in_request - dio->block_in_file;
			if (this_chunk_blocks > u)
				this_chunk_blocks = u;
			this_chunk_bytes = this_chunk_blocks << blkbits;
			BUG_ON(this_chunk_bytes == 0);

			dio->bvec->bv_len += this_chunk_bytes;
			bio->bi_size += this_chunk_bytes;
			dio->next_block_in_bio += this_chunk_blocks;
			dio->last_block_in_bio = dio->next_block_in_bio - 1;
			dio->boundary = buffer_boundary(&dio->map_bh);
			dio->block_in_file += this_chunk_blocks;
			block_in_page += this_chunk_blocks;
			dio->blocks_available -= this_chunk_blocks;
next_block:
			if (dio->block_in_file > dio->final_block_in_request)
				BUG();
			if (dio->block_in_file == dio->final_block_in_request)
				break;
		}
		block_in_page = 0;
		page_cache_release(page);
	}
	ret = 0;
	goto out;
fail_release:
	page_cache_release(page);
out:
	return ret;
}

int
direct_io_worker(int rw, struct inode *inode, const struct iovec *iov, 
	loff_t offset, unsigned long nr_segs, get_blocks_t get_blocks)
{
	const unsigned blkbits = inode->i_blkbits;
	unsigned long user_addr; 
	int seg, ret2, ret = 0;
	struct dio dio;
	size_t bytes, tot_bytes = 0;

	dio.bio = NULL;
	dio.bvec = NULL;
	dio.inode = inode;
	dio.rw = rw;
	dio.blkbits = blkbits;
	dio.block_in_file = offset >> blkbits;
	dio.blocks_available = 0;

	dio.boundary = 0;
	dio.reap_counter = 0;
	dio.get_blocks = get_blocks;
	dio.last_block_in_bio = -1;
	dio.next_block_in_bio = -1;

	dio.page_errors = 0;

	/* BIO completion state */
	atomic_set(&dio.bio_count, 0);
	spin_lock_init(&dio.bio_list_lock);
	dio.bio_list = NULL;
	dio.waiter = NULL;

	for (seg = 0; seg < nr_segs; seg++) {
		user_addr = (unsigned long)iov[seg].iov_base;
		bytes = iov[seg].iov_len;

		/* Index into the first page of the first block */
		dio.first_block_in_page = (user_addr & (PAGE_SIZE - 1)) >> blkbits;
		dio.final_block_in_request = dio.block_in_file + (bytes >> blkbits);
		/* Page fetching state */
		dio.head = 0;
		dio.tail = 0;
		dio.curr_page = 0;

		dio.total_pages = 0;
		if (user_addr & (PAGE_SIZE-1)) {
			dio.total_pages++;
			bytes -= PAGE_SIZE - (user_addr & (PAGE_SIZE - 1));
		}
		dio.total_pages += (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
		dio.curr_user_address = user_addr;
	
		ret = do_direct_IO(&dio);

		if (ret) {
			dio_cleanup(&dio);
			break;
		}

		tot_bytes += iov[seg].iov_len - ((dio.final_block_in_request -
					dio.block_in_file) << blkbits);

	} /* end iovec loop */

	ret2 = dio_await_completion(&dio);
	if (ret == 0)
		ret = ret2;
	if (ret == 0)
		ret = dio.page_errors;
	if (ret == 0)
		ret = tot_bytes; 

	return ret;
}

/*
 * This is a library function for use by filesystem drivers.
 */
int
generic_direct_IO(int rw, struct inode *inode, const struct iovec *iov, 
	loff_t offset, unsigned long nr_segs, get_blocks_t get_blocks)
{
	int seg;
	size_t size;
	unsigned long addr;
	struct address_space *mapping = inode->i_mapping;
	unsigned blocksize_mask = (1 << inode->i_blkbits) - 1;
	ssize_t retval = -EINVAL;

	if (offset & blocksize_mask) {
		goto out;
	}

	/* Check the memory alignment.  Blocks cannot straddle pages */
	for (seg = 0; seg < nr_segs; seg++) {
		addr = (unsigned long)iov[seg].iov_base;
		size = iov[seg].iov_len;
		if ((addr & blocksize_mask) || (size & blocksize_mask)) 
			goto out;	
	}

	if (mapping->nrpages) {
		retval = filemap_fdatawrite(mapping);
		if (retval == 0)
			retval = filemap_fdatawait(mapping);
		if (retval)
			goto out;
	}

	retval = direct_io_worker(rw, inode, iov, offset, nr_segs, get_blocks);
out:
	return retval;
}

ssize_t
generic_file_direct_IO(int rw, struct inode *inode, const struct iovec *iov, 
	loff_t offset, unsigned long nr_segs)
{
	struct address_space *mapping = inode->i_mapping;
	ssize_t retval;

	retval = mapping->a_ops->direct_IO(rw, inode, iov, offset, nr_segs);
	if (inode->i_mapping->nrpages)
		invalidate_inode_pages2(inode->i_mapping);
	return retval;
}
