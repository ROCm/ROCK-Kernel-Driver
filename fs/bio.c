/*
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of

 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/iobuf.h>
#include <linux/blk.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/prefetch.h>
#include <linux/compiler.h>

#include <asm/uaccess.h>

kmem_cache_t *bio_cachep;
static spinlock_t __cacheline_aligned bio_lock = SPIN_LOCK_UNLOCKED;
static struct bio *bio_pool;
static DECLARE_WAIT_QUEUE_HEAD(bio_pool_wait);
static DECLARE_WAIT_QUEUE_HEAD(biovec_pool_wait);

static unsigned int bio_pool_free;

#define BIOVEC_NR_POOLS 6

struct biovec_pool {
	int bp_size;
	kmem_cache_t *bp_cachep;
	wait_queue_head_t bp_wait;
};

static struct biovec_pool bvec_list[BIOVEC_NR_POOLS];

/*
 * if you change this list, also change bvec_alloc or things will
 * break badly!
 */
static const int bvec_pool_sizes[BIOVEC_NR_POOLS] = { 1, 4, 16, 64, 128, 256 };

#define BIO_MAX_PAGES	(bvec_pool_sizes[BIOVEC_NR_POOLS - 1])

/*
 * TODO: change this to use slab reservation scheme once that infrastructure
 * is in place...
 */
#define BIO_POOL_SIZE		(256)

/*
 * if need be, add bio_pool_get_irq() to match...
 */
static inline struct bio *__bio_pool_get(void)
{
	struct bio *bio;

	if ((bio = bio_pool)) {
		BUG_ON(bio_pool_free <= 0);
		bio_pool = bio->bi_next;
		bio->bi_next = NULL;
		bio_pool_free--;
	}

	return bio;
}

static inline struct bio *bio_pool_get(void)
{
	unsigned long flags;
	struct bio *bio;

	spin_lock_irqsave(&bio_lock, flags);
	bio = __bio_pool_get();
	BUG_ON(!bio && bio_pool_free);
	spin_unlock_irqrestore(&bio_lock, flags);

	return bio;
}

static inline void bio_pool_put(struct bio *bio)
{
	unsigned long flags;
	int wake_pool = 0;

	spin_lock_irqsave(&bio_lock, flags);

	/*
	 * if the pool has enough free entries, just slab free the bio
	 */
	if (bio_pool_free < BIO_POOL_SIZE) {
		bio->bi_next = bio_pool;
		bio_pool = bio;
		bio_pool_free++;
		wake_pool = waitqueue_active(&bio_pool_wait);
		spin_unlock_irqrestore(&bio_lock, flags);

		if (wake_pool)
			wake_up_nr(&bio_pool_wait, 1);
	} else {
		spin_unlock_irqrestore(&bio_lock, flags);
		kmem_cache_free(bio_cachep, bio);
	}
}

#define BIO_CAN_WAIT(gfp_mask)	\
	(((gfp_mask) & (__GFP_WAIT | __GFP_IO)) == (__GFP_WAIT | __GFP_IO))

static inline struct bio_vec *bvec_alloc(int gfp_mask, int nr, int *idx)
{
	struct bio_vec *bvl = NULL;
	struct biovec_pool *bp;

	/*
	 * see comment near bvec_pool_sizes define!
	 */
	switch (nr) {
		case 1:
			*idx = 0;
			break;
		case 2 ... 4:
			*idx = 1;
			break;
		case 5 ... 16:
			*idx = 2;
			break;
		case 17 ... 64:
			*idx = 3;
			break;
		case 65 ... 128:
			*idx = 4;
			break;
		case 129 ... 256:
			*idx = 5;
			break;
		default:
			return NULL;
	}
	bp = &bvec_list[*idx];

	/*
	 * ok, so idx now points to the slab we want to allocate from
	 */
	if ((bvl = kmem_cache_alloc(bp->bp_cachep, gfp_mask)))
		goto out_gotit;

	/*
	 * we need slab reservations for this to be completely
	 * deadlock free...
	 */
	if (BIO_CAN_WAIT(gfp_mask)) {
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue_exclusive(&bp->bp_wait, &wait);
		for (;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			bvl = kmem_cache_alloc(bp->bp_cachep, gfp_mask);
			if (bvl)
				goto out_gotit;

			run_task_queue(&tq_disk);
			schedule();
		}
		remove_wait_queue(&bp->bp_wait, &wait);
		__set_current_state(TASK_RUNNING);
	}

	if (bvl) {
out_gotit:
		memset(bvl, 0, bp->bp_size);
	}

	return bvl;
}

/*
 * default destructor for a bio allocated with bio_alloc()
 */
