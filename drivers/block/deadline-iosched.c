/*
 *  linux/drivers/block/deadline-iosched.c
 *
 *  Deadline i/o scheduler.
 *
 *  Copyright (C) 2002 Jens Axboe <axboe@suse.de>
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/blk.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/hash.h>

/*
 * feel free to try other values :-). read_expire value is the timeout for
 * reads, our goal is to start a request "around" the time when it expires.
 * fifo_batch is how many steps along the sorted list we will take when the
 * front fifo request expires.
 */
static int read_expire = HZ / 2;	/* 500ms start timeout */
static int fifo_batch = 32;		/* 4 seeks, or 64 contig */
static int seek_cost = 16;		/* seek is 16 times more expensive */

/*
 * how many times reads are allowed to starve writes
 */
static int writes_starved = 2;

static const int deadline_hash_shift = 8;
#define DL_HASH_BLOCK(sec)	((sec) >> 3)
#define DL_HASH_FN(sec)		(hash_long(DL_HASH_BLOCK((sec)), deadline_hash_shift))
#define DL_HASH_ENTRIES		(1 << deadline_hash_shift)

#define DL_INVALIDATE_HASH(dd)				\
	do {						\
		if (!++(dd)->hash_valid_count)		\
			(dd)->hash_valid_count = 1;	\
	} while (0)

struct deadline_data {
	/*
	 * run time data
	 */
	struct list_head sort_list[2];	/* sorted listed */
	struct list_head read_fifo;	/* fifo list */
	struct list_head *dispatch;	/* driver dispatch queue */
	struct list_head *hash;		/* request hash */
	sector_t last_sector;		/* last sector sent to drive */
	unsigned long hash_valid_count;	/* barrier hash count */
	unsigned int starved;		/* writes starved */

	/*
	 * settings that change how the i/o scheduler behaves
	 */
	unsigned int fifo_batch;
	unsigned long read_expire;
	unsigned int seek_cost;
	unsigned int writes_starved;
};

/*
 * pre-request data.
 */
struct deadline_rq {
	struct list_head fifo;
	struct list_head hash;
	unsigned long hash_valid_count;
	struct request *request;
	unsigned long expires;
};

static kmem_cache_t *drq_pool;

#define RQ_DATA(rq)	((struct deadline_rq *) (rq)->elevator_private)

/*
 * rq hash
 */
static inline void __deadline_del_rq_hash(struct deadline_rq *drq)
{
	drq->hash_valid_count = 0;
	list_del_init(&drq->hash);
}

#define ON_HASH(drq)	(drq)->hash_valid_count
static inline void deadline_del_rq_hash(struct deadline_rq *drq)
{
	if (ON_HASH(drq))
		__deadline_del_rq_hash(drq);
}

static inline void
deadline_add_rq_hash(struct deadline_data *dd, struct deadline_rq *drq)
{
	struct request *rq = drq->request;

	BUG_ON(ON_HASH(drq));

	drq->hash_valid_count = dd->hash_valid_count;
	list_add(&drq->hash, &dd->hash[DL_HASH_FN(rq->sector +rq->nr_sectors)]);
}

#define list_entry_hash(ptr)	list_entry((ptr), struct deadline_rq, hash)
static struct request *
deadline_find_hash(struct deadline_data *dd, sector_t offset)
{
	struct list_head *hash_list = &dd->hash[DL_HASH_FN(offset)];
	struct list_head *entry, *next = hash_list->next;
	struct deadline_rq *drq;
	struct request *rq = NULL;

	while ((entry = next) != hash_list) {
		next = entry->next;
		drq = list_entry_hash(entry);

		BUG_ON(!drq->hash_valid_count);

		if (!rq_mergeable(drq->request)
		    || drq->hash_valid_count != dd->hash_valid_count) {
			__deadline_del_rq_hash(drq);
			continue;
		}

		if (drq->request->sector + drq->request->nr_sectors == offset) {
			rq = drq->request;
			break;
		}
	}

	return rq;
}

static sector_t deadline_get_last_sector(struct deadline_data *dd)
{
	sector_t last_sec = dd->last_sector;

	/*
	 * if dispatch is non-empty, disregard last_sector and check last one
	 */
	if (!list_empty(dd->dispatch)) {
		struct request *__rq = list_entry_rq(dd->dispatch->prev);

		last_sec = __rq->sector + __rq->nr_sectors;
	}

	return last_sec;
}

