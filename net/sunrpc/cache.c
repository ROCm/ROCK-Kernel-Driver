/*
 * net/sunrpc/cache.c
 *
 * Generic code for various authentication-related caches
 * used by sunrpc clients and servers.
 *
 * Copyright (C) 2002 Neil Brown <neilb@cse.unsw.edu.au>
 *
 * Released under terms in GPL version 2.  See COPYING.
 *
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/cache.h>

#define	 RPCDBG_FACILITY RPCDBG_CACHE

void cache_init(struct cache_head *h)
{
	time_t now = CURRENT_TIME;
	h->next = NULL;
	h->flags = 0;
	atomic_set(&h->refcnt, 0);
	h->expiry_time = now + CACHE_NEW_EXPIRY;
	h->last_refresh = now;
}


/*
 * This is the generic cache management routine for all
 * the authentication caches.
 * It checks the currency of a cache item and will (later)
 * initiate an upcall to fill it if needed.
 *
 *
 * Returns 0 if the cache_head can be used, or cache_puts it and returns
 * -EAGAIN if upcall is pending,
 * -ENOENT if cache entry was negative
 */
int cache_check(struct cache_detail *detail,
		    struct cache_head *h, struct cache_req *rqstp)
{
	int rv;

	/* First decide return status as best we can */
	if (!test_bit(CACHE_VALID, &h->flags) ||
	    h->expiry_time < CURRENT_TIME)
		rv = -EAGAIN;
	else if (detail->flush_time > h->last_refresh)
		rv = -EAGAIN;
	else {
		/* entry is valid */
		if (test_bit(CACHE_NEGATIVE, &h->flags))
			rv = -ENOENT;
		else rv = 0;
	}

	/* up-call processing goes here later */
	/* if cache_pending, initiate upcall if none pending.
	 * if upcall cannot be initiated, change to CACHE_NEGATIVE
	 */
	if (rv == CACHE_PENDING) rv = CACHE_NEGATIVE;

	if (rv == CACHE_PENDING)
		cache_defer_req(rqstp, h);

	if (rv == -EAGAIN /* && cannot do upcall */)
		rv = -ENOENT;

	if (rv && h)
		detail->cache_put(h, detail);
	return rv;
}

void cache_fresh(struct cache_detail *detail,
		 struct cache_head *head, time_t expiry)
{

	head->expiry_time = expiry;
	head->last_refresh = CURRENT_TIME;
	set_bit(CACHE_VALID, &head->flags);
	clear_bit(CACHE_PENDING, &head->flags);
}

/*
 * caches need to be periodically cleaned.
 * For this we maintain a list of cache_detail and
 * a current pointer into that list and into the table
 * for that entry.
 *
 * Each time clean_cache is called it finds the next non-empty entry
 * in the current table and walks the list in that entry
 * looking for entries that can be removed.
 *
 * An entry gets removed if:
 * - The expiry is before current time
 * - The last_refresh time is before the flush_time for that cache
 *
 * later we might drop old entries with non-NEVER expiry if that table
 * is getting 'full' for some definition of 'full'
 *
 * The question of "how often to scan a table" is an interesting one
 * and is answered in part by the use of the "nextcheck" field in the
 * cache_detail.
 * When a scan of a table begins, the nextcheck field is set to a time
 * that is well into the future.
 * While scanning, if an expiry time is found that is earlier than the
 * current nextcheck time, nextcheck is set to that expiry time.
 * If the flush_time is ever set to a time earlier than the nextcheck
 * time, the nextcheck time is then set to that flush_time.
 *
 * A table is then only scanned if the current time is at least
 * the nextcheck time.
 * 
 */

static LIST_HEAD(cache_list);
static spinlock_t cache_list_lock = SPIN_LOCK_UNLOCKED;
static struct cache_detail *current_detail;
static int current_index;

void cache_register(struct cache_detail *cd)
{
	rwlock_init(&cd->hash_lock);
	spin_lock(&cache_list_lock);
	cd->nextcheck = 0;
	cd->entries = 0;
	list_add(&cd->others, &cache_list);
	spin_unlock(&cache_list_lock);
}

int cache_unregister(struct cache_detail *cd)
{
	cache_purge(cd);
	spin_lock(&cache_list_lock);
	write_lock(&cd->hash_lock);
	if (cd->entries || atomic_read(&cd->inuse)) {
		write_unlock(&cd->hash_lock);
		spin_unlock(&cache_list_lock);
		return -EBUSY;
	}
	if (current_detail == cd)
		current_detail = NULL;
	list_del_init(&cd->others);
	write_unlock(&cd->hash_lock);
	spin_unlock(&cache_list_lock);
	return 0;
}

struct cache_detail *cache_find(char *name)
{
	struct list_head *l;

	spin_lock(&cache_list_lock);
	list_for_each(l, &cache_list) {
		struct cache_detail *cd = list_entry(l, struct cache_detail, others);
		
		if (strcmp(cd->name, name)==0) {
			atomic_inc(&cd->inuse);
			spin_unlock(&cache_list_lock);
			return cd;
		}
	}
	spin_unlock(&cache_list_lock);
	return NULL;
}

/* cache_drop must be called on any cache returned by
 * cache_find, after it has been used
 */
void cache_drop(struct cache_detail *detail)
{
	atomic_dec(&detail->inuse);
}

/* clean cache tries to find something to clean
 * and cleans it.
 * It returns 1 if it cleaned something,
 *            0 if it didn't find anything this time
 *           -1 if it fell off the end of the list.
 */
