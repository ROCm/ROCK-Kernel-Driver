/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/blk.h>
#include <linux/blkpg.h>
#include <linux/bio.h>
#include <linux/mempool.h>
#include <linux/slab.h>

static const char *_name = DM_NAME;
#define MAX_DEVICES 256
#define SECTOR_SHIFT 9

static int major = 0;
static int _major = 0;

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

	kdev_t kdev;
	atomic_t holders;

	unsigned long flags;

	request_queue_t queue;
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
};

#define MIN_IOS 256
static kmem_cache_t *_io_cache;
static mempool_t *_io_pool;

static __init int local_init(void)
{
	int r;

	/* allocate a slab for the dm_ios */
	_io_cache = kmem_cache_create("dm io",
				      sizeof(struct dm_io), 0, 0, NULL, NULL);
	if (!_io_cache)
		return -ENOMEM;

	_io_pool = mempool_create(MIN_IOS, mempool_alloc_slab,
				  mempool_free_slab, _io_cache);
	if (!_io_pool) {
		kmem_cache_destroy(_io_cache);
		return -ENOMEM;
	}

	_major = major;
	r = register_blkdev(_major, _name, &dm_blk_dops);
	if (r < 0) {
		DMERR("register_blkdev failed");
		mempool_destroy(_io_pool);
		kmem_cache_destroy(_io_cache);
		return r;
	}

	if (!_major)
		_major = r;

	return 0;
}

