/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/blkpg.h>
#include <linux/bio.h>
#include <linux/mempool.h>
#include <linux/slab.h>

static const char *_name = DM_NAME;
#define MAX_DEVICES 1024

static unsigned int major = 0;
static unsigned int _major = 0;

struct dm_io {
	struct mapped_device *md;
	int error;
	struct bio *bio;
	atomic_t io_count;
};

struct deferred_io {
	struct bio *bio;
	struct deferred_io *next;
};

/*
 * Bits for the md->flags field.
 */
#define DMF_BLOCK_IO 0
#define DMF_SUSPENDED 1

struct mapped_device {
	struct rw_semaphore lock;
	atomic_t holders;

	unsigned long flags;

	request_queue_t *queue;
	struct gendisk *disk;

	/*
	 * A list of ios that arrived while we were suspended.
	 */
	atomic_t pending;
	wait_queue_head_t wait;
	struct deferred_io *deferred;

	/*
	 * The current mapping.
	 */
	struct dm_table *map;

	/*
	 * io objects are allocated from here.
	 */
	mempool_t *io_pool;

	/*
	 * Event handling.
	 */
	uint32_t event_nr;
	wait_queue_head_t eventq;
};

#define MIN_IOS 256
static kmem_cache_t *_io_cache;

static __init int local_init(void)
{
	int r;

	/* allocate a slab for the dm_ios */
	_io_cache = kmem_cache_create("dm_io",
				      sizeof(struct dm_io), 0, 0, NULL, NULL);
	if (!_io_cache)
		return -ENOMEM;

	_major = major;
	r = register_blkdev(_major, _name);
	if (r < 0) {
		kmem_cache_destroy(_io_cache);
		return r;
	}

	if (!_major)
		_major = r;

	return 0;
}

static void local_exit(void)
{
	kmem_cache_destroy(_io_cache);

	if (unregister_blkdev(_major, _name) < 0)
		DMERR("devfs_unregister_blkdev failed");

	_major = 0;

	DMINFO("cleaned up");
}

/*
 * We have a lot of init/exit functions, so it seems easier to
 * store them in an array.  The disposable macro 'xx'
 * expands a prefix into a pair of function names.
 */
static struct {
	int (*init) (void);
	void (*exit) (void);

} _inits[] = {
#define xx(n) {n ## _init, n ## _exit},
	xx(local)
	xx(dm_target)
	xx(dm_linear)
	xx(dm_stripe)
	xx(dm_interface)
#undef xx
};

static int __init dm_init(void)
{
	const int count = ARRAY_SIZE(_inits);

	int r, i;

	for (i = 0; i < count; i++) {
		r = _inits[i].init();
		if (r)
			goto bad;
	}

	return 0;

      bad:
	while (i--)
		_inits[i].exit();

	return r;
}

static void __exit dm_exit(void)
{
	int i = ARRAY_SIZE(_inits);

	while (i--)
		_inits[i].exit();
}

/*
 * Block device functions
 */
static int dm_blk_open(struct inode *inode, struct file *file)
{
	struct mapped_device *md;

	md = inode->i_bdev->bd_disk->private_data;
	dm_get(md);
	return 0;
}

static int dm_blk_close(struct inode *inode, struct file *file)
{
	struct mapped_device *md;

	md = inode->i_bdev->bd_disk->private_data;
	dm_put(md);
	return 0;
}

static inline struct dm_io *alloc_io(struct mapped_device *md)
{
	return mempool_alloc(md->io_pool, GFP_NOIO);
}

static inline void free_io(struct mapped_device *md, struct dm_io *io)
{
	mempool_free(io, md->io_pool);
}

static inline struct deferred_io *alloc_deferred(void)
{
	return kmalloc(sizeof(struct deferred_io), GFP_NOIO);
}

static inline void free_deferred(struct deferred_io *di)
{
	kfree(di);
}

/*
 * Add the bio to the list of deferred io.
 */
static int queue_io(struct mapped_device *md, struct bio *bio)
{
	struct deferred_io *di;

	di = alloc_deferred();
	if (!di)
		return -ENOMEM;

	down_write(&md->lock);

	if (!test_bit(DMF_BLOCK_IO, &md->flags)) {
		up_write(&md->lock);
		free_deferred(di);
		return 1;
	}

	di->bio = bio;
	di->next = md->deferred;
	md->deferred = di;

	up_write(&md->lock);
	return 0;		/* deferred successfully */
}

