/*
 *  linux/drivers/block/cfq-iosched.c
 *
 *  CFQ, or complete fairness queueing, disk scheduler.
 *
 *  Based on ideas from a previously unfinished io
 *  scheduler (round robin per-process disk scheduling) and Andrea Arcangeli.
 *
 *  Copyright (C) 2003 Jens Axboe <axboe@suse.de>
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/hash.h>
#include <linux/rbtree.h>
#include <linux/mempool.h>

/*
 * tunables
 */
static int cfq_quantum = 4;
static int cfq_queued = 8;

#define CFQ_QHASH_SHIFT		6
#define CFQ_QHASH_ENTRIES	(1 << CFQ_QHASH_SHIFT)
#define list_entry_qhash(entry)	list_entry((entry), struct cfq_queue, cfq_hash)

#define CFQ_MHASH_SHIFT		8
#define CFQ_MHASH_BLOCK(sec)	((sec) >> 3)
#define CFQ_MHASH_ENTRIES	(1 << CFQ_MHASH_SHIFT)
#define CFQ_MHASH_FN(sec)	(hash_long(CFQ_MHASH_BLOCK((sec)),CFQ_MHASH_SHIFT))
#define ON_MHASH(crq)		!list_empty(&(crq)->hash)
#define rq_hash_key(rq)		((rq)->sector + (rq)->nr_sectors)
#define list_entry_hash(ptr)	list_entry((ptr), struct cfq_rq, hash)

#define list_entry_cfqq(ptr)	list_entry((ptr), struct cfq_queue, cfq_list)

#define RQ_DATA(rq)		((struct cfq_rq *) (rq)->elevator_private)

static kmem_cache_t *crq_pool;
static kmem_cache_t *cfq_pool;
static mempool_t *cfq_mpool;

struct cfq_data {
	struct list_head rr_list;
	struct list_head *dispatch;
	struct list_head *cfq_hash;

	struct list_head *crq_hash;

	unsigned int busy_queues;
	unsigned int max_queued;

	mempool_t *crq_pool;

	request_queue_t *queue;

	/*
	 * tunables
	 */
	unsigned int cfq_quantum;
	unsigned int cfq_queued;
};

struct cfq_queue {
	struct list_head cfq_hash;
	struct list_head cfq_list;
	struct rb_root sort_list;
	int pid;
	int queued[2];
#if 0
	/*
	 * with a simple addition like this, we can do io priorities. almost.
	 * does need a split request free list, too.
	 */
	int io_prio
#endif
};

struct cfq_rq {
	struct rb_node rb_node;
	sector_t rb_key;

	struct request *request;

	struct cfq_queue *cfq_queue;

	struct list_head hash;
};

static void cfq_put_queue(struct cfq_data *cfqd, struct cfq_queue *cfqq);
static struct cfq_queue *cfq_find_cfq_hash(struct cfq_data *cfqd, int pid);
static void cfq_dispatch_sort(struct cfq_data *cfqd, struct cfq_queue *cfqq,
			      struct cfq_rq *crq);

/*
 * lots of deadline iosched dupes, can be abstracted later...
 */
static inline void __cfq_del_crq_hash(struct cfq_rq *crq)
{
	list_del_init(&crq->hash);
}

static inline void cfq_del_crq_hash(struct cfq_rq *crq)
{
	if (ON_MHASH(crq))
		__cfq_del_crq_hash(crq);
}

static void cfq_remove_merge_hints(request_queue_t *q, struct cfq_rq *crq)
{
	cfq_del_crq_hash(crq);

	if (q->last_merge == crq->request)
		q->last_merge = NULL;
}

static inline void cfq_add_crq_hash(struct cfq_data *cfqd, struct cfq_rq *crq)
{
	struct request *rq = crq->request;

	BUG_ON(ON_MHASH(crq));

	list_add(&crq->hash, &cfqd->crq_hash[CFQ_MHASH_FN(rq_hash_key(rq))]);
}

static struct request *cfq_find_rq_hash(struct cfq_data *cfqd, sector_t offset)
{
	struct list_head *hash_list = &cfqd->crq_hash[CFQ_MHASH_FN(offset)];
	struct list_head *entry, *next = hash_list->next;

	while ((entry = next) != hash_list) {
		struct cfq_rq *crq = list_entry_hash(entry);
		struct request *__rq = crq->request;

		next = entry->next;

		BUG_ON(!ON_MHASH(crq));

		if (!rq_mergeable(__rq)) {
			__cfq_del_crq_hash(crq);
			continue;
		}

		if (rq_hash_key(__rq) == offset)
			return __rq;
	}