void bio_destructor(struct bio *bio)
{
	struct biovec_pool *bp = &bvec_list[bio->bi_max];

	BUG_ON(bio->bi_max >= BIOVEC_NR_POOLS);

	/*
	 * cloned bio doesn't own the veclist
	 */
	if (!(bio->bi_flags & (1 << BIO_CLONED)))
		kmem_cache_free(bp->bp_cachep, bio->bi_io_vec);

	bio_pool_put(bio);
}

inline void bio_init(struct bio *bio)
{
	bio->bi_next = NULL;
	atomic_set(&bio->bi_cnt, 1);
	bio->bi_flags = 0;
	bio->bi_rw = 0;
	bio->bi_vcnt = 0;
	bio->bi_idx = 0;
	bio->bi_size = 0;
	bio->bi_end_io = NULL;
}

static inline struct bio *__bio_alloc(int gfp_mask, bio_destructor_t *dest)
{
	struct bio *bio;

	/*
	 * first try our reserved pool
	 */
	if ((bio = bio_pool_get()))
		goto gotit;

	/*
	 * no such luck, try slab alloc
	 */
	if ((bio = kmem_cache_alloc(bio_cachep, gfp_mask)))
		goto gotit;

	/*
	 * hrmpf, not much luck. if we are allowed to wait, wait on
	 * bio_pool to be replenished
	 */
	if (BIO_CAN_WAIT(gfp_mask)) {
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue_exclusive(&bio_pool_wait, &wait);
		for (;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if ((bio = bio_pool_get()))
				break;

			run_task_queue(&tq_disk);
			schedule();
		}
		remove_wait_queue(&bio_pool_wait, &wait);
		__set_current_state(TASK_RUNNING);
	}

	if (bio) {
gotit:
		bio_init(bio);
		bio->bi_io_vec = NULL;
		bio->bi_destructor = dest;
	}

	return bio;
}

/**
 * bio_alloc - allocate a bio for I/O
 * @gfp_mask:   the GFP_ mask given to the slab allocator
 * @nr_iovecs:	number of iovecs to pre-allocate
 *
 * Description:
 *   bio_alloc will first try it's on internal pool to satisfy the allocation
 *   and if that fails fall back to the bio slab cache. In the latter case,
 *   the @gfp_mask specifies the priority of the allocation. In particular,
 *   if %__GFP_WAIT is set then we will block on the internal pool waiting
 *   for a &struct bio to become free.
 **/
struct bio *bio_alloc(int gfp_mask, int nr_iovecs)
{
	struct bio *bio = __bio_alloc(gfp_mask, bio_destructor);
	struct bio_vec *bvl = NULL;

	if (unlikely(!bio))
		return NULL;

	if (!nr_iovecs || (bvl = bvec_alloc(gfp_mask,nr_iovecs,&bio->bi_max))) {
		bio->bi_io_vec = bvl;
		return bio;
	}

	bio_pool_put(bio);
	return NULL;
}

/*
 * queue lock assumed held!
 */
static inline void bio_free(struct bio *bio)
{
	bio->bi_destructor(bio);
}

/**
 * bio_put - release a reference to a bio
 * @bio:   bio to release reference to
 *
 * Description:
 *   Put a reference to a &struct bio, either one you have gotten with
 *   bio_alloc or bio_get. The last put of a bio will free it.
 **/
void bio_put(struct bio *bio)
{
	BUG_ON(!atomic_read(&bio->bi_cnt));

	/*
	 * last put frees it
	 */
	if (atomic_dec_and_test(&bio->bi_cnt)) {
		BUG_ON(bio->bi_next);

		bio_free(bio);
	}
}

/**
 *	bio_clone	-	duplicate a bio
 *	@bio: bio to clone
 *	@gfp_mask: allocation priority
 *
 *	Duplicate a &bio. Caller will own the returned bio, but not
 *	the actual data it points to. Reference count of returned
 * 	bio will be one.
 */
struct bio *bio_clone(struct bio *bio, int gfp_mask)
{
	struct bio *b = bio_alloc(gfp_mask, 0);

	if (b) {
		b->bi_io_vec = bio->bi_io_vec;

		b->bi_sector = bio->bi_sector;
		b->bi_dev = bio->bi_dev;
		b->bi_flags |= 1 << BIO_CLONED;
		b->bi_rw = bio->bi_rw;

		b->bi_vcnt = bio->bi_vcnt;
		b->bi_idx = bio->bi_idx;
		b->bi_size = bio->bi_size;
		b->bi_max = bio->bi_max;
	}

	return b;
}

/**
 *	bio_copy	-	create copy of a bio
 *	@bio: bio to copy
 *	@gfp_mask: allocation priority
 *	@copy: copy data to allocated bio
 *
 *	Create a copy of a &bio. Caller will own the returned bio and
 *	the actual data it points to. Reference count of returned
 * 	bio will be one.
 */