static void local_exit(void)
{
	mempool_destroy(_io_pool);
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

static inline struct dm_io *alloc_io(void)
{
	return mempool_alloc(_io_pool, GFP_NOIO);
}

static inline void free_io(struct dm_io *io)
{
	mempool_free(io, _io_pool);
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

	if (!test_bit(DMF_SUSPENDED, &md->flags)) {
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

/*
 * Decrements the number of outstanding ios that a bio has been
 * cloned into, completing the original io if necc.
 */
static inline void dec_pending(struct dm_io *io, int error)
{
	static spinlock_t _uptodate_lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;

	spin_lock_irqsave(&_uptodate_lock, flags);
	if (error)
		io->error = error;
	spin_unlock_irqrestore(&_uptodate_lock, flags);

	if (atomic_dec_and_test(&io->io_count)) {
		if (atomic_dec_and_test(&io->md->pending))
			/* nudge anyone waiting on suspend queue */
			wake_up(&io->md->wait);

		bio_endio(io->bio, io->error ? 0 : io->bio->bi_size, io->error);
		free_io(io);
	}
}

static int clone_endio(struct bio *bio, unsigned int done, int error)
{
	struct dm_io *io = bio->bi_private;

	/*
	 * Only call dec_pending if the clone has completely
	 * finished.  If a partial io errors I'm assuming it won't
	 * be requeued.  FIXME: check this.
	 */
	if (error || !bio->bi_size) {
		dec_pending(io, error);
		bio_put(bio);
	}

	return 0;
}


static sector_t max_io_len(struct mapped_device *md,
			   sector_t sector, struct dm_target *ti)
{
	sector_t len = ti->len;

	/* FIXME: obey io_restrictions ! */

	/*
	 * Does the target need to split even further ?
	 */
	if (ti->split_io) {
		sector_t boundary;
		sector_t offset = sector - ti->begin;
		boundary = dm_round_up(offset + 1, ti->split_io) - offset;

		if (len > boundary)
			len = boundary;
	}

	return len;
}

static void __map_bio(struct dm_target *ti, struct bio *clone)
{
	struct dm_io *io = clone->bi_private;
	int r;

	/*
	 * Sanity checks.
	 */
	if (!clone->bi_size)
		BUG();

	/*
	 * Map the clone.  If r == 0 we don't need to do
	 * anything, the target has assumed ownership of
	 * this io.
	 */
	atomic_inc(&io->md->pending);
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
 * Issues a little bio that just does the back end of a split page.
 */
static void __split_page(struct clone_info *ci, unsigned int len)
{
	struct dm_target *ti = dm_table_find_target(ci->md->map, ci->sector);
	struct bio *clone, *bio = ci->bio;
	struct bio_vec *bv = bio->bi_io_vec + (bio->bi_vcnt - 1);

	DMWARN("splitting page");

	if (len > ci->sector_count)
		len = ci->sector_count;

	clone = bio_alloc(GFP_NOIO, 1);
	memcpy(clone->bi_io_vec, bv, sizeof(*bv));

	clone->bi_sector = ci->sector;
	clone->bi_bdev = bio->bi_bdev;
	clone->bi_flags = bio->bi_flags | (1 << BIO_SEG_VALID);
	clone->bi_rw = bio->bi_rw;
	clone->bi_size = len << SECTOR_SHIFT;
	clone->bi_end_io = clone_endio;
	clone->bi_private = ci->io;

	ci->sector += len;
	ci->sector_count -= len;

	__map_bio(ti, clone);
}

static void __clone_and_map(struct clone_info *ci)
{
	struct bio *clone, *bio = ci->bio;
	struct dm_target *ti = dm_table_find_target(ci->md->map, ci->sector);
	sector_t len = max_io_len(ci->md, bio->bi_sector, ti);

	/* shorter than current target ? */
	if (ci->sector_count < len)
		len = ci->sector_count;

	/* create the clone */
	clone = bio_clone(ci->bio, GFP_NOIO);
	clone->bi_sector = ci->sector;
	clone->bi_idx = ci->idx;
	clone->bi_size = len << SECTOR_SHIFT;
	clone->bi_end_io = clone_endio;
	clone->bi_private = ci->io;

	/* adjust the remaining io */
	ci->sector += len;
	ci->sector_count -= len;
	__map_bio(ti, clone);

	/*
	 * If we are not performing all remaining io in this
	 * clone then we need to calculate ci->idx for the next
	 * time round.
	 */
	if (ci->sector_count) {
		while (len) {
			struct bio_vec *bv = clone->bi_io_vec + ci->idx;
			sector_t bv_len = bv->bv_len >> SECTOR_SHIFT;
			if (bv_len <= len)
				len -= bv_len;

			else {
				__split_page(ci, bv_len - len);
				len = 0;
			}
			ci->idx++;
		}
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
	ci.io = alloc_io();
	ci.io->error = 0;
	atomic_set(&ci.io->io_count, 1);
	ci.io->bio = bio;
	ci.io->md = md;
	ci.sector = bio->bi_sector;
	ci.sector_count = bio_sectors(bio);
	ci.idx = 0;

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
			bio_io_error(bio, 0);
			return 0;
		}

		r = queue_io(md, bio);
		if (r < 0) {
			bio_io_error(bio, 0);
			return 0;

		} else if (r == 0)
			return 0;	/* deferred successfully */

		/*
		 * We're in a while loop, because someone could suspend
		 * before we get to the following read lock.
		 */
		down_read(&md->lock);
	}

	__split_bio(md, bio);
	up_read(&md->lock);
	return 0;
}

/*
 * See if the device with a specific minor # is free.
 */
static int specific_dev(int minor, struct mapped_device *md)
{
	struct gendisk *disk;
	int part;

	if (minor >= MAX_DEVICES) {
		DMWARN("request for a mapped_device beyond MAX_DEVICES (%d)",
		       MAX_DEVICES);
		return -EINVAL;
	}

	disk = get_gendisk(MKDEV(_major, minor), &part);
	if (disk) {
		put_disk(disk);
		return -EBUSY;
	}

	return minor;
}

static int any_old_dev(struct mapped_device *md)
{
	int i;

	for (i = 0; i < MAX_DEVICES; i++)
		if (specific_dev(i, md) >= 0) {
			DMWARN("allocating minor = %d", i);
			return i;
		}

	return -EBUSY;
}

/*
 * Allocate and initialise a blank device with a given minor.
 */
static struct mapped_device *alloc_dev(int minor)
{
	struct mapped_device *md = kmalloc(sizeof(*md), GFP_KERNEL);

	if (!md) {
		DMWARN("unable to allocate device, out of memory.");
		return NULL;
	}

	/* get a minor number for the dev */
	minor = (minor < 0) ? any_old_dev(md) : specific_dev(minor, md);
	if (minor < 0) {
		kfree(md);
		return NULL;
	}

	memset(md, 0, sizeof(*md));
	init_rwsem(&md->lock);
	md->kdev = mk_kdev(_major, minor);
	atomic_set(&md->holders, 1);

	md->queue.queuedata = md;
	blk_queue_make_request(&md->queue, dm_request);

	md->disk = alloc_disk(1);
	if (!md->disk) {
		kfree(md);
		return NULL;
	}

	md->disk->major = _major;
	md->disk->first_minor = minor;
	md->disk->fops = &dm_blk_dops;
	md->disk->queue = &md->queue;
	md->disk->private_data = md;
	sprintf(md->disk->disk_name, "dm-%d", minor);
	add_disk(md->disk);

	atomic_set(&md->pending, 0);
	init_waitqueue_head(&md->wait);
	return md;
}

static void free_dev(struct mapped_device *md)
{
	del_gendisk(md->disk);
	put_disk(md->disk);
	kfree(md);
}

/*
 * Bind a table to the device.
 */
static int __bind(struct mapped_device *md, struct dm_table *t)
{
	request_queue_t *q = &md->queue;
	sector_t size;
	md->map = t;

	size = dm_table_get_size(t);
	set_capacity(md->disk, size);
	if (size == 0)
		return 0;

	dm_table_get(t);
	dm_table_set_restrictions(t, q);
	return 0;
}

static void __unbind(struct mapped_device *md)
{
	dm_table_put(md->map);
	md->map = NULL;
	set_capacity(md->disk, 0);
}

/*
 * Constructor for a new device.
 */
int dm_create(int minor, struct dm_table *table, struct mapped_device **result)
{
	int r;
	struct mapped_device *md;

	md = alloc_dev(minor);
	if (!md)
		return -ENXIO;

	r = __bind(md, table);
	if (r) {
		free_dev(md);
		return r;
	}

	*result = md;
	return 0;
}

void dm_get(struct mapped_device *md)
{
	atomic_inc(&md->holders);
}

void dm_put(struct mapped_device *md)
{
	if (atomic_dec_and_test(&md->holders)) {
		DMWARN("destroying md");
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
	up_write(&md->lock);

	/*
	 * Then we wait for the already mapped ios to
	 * complete.
	 */
	down_read(&md->lock);

	add_wait_queue(&md->wait, &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!atomic_read(&md->pending))
			break;

		yield();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&md->wait, &wait);
	up_read(&md->lock);

	/* set_bit is atomic */
	set_bit(DMF_SUSPENDED, &md->flags);

	return 0;
}

int dm_resume(struct mapped_device *md)
{
	struct deferred_io *def;

	down_write(&md->lock);
	if (!test_bit(DMF_SUSPENDED, &md->flags) ||
	    !dm_table_get_size(md->map)) {
		up_write(&md->lock);
		return -EINVAL;
	}

	clear_bit(DMF_SUSPENDED, &md->flags);
	clear_bit(DMF_BLOCK_IO, &md->flags);
	def = md->deferred;
	md->deferred = NULL;
	up_write(&md->lock);

	flush_deferred_io(def);
	blk_run_queues();

	return 0;
}

struct gendisk *dm_disk(struct mapped_device *md)
{
	return md->disk;
}

struct dm_table *dm_get_table(struct mapped_device *md)
{
	struct dm_table *t;

	down_read(&md->lock);
	t = md->map;
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

MODULE_PARM(major, "i");
MODULE_PARM_DESC(major, "The major number of the device mapper");
MODULE_DESCRIPTION(DM_NAME " driver");
MODULE_AUTHOR("Joe Thornber <thornber@sistina.com>");
MODULE_LICENSE("GPL");
