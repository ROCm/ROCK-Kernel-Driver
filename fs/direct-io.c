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
#define DIO_BIO_MAX_SIZE BIO_MAX_SIZE

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
	sector_t block_in_file;		/* changes */
	sector_t final_block_in_request;/* doesn't change */
	unsigned first_block_in_page;	/* doesn't change */
	int boundary;			/* prev block is at a boundary */
	int reap_counter;		/* rate limit reaping */
	get_block_t *get_block;
	sector_t last_block_in_bio;

	/* Page fetching state */
	int curr_page;			/* changes */
	int total_pages;		/* doesn't change */
	unsigned long curr_user_address;/* changes */

	/* Page queue */
	struct page *pages[DIO_PAGES];
	unsigned head;
	unsigned tail;

	/* BIO completion state */
	atomic_t bio_count;
	spinlock_t bio_list_lock;
	struct bio *bio_list;		/* singly linked via bi_private */
	struct task_struct *waiter;
};

/*
 * How many pages are in the queue?
 */
static inline unsigned dio_pages_present(struct dio *dio)
{
	return dio->head - dio->tail;
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

	if (ret >= 0) {
		dio->curr_user_address += ret * PAGE_SIZE;
		dio->curr_page += ret;
		dio->head = 0;
		dio->tail = ret;
		ret = 0;
	}
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
		if (ret) {
			printk("%s: dio_refill_pages returns %d\n",
				__FUNCTION__, ret);
			return ERR_PTR(ret);
		}
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
static void dio_bio_end_io(struct bio *bio)
{
	struct dio *dio = bio->bi_private;
	unsigned long flags;

	spin_lock_irqsave(&dio->bio_list_lock, flags);
	bio->bi_private = dio->bio_list;
	dio->bio_list = bio;
	spin_unlock_irqrestore(&dio->bio_list_lock, flags);
	wake_up_process(dio->waiter);
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
			spin_unlock_irqrestore(&dio->bio_list_lock, flags);
			blk_run_queues();
			schedule();
			spin_lock_irqsave(&dio->bio_list_lock, flags);
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
			int ret2;

			spin_lock_irqsave(&dio->bio_list_lock, flags);
			bio = dio->bio_list;
			dio->bio_list = bio->bi_private;
			spin_unlock_irqrestore(&dio->bio_list_lock, flags);
			ret2 = dio_bio_complete(dio, bio);
			if (ret == 0)
				ret = ret2;
		}
		dio->reap_counter = 0;
	}
	return ret;
}

/*
 * Walk the user pages, and the file, mapping blocks to disk and emitting BIOs.
 */