	return NULL;
}

/*
 * rb tree support functions
 */
#define RB_NONE		(2)
#define RB_EMPTY(node)	((node)->rb_node == NULL)
#define RB_CLEAR(node)	((node)->rb_color = RB_NONE)
#define RB_CLEAR_ROOT(root)	((root)->rb_node = NULL)
#define ON_RB(node)	((node)->rb_color != RB_NONE)
#define rb_entry_crq(node)	rb_entry((node), struct cfq_rq, rb_node)
#define rq_rb_key(rq)		(rq)->sector

static inline void cfq_del_crq_rb(struct cfq_queue *cfqq, struct cfq_rq *crq)
{
	if (ON_RB(&crq->rb_node)) {
		cfqq->queued[rq_data_dir(crq->request)]--;
		rb_erase(&crq->rb_node, &cfqq->sort_list);
		crq->cfq_queue = NULL;
	}
}

static struct cfq_rq *
__cfq_add_crq_rb(struct cfq_queue *cfqq, struct cfq_rq *crq)
{
	struct rb_node **p = &cfqq->sort_list.rb_node;
	struct rb_node *parent = NULL;
	struct cfq_rq *__crq;

	while (*p) {
		parent = *p;
		__crq = rb_entry_crq(parent);

		if (crq->rb_key < __crq->rb_key)
			p = &(*p)->rb_left;
		else if (crq->rb_key > __crq->rb_key)
			p = &(*p)->rb_right;
		else
			return __crq;
	}

	rb_link_node(&crq->rb_node, parent, p);
	return NULL;
}

static void
cfq_add_crq_rb(struct cfq_data *cfqd, struct cfq_queue *cfqq,struct cfq_rq *crq)
{
	struct request *rq = crq->request;
	struct cfq_rq *__alias;

	crq->rb_key = rq_rb_key(rq);
	cfqq->queued[rq_data_dir(rq)]++;
retry:
	__alias = __cfq_add_crq_rb(cfqq, crq);
	if (!__alias) {
		rb_insert_color(&crq->rb_node, &cfqq->sort_list);
		crq->cfq_queue = cfqq;
		return;
	}

	cfq_dispatch_sort(cfqd, cfqq, __alias);
	goto retry;
}

static struct request *
cfq_find_rq_rb(struct cfq_data *cfqd, sector_t sector)
{
	struct cfq_queue *cfqq = cfq_find_cfq_hash(cfqd, current->tgid);
	struct rb_node *n;

	if (!cfqq)
		goto out;

	n = cfqq->sort_list.rb_node;
	while (n) {
		struct cfq_rq *crq = rb_entry_crq(n);

		if (sector < crq->rb_key)
			n = n->rb_left;
		else if (sector > crq->rb_key)
			n = n->rb_right;
		else
			return crq->request;
	}

out:
	return NULL;
}

static void cfq_remove_request(request_queue_t *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct cfq_rq *crq = RQ_DATA(rq);

	if (crq) {
		struct cfq_queue *cfqq = crq->cfq_queue;

		cfq_remove_merge_hints(q, crq);
		list_del_init(&rq->queuelist);

		if (cfqq) {
			cfq_del_crq_rb(cfqq, crq);

			if (RB_EMPTY(&cfqq->sort_list))
				cfq_put_queue(cfqd, cfqq);
		}
	}
}

static int
cfq_merge(request_queue_t *q, struct request **req, struct bio *bio)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct request *__rq;
	int ret;

	ret = elv_try_last_merge(q, bio);
	if (ret != ELEVATOR_NO_MERGE) {
		__rq = q->last_merge;
		goto out_insert;
	}

	__rq = cfq_find_rq_hash(cfqd, bio->bi_sector);
	if (__rq) {
		BUG_ON(__rq->sector + __rq->nr_sectors != bio->bi_sector);

		if (elv_rq_merge_ok(__rq, bio)) {
			ret = ELEVATOR_BACK_MERGE;
			goto out;
		}
	}

	__rq = cfq_find_rq_rb(cfqd, bio->bi_sector + bio_sectors(bio));
	if (__rq) {
		if (elv_rq_merge_ok(__rq, bio)) {
			ret = ELEVATOR_FRONT_MERGE;
			goto out;
		}
	}

	return ELEVATOR_NO_MERGE;
out:
	q->last_merge = __rq;
out_insert:
	*req = __rq;
	return ret;
}

