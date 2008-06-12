/*
 * Copyright (C) 2003 Sistina Software Limited.
 * Copyright (C) 2004-2007 Red Hat Inc.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include <linux/dm-dirty-log.h>
#include "dm-region_hash.h"

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#define	DM_MSG_PREFIX	"region hash"

/*-----------------------------------------------------------------
 * Region hash
 *
 * The set splits itself up into discrete regions.
 * Each region can be in one of three states:
 *
 * o clean
 * o dirty,
 * o nosync.
 *
 * There is no need to put clean regions in the hash.
 *
 *
 * In addition to being present in the hash table a region _may_
 * be present on one of three lists.
 *
 *   clean_regions: Regions on this list have no io pending to
 *   them, they are in sync, we are no longer interested in them,
 *   they are dull.  rh_update_states() will remove them from the
 *   hash table.
 *
 *   quiesced_regions: These regions have been spun down, ready
 *   for recovery.  rh_recovery_start() will remove regions from
 *   this list and hand them to kmirrord, which will schedule the
 *   recovery io with kcopyd.
 *
 *   recovered_regions: Regions that kcopyd has successfully
 *   recovered.  rh_update_states() will now schedule any delayed
 *   io, up the recovery_count, and remove the region from the hash.
 *
 * There are 2 locks:
 *   A rw spin lock 'hash_lock' protects just the hash table,
 *   this is never held in write mode from interrupt context,
 *   which I believe means that we only have to disable irqs when
 *   doing a write lock.
 *
 *   An ordinary spin lock 'region_lock' that protects the three
 *   lists in the region_hash, with the 'state', 'list' and
 *   'delayed_bios' fields of the regions.  This is used from irq
 *   context, so all other uses will have to suspend local irqs.
 *---------------------------------------------------------------*/
enum region_hash_flags {
	RECOVERY,
};

struct region_hash {
	unsigned int max_recovery; /* Max # of regions to recover in parallel */
	unsigned long flags;

	/* Callback function to dispatch queued writes on recovered regions. */
	void (*dispatch)(void *context, struct bio_list *bios);
	void *dispatch_context;

	/* Callback function to wakeup callers worker thread. */
	void (*wake)(void *context);
	void *wake_context;

	uint32_t region_size;
	unsigned int region_shift;

	/* holds persistent region state */
	struct dm_dirty_log *log;

	/* hash table */
	rwlock_t hash_lock;
	mempool_t *region_pool;
	unsigned int mask;
	unsigned int nr_buckets;
	unsigned int prime;
	unsigned int shift;
	struct list_head *buckets;

	spinlock_t region_lock;
	struct semaphore recovery_count;
	struct list_head clean_regions;
	struct list_head quiesced_regions;
	struct list_head recovered_regions;
};

struct region {
	struct region_hash *rh;	/* FIXME: can we get rid of this ? */
	region_t key;
	int state;
	void *context;	/* Caller context. */

	struct list_head hash_list;
	struct list_head list;

	atomic_t pending;
	struct bio_list delayed_bios;
};

/*
 * Conversion fns
 */
region_t rh_sector_to_region(void *rh, sector_t sector)
{
	return sector >> ((struct region_hash*) rh)->region_shift;
}

region_t rh_bio_to_region(void *rh, struct bio *bio)
{
	return rh_sector_to_region(rh, bio->bi_sector);
}

sector_t rh_region_to_sector(void *rh, region_t region)
{
	return region << ((struct region_hash*) rh)->region_shift;
}

/*
 * Retrival fns.
 */
region_t rh_get_region_key(void *reg)
{
	return ((struct region *)reg)->key;
}

sector_t rh_get_region_size(void *rh)
{
	return ((struct region_hash *)rh)->region_size;
}

/* Squirrel a context with a region. */
void *rh_reg_get_context(void *reg)
{
	return ((struct region*) reg)->context;
}

void rh_reg_set_context(void *reg, void *context)
{
	((struct region*) reg)->context = context;
}

/*
 * Region struct allocation/free.
 */
static void *region_alloc(unsigned int gfp_mask, void *pool_data)
{
	return kmalloc(sizeof(struct region), gfp_mask);
}

static void region_free(void *element, void *pool_data)
{
	kfree(element);
}

#define MIN_REGIONS 64
int rh_init(void **region_hash,
	    unsigned int max_recovery,

            void (*dispatch)(void *dispatch_context, struct bio_list *bios),
	    void *dispatch_context,
            void (*wake)(void *wake_context),
	    void *wake_context,
	    struct dm_dirty_log *log, uint32_t region_size, region_t nr_regions)
{
	unsigned int nr_buckets, max_buckets;
	unsigned hash_primes[] = {
		/* Table of primes for rh_hash/table size optimization. */
		3, 7, 13, 27, 53, 97, 193, 389, 769,
		1543, 3079, 6151, 12289, 24593,
	};
	size_t i;
	struct region_hash *rh;