int do_direct_IO(struct dio *dio)
{
	struct inode * const inode = dio->inode;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocksize = 1 << blkbits;
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
		for ( ; block_in_page < blocks_per_page; block_in_page++) {
			struct buffer_head map_bh;
			struct bio *bio;

			map_bh.b_state = 0;
			ret = (*dio->get_block)(inode, dio->block_in_file,
						&map_bh, dio->rw == WRITE);
			if (ret) {
				printk("%s: get_block returns %d\n",
					__FUNCTION__, ret);
				goto fail_release;
			}
			/* blockdevs do not set buffer_new */
			if (buffer_new(&map_bh))
				unmap_underlying_metadata(map_bh.b_bdev,
							map_bh.b_blocknr);
			if (!buffer_mapped(&map_bh)) {
				ret = -EINVAL;		/* A hole */
				goto fail_release;
			}
			if (dio->bio) {
				if (dio->bio->bi_idx == dio->bio->bi_vcnt ||
						dio->boundary ||
						dio->last_block_in_bio !=
							map_bh.b_blocknr - 1) {
					dio_bio_submit(dio);
					dio->boundary = 0;
				}
			}
			if (dio->bio == NULL) {
				ret = dio_bio_reap(dio);
				if (ret)
					goto fail_release;
				ret = dio_bio_alloc(dio, map_bh.b_bdev,
					map_bh.b_blocknr << (blkbits - 9),
					DIO_BIO_MAX_SIZE / PAGE_SIZE);
				if (ret)
					goto fail_release;
				new_page = 1;
				dio->boundary = 0;
			}

			bio = dio->bio;
			if (new_page) {
				dio->bvec = &bio->bi_io_vec[bio->bi_idx];
				page_cache_get(page);
				dio->bvec->bv_page = page;
				dio->bvec->bv_len = 0;
				dio->bvec->bv_offset = block_in_page*blocksize;
				bio->bi_idx++;
			}
			new_page = 0;
			dio->bvec->bv_len += blocksize;
			bio->bi_size += blocksize;
			dio->last_block_in_bio = map_bh.b_blocknr;
			dio->boundary = buffer_boundary(&map_bh);

			dio->block_in_file++;
			if (dio->block_in_file >= dio->final_block_in_request)
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
generic_direct_IO(int rw, struct inode *inode, char *buf, loff_t offset,
			size_t count, get_block_t get_block)
{
	const unsigned blocksize_mask = (1 << inode->i_blkbits) - 1;
	const unsigned long user_addr = (unsigned long)buf;
	int ret;
	int ret2;
	struct dio dio;
	size_t bytes;

	/* Check the memory alignment.  Blocks cannot straddle pages */
	if ((user_addr & blocksize_mask) || (count & blocksize_mask)) {
		ret = -EINVAL;
		goto out;
	}

	/* BIO submission state */
	dio.bio = NULL;
	dio.bvec = NULL;
	dio.inode = inode;
	dio.rw = rw;
	dio.block_in_file = offset >> inode->i_blkbits;
	dio.final_block_in_request = (offset + count) >> inode->i_blkbits;

	/* Index into the first page of the first block */
	dio.first_block_in_page = (user_addr & (PAGE_SIZE - 1))
						>> inode->i_blkbits;
	dio.boundary = 0;
	dio.reap_counter = 0;
	dio.get_block = get_block;
	dio.last_block_in_bio = -1;

	/* Page fetching state */
	dio.curr_page = 0;
	bytes = count;
	dio.total_pages = 0;
	if (offset & PAGE_SIZE) {
		dio.total_pages++;
		bytes -= PAGE_SIZE - (offset & ~(PAGE_SIZE - 1));
	}
	dio.total_pages += (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
	dio.curr_user_address = user_addr;

	/* Page queue */
	dio.head = 0;
	dio.tail = 0;

	/* BIO completion state */
	atomic_set(&dio.bio_count, 0);
	spin_lock_init(&dio.bio_list_lock);
	dio.bio_list = NULL;
	dio.waiter = current;

	ret = do_direct_IO(&dio);

	if (dio.bio)
		dio_bio_submit(&dio);
	if (ret)
		dio_cleanup(&dio);
	ret2 = dio_await_completion(&dio);
	if (ret == 0)
		ret = ret2;
	if (ret == 0)
		ret = count - ((dio.final_block_in_request -
				dio.block_in_file) << inode->i_blkbits);
out:
	return ret;
}

ssize_t
generic_file_direct_IO(int rw, struct inode *inode, char *buf,
			loff_t offset, size_t count)
{
	struct address_space *mapping = inode->i_mapping;
	unsigned blocksize_mask;
	ssize_t retval;

	blocksize_mask = (1 << inode->i_blkbits) - 1;
	if ((offset & blocksize_mask) || (count & blocksize_mask)) {
		retval = -EINVAL;
		goto out;
	}

	if (mapping->nrpages) {
		retval = filemap_fdatawrite(mapping);
		if (retval == 0)
			retval = filemap_fdatawait(mapping);
		if (retval)
			goto out;
	}
	retval = mapping->a_ops->direct_IO(rw, inode, buf, offset, count);
out:
	return retval;
}