static void cfq_merged_request(request_queue_t *q, struct request *req)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct cfq_rq *crq = RQ_DATA(req);

	cfq_del_crq_hash(crq);
	cfq_add_crq_hash(cfqd, crq);

	if (ON_RB(&crq->rb_node) && (rq_rb_key(req) != crq->rb_key)) {
		struct cfq_queue *cfqq = crq->cfq_queue;

		cfq_del_crq_rb(cfqq, crq);
		cfq_add_crq_rb(cfqd, cfqq, crq);
	}

	q->last_merge = req;
}

static void
cfq_merged_requests(request_queue_t *q, struct request *req,
		    struct request *next)
{
	cfq_merged_request(q, req);
	cfq_remove_request(q, next);
}

static void
cfq_dispatch_sort(struct cfq_data *cfqd, struct cfq_queue *cfqq,
		  struct cfq_rq *crq)
{
	struct list_head *head = cfqd->dispatch, *entry = head;
	struct request *__rq;

	cfq_del_crq_rb(cfqq, crq);
	cfq_remove_merge_hints(cfqd->queue, crq);

	if (!list_empty(head)) {
		__rq = list_entry_rq(head->next);

		if (crq->request->sector < __rq->sector) {
			entry = head->prev;
			goto link;
		}
	}

	while ((entry = entry->prev) != head) {
		__rq = list_entry_rq(entry);

		if (crq->request->sector <= __rq->sector)
			break;
	}

link:
	list_add_tail(&crq->request->queuelist, entry);
}

static inline void
__cfq_dispatch_requests(request_queue_t *q, struct cfq_data *cfqd,
			struct cfq_queue *cfqq)
{
	struct cfq_rq *crq = rb_entry_crq(rb_first(&cfqq->sort_list));

	cfq_dispatch_sort(cfqd, cfqq, crq);
}

static int cfq_dispatch_requests(request_queue_t *q, struct cfq_data *cfqd)
{
	struct cfq_queue *cfqq;
	struct list_head *entry, *tmp;
	int ret, queued, good_queues;

	if (list_empty(&cfqd->rr_list))
		return 0;

	queued = ret = 0;
restart:
	good_queues = 0;
	list_for_each_safe(entry, tmp, &cfqd->rr_list) {
		cfqq = list_entry_cfqq(cfqd->rr_list.next);

		BUG_ON(RB_EMPTY(&cfqq->sort_list));

		__cfq_dispatch_requests(q, cfqd, cfqq);

		if (RB_EMPTY(&cfqq->sort_list))
			cfq_put_queue(cfqd, cfqq);
		else
			good_queues++;

		queued++;
		ret = 1;
	}

	if ((queued < cfqd->cfq_quantum) && good_queues)
		goto restart;

	return ret;
}

static struct request *cfq_next_request(request_queue_t *q)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct request *rq;

	if (!list_empty(cfqd->dispatch)) {
		struct cfq_rq *crq;
dispatch:
		rq = list_entry_rq(cfqd->dispatch->next);

		crq = RQ_DATA(rq);
		if (crq)
			cfq_remove_merge_hints(q, crq);

		return rq;
	}

	if (cfq_dispatch_requests(q, cfqd))
		goto dispatch;

	return NULL;
}

static inline struct cfq_queue *
__cfq_find_cfq_hash(struct cfq_data *cfqd, int pid, const int hashval)
{
	struct list_head *hash_list = &cfqd->cfq_hash[hashval];
	struct list_head *entry;

	list_for_each(entry, hash_list) {
		struct cfq_queue *__cfqq = list_entry_qhash(entry);

		if (__cfqq->pid == pid)
			return __cfqq;
	}

	return NULL;
}

static struct cfq_queue *cfq_find_cfq_hash(struct cfq_data *cfqd, int pid)
{
	const int hashval = hash_long(current->tgid, CFQ_QHASH_SHIFT);

	return __cfq_find_cfq_hash(cfqd, pid, hashval);
}

static void cfq_put_queue(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	cfqd->busy_queues--;
	list_del(&cfqq->cfq_list);
	list_del(&cfqq->cfq_hash);
	mempool_free(cfqq, cfq_mpool);
}