	if (region_size & (region_size - 1)) {
		DMERR("region size must be 2^^n");
		return -EINVAL;
	}

	rh = kmalloc(sizeof(*rh), GFP_KERNEL);
	if (!rh) {
		DMERR("unable to allocate region hash memory");
		return -ENOMEM;
	}

	rh->max_recovery = max_recovery;
	rh->dispatch = dispatch;
	rh->dispatch_context = dispatch_context;
	rh->wake = wake;
	rh->wake_context = wake_context;
	rh->log = log;
	rh->region_size = region_size;
	rh->region_shift = ffs(region_size) - 1;
	rwlock_init(&rh->hash_lock);

	/* Calculate a suitable number of buckets for our hash table. */
	max_buckets = nr_regions >> 6;
	for (nr_buckets = 128u; nr_buckets < max_buckets; nr_buckets <<= 1);
	nr_buckets >>= 1;
	rh->mask = rh->nr_buckets = nr_buckets;
	rh->mask--;
	rh->shift = ffs(nr_buckets);
	rh->prime = hash_primes[rh->shift - 1];
	if (rh->prime > ARRAY_SIZE(hash_primes) - 2)
		rh->prime = ARRAY_SIZE(hash_primes) - 1;

	rh->buckets = vmalloc(nr_buckets * sizeof(*rh->buckets));
	if (!rh->buckets) {
		DMERR("unable to allocate region hash bucket memory");
		vfree(rh);
		return -ENOMEM;
	}

	for (i = 0; i < nr_buckets; i++)
		INIT_LIST_HEAD(rh->buckets + i);

	spin_lock_init(&rh->region_lock);
	sema_init(&rh->recovery_count, 0);
	INIT_LIST_HEAD(&rh->clean_regions);
	INIT_LIST_HEAD(&rh->quiesced_regions);
	INIT_LIST_HEAD(&rh->recovered_regions);

	rh->region_pool = mempool_create(MIN_REGIONS, region_alloc,
					 region_free, NULL);
	if (!rh->region_pool) {
		vfree(rh->buckets);
		vfree(rh);
		return -ENOMEM;
	}

	*region_hash = rh;

	return 0;
}

void rh_exit(void *v)
{
	unsigned int h;
	struct region *reg, *tmp;
	struct region_hash *rh = v;

	BUG_ON(!list_empty(&rh->quiesced_regions));

	for (h = 0; h < rh->nr_buckets; h++) {
		list_for_each_entry_safe(reg, tmp, rh->buckets + h, hash_list) {
			BUG_ON(atomic_read(&reg->pending));
			mempool_free(reg, rh->region_pool);
		}
	}

	dm_dirty_log_destroy(rh->log);

	if (rh->region_pool)
		mempool_destroy(rh->region_pool);

	vfree(rh->buckets);
	kfree(rh);
}

static inline unsigned int rh_hash(struct region_hash *rh, region_t region)
{
	return (unsigned int) ((region * rh->prime) >> rh->shift) & rh->mask;
}

static struct region *__rh_lookup(struct region_hash *rh, region_t region)
{
	struct region *reg;
	struct list_head *bucket = rh->buckets + rh_hash(rh, region);

	list_for_each_entry(reg, bucket, hash_list) {
		if (reg->key == region)
			return reg;
	}

	return NULL;
}

static void __rh_insert(struct region_hash *rh, struct region *reg)
{
	unsigned int h = rh_hash(rh, reg->key);
	list_add(&reg->hash_list, rh->buckets + h);
}

static struct region *__rh_alloc(struct region_hash *rh, region_t region)
{
	struct region *reg, *nreg;

	read_unlock(&rh->hash_lock);

	nreg = mempool_alloc(rh->region_pool, GFP_NOIO);
	nreg->state = rh->log->type->in_sync(rh->log, region, 1) ?
		      RH_CLEAN : RH_NOSYNC;
	nreg->rh = rh;
	nreg->key = region;

	INIT_LIST_HEAD(&nreg->list);

	atomic_set(&nreg->pending, 0);
	bio_list_init(&nreg->delayed_bios);

	write_lock_irq(&rh->hash_lock);

	reg = __rh_lookup(rh, region);
	if (reg)
		/* we lost the race */
		mempool_free(nreg, rh->region_pool);
	else {
		__rh_insert(rh, nreg);
		if (nreg->state == RH_CLEAN) {
			spin_lock(&rh->region_lock);
			list_add(&nreg->list, &rh->clean_regions);
			spin_unlock(&rh->region_lock);
		}
		reg = nreg;
	}