struct bio *bio_copy(struct bio *bio, int gfp_mask, int copy)
{
	struct bio *b = bio_alloc(gfp_mask, bio->bi_vcnt);
	unsigned long flags = 0; /* gcc silly */
	int i;

	if (b) {
		struct bio_vec *bv;

		/*
		 * iterate iovec list and alloc pages + copy data
		 */
		bio_for_each_segment(bv, bio, i) {
			struct bio_vec *bbv = &b->bi_io_vec[i];
			char *vfrom, *vto;

			bbv->bv_page = alloc_page(gfp_mask);
			if (bbv->bv_page == NULL)
				goto oom;

			if (!copy)
				goto fill_in;

			if (gfp_mask & __GFP_WAIT) {
				vfrom = kmap(bv->bv_page);
				vto = kmap(bbv->bv_page);
			} else {
				__save_flags(flags);
				__cli();
				vfrom = kmap_atomic(bv->bv_page, KM_BIO_IRQ);
				vto = kmap_atomic(bbv->bv_page, KM_BIO_IRQ);
			}

			memcpy(vto + bbv->bv_offset, vfrom + bv->bv_offset, bv->bv_len);
			if (gfp_mask & __GFP_WAIT) {
				kunmap(bbv->bv_page);
				kunmap(bv->bv_page);
			} else {
				kunmap_atomic(vto, KM_BIO_IRQ);
				kunmap_atomic(vfrom, KM_BIO_IRQ);
				__restore_flags(flags);
			}

fill_in:
			bbv->bv_len = bv->bv_len;
			bbv->bv_offset = bv->bv_offset;
		}

		b->bi_sector = bio->bi_sector;
		b->bi_dev = bio->bi_dev;
		b->bi_rw = bio->bi_rw;

		b->bi_vcnt = bio->bi_vcnt;
		b->bi_size = bio->bi_size;
	}

	return b;

oom:
	while (i >= 0) {
		__free_page(b->bi_io_vec[i].bv_page);
		i--;
	}

	bio_pool_put(b);
	return NULL;
}

#ifdef BIO_PAGEIO
static int bio_end_io_page(struct bio *bio)
{
	struct page *page = bio_page(bio);

	if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		SetPageError(page);
	if (!PageError(page))
		SetPageUptodate(page);

	/*
	 * Run the hooks that have to be done when a page I/O has completed.
	 */
	if (PageTestandClearDecrAfter(page))
		atomic_dec(&nr_async_pages);

	UnlockPage(page);
	bio_put(bio);
	return 1;
}
#endif

static int bio_end_io_kio(struct bio *bio, int nr_sectors)
{
	struct kiobuf *kio = (struct kiobuf *) bio->bi_private;
	int uptodate, done;

	done = 0;
	uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	do {
		int sectors = bio->bi_io_vec[bio->bi_idx].bv_len >> 9;

		nr_sectors -= sectors;

		bio->bi_idx++;

		done = !end_kio_request(kio, uptodate);

		if (bio->bi_idx == bio->bi_vcnt)
			done = 1;

	} while (!done && nr_sectors > 0);

	/*
	 * all done
	 */
	if (done) {
		bio_put(bio);
		return 0;
	}

	return 1;
}

/*
 * obviously doesn't work for stacking drivers, but ll_rw_blk will split
 * bio for those
 */
int get_max_segments(kdev_t dev)
{
	int segments = MAX_SEGMENTS;
	request_queue_t *q;

	if ((q = blk_get_queue(dev)))
		segments = q->max_segments;

	return segments;
}

int get_max_sectors(kdev_t dev)
{
	int sectors = MAX_SECTORS;
	request_queue_t *q;

	if ((q = blk_get_queue(dev)))
		sectors = q->max_sectors;

	return sectors;
}

/**
 * ll_rw_kio - submit a &struct kiobuf for I/O
 * @rw:   %READ or %WRITE
 * @kio:   the kiobuf to do I/O on
 * @dev:   target device
 * @sector:   start location on disk
 *
 * Description:
 *   ll_rw_kio will map the page list inside the &struct kiobuf to
 *   &struct bio and queue them for I/O. The kiobuf given must describe
 *   a continous range of data, and must be fully prepared for I/O.
 **/