/*-----------------------------------------------------------------
 * CRUD START:
 *   A more elegant soln is in the works that uses the queue
 *   merge fn, unfortunately there are a couple of changes to
 *   the block layer that I want to make for this.  So in the
 *   interests of getting something for people to use I give
 *   you this clearly demarcated crap.
 *---------------------------------------------------------------*/
static inline sector_t to_sector(unsigned int bytes)
{
	return bytes >> SECTOR_SHIFT;
}

static inline unsigned int to_bytes(sector_t sector)
{
	return sector << SECTOR_SHIFT;
}

/*
 * Decrements the number of outstanding ios that a bio has been
 * cloned into, completing the original io if necc.
 */
static inline void dec_pending(struct dm_io *io, int error)
{
	static spinlock_t _uptodate_lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;

	if (error) {
		spin_lock_irqsave(&_uptodate_lock, flags);
		io->error = error;
		spin_unlock_irqrestore(&_uptodate_lock, flags);
	}

	if (atomic_dec_and_test(&io->io_count)) {
		if (atomic_dec_and_test(&io->md->pending))
			/* nudge anyone waiting on suspend queue */
			wake_up(&io->md->wait);

		bio_endio(io->bio, io->bio->bi_size, io->error);
		free_io(io->md, io);
	}
}

static int clone_endio(struct bio *bio, unsigned int done, int error)
{
	struct dm_io *io = bio->bi_private;

	if (bio->bi_size)
		return 1;

	dec_pending(io, error);
	bio_put(bio);
	return 0;
}


static sector_t max_io_len(struct mapped_device *md,
			   sector_t sector, struct dm_target *ti)
{
	sector_t offset = sector - ti->begin;
	sector_t len = ti->len - offset;

	/*
	 * Does the target need to split even further ?
	 */
	if (ti->split_io) {
		sector_t boundary;
		boundary = dm_round_up(offset + 1, ti->split_io) - offset;

		if (len > boundary)
			len = boundary;
	}

	return len;
}

static void __map_bio(struct dm_target *ti, struct bio *clone, struct dm_io *io)
{
	int r;

	/*
	 * Sanity checks.
	 */
	BUG_ON(!clone->bi_size);

	clone->bi_end_io = clone_endio;
	clone->bi_private = io;

	/*
	 * Map the clone.  If r == 0 we don't need to do
	 * anything, the target has assumed ownership of
	 * this io.
	 */
	atomic_inc(&io->io_count);
	r = ti->type->map(ti, clone);
	if (r > 0)
		/* the bio has been remapped so dispatch it */
		generic_make_request(clone);

	else if (r < 0)
		/* error the io and bail out */
		dec_pending(io, -EIO);
}

struct clone_info {
	struct mapped_device *md;
	struct bio *bio;
	struct dm_io *io;
	sector_t sector;
	sector_t sector_count;
	unsigned short idx;
};

/*
 * Creates a little bio that is just does part of a bvec.
 */
static struct bio *split_bvec(struct bio *bio, sector_t sector,
			      unsigned short idx, unsigned int offset,
			      unsigned int len)
{
	struct bio *clone;
	struct bio_vec *bv = bio->bi_io_vec + idx;

	clone = bio_alloc(GFP_NOIO, 1);
	memcpy(clone->bi_io_vec, bv, sizeof(*bv));

	clone->bi_sector = sector;
	clone->bi_bdev = bio->bi_bdev;
	clone->bi_rw = bio->bi_rw;
	clone->bi_vcnt = 1;
	clone->bi_size = to_bytes(len);
	clone->bi_io_vec->bv_offset = offset;
	clone->bi_io_vec->bv_len = clone->bi_size;

	return clone;
}

/*
 * Creates a bio that consists of range of complete bvecs.
 */
static struct bio *clone_bio(struct bio *bio, sector_t sector,
			     unsigned short idx, unsigned short bv_count,
			     unsigned int len)
{
	struct bio *clone;

	clone = bio_clone(bio, GFP_NOIO);
	clone->bi_sector = sector;
	clone->bi_idx = idx;
	clone->bi_vcnt = idx + bv_count;
	clone->bi_size = to_bytes(len);

	return clone;
}