	write_unlock_irq(&rh->hash_lock);
	read_lock(&rh->hash_lock);

	return reg;
}

static inline struct region *__rh_find(struct region_hash *rh, region_t region)
{
	struct region *reg;

	reg = __rh_lookup(rh, region);
	if (!reg)
		reg = __rh_alloc(rh, region);

	return reg;
}

int rh_state(void *v, region_t region, int may_block)
{
	int r = 0;
	struct region *reg;
	struct region_hash *rh = v;

	read_lock(&rh->hash_lock);
	reg = __rh_lookup(rh, region);
	if (reg)
		r = reg->state;
	read_unlock(&rh->hash_lock);

	if (r)
		return r;

	/*
	 * The region wasn't in the hash, so we fall back to the dirty log.
	 */
	r = rh->log->type->in_sync(rh->log, region, may_block);

	/*
	 * Any error from the dirty log (eg. -EWOULDBLOCK) gets
	 * taken as a RH_NOSYNC
	 */
	return r == 1 ? RH_CLEAN : RH_NOSYNC;
}

void rh_update_states(void *v)
{
	struct region *reg, *next;
	struct region_hash *rh = v;
	LIST_HEAD(clean);
	LIST_HEAD(recovered);

	/*
	 * Quickly grab the lists.
	 */
	write_lock_irq(&rh->hash_lock);
	spin_lock(&rh->region_lock);
	if (!list_empty(&rh->clean_regions)) {
		list_splice(&rh->clean_regions, &clean);
		INIT_LIST_HEAD(&rh->clean_regions);

		list_for_each_entry(reg, &clean, list)
			list_del(&reg->hash_list);
	}

	if (!list_empty(&rh->recovered_regions)) {
		list_splice(&rh->recovered_regions, &recovered);
		INIT_LIST_HEAD(&rh->recovered_regions);

		list_for_each_entry(reg, &recovered, list)
			list_del(&reg->hash_list);
	}

	spin_unlock(&rh->region_lock);
	write_unlock_irq(&rh->hash_lock);

	/*
	 * All the regions on the recovered and clean lists have
	 * now been pulled out of the system, so no need to do
	 * any more locking.
	 */
	list_for_each_entry_safe (reg, next, &recovered, list) {
		if (reg->state != RH_ERROR)
			rh->log->type->clear_region(rh->log, reg->key);

		rh->log->type->set_region_sync(rh->log, reg->key,
					       reg->state != RH_ERROR);
		up(&rh->recovery_count);
		if (reg->delayed_bios.head)
			rh->dispatch(rh->dispatch_context, &reg->delayed_bios);

		mempool_free(reg, rh->region_pool);
	}

	list_for_each_entry_safe(reg, next, &clean, list) {
		rh->log->type->clear_region(rh->log, reg->key);
		mempool_free(reg, rh->region_pool);
	}

	rh_flush(rh);
}

void rh_inc(void *v, region_t region)
{
	struct region *reg;
	struct region_hash *rh = v;

	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	if (reg->state == RH_CLEAN) {
		rh->log->type->mark_region(rh->log, reg->key);

		spin_lock_irq(&rh->region_lock);
		reg->state = RH_DIRTY;
		list_del_init(&reg->list);	/* Take off the clean list. */
		spin_unlock_irq(&rh->region_lock);
	}

	atomic_inc(&reg->pending);
	read_unlock(&rh->hash_lock);
}

void rh_inc_pending(void *v, struct bio_list *bios)
{
	struct bio *bio;
	struct region_hash *rh = v;

	for (bio = bios->head; bio; bio = bio->bi_next)
		rh_inc(rh, rh_bio_to_region(rh, bio));
}

void rh_dec(void *v, region_t region)
{
	unsigned long flags;
	struct region *reg;
	struct region_hash *rh = v;

	read_lock(&rh->hash_lock);
	reg = __rh_lookup(rh, region);
	read_unlock(&rh->hash_lock);

	BUG_ON(!reg);

	if (atomic_dec_and_test(&reg->pending)) {
		spin_lock_irqsave(&rh->region_lock, flags);
		if (reg->state == RH_RECOVERING) {
			list_add_tail(&reg->list, &rh->quiesced_regions);
		} else {
			reg->state = RH_CLEAN;
			list_add(&reg->list, &rh->clean_regions);
		}
		spin_unlock_irqrestore(&rh->region_lock, flags);
	}
}