static struct cfq_queue *__cfq_get_queue(struct cfq_data *cfqd, int pid,
					 int gfp_mask)
{
	const int hashval = hash_long(current->tgid, CFQ_QHASH_SHIFT);
	struct cfq_queue *cfqq, *new_cfqq = NULL;
	request_queue_t *q = cfqd->queue;

retry:
	cfqq = __cfq_find_cfq_hash(cfqd, pid, hashval);

	if (!cfqq) {
		if (new_cfqq) {
			cfqq = new_cfqq;
			new_cfqq = NULL;
		} else if (gfp_mask & __GFP_WAIT) {
			spin_unlock_irq(q->queue_lock);
			new_cfqq = mempool_alloc(cfq_mpool, gfp_mask);
			spin_lock_irq(q->queue_lock);
			goto retry;
		} else
			return NULL;

		INIT_LIST_HEAD(&cfqq->cfq_hash);
		INIT_LIST_HEAD(&cfqq->cfq_list);
		RB_CLEAR_ROOT(&cfqq->sort_list);

		cfqq->pid = pid;
		cfqq->queued[0] = cfqq->queued[1] = 0;
		list_add(&cfqq->cfq_hash, &cfqd->cfq_hash[hashval]);
	}

	if (new_cfqq)
		mempool_free(new_cfqq, cfq_mpool);

	return cfqq;
}

static struct cfq_queue *cfq_get_queue(struct cfq_data *cfqd, int pid,
				       int gfp_mask)
{
	request_queue_t *q = cfqd->queue;
	struct cfq_queue *cfqq;

	spin_lock_irq(q->queue_lock);
	cfqq = __cfq_get_queue(cfqd, pid, gfp_mask);
	spin_unlock_irq(q->queue_lock);

	return cfqq;
}

static void cfq_enqueue(struct cfq_data *cfqd, struct cfq_rq *crq)
{
	struct cfq_queue *cfqq;

	cfqq = __cfq_get_queue(cfqd, current->tgid, GFP_ATOMIC);
	if (cfqq) {
		cfq_add_crq_rb(cfqd, cfqq, crq);

		if (list_empty(&cfqq->cfq_list)) {
			list_add(&cfqq->cfq_list, &cfqd->rr_list);
			cfqd->busy_queues++;
		}
	} else {
		/*
		 * should can only happen if the request wasn't allocated
		 * through blk_alloc_request(), eg stack requests from ide-cd
		 * (those should be removed) _and_ we are in OOM.
		 */
		list_add_tail(&crq->request->queuelist, cfqd->dispatch);
	}
}

static void
cfq_insert_request(request_queue_t *q, struct request *rq, int where)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct cfq_rq *crq = RQ_DATA(rq);

	switch (where) {
		case ELEVATOR_INSERT_BACK:
			while (cfq_dispatch_requests(q, cfqd))
				;
			list_add_tail(&rq->queuelist, cfqd->dispatch);
			break;
		case ELEVATOR_INSERT_FRONT:
			list_add(&rq->queuelist, cfqd->dispatch);
			break;
		case ELEVATOR_INSERT_SORT:
			BUG_ON(!blk_fs_request(rq));
			cfq_enqueue(cfqd, crq);
			break;
		default:
			printk("%s: bad insert point %d\n", __FUNCTION__,where);
			return;
	}

	if (rq_mergeable(rq)) {
		cfq_add_crq_hash(cfqd, crq);

		if (!q->last_merge)
			q->last_merge = rq;
	}
}

static int cfq_queue_empty(request_queue_t *q)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;

	if (list_empty(cfqd->dispatch) && list_empty(&cfqd->rr_list))
		return 1;

	return 0;
}

static struct request *
cfq_former_request(request_queue_t *q, struct request *rq)
{
	struct cfq_rq *crq = RQ_DATA(rq);
	struct rb_node *rbprev = rb_prev(&crq->rb_node);

	if (rbprev)
		return rb_entry_crq(rbprev)->request;

	return NULL;
}

static struct request *
cfq_latter_request(request_queue_t *q, struct request *rq)
{
	struct cfq_rq *crq = RQ_DATA(rq);
	struct rb_node *rbnext = rb_next(&crq->rb_node);

	if (rbnext)
		return rb_entry_crq(rbnext)->request;

	return NULL;
}

static int cfq_may_queue(request_queue_t *q, int rw)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct cfq_queue *cfqq;
	int ret = 1;

	if (!cfqd->busy_queues)
		goto out;

	cfqq = cfq_find_cfq_hash(cfqd, current->tgid);
	if (cfqq) {
		int limit = (q->nr_requests - cfqd->cfq_queued) / cfqd->busy_queues;

		if (limit < 3)
			limit = 3;
		else if (limit > cfqd->max_queued)
			limit = cfqd->max_queued;

		if (cfqq->queued[rw] > limit)
			ret = 0;
	}