static int
deadline_merge(request_queue_t *q, struct list_head **insert, struct bio *bio)
{
	struct deadline_data *dd = q->elevator.elevator_data;
	const int data_dir = bio_data_dir(bio);
	struct list_head *entry, *sort_list;
	struct request *__rq;
	int ret = ELEVATOR_NO_MERGE;

	/*
	 * try last_merge to avoid going to hash
	 */
	ret = elv_try_last_merge(q, bio);
	if (ret != ELEVATOR_NO_MERGE) {
		*insert = q->last_merge;
		goto out;
	}

	/*
	 * see if the merge hash can satisfy a back merge
	 */
	if ((__rq = deadline_find_hash(dd, bio->bi_sector))) {
		BUG_ON(__rq->sector + __rq->nr_sectors != bio->bi_sector);

		if (elv_rq_merge_ok(__rq, bio)) {
			*insert = &__rq->queuelist;
			ret = ELEVATOR_BACK_MERGE;
			goto out;
		}
	}

	/*
	 * scan list from back to find insertion point.
	 */
	entry = sort_list = &dd->sort_list[data_dir];
	while ((entry = entry->prev) != sort_list) {
		__rq = list_entry_rq(entry);

		BUG_ON(__rq->flags & REQ_STARTED);

		if (!(__rq->flags & REQ_CMD))
			continue;

		/*
		 * it's not necessary to break here, and in fact it could make
		 * us loose a front merge. emperical evidence shows this to
		 * be a big waste of cycles though, so quit scanning
		 */
		if (!*insert && bio_rq_in_between(bio, __rq, sort_list)) {
			*insert = &__rq->queuelist;
			break;
		}

		if (__rq->flags & REQ_BARRIER)
			break;

		/*
		 * checking for a front merge, hash will miss those
		 */
		if (__rq->sector - bio_sectors(bio) == bio->bi_sector) {
			ret = elv_try_merge(__rq, bio);
			if (ret != ELEVATOR_NO_MERGE) {
				*insert = &__rq->queuelist;
				break;
			}
		}
	}

	/*
	 * no insertion point found, check the very front
	 */
	if (!*insert && !list_empty(sort_list)) {
		__rq = list_entry_rq(sort_list->next);

		if (bio->bi_sector + bio_sectors(bio) < __rq->sector &&
		    bio->bi_sector > deadline_get_last_sector(dd))
			*insert = sort_list;
	}

out:
	return ret;
}

static void deadline_merged_request(request_queue_t *q, struct request *req)
{
	struct deadline_data *dd = q->elevator.elevator_data;
	struct deadline_rq *drq = RQ_DATA(req);

	deadline_del_rq_hash(drq);
	deadline_add_rq_hash(dd, drq);

	q->last_merge = &req->queuelist;
}

static void
deadline_merge_request(request_queue_t *q, struct request *req, struct request *next)
{
	struct deadline_data *dd = q->elevator.elevator_data;
	struct deadline_rq *drq = RQ_DATA(req);
	struct deadline_rq *dnext = RQ_DATA(next);

	BUG_ON(!drq);
	BUG_ON(!dnext);

	deadline_del_rq_hash(drq);
	deadline_add_rq_hash(dd, drq);

	/*
	 * if dnext expires before drq, assign it's expire time to drq
	 * and move into dnext position (dnext will be deleted) in fifo
	 */
	if (!list_empty(&drq->fifo) && !list_empty(&dnext->fifo)) {
		if (time_before(dnext->expires, drq->expires)) {
			list_move(&drq->fifo, &dnext->fifo);
			drq->expires = dnext->expires;
		}
	}
}

/*
 * move request from sort list to dispatch queue. maybe remove from rq hash
 * here too?
 */
static inline void
deadline_move_to_dispatch(struct deadline_data *dd, struct request *rq)
{
	struct deadline_rq *drq = RQ_DATA(rq);

	list_move_tail(&rq->queuelist, dd->dispatch);
	list_del_init(&drq->fifo);
}

/*
 * move along sort list and move entries to dispatch queue, starting from rq
 */