static void __clone_and_map(struct clone_info *ci)
{
	struct bio *clone, *bio = ci->bio;
	struct dm_target *ti = dm_table_find_target(ci->md->map, ci->sector);
	sector_t len = 0, max = max_io_len(ci->md, ci->sector, ti);

	if (ci->sector_count <= max) {
		/*
		 * Optimise for the simple case where we can do all of
		 * the remaining io with a single clone.
		 */
		clone = clone_bio(bio, ci->sector, ci->idx,
				  bio->bi_vcnt - ci->idx, ci->sector_count);
		__map_bio(ti, clone, ci->io);
		ci->sector_count = 0;

	} else if (to_sector(bio->bi_io_vec[ci->idx].bv_len) <= max) {
		/*
		 * There are some bvecs that don't span targets.
		 * Do as many of these as possible.
		 */
		int i;
		sector_t remaining = max;
		sector_t bv_len;

		for (i = ci->idx; remaining && (i < bio->bi_vcnt); i++) {
			bv_len = to_sector(bio->bi_io_vec[i].bv_len);

			if (bv_len > remaining)
				break;

			remaining -= bv_len;
			len += bv_len;
		}

		clone = clone_bio(bio, ci->sector, ci->idx, i - ci->idx, len);
		__map_bio(ti, clone, ci->io);

		ci->sector += len;
		ci->sector_count -= len;
		ci->idx = i;

	} else {
		/*
		 * Create two copy bios to deal with io that has
		 * been split across a target.
		 */
		struct bio_vec *bv = bio->bi_io_vec + ci->idx;

		clone = split_bvec(bio, ci->sector, ci->idx,
				   bv->bv_offset, max);
		__map_bio(ti, clone, ci->io);

		ci->sector += max;
		ci->sector_count -= max;
		ti = dm_table_find_target(ci->md->map, ci->sector);

		len = to_sector(bv->bv_len) - max;
		clone = split_bvec(bio, ci->sector, ci->idx,
				   bv->bv_offset + to_bytes(max), len);
		__map_bio(ti, clone, ci->io);

		ci->sector += len;
		ci->sector_count -= len;
		ci->idx++;
	}
}

/*
 * Split the bio into several clones.
 */
static void __split_bio(struct mapped_device *md, struct bio *bio)
{
	struct clone_info ci;

	ci.md = md;
	ci.bio = bio;
	ci.io = alloc_io(md);
	ci.io->error = 0;
	atomic_set(&ci.io->io_count, 1);
	ci.io->bio = bio;
	ci.io->md = md;
	ci.sector = bio->bi_sector;
	ci.sector_count = bio_sectors(bio);
	ci.idx = bio->bi_idx;

	atomic_inc(&md->pending);
	while (ci.sector_count)
		__clone_and_map(&ci);

	/* drop the extra reference count */
	dec_pending(ci.io, 0);
}
/*-----------------------------------------------------------------
 * CRUD END
 *---------------------------------------------------------------*/


/*
 * The request function that just remaps the bio built up by
 * dm_merge_bvec.
 */
static int dm_request(request_queue_t *q, struct bio *bio)
{
	int r;
	struct mapped_device *md = q->queuedata;

	down_read(&md->lock);

	/*
	 * If we're suspended we have to queue
	 * this io for later.
	 */
	while (test_bit(DMF_BLOCK_IO, &md->flags)) {
		up_read(&md->lock);

		if (bio_rw(bio) == READA) {
			bio_io_error(bio, bio->bi_size);
			return 0;
		}

		r = queue_io(md, bio);
		if (r < 0) {
			bio_io_error(bio, bio->bi_size);
			return 0;

		} else if (r == 0)
			return 0;	/* deferred successfully */

		/*
		 * We're in a while loop, because someone could suspend
		 * before we get to the following read lock.
		 */
		down_read(&md->lock);
	}

	if (!md->map) {
		bio_io_error(bio, bio->bi_size);
		return 0;
	}

	__split_bio(md, bio);
	up_read(&md->lock);
	return 0;
}

/*-----------------------------------------------------------------
 * A bitset is used to keep track of allocated minor numbers.
 *---------------------------------------------------------------*/
static spinlock_t _minor_lock = SPIN_LOCK_UNLOCKED;
static unsigned long _minor_bits[MAX_DEVICES / BITS_PER_LONG];

static void free_minor(unsigned int minor)
{
	spin_lock(&_minor_lock);
	clear_bit(minor, _minor_bits);
	spin_unlock(&_minor_lock);
}

/*
 * See if the device with a specific minor # is free.
 */
static int specific_minor(unsigned int minor)
{
	int r = -EBUSY;

	if (minor >= MAX_DEVICES) {
		DMWARN("request for a mapped_device beyond MAX_DEVICES (%d)",
		       MAX_DEVICES);
		return -EINVAL;
	}

	spin_lock(&_minor_lock);
	if (!test_and_set_bit(minor, _minor_bits))
		r = 0;
	spin_unlock(&_minor_lock);

	return r;
}