out:
	return ret;
}

static void cfq_put_request(request_queue_t *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct cfq_rq *crq = RQ_DATA(rq);
	struct request_list *rl;
	int other_rw;

	if (crq) {
		BUG_ON(q->last_merge == rq);
		BUG_ON(ON_MHASH(crq));

		mempool_free(crq, cfqd->crq_pool);
		rq->elevator_private = NULL;
	}

	/*
	 * work-around for may_queue "bug": if a read gets issued and refused
	 * to queue because writes ate all the allowed slots and no other
	 * reads are pending for this queue, it could get stuck infinitely
	 * since freed_request() only checks the waitqueue for writes when
	 * freeing them. or vice versa for a single write vs many reads.
	 * so check here whether "the other" data direction might be able
	 * to queue and wake them
	 */
	rl = &q->rq;
	other_rw = rq_data_dir(rq) ^ 1;
	if (rl->count[other_rw] <= q->nr_requests) {
		smp_mb();
		if (waitqueue_active(&rl->wait[other_rw]))
			wake_up(&rl->wait[other_rw]);
	}
}

static int cfq_set_request(request_queue_t *q, struct request *rq, int gfp_mask)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct cfq_queue *cfqq;
	struct cfq_rq *crq;

	/*
	 * prepare a queue up front, so cfq_enqueue() doesn't have to
	 */
	cfqq = cfq_get_queue(cfqd, current->tgid, gfp_mask);
	if (!cfqq)
		return 1;

	crq = mempool_alloc(cfqd->crq_pool, gfp_mask);
	if (crq) {
		memset(crq, 0, sizeof(*crq));
		RB_CLEAR(&crq->rb_node);
		crq->request = rq;
		crq->cfq_queue = NULL;
		INIT_LIST_HEAD(&crq->hash);
		rq->elevator_private = crq;
		return 0;
	}

	return 1;
}

static void cfq_exit(request_queue_t *q, elevator_t *e)
{
	struct cfq_data *cfqd = e->elevator_data;

	e->elevator_data = NULL;
	mempool_destroy(cfqd->crq_pool);
	kfree(cfqd->crq_hash);
	kfree(cfqd->cfq_hash);
	kfree(cfqd);
}

static int cfq_init(request_queue_t *q, elevator_t *e)
{
	struct cfq_data *cfqd;
	int i;

	cfqd = kmalloc(sizeof(*cfqd), GFP_KERNEL);
	if (!cfqd)
		return -ENOMEM;

	memset(cfqd, 0, sizeof(*cfqd));
	INIT_LIST_HEAD(&cfqd->rr_list);

	cfqd->crq_hash = kmalloc(sizeof(struct list_head) * CFQ_MHASH_ENTRIES, GFP_KERNEL);
	if (!cfqd->crq_hash)
		goto out_crqhash;

	cfqd->cfq_hash = kmalloc(sizeof(struct list_head) * CFQ_QHASH_ENTRIES, GFP_KERNEL);
	if (!cfqd->cfq_hash)
		goto out_cfqhash;

	cfqd->crq_pool = mempool_create(BLKDEV_MIN_RQ, mempool_alloc_slab, mempool_free_slab, crq_pool);
	if (!cfqd->crq_pool)
		goto out_crqpool;

	for (i = 0; i < CFQ_MHASH_ENTRIES; i++)
		INIT_LIST_HEAD(&cfqd->crq_hash[i]);
	for (i = 0; i < CFQ_QHASH_ENTRIES; i++)
		INIT_LIST_HEAD(&cfqd->cfq_hash[i]);

	cfqd->dispatch = &q->queue_head;
	e->elevator_data = cfqd;
	cfqd->queue = q;

	/*
	 * just set it to some high value, we want anyone to be able to queue
	 * some requests. fairness is handled differently
	 */
	cfqd->max_queued = q->nr_requests;
	q->nr_requests = 8192;

	cfqd->cfq_queued = cfq_queued;
	cfqd->cfq_quantum = cfq_quantum;

	return 0;
out_crqpool:
	kfree(cfqd->cfq_hash);
out_cfqhash:
	kfree(cfqd->crq_hash);
out_crqhash:
	kfree(cfqd);
	return -ENOMEM;
}

