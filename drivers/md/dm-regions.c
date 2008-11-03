/*
 * Copyright (C) 2003 Sistina Software Limited.
 * Copyright (C) 2004-2008 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/dm-dirty-log.h>
#include <linux/dm-regions.h>

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include "dm.h"
#include "dm-bio-list.h"

#define	DM_MSG_PREFIX	"region hash"

/*-----------------------------------------------------------------
 * Region hash
 *
 * A storage set (eg. RAID1, RAID5) splits itself up into discrete regions.
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
 *   they are dull.  dm_rh_update_states() will remove them from the
 *   hash table.
 *
 *   quiesced_regions: These regions have been spun down, ready
 *   for recovery.  dm_rh_recovery_start() will remove regions from
 *   this list and hand them to the caller, which will schedule the
 *   recovery io.
 *
 *   recovered_regions: Regions that the caller has successfully
 *   recovered.  dm_rh_update_states() will now schedule any delayed
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
struct region_hash {
	unsigned max_recovery; /* Max # of regions to recover in parallel */

	/* Callback function to dispatch queued writes on recovered regions. */
	void (*dispatch)(void *context, struct bio_list *bios, int error);
	void *dispatch_context;

	/* Callback function to wakeup callers worker thread. */
	void (*wake)(void *context);
	void *wake_context;

	uint32_t region_size;
	unsigned region_shift;

	/* holds persistent region state */
	struct dm_dirty_log *log;

	/* hash table */
	rwlock_t hash_lock;
	mempool_t *region_pool;
	unsigned mask;
	unsigned nr_buckets;
	unsigned prime;
	unsigned shift;
	struct list_head *buckets;

	spinlock_t region_lock;
	atomic_t recovery_in_flight;
	struct semaphore recovery_count;
	struct list_head clean_regions;
	struct list_head quiesced_regions;
	struct list_head recovered_regions;
	struct list_head failed_recovered_regions;
};

struct region {
	region_t key;
	enum dm_rh_region_states state;
	void *context;	/* Caller context. */

	struct list_head hash_list;
	struct list_head list;

	atomic_t pending;
	struct bio_list delayed_bios;
};

/*
 * Conversion fns
 */
region_t dm_rh_sector_to_region(struct dm_rh_client *rh, sector_t sector)
{
	return sector >> ((struct region_hash *) rh)->region_shift;
}
EXPORT_SYMBOL_GPL(dm_rh_sector_to_region);

region_t dm_rh_bio_to_region(struct dm_rh_client *rh, struct bio *bio)
{
	return dm_rh_sector_to_region(rh, bio->bi_sector);
}
EXPORT_SYMBOL_GPL(dm_rh_bio_to_region);

sector_t dm_rh_region_to_sector(struct dm_rh_client *rh, region_t region)
{
	return region << ((struct region_hash *) rh)->region_shift;
}
EXPORT_SYMBOL_GPL(dm_rh_region_to_sector);

/*
 * Retrival fns.
 */
region_t dm_rh_get_region_key(struct dm_region *reg)
{
	return ((struct region *) reg)->key;
}
EXPORT_SYMBOL_GPL(dm_rh_get_region_key);

sector_t dm_rh_get_region_size(struct dm_rh_client *rh)
{
	return ((struct region_hash *) rh)->region_size;
}
EXPORT_SYMBOL_GPL(dm_rh_get_region_size);

/* Squirrel a context with a region. */
void *dm_rh_reg_get_context(struct dm_region *reg)
{
	return ((struct region *) reg)->context;
}
EXPORT_SYMBOL_GPL(dm_rh_reg_get_context);

void dm_rh_reg_set_context(struct dm_region *reg, void *context)
{
	((struct region *) reg)->context = context;
}
EXPORT_SYMBOL_GPL(dm_rh_reg_set_context);

/*
 * Create region hash client.
 */