static int next_free_minor(unsigned int *minor)
{
	int r = -EBUSY;
	unsigned int m;

	spin_lock(&_minor_lock);
	m = find_first_zero_bit(_minor_bits, MAX_DEVICES);
	if (m != MAX_DEVICES) {
		set_bit(m, _minor_bits);
		*minor = m;
		r = 0;
	}
	spin_unlock(&_minor_lock);

	return r;
}

/*
 * Allocate and initialise a blank device with a given minor.
 */
static struct mapped_device *alloc_dev(unsigned int minor, int persistent)
{
	int r;
	struct mapped_device *md = kmalloc(sizeof(*md), GFP_KERNEL);

	if (!md) {
		DMWARN("unable to allocate device, out of memory.");
		return NULL;
	}

	/* get a minor number for the dev */
	r = persistent ? specific_minor(minor) : next_free_minor(&minor);
	if (r < 0) {
		kfree(md);
		return NULL;
	}

	memset(md, 0, sizeof(*md));
	init_rwsem(&md->lock);
	atomic_set(&md->holders, 1);

	md->queue = blk_alloc_queue(GFP_KERNEL);
	if (!md->queue) {
		kfree(md);
		return NULL;
	}

	md->queue->queuedata = md;
	blk_queue_make_request(md->queue, dm_request);

	md->io_pool = mempool_create(MIN_IOS, mempool_alloc_slab,
				     mempool_free_slab, _io_cache);
	if (!md->io_pool) {
		free_minor(minor);
		blk_put_queue(md->queue);
		kfree(md);
		return NULL;
	}

	md->disk = alloc_disk(1);
	if (!md->disk) {
		mempool_destroy(md->io_pool);
		free_minor(minor);
		blk_put_queue(md->queue);
		kfree(md);
		return NULL;
	}

	md->disk->major = _major;
	md->disk->first_minor = minor;
	md->disk->fops = &dm_blk_dops;
	md->disk->queue = md->queue;
	md->disk->private_data = md;
	sprintf(md->disk->disk_name, "dm-%d", minor);
	add_disk(md->disk);

	atomic_set(&md->pending, 0);
	init_waitqueue_head(&md->wait);
	init_waitqueue_head(&md->eventq);

	return md;
}

static void free_dev(struct mapped_device *md)
{
	free_minor(md->disk->first_minor);
	mempool_destroy(md->io_pool);
	del_gendisk(md->disk);
	put_disk(md->disk);
	blk_put_queue(md->queue);
	kfree(md);
}

/*
 * Bind a table to the device.
 */
static void event_callback(void *context)
{
	struct mapped_device *md = (struct mapped_device *) context;

	down_write(&md->lock);
	md->event_nr++;
	wake_up_interruptible(&md->eventq);
	up_write(&md->lock);
}

static int __bind(struct mapped_device *md, struct dm_table *t)
{
	request_queue_t *q = md->queue;
	sector_t size;
	md->map = t;

	size = dm_table_get_size(t);
	set_capacity(md->disk, size);
	if (size == 0)
		return 0;

	dm_table_event_callback(md->map, event_callback, md);

	dm_table_get(t);
	dm_table_set_restrictions(t, q);
	return 0;
}

static void __unbind(struct mapped_device *md)
{
	if (!md->map)
		return;

	dm_table_event_callback(md->map, NULL, NULL);
	dm_table_put(md->map);
	md->map = NULL;
	set_capacity(md->disk, 0);
}

/*
 * Constructor for a new device.
 */
static int create_aux(unsigned int minor, int persistent,
		      struct mapped_device **result)
{
	struct mapped_device *md;

	md = alloc_dev(minor, persistent);
	if (!md)
		return -ENXIO;

	*result = md;
	return 0;
}

int dm_create(struct mapped_device **result)
{
	return create_aux(0, 0, result);
}

int dm_create_with_minor(unsigned int minor, struct mapped_device **result)
{
	return create_aux(minor, 1, result);
}

void dm_get(struct mapped_device *md)
{
	atomic_inc(&md->holders);
}

void dm_put(struct mapped_device *md)
{
	if (atomic_dec_and_test(&md->holders)) {
		if (!test_bit(DMF_SUSPENDED, &md->flags) && md->map)
			dm_table_suspend_targets(md->map);
		__unbind(md);
		free_dev(md);
	}
}

/*
 * Requeue the deferred bios by calling generic_make_request.
 */