void ll_rw_kio(int rw, struct kiobuf *kio, kdev_t dev, sector_t sector)
{
	int i, offset, size, err, map_i, total_nr_pages, nr_pages;
	int max_bytes, max_segments;
	struct bio_vec *bvec;
	struct bio *bio;

	err = 0;
	if ((rw & WRITE) && is_read_only(dev)) {
		printk("ll_rw_bio: WRITE to ro device %s\n", kdevname(dev));
		err = -EPERM;
		goto out;
	}

	if (!kio->nr_pages) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * rudimentary max sectors/segments checks and setup. once we are
	 * sure that drivers can handle requests that cannot be completed in
	 * one go this will die
	 */
	max_bytes = get_max_sectors(dev) << 9;
	max_segments = get_max_segments(dev);
	if ((max_bytes >> PAGE_SHIFT) < (max_segments + 1))
		max_segments = (max_bytes >> PAGE_SHIFT) + 1;

	if (max_segments > BIO_MAX_PAGES)
		max_segments = BIO_MAX_PAGES;

	/*
	 * maybe kio is bigger than the max we can easily map into a bio.
	 * if so, split it up in appropriately sized chunks.
	 */
	total_nr_pages = kio->nr_pages;
	offset = kio->offset & ~PAGE_MASK;
	size = kio->length;

	/*
	 * set I/O count to number of pages for now
	 */
	atomic_set(&kio->io_count, total_nr_pages);

	map_i = 0;

next_chunk:
	if ((nr_pages = total_nr_pages) > max_segments)
		nr_pages = max_segments;

	/*
	 * allocate bio and do initial setup
	 */
	if ((bio = bio_alloc(GFP_NOIO, nr_pages)) == NULL) {
		err = -ENOMEM;
		goto out;
	}

	bio->bi_sector = sector;
	bio->bi_dev = dev;
	bio->bi_idx = 0;
	bio->bi_flags |= 1 << BIO_PREBUILT;
	bio->bi_end_io = bio_end_io_kio;
	bio->bi_private = kio;

	bvec = bio->bi_io_vec;
	for (i = 0; i < nr_pages; i++, bvec++, map_i++) {
		int nbytes = PAGE_SIZE - offset;

		if (nbytes > size)
			nbytes = size;

		BUG_ON(kio->maplist[map_i] == NULL);

		if (bio->bi_size + nbytes > max_bytes)
			goto queue_io;

		bio->bi_vcnt++;
		bio->bi_size += nbytes;

		bvec->bv_page = kio->maplist[map_i];
		bvec->bv_len = nbytes;
		bvec->bv_offset = offset;

		/*
		 * kiobuf only has an offset into the first page
		 */
		offset = 0;

		sector += nbytes >> 9;
		size -= nbytes;
		total_nr_pages--;
	}

queue_io:
	submit_bio(rw, bio);

	if (total_nr_pages)
		goto next_chunk;

	if (size) {
		printk("ll_rw_kio: size %d left (kio %d)\n", size, kio->length);
		BUG();
	}

out:
	if (err)
		kio->errno = err;
}

int bio_endio(struct bio *bio, int uptodate, int nr_sectors)
{
	if (uptodate)
		set_bit(BIO_UPTODATE, &bio->bi_flags);
	else
		clear_bit(BIO_UPTODATE, &bio->bi_flags);

	return bio->bi_end_io(bio, nr_sectors);
}

static int __init bio_init_pool(void)
{
	struct bio *bio;
	int i;

	for (i = 0; i < BIO_POOL_SIZE; i++) {
		bio = kmem_cache_alloc(bio_cachep, GFP_ATOMIC);
		if (!bio)
			panic("bio: cannot init bio pool\n");

		bio_pool_put(bio);
	}

	return i;
}

static void __init biovec_init_pool(void)
{
	char name[16];
	int i, size;

	memset(&bvec_list, 0, sizeof(bvec_list));

	for (i = 0; i < BIOVEC_NR_POOLS; i++) {
		struct biovec_pool *bp = &bvec_list[i];

		size = bvec_pool_sizes[i] * sizeof(struct bio_vec);

		printk("biovec: init pool %d, %d entries, %d bytes\n", i,
						bvec_pool_sizes[i], size);

		snprintf(name, sizeof(name) - 1,"biovec-%d",bvec_pool_sizes[i]);
		bp->bp_cachep = kmem_cache_create(name, size, 0,
						SLAB_HWCACHE_ALIGN, NULL, NULL);

		if (!bp->bp_cachep)
			panic("biovec: can't init slab pools\n");

		bp->bp_size = size;
		init_waitqueue_head(&bp->bp_wait);
	}
}

static int __init init_bio(void)
{
	int nr;

	bio_cachep = kmem_cache_create("bio", sizeof(struct bio), 0,
					SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!bio_cachep)
		panic("bio: can't create bio_cachep slab cache\n");

	nr = bio_init_pool();
	printk("BIO: pool of %d setup, %uKb (%d bytes/bio)\n", nr, nr * sizeof(struct bio) >> 10, sizeof(struct bio));

	biovec_init_pool();

	return 0;
}

module_init(init_bio);

EXPORT_SYMBOL(bio_alloc);
EXPORT_SYMBOL(bio_put);
EXPORT_SYMBOL(ll_rw_kio);
EXPORT_SYMBOL(bio_endio);
EXPORT_SYMBOL(bio_init);
EXPORT_SYMBOL(bio_copy);
EXPORT_SYMBOL(bio_clone);