#define MIN_REGIONS 64
struct dm_rh_client *dm_rh_client_create(
		 unsigned max_recovery,
		 void (*dispatch)(void *dispatch_context,
				  struct bio_list *bios, int error),
		 void *dispatch_context,
		 void (*wake)(void *wake_context), void *wake_context,
		 struct dm_dirty_log *log, uint32_t region_size,
		 region_t nr_regions)
{
	unsigned i;
	unsigned nr_buckets, max_buckets;
	unsigned hash_primes[] = {
		/* Table of primes for rh_hash/table size optimization. */
		3, 7, 13, 27, 53, 97, 193, 389, 769,
		1543, 3079, 6151, 12289, 24593,
	};
	struct region_hash *rh;

	if (region_size & (region_size - 1)) {
		DMERR("region size must be 2^^n");
		return ERR_PTR(-EINVAL);
	}

	/* Calculate a suitable number of buckets for our hash table. */
	max_buckets = nr_regions >> 6;
	for (nr_buckets = 128u; nr_buckets < max_buckets; nr_buckets <<= 1)
		;
	nr_buckets >>= 1;

	rh = kmalloc(sizeof(*rh), GFP_KERNEL);
	if (!rh) {
		DMERR("unable to allocate region hash memory");
		return ERR_PTR(-ENOMEM);
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
	rh->mask = nr_buckets - 1;
	rh->nr_buckets = nr_buckets;
	rh->shift = ffs(nr_buckets);

	/* Check prime array limits. */
	i = rh->shift - 1 > ARRAY_SIZE(hash_primes) ?
	    ARRAY_SIZE(hash_primes) - 1 : rh->shift - 2;
	rh->prime = hash_primes[i];

	rh->buckets = vmalloc(nr_buckets * sizeof(*rh->buckets));
	if (!rh->buckets) {
		DMERR("unable to allocate region hash bucket memory");
		kfree(rh);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < nr_buckets; i++)
		INIT_LIST_HEAD(rh->buckets + i);

	spin_lock_init(&rh->region_lock);
	sema_init(&rh->recovery_count, 0);
	atomic_set(&rh->recovery_in_flight, 0);
	INIT_LIST_HEAD(&rh->clean_regions);
	INIT_LIST_HEAD(&rh->quiesced_regions);
	INIT_LIST_HEAD(&rh->recovered_regions);
	INIT_LIST_HEAD(&rh->failed_recovered_regions);

	rh->region_pool = mempool_create_kmalloc_pool(MIN_REGIONS,
						      sizeof(struct region));
	if (!rh->region_pool) {
		vfree(rh->buckets);
		kfree(rh);
		rh = ERR_PTR(-ENOMEM);
	}

	return (struct dm_rh_client *) rh;
}
EXPORT_SYMBOL_GPL(dm_rh_client_create);

void dm_rh_client_destroy(struct dm_rh_client *rh_in)
{
	unsigned h;
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg, *tmp;

	BUG_ON(!list_empty(&rh->quiesced_regions));

	for (h = 0; h < rh->nr_buckets; h++) {
		list_for_each_entry_safe(reg, tmp, rh->buckets + h, hash_list) {
			BUG_ON(atomic_read(&reg->pending));
			mempool_free(reg, rh->region_pool);
		}
	}

	if (rh->region_pool)
		mempool_destroy(rh->region_pool);

	vfree(rh->buckets);
	kfree(rh);
}
EXPORT_SYMBOL_GPL(dm_rh_client_destroy);

static inline unsigned rh_hash(struct region_hash *rh, region_t region)
{
	return (unsigned) ((region * rh->prime) >> rh->shift) & rh->mask;
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
	list_add(&reg->hash_list, rh->buckets + rh_hash(rh, reg->key));
}

static struct region *__rh_alloc(struct region_hash *rh, region_t region)
{
	struct region *reg, *nreg;

	read_unlock(&rh->hash_lock);
	nreg = mempool_alloc(rh->region_pool, GFP_ATOMIC);
	if (unlikely(!nreg))
		nreg = kmalloc(sizeof(*nreg), GFP_NOIO);

	nreg->state = rh->log->type->in_sync(rh->log, region, 1) ?
		      DM_RH_CLEAN : DM_RH_NOSYNC;
	nreg->key = region;
	INIT_LIST_HEAD(&nreg->list);
	atomic_set(&nreg->pending, 0);
	bio_list_init(&nreg->delayed_bios);

	write_lock_irq(&rh->hash_lock);
	reg = __rh_lookup(rh, region);
	if (reg)
		/* We lost the race. */
		mempool_free(nreg, rh->region_pool);
	else {
		__rh_insert(rh, nreg);
		if (nreg->state == DM_RH_CLEAN) {
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
	return reg ? reg : __rh_alloc(rh, region);
}

int dm_rh_get_state(struct dm_rh_client *rh_in, region_t region, int may_block)
{
	int r;
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg;

	read_lock(&rh->hash_lock);
	reg = __rh_lookup(rh, region);
	read_unlock(&rh->hash_lock);

	if (reg)
		return reg->state;

	/*
	 * The region wasn't in the hash, so we fall back to the dirty log.
	 */
	r = rh->log->type->in_sync(rh->log, region, may_block);

	/*
	 * Any error from the dirty log (eg. -EWOULDBLOCK)
	 * gets taken as a DM_RH_NOSYNC
	 */
	return r == 1 ? DM_RH_CLEAN : DM_RH_NOSYNC;
}
EXPORT_SYMBOL_GPL(dm_rh_get_state);

void dm_rh_set_state(struct dm_rh_client *rh_in, region_t region,
		     enum dm_rh_region_states state, int may_block)
{
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg;
	struct dm_dirty_log *log = rh->log;

	if (state == DM_RH_NOSYNC)
		log->type->set_region_sync(log, region, 0);
	else if (state == DM_RH_CLEAN)
		log->type->clear_region(log, region);
	else if (state == DM_RH_DIRTY)
		log->type->mark_region(log, region);

	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	reg->state = state;
	read_unlock(&rh->hash_lock);
}
EXPORT_SYMBOL_GPL(dm_rh_set_state);

void dm_rh_update_states(struct dm_rh_client *rh_in, int errors_handled)
{
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg, *next;
	LIST_HEAD(clean);
	LIST_HEAD(recovered);
	LIST_HEAD(failed_recovered);

	/*
	 * Quickly grab the lists and remove any regions from hash.
	 */
	write_lock_irq(&rh->hash_lock);
	spin_lock(&rh->region_lock);
	if (!list_empty(&rh->clean_regions)) {
		list_splice_init(&rh->clean_regions, &clean);

		list_for_each_entry(reg, &clean, list)
			list_del(&reg->hash_list);
	}

	if (!list_empty(&rh->recovered_regions)) {
		list_splice_init(&rh->recovered_regions, &recovered);

		list_for_each_entry(reg, &recovered, list)
			list_del(&reg->hash_list);
	}

	if (!list_empty(&rh->failed_recovered_regions)) {
		list_splice_init(&rh->failed_recovered_regions,
				 &failed_recovered);

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
	list_for_each_entry_safe(reg, next, &recovered, list) {
		rh->log->type->clear_region(rh->log, reg->key);
		rh->log->type->set_region_sync(rh->log, reg->key, 1);

		if (reg->delayed_bios.head)
			rh->dispatch(rh->dispatch_context,
				     &reg->delayed_bios, 0);

		up(&rh->recovery_count);
		mempool_free(reg, rh->region_pool);
	}

	list_for_each_entry_safe(reg, next, &failed_recovered, list) {
		rh->log->type->set_region_sync(rh->log, reg->key,
					       errors_handled ? 0 : 1);
		if (reg->delayed_bios.head)
			rh->dispatch(rh->dispatch_context,
				     &reg->delayed_bios, -EIO);

		up(&rh->recovery_count);
		mempool_free(reg, rh->region_pool);
	}

	list_for_each_entry_safe(reg, next, &clean, list) {
		rh->log->type->clear_region(rh->log, reg->key);
		mempool_free(reg, rh->region_pool);
	}

	dm_rh_flush(rh_in);
}
EXPORT_SYMBOL_GPL(dm_rh_update_states);

void dm_rh_inc(struct dm_rh_client *rh_in, region_t region)
{
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg;

	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	if (reg->state == DM_RH_CLEAN) {
		rh->log->type->mark_region(rh->log, reg->key);

		spin_lock_irq(&rh->region_lock);
		reg->state = DM_RH_DIRTY;
		list_del_init(&reg->list);	/* Take off the clean list. */
		spin_unlock_irq(&rh->region_lock);
	}

	atomic_inc(&reg->pending);
	read_unlock(&rh->hash_lock);
}
EXPORT_SYMBOL_GPL(dm_rh_inc);

void dm_rh_inc_pending(struct dm_rh_client *rh_in, struct bio_list *bios)
{
	struct bio *bio;

	for (bio = bios->head; bio; bio = bio->bi_next)
		dm_rh_inc(rh_in, dm_rh_bio_to_region(rh_in, bio));
}
EXPORT_SYMBOL_GPL(dm_rh_inc_pending);

int dm_rh_dec(struct dm_rh_client *rh_in, region_t region)
{
	int r = 0;
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg;

	read_lock(&rh->hash_lock);
	reg = __rh_lookup(rh, region);
	read_unlock(&rh->hash_lock);

	BUG_ON(!reg);

	if (atomic_dec_and_test(&reg->pending)) {
		unsigned long flags;

		/*
		 * There is no pending I/O for this region.
		 * We can move the region to corresponding list for next action.
		 * At this point, the region is not yet connected to any list.
		 *
		 * If the state is DM_RH_NOSYNC, the region should be kept off
		 * from clean list.
		 * The hash entry for DM_RH_NOSYNC will remain in memory
		 * until the region is recovered or the map is reloaded.
		 */

		spin_lock_irqsave(&rh->region_lock, flags);
		if (reg->state == DM_RH_RECOVERING)
			list_add_tail(&reg->list, &rh->quiesced_regions);
		else {
			reg->state = DM_RH_CLEAN;
			list_add(&reg->list, &rh->clean_regions);
		}
		spin_unlock_irqrestore(&rh->region_lock, flags);

		r = 1;
	}

	return r;
}
EXPORT_SYMBOL_GPL(dm_rh_dec);

/*
 * Starts quiescing a region in preparation for recovery.
 */
static int __rh_recovery_prepare(struct region_hash *rh)
{
	int r;
	region_t region;
	struct region *reg;

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

	reg->state = DM_RH_RECOVERING;

	/* Already quiesced ? */
	list_del_init(&reg->list);
	if (!atomic_read(&reg->pending))
		list_add(&reg->list, &rh->quiesced_regions);

	spin_unlock_irq(&rh->region_lock);
	return 1;
}

int dm_rh_recovery_prepare(struct dm_rh_client *rh_in)
{
	int r = 0;
	struct region_hash *rh = (struct region_hash *) rh_in;

	/* Extra reference to avoid race with rh_stop_recovery */
	atomic_inc(&rh->recovery_in_flight);

	while (!down_trylock(&rh->recovery_count)) {
		atomic_inc(&rh->recovery_in_flight);

		if (__rh_recovery_prepare(rh) <= 0) {
			atomic_dec(&rh->recovery_in_flight);
			up(&rh->recovery_count);
			r = -ENOENT;
			break;
		}
	}

	/* Drop the extra reference. */
	if (atomic_dec_and_test(&rh->recovery_in_flight))
		r = -ESRCH;

	return r;
}
EXPORT_SYMBOL_GPL(dm_rh_recovery_prepare);

/*
 * Returns any quiesced regions.
 */
struct dm_region *dm_rh_recovery_start(struct dm_rh_client *rh_in)
{
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg = NULL;

	spin_lock_irq(&rh->region_lock);
	if (!list_empty(&rh->quiesced_regions)) {
		reg = list_entry(rh->quiesced_regions.next,
				 struct region, list);
		list_del_init(&reg->list); /* Remove from the quiesced list. */
	}

	spin_unlock_irq(&rh->region_lock);
	return (struct dm_region *) reg;
}
EXPORT_SYMBOL_GPL(dm_rh_recovery_start);

/*
 * Put region on list of recovered ones.
 */
void dm_rh_recovery_end(struct dm_rh_client *rh_in, struct dm_region *reg_in,
			int error)
{
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg = (struct region *) reg_in;

	spin_lock_irq(&rh->region_lock);
	if (error) {
		reg->state = DM_RH_NOSYNC;
		list_add(&reg->list, &rh->failed_recovered_regions);
	} else
		list_add(&reg->list, &rh->recovered_regions);

	atomic_dec(&rh->recovery_in_flight);
	spin_unlock_irq(&rh->region_lock);

	rh->wake(rh->wake_context);
	BUG_ON(atomic_read(&rh->recovery_in_flight) < 0);
}
EXPORT_SYMBOL_GPL(dm_rh_recovery_end);

/* Return recovery in flight count. */
int dm_rh_recovery_in_flight(struct dm_rh_client *rh_in)
{
	return atomic_read(&((struct region_hash *) rh_in)->recovery_in_flight);
}
EXPORT_SYMBOL_GPL(dm_rh_recovery_in_flight);

int dm_rh_flush(struct dm_rh_client *rh_in)
{
	struct region_hash *rh = (struct region_hash *) rh_in;

	return rh->log->type->flush(rh->log);
}
EXPORT_SYMBOL_GPL(dm_rh_flush);

void dm_rh_delay_by_region(struct dm_rh_client *rh_in,
			   struct bio *bio, region_t region)
{
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg;

	/* FIXME: locking. */
	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	bio_list_add(&reg->delayed_bios, bio);
	read_unlock(&rh->hash_lock);
}
EXPORT_SYMBOL_GPL(dm_rh_delay_by_region);

void dm_rh_delay(struct dm_rh_client *rh_in, struct bio *bio)
{
	return dm_rh_delay_by_region(rh_in, bio,
				     dm_rh_bio_to_region(rh_in, bio));
}
EXPORT_SYMBOL_GPL(dm_rh_delay);

void dm_rh_dispatch_bios(struct dm_rh_client *rh_in,
			 region_t region, int error)
{
	struct region_hash *rh = (struct region_hash *) rh_in;
	struct region *reg;
	struct bio_list delayed_bios;

	/* FIXME: locking. */
	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	BUG_ON(!reg);
	delayed_bios = reg->delayed_bios;
	bio_list_init(&reg->delayed_bios);
	read_unlock(&rh->hash_lock);

	if (delayed_bios.head)
		rh->dispatch(rh->dispatch_context, &delayed_bios, error);

	up(&rh->recovery_count);
}
EXPORT_SYMBOL_GPL(dm_rh_dispatch_bios);

void dm_rh_stop_recovery(struct dm_rh_client *rh_in)
{
	int i;
	struct region_hash *rh = (struct region_hash *) rh_in;

	rh->wake(rh->wake_context);

	/* wait for any recovering regions */
	for (i = 0; i < rh->max_recovery; i++)
		down(&rh->recovery_count);
}
EXPORT_SYMBOL_GPL(dm_rh_stop_recovery);

void dm_rh_start_recovery(struct dm_rh_client *rh_in)
{
	int i;
	struct region_hash *rh = (struct region_hash *) rh_in;

	for (i = 0; i < rh->max_recovery; i++)
		up(&rh->recovery_count);

	rh->wake(rh->wake_context);
}
EXPORT_SYMBOL_GPL(dm_rh_start_recovery);

MODULE_DESCRIPTION(DM_NAME " region hash");
MODULE_AUTHOR("Joe Thornber/Heinz Mauelshagen <hjm@redhat.com>");
MODULE_LICENSE("GPL");