static void flush_deferred_io(struct deferred_io *c)
{
	struct deferred_io *n;

	while (c) {
		n = c->next;
		generic_make_request(c->bio);
		free_deferred(c);
		c = n;
	}
}

/*
 * Swap in a new table (destroying old one).
 */
int dm_swap_table(struct mapped_device *md, struct dm_table *table)
{
	int r;

	down_write(&md->lock);

	/* device must be suspended */
	if (!test_bit(DMF_SUSPENDED, &md->flags)) {
		up_write(&md->lock);
		return -EPERM;
	}

	__unbind(md);
	r = __bind(md, table);
	if (r)
		return r;

	up_write(&md->lock);
	return 0;
}

/*
 * We need to be able to change a mapping table under a mounted
 * filesystem.  For example we might want to move some data in
 * the background.  Before the table can be swapped with
 * dm_bind_table, dm_suspend must be called to flush any in
 * flight bios and ensure that any further io gets deferred.
 */
int dm_suspend(struct mapped_device *md)
{
	DECLARE_WAITQUEUE(wait, current);

	down_write(&md->lock);

	/*
	 * First we set the BLOCK_IO flag so no more ios will be
	 * mapped.
	 */
	if (test_bit(DMF_BLOCK_IO, &md->flags)) {
		up_write(&md->lock);
		return -EINVAL;
	}

	set_bit(DMF_BLOCK_IO, &md->flags);
	add_wait_queue(&md->wait, &wait);
	up_write(&md->lock);

	/*
	 * Then we wait for the already mapped ios to
	 * complete.
	 */
	blk_run_queues();
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!atomic_read(&md->pending))
			break;

		io_schedule();
	}
	set_current_state(TASK_RUNNING);

	down_write(&md->lock);
	remove_wait_queue(&md->wait, &wait);
	set_bit(DMF_SUSPENDED, &md->flags);
	if (md->map)
		dm_table_suspend_targets(md->map);
	up_write(&md->lock);

	return 0;
}

int dm_resume(struct mapped_device *md)
{
	struct deferred_io *def;

	down_write(&md->lock);
	if (!md->map ||
	    !test_bit(DMF_SUSPENDED, &md->flags) ||
	    !dm_table_get_size(md->map)) {
		up_write(&md->lock);
		return -EINVAL;
	}

	dm_table_resume_targets(md->map);
	clear_bit(DMF_SUSPENDED, &md->flags);
	clear_bit(DMF_BLOCK_IO, &md->flags);
	def = md->deferred;
	md->deferred = NULL;
	up_write(&md->lock);

	flush_deferred_io(def);
	blk_run_queues();

	return 0;
}

/*-----------------------------------------------------------------
 * Event notification.
 *---------------------------------------------------------------*/
uint32_t dm_get_event_nr(struct mapped_device *md)
{
	uint32_t r;

	down_read(&md->lock);
	r = md->event_nr;
	up_read(&md->lock);

	return r;
}

int dm_add_wait_queue(struct mapped_device *md, wait_queue_t *wq,
		      uint32_t event_nr)
{
	down_write(&md->lock);
	if (event_nr != md->event_nr) {
		up_write(&md->lock);
		return 1;
	}

	add_wait_queue(&md->eventq, wq);
	up_write(&md->lock);

	return 0;
}

void dm_remove_wait_queue(struct mapped_device *md, wait_queue_t *wq)
{
	down_write(&md->lock);
	remove_wait_queue(&md->eventq, wq);
	up_write(&md->lock);
}

/*
 * The gendisk is only valid as long as you have a reference
 * count on 'md'.
 */
struct gendisk *dm_disk(struct mapped_device *md)
{
	return md->disk;
}

struct dm_table *dm_get_table(struct mapped_device *md)
{
	struct dm_table *t;

	down_read(&md->lock);
	t = md->map;
	if (t)
		dm_table_get(t);
	up_read(&md->lock);

	return t;
}

int dm_suspended(struct mapped_device *md)
{
	return test_bit(DMF_SUSPENDED, &md->flags);
}

struct block_device_operations dm_blk_dops = {
	.open = dm_blk_open,
	.release = dm_blk_close,
	.owner = THIS_MODULE
};

/*
 * module hooks
 */
module_init(dm_init);
module_exit(dm_exit);

module_param(major, uint, 0);
MODULE_PARM_DESC(major, "The major number of the device mapper");
MODULE_DESCRIPTION(DM_NAME " driver");
MODULE_AUTHOR("Joe Thornber <thornber@sistina.com>");
MODULE_LICENSE("GPL");