static void deadline_move_requests(struct deadline_data *dd, struct request *rq)
{
	struct list_head *sort_head = &dd->sort_list[rq_data_dir(rq)];
	sector_t last_sec = deadline_get_last_sector(dd);
	int batch_count = dd->fifo_batch;

	do {
		struct list_head *nxt = rq->queuelist.next;
		int this_rq_cost;

		/*
		 * take it off the sort and fifo list, move
		 * to dispatch queue
		 */
		deadline_move_to_dispatch(dd, rq);

		/*
		 * if this is the last entry, don't bother doing accounting
		 */
		if (nxt == sort_head)
			break;

		this_rq_cost = dd->seek_cost;
		if (rq->sector == last_sec)
			this_rq_cost = (rq->nr_sectors + 255) >> 8;

		batch_count -= this_rq_cost;
		if (batch_count <= 0)
			break;

		last_sec = rq->sector + rq->nr_sectors;
		rq = list_entry_rq(nxt);
	} while (1);
}

/*
 * returns 0 if there are no expired reads on the fifo, 1 otherwise
 */
#define list_entry_fifo(ptr)	list_entry((ptr), struct deadline_rq, fifo)
static inline int deadline_check_fifo(struct deadline_data *dd)
{
	if (!list_empty(&dd->read_fifo)) {
		struct deadline_rq *drq = list_entry_fifo(dd->read_fifo.next);

		/*
		 * drq is expired!
		 */
		if (time_after(jiffies, drq->expires))
			return 1;
	}

	return 0;
}

static struct request *deadline_next_request(request_queue_t *q)
{
	struct deadline_data *dd = q->elevator.elevator_data;
	struct deadline_rq *drq;
	struct list_head *nxt;
	struct request *rq;
	int writes;

	/*
	 * if still requests on the dispatch queue, just grab the first one
	 */
	if (!list_empty(&q->queue_head)) {
dispatch:
		rq = list_entry_rq(q->queue_head.next);
		dd->last_sector = rq->sector + rq->nr_sectors;
		return rq;
	}

	writes = !list_empty(&dd->sort_list[WRITE]);

	/*
	 * if we have expired entries on the fifo list, move some to dispatch
	 */
	if (deadline_check_fifo(dd)) {
		if (writes && (dd->starved++ >= dd->writes_starved))
			goto dispatch_writes;

		nxt = dd->read_fifo.next;
		drq = list_entry_fifo(nxt);
		deadline_move_requests(dd, drq->request);
		goto dispatch;
	}

	if (!list_empty(&dd->sort_list[READ])) {
		if (writes && (dd->starved++ >= dd->writes_starved))
			goto dispatch_writes;

		nxt = dd->sort_list[READ].next;
		deadline_move_requests(dd, list_entry_rq(nxt));
		goto dispatch;
	}

	/*
	 * either there are no reads expired or on sort list, or the reads
	 * have starved writes for too long. dispatch some writes
	 */
	if (writes) {
dispatch_writes:
		nxt = dd->sort_list[WRITE].next;
		deadline_move_requests(dd, list_entry_rq(nxt));
		dd->starved = 0;
		goto dispatch;
	}

	BUG_ON(!list_empty(&dd->sort_list[READ]));
	BUG_ON(writes);
	return NULL;
}

static void
deadline_add_request(request_queue_t *q, struct request *rq, struct list_head *insert_here)
{
	struct deadline_data *dd = q->elevator.elevator_data;
	struct deadline_rq *drq = RQ_DATA(rq);
	const int data_dir = rq_data_dir(rq);

	/*
	 * flush hash on barrier insert, as not to allow merges before a
	 * barrier.
	 */
	if (unlikely(rq->flags & REQ_BARRIER)) {
		DL_INVALIDATE_HASH(dd);
		q->last_merge = NULL;
	}

	/*
	 * add to sort list
	 */
	if (!insert_here)
		insert_here = dd->sort_list[data_dir].prev;

	list_add(&rq->queuelist, insert_here);

	if (unlikely(!(rq->flags & REQ_CMD)))
		return;

	if (rq_mergeable(rq)) {
		deadline_add_rq_hash(dd, drq);

		if (!q->last_merge)
			q->last_merge = &rq->queuelist;
	}

	if (data_dir == READ) {
		/*
		 * set expire time and add to fifo list
		 */
		drq->expires = jiffies + dd->read_expire;
		list_add_tail(&drq->fifo, &dd->read_fifo);
	}
}

static void deadline_remove_request(request_queue_t *q, struct request *rq)
{
	struct deadline_rq *drq = RQ_DATA(rq);

	if (drq) {
		list_del_init(&drq->fifo);
		deadline_del_rq_hash(drq);
	}
}