/*
 * Starts quiescing a region in preparation for recovery.
 */
static int __rh_recovery_prepare(struct region_hash *rh)
{
	int r;
	struct region *reg;
	region_t region;

	/*
	 * Ask the dirty log what's next.
	 */
	r = rh->log->type->get_resync_work(rh->log, &region);
	if (r <= 0)
		return r;

	/*
	 * Get this region, and start it quiescing
	 * by setting the recovering flag.
	 */
	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	read_unlock(&rh->hash_lock);

	spin_lock_irq(&rh->region_lock);

	reg->state = RH_RECOVERING;

	/* Already quiesced ? */
	list_del_init(&reg->list);
	if (!atomic_read(&reg->pending))
		list_add(&reg->list, &rh->quiesced_regions);

	spin_unlock_irq(&rh->region_lock);

	return 1;
}

int rh_recovery_prepare(void *v)
{
	struct region_hash *rh = v;

	if (test_bit(RECOVERY, &rh->flags)) {
		while (!down_trylock(&rh->recovery_count)) {
			if (__rh_recovery_prepare(rh) <= 0) {
				up(&rh->recovery_count);
				return -ENOENT;
			}
		}
	}

	return 0;
}

/*
 * Returns any quiesced regions.
 */
void *rh_recovery_start(void *v)
{
	struct region *reg = NULL;
	struct region_hash *rh = v;

	spin_lock_irq(&rh->region_lock);
	if (!list_empty(&rh->quiesced_regions)) {
		reg = list_entry(rh->quiesced_regions.next,
				 struct region, list);
		list_del_init(&reg->list); /* Remove from the quiesced list. */
	}
	spin_unlock_irq(&rh->region_lock);

	return (void*) reg;
}

/*
 * Put region on list of recovered ones.
 */
void rh_recovery_end(void *v, int error)
{
	struct region *reg = v;
	struct region_hash *rh = reg->rh;

	if (error)
		reg->state = RH_ERROR;

	spin_lock_irq(&rh->region_lock);
	list_add(&reg->list, &rh->recovered_regions);
	spin_unlock_irq(&rh->region_lock);
}

void rh_flush(void *v)
{
	struct region_hash *rh = v;

	rh->log->type->flush(rh->log);
}

void rh_delay_by_region(void *v, struct bio *bio, region_t region)
{
	struct region_hash *rh = v;
	struct region *reg;

	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	bio_list_add(&reg->delayed_bios, bio);
	read_unlock(&rh->hash_lock);
}

void rh_delay(void *v, struct bio *bio)
{
	return rh_delay_by_region(v, bio, rh_bio_to_region(v, bio));
}

void rh_stop_recovery(void *v)
{
	int i;
	struct region_hash *rh = v;

	clear_bit(RECOVERY, &rh->flags);
	rh->wake(rh->wake_context);

	/* wait for any recovering regions */
	for (i = 0; i < rh->max_recovery; i++)
		down(&rh->recovery_count);
}

void rh_start_recovery(void *v)
{
	int i;
	struct region_hash *rh = v;

	set_bit(RECOVERY, &rh->flags);
	for (i = 0; i < rh->max_recovery; i++)
		up(&rh->recovery_count);

	rh->wake(rh->wake_context);
}

EXPORT_SYMBOL(rh_bio_to_region);
EXPORT_SYMBOL(rh_sector_to_region);
EXPORT_SYMBOL(rh_region_to_sector);
EXPORT_SYMBOL(rh_init);
EXPORT_SYMBOL(rh_exit);
EXPORT_SYMBOL(rh_state);
EXPORT_SYMBOL(rh_update_states);
EXPORT_SYMBOL(rh_flush);
EXPORT_SYMBOL(rh_inc);
EXPORT_SYMBOL(rh_inc_pending);
EXPORT_SYMBOL(rh_dec);
EXPORT_SYMBOL(rh_delay);
EXPORT_SYMBOL(rh_delay_by_region);
EXPORT_SYMBOL(rh_recovery_prepare);
EXPORT_SYMBOL(rh_recovery_start);
EXPORT_SYMBOL(rh_recovery_end);
EXPORT_SYMBOL(rh_stop_recovery);
EXPORT_SYMBOL(rh_start_recovery);
EXPORT_SYMBOL(rh_reg_get_context);
EXPORT_SYMBOL(rh_reg_set_context);
EXPORT_SYMBOL(rh_get_region_key);
EXPORT_SYMBOL(rh_get_region_size);

MODULE_DESCRIPTION(DM_NAME " region hash");
MODULE_AUTHOR("Heinz Mauelshagen <hjm@redhat.de>");
MODULE_LICENSE("GPL");