int cache_clean(void)
{
	int rv = 0;
	struct list_head *next;

	spin_lock(&cache_list_lock);

	/* find a suitable table if we don't already have one */
	while (current_detail == NULL ||
	    current_index >= current_detail->hash_size) {
		if (current_detail)
			next = current_detail->others.next;
		else
			next = cache_list.next;
		if (next == &cache_list) {
			current_detail = NULL;
			spin_unlock(&cache_list_lock);
			return -1;
		}
		current_detail = list_entry(next, struct cache_detail, others);
		if (current_detail->nextcheck > CURRENT_TIME)
			current_index = current_detail->hash_size;
		else {
			current_index = 0;
			current_detail->nextcheck = CURRENT_TIME+30*60;
		}
	}

	/* find a non-empty bucket in the table */
	while (current_detail &&
	       current_index < current_detail->hash_size &&
	       current_detail->hash_table[current_index] == NULL)
		current_index++;

	/* find a cleanable entry in the bucket and clean it, or set to next bucket */
	
	if (current_detail && current_index < current_detail->hash_size) {
		struct cache_head *ch, **cp;
		
		write_lock(&current_detail->hash_lock);

		/* Ok, now to clean this strand */
			
		cp = & current_detail->hash_table[current_index];
		ch = *cp;
		for (; ch; cp= & ch->next, ch= *cp) {
			if (atomic_read(&ch->refcnt))
				continue;
			if (ch->expiry_time < CURRENT_TIME
			    || ch->last_refresh < current_detail->flush_time
				)
				break;
			if (current_detail->nextcheck > ch->expiry_time)
				current_detail->nextcheck = ch->expiry_time+1;
		}
		if (ch) {
			cache_get(ch);
			clear_bit(CACHE_HASHED, &ch->flags);
			*cp = ch->next;
			ch->next = NULL;
			current_detail->entries--;
			rv = 1;
		}
		write_unlock(&current_detail->hash_lock);
		if (ch)
			current_detail->cache_put(ch, current_detail);
		else
			current_index ++;
	}
	spin_unlock(&cache_list_lock);

	return rv;
}

/* 
 * Clean all caches promptly.  This just calls cache_clean
 * repeatedly until we are sure that every cache has had a chance to 
 * be fully cleaned
 */
void cache_flush(void)
{
	while (cache_clean() != -1)
		cond_resched();
	while (cache_clean() != -1)
		cond_resched();
}

void cache_purge(struct cache_detail *detail)
{
	detail->flush_time = CURRENT_TIME+1;
	detail->nextcheck = CURRENT_TIME;
	cache_flush();
}



/*
 * Deferral and Revisiting of Requests.
 *
 * If a cache lookup finds a pending entry, we
 * need to defer the request and revisit it later.
 * All deferred requests are stored in a hash table,
 * indexed by "struct cache_head *".
 * As it may be wasteful to store a whole request
 * structure, we allow the request to provide a 
 * deferred form, which must contain a
 * 'struct cache_deferred_req'
 * This cache_deferred_req contains a method to allow
 * it to be revisited when cache info is available
 */

#define	DFR_HASHSIZE	(PAGE_SIZE/sizeof(struct list_head))
#define	DFR_HASH(item)	((((long)item)>>4 ^ (((long)item)>>13)) % DFR_HASHSIZE)

#define	DFR_MAX	300	/* ??? */

spinlock_t cache_defer_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(cache_defer_list);
static struct list_head cache_defer_hash[DFR_HASHSIZE];
static int cache_defer_cnt;

void cache_defer_req(struct cache_req *req, struct cache_head *item)
{
	struct cache_deferred_req *dreq;
	int hash = DFR_HASH(item);

	dreq = req->defer(req);
	if (dreq == NULL)
		return;

	dreq->item = item;
	dreq->recv_time = CURRENT_TIME;

	spin_lock(&cache_defer_lock);

	list_add(&dreq->recent, &cache_defer_list);

	if (cache_defer_hash[hash].next == NULL)
		INIT_LIST_HEAD(&cache_defer_hash[hash]);
	list_add(&dreq->hash, &cache_defer_hash[hash]);

	/* it is in, now maybe clean up */
	dreq = NULL;
	if (++cache_defer_cnt > DFR_MAX) {
		/* too much in the cache, randomly drop
		 * first or last
		 */
		if (net_random()&1) 
			dreq = list_entry(cache_defer_list.next,
					  struct cache_deferred_req,
					  recent);
		else
			dreq = list_entry(cache_defer_list.prev,
					  struct cache_deferred_req,
					  recent);
		list_del(&dreq->recent);
		list_del(&dreq->hash);
		cache_defer_cnt--;
	}
	spin_unlock(&cache_defer_lock);

	if (dreq) {
		/* there was one too many */
		dreq->revisit(dreq, 1);
	}
	if (test_bit(CACHE_VALID, &item->flags)) {
		/* must have just been validated... */
		cache_revisit_request(item);
	}
}

void cache_revisit_request(struct cache_head *item)
{
	struct cache_deferred_req *dreq;
	struct list_head pending;

	struct list_head *lp;
	int hash = DFR_HASH(item);

	INIT_LIST_HEAD(&pending);
	spin_lock(&cache_defer_lock);
	
	lp = cache_defer_hash[hash].next;
	if (lp) {
		while (lp != &cache_defer_hash[hash]) {
			dreq = list_entry(lp, struct cache_deferred_req, hash);
			lp = lp->next;
			if (dreq->item == item) {
				list_del(&dreq->hash);
				list_move(&dreq->recent, &pending);
				cache_defer_cnt--;
			}
		}
	}
	spin_unlock(&cache_defer_lock);

	while (!list_empty(&pending)) {
		dreq = list_entry(pending.next, struct cache_deferred_req, recent);
		list_del_init(&dreq->recent);
		dreq->revisit(dreq, 0);
	}
}
