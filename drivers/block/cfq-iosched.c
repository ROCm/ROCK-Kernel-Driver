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
static void cfq_dispatch_sort(struct list_head *head, struct cfq_rq *crq);

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
	return 0;
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

	cfq_del_crq_rb(cfqq, __alias);
	cfq_dispatch_sort(cfqd->dispatch, __alias);
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

static void cfq_dispatch_sort(struct list_head *head, struct cfq_rq *crq)
{
	struct list_head *entry = head;
	struct request *__rq;

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

	cfq_del_crq_rb(cfqq, crq);
	cfq_remove_merge_hints(q, crq);
	cfq_dispatch_sort(cfqd->dispatch, crq);
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

	if ((queued < cfq_quantum) && good_queues)
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

		BUG_ON(q->last_merge == rq);
		crq = RQ_DATA(rq);
		if (crq)
			BUG_ON(ON_MHASH(crq));

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

static struct cfq_queue *cfq_get_queue(struct cfq_data *cfqd, int pid)
{
	const int hashval = hash_long(current->tgid, CFQ_QHASH_SHIFT);
	struct cfq_queue *cfqq = __cfq_find_cfq_hash(cfqd, pid, hashval);

	if (!cfqq) {
		cfqq = mempool_alloc(cfq_mpool, GFP_NOIO);

		INIT_LIST_HEAD(&cfqq->cfq_hash);
		INIT_LIST_HEAD(&cfqq->cfq_list);
		RB_CLEAR_ROOT(&cfqq->sort_list);

		cfqq->pid = pid;
		cfqq->queued[0] = cfqq->queued[1] = 0;
		list_add(&cfqq->cfq_hash, &cfqd->cfq_hash[hashval]);
	}

	return cfqq;
}

static void cfq_enqueue(struct cfq_data *cfqd, struct cfq_rq *crq)
{
	struct cfq_queue *cfqq;

	cfqq = cfq_get_queue(cfqd, current->tgid);

	cfq_add_crq_rb(cfqd, cfqq, crq);

	if (list_empty(&cfqq->cfq_list)) {
		list_add(&cfqq->cfq_list, &cfqd->rr_list);
		cfqd->busy_queues++;
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
		int limit = (q->nr_requests - cfq_queued) / cfqd->busy_queues;

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

	if (crq) {
		BUG_ON(q->last_merge == rq);
		BUG_ON(ON_MHASH(crq));

		mempool_free(crq, cfqd->crq_pool);
		rq->elevator_private = NULL;
	}
}

static int cfq_set_request(request_queue_t *q, struct request *rq, int gfp_mask)
{
	struct cfq_data *cfqd = q->elevator.elevator_data;
	struct cfq_rq *crq = mempool_alloc(cfqd->crq_pool, gfp_mask);

	if (crq) {
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

	/*
	 * just set it to some high value, we want anyone to be able to queue
	 * some requests. fairness is handled differently
	 */
	cfqd->max_queued = q->nr_requests;
	q->nr_requests = 8192;

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

elevator_t iosched_cfq = {
	.elevator_name =		"cfq",
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