static int deadline_queue_empty(request_queue_t *q)
{
	struct deadline_data *dd = q->elevator.elevator_data;

	if (!list_empty(&dd->sort_list[WRITE]) ||
	    !list_empty(&dd->sort_list[READ]) ||
	    !list_empty(&q->queue_head))
		return 0;

	BUG_ON(!list_empty(&dd->read_fifo));
	return 1;
}

static struct list_head *
deadline_get_sort_head(request_queue_t *q, struct request *rq)
{
	struct deadline_data *dd = q->elevator.elevator_data;

	return &dd->sort_list[rq_data_dir(rq)];
}

static void deadline_exit(request_queue_t *q, elevator_t *e)
{
	struct deadline_data *dd = e->elevator_data;
	struct deadline_rq *drq;
	struct request *rq;
	int i;

	BUG_ON(!list_empty(&dd->read_fifo));
	BUG_ON(!list_empty(&dd->sort_list[READ]));
	BUG_ON(!list_empty(&dd->sort_list[WRITE]));

	for (i = READ; i <= WRITE; i++) {
		struct request_list *rl = &q->rq[i];
		struct list_head *entry = &rl->free;

		if (list_empty(&rl->free))
			continue;
	
		while ((entry = entry->next) != &rl->free) {
			rq = list_entry_rq(entry);

			if ((drq = RQ_DATA(rq)) == NULL)
				continue;

			rq->elevator_private = NULL;
			kmem_cache_free(drq_pool, drq);
		}
	}

	kfree(dd->hash);
	kfree(dd);
}

/*
 * initialize elevator private data (deadline_data), and alloc a drq for
 * each request on the free lists
 */
static int deadline_init(request_queue_t *q, elevator_t *e)
{
	struct deadline_data *dd;
	struct deadline_rq *drq;
	struct request *rq;
	int i, ret = 0;

	if (!drq_pool)
		return -ENOMEM;

	dd = kmalloc(sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;
	memset(dd, 0, sizeof(*dd));

	dd->hash = kmalloc(sizeof(struct list_head)*DL_HASH_ENTRIES,GFP_KERNEL);
	if (!dd->hash) {
		kfree(dd);
		return -ENOMEM;
	}

	for (i = 0; i < DL_HASH_ENTRIES; i++)
		INIT_LIST_HEAD(&dd->hash[i]);

	INIT_LIST_HEAD(&dd->read_fifo);
	INIT_LIST_HEAD(&dd->sort_list[READ]);
	INIT_LIST_HEAD(&dd->sort_list[WRITE]);
	dd->dispatch = &q->queue_head;
	dd->fifo_batch = fifo_batch;
	dd->read_expire = read_expire;
	dd->seek_cost = seek_cost;
	dd->hash_valid_count = 1;
	dd->writes_starved = writes_starved;
	e->elevator_data = dd;

	for (i = READ; i <= WRITE; i++) {
		struct request_list *rl = &q->rq[i];
		struct list_head *entry = &rl->free;

		if (list_empty(&rl->free))
			continue;
	
		while ((entry = entry->next) != &rl->free) {
			rq = list_entry_rq(entry);

			drq = kmem_cache_alloc(drq_pool, GFP_KERNEL);
			if (!drq) {
				ret = -ENOMEM;
				break;
			}

			memset(drq, 0, sizeof(*drq));
			INIT_LIST_HEAD(&drq->fifo);
			INIT_LIST_HEAD(&drq->hash);
			drq->request = rq;
			rq->elevator_private = drq;
		}
	}

	if (ret)
		deadline_exit(q, e);

	return ret;
}

static int __init deadline_slab_setup(void)
{
	drq_pool = kmem_cache_create("deadline_drq", sizeof(struct deadline_rq),
				     0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!drq_pool)
		panic("deadline: can't init slab pool\n");

	return 0;
}

subsys_init(deadline_slab_setup);

elevator_t iosched_deadline = {
	.elevator_merge_fn = 		deadline_merge,
	.elevator_merged_fn =		deadline_merged_request,
	.elevator_merge_req_fn =	deadline_merge_request,
	.elevator_next_req_fn =		deadline_next_request,
	.elevator_add_req_fn =		deadline_add_request,
	.elevator_remove_req_fn =	deadline_remove_request,
	.elevator_queue_empty_fn =	deadline_queue_empty,
	.elevator_get_sort_head_fn =	deadline_get_sort_head,
	.elevator_init_fn =		deadline_init,
	.elevator_exit_fn =		deadline_exit,
};

EXPORT_SYMBOL(iosched_deadline);