static int __init cfq_slab_setup(void)
{
	crq_pool = kmem_cache_create("crq_pool", sizeof(struct cfq_rq), 0, 0,
					NULL, NULL);

	if (!crq_pool)
		panic("cfq_iosched: can't init crq pool\n");

	cfq_pool = kmem_cache_create("cfq_pool", sizeof(struct cfq_queue), 0, 0,
					NULL, NULL);

	if (!cfq_pool)
		panic("cfq_iosched: can't init cfq pool\n");

	cfq_mpool = mempool_create(64, mempool_alloc_slab, mempool_free_slab, cfq_pool);

	if (!cfq_mpool)
		panic("cfq_iosched: can't init cfq mpool\n");

	return 0;
}

subsys_initcall(cfq_slab_setup);

/*
 * sysfs parts below -->
 */
struct cfq_fs_entry {
	struct attribute attr;
	ssize_t (*show)(struct cfq_data *, char *);
	ssize_t (*store)(struct cfq_data *, const char *, size_t);
};

static ssize_t
cfq_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
cfq_var_store(unsigned int *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtoul(p, &p, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR)					\
static ssize_t __FUNC(struct cfq_data *cfqd, char *page)		\
{									\
	return cfq_var_show(__VAR, (page));				\
}
SHOW_FUNCTION(cfq_quantum_show, cfqd->cfq_quantum);
SHOW_FUNCTION(cfq_queued_show, cfqd->cfq_queued);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX)				\
static ssize_t __FUNC(struct cfq_data *cfqd, const char *page, size_t count)	\
{									\
	int ret = cfq_var_store(__PTR, (page), count);			\
	if (*(__PTR) < (MIN))						\
		*(__PTR) = (MIN);					\
	else if (*(__PTR) > (MAX))					\
		*(__PTR) = (MAX);					\
	return ret;							\
}
STORE_FUNCTION(cfq_quantum_store, &cfqd->cfq_quantum, 1, INT_MAX);
STORE_FUNCTION(cfq_queued_store, &cfqd->cfq_queued, 1, INT_MAX);
#undef STORE_FUNCTION

static struct cfq_fs_entry cfq_quantum_entry = {
	.attr = {.name = "quantum", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_quantum_show,
	.store = cfq_quantum_store,
};
static struct cfq_fs_entry cfq_queued_entry = {
	.attr = {.name = "queued", .mode = S_IRUGO | S_IWUSR },
	.show = cfq_queued_show,
	.store = cfq_queued_store,
};

static struct attribute *default_attrs[] = {
	&cfq_quantum_entry.attr,
	&cfq_queued_entry.attr,
	NULL,
};

#define to_cfq(atr) container_of((atr), struct cfq_fs_entry, attr)

static ssize_t
cfq_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	elevator_t *e = container_of(kobj, elevator_t, kobj);
	struct cfq_fs_entry *entry = to_cfq(attr);

	if (!entry->show)
		return 0;

	return entry->show(e->elevator_data, page);
}

static ssize_t
cfq_attr_store(struct kobject *kobj, struct attribute *attr,
	       const char *page, size_t length)
{
	elevator_t *e = container_of(kobj, elevator_t, kobj);
	struct cfq_fs_entry *entry = to_cfq(attr);

	if (!entry->store)
		return -EINVAL;

	return entry->store(e->elevator_data, page, length);
}

static struct sysfs_ops cfq_sysfs_ops = {
	.show	= cfq_attr_show,
	.store	= cfq_attr_store,
};

struct kobj_type cfq_ktype = {
	.sysfs_ops	= &cfq_sysfs_ops,
	.default_attrs	= default_attrs,
};

elevator_t iosched_cfq = {
	.elevator_name =		"cfq",
	.elevator_ktype =		&cfq_ktype,
	.elevator_merge_fn = 		cfq_merge,
	.elevator_merged_fn =		cfq_merged_request,
	.elevator_merge_req_fn =	cfq_merged_requests,
	.elevator_next_req_fn =		cfq_next_request,
	.elevator_add_req_fn =		cfq_insert_request,
	.elevator_remove_req_fn =	cfq_remove_request,
	.elevator_queue_empty_fn =	cfq_queue_empty,
	.elevator_former_req_fn =	cfq_former_request,
	.elevator_latter_req_fn =	cfq_latter_request,
	.elevator_set_req_fn =		cfq_set_request,
	.elevator_put_req_fn =		cfq_put_request,
	.elevator_may_queue_fn =	cfq_may_queue,
	.elevator_init_fn =		cfq_init,
	.elevator_exit_fn =		cfq_exit,
};

EXPORT_SYMBOL(iosched_cfq);
