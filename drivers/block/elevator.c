/*
 *  linux/drivers/block/elevator.c
 *
 *  Block device elevator/IO-scheduler.
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *
 * 30042000 Jens Axboe <axboe@suse.de> :
 *
 * Split the elevator a bit so that it is possible to choose a different
 * one or even write a new "plug in". There are three pieces:
 * - elevator_fn, inserts a new request in the queue list
 * - elevator_merge_fn, decides whether a new buffer can be merged with
 *   an existing request
 * - elevator_dequeue_fn, called when a request is taken off the active list
 *
 * 20082000 Dave Jones <davej@suse.de> :
 * Removed tests for max-bomb-segments, which was breaking elvtune
 *  when run without -bN
 *
 * Jens:
 * - Rework again to work with bio instead of buffer_heads
 * - loose bi_dev comparisons, partition handling is right now
 * - completely modularize elevator setup and teardown
 *
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

#include <asm/uaccess.h>

/*
 * can we safely merge with this request?
 */
inline int elv_rq_merge_ok(struct request *rq, struct bio *bio)
{
	if (!rq_mergeable(rq))
		return 0;

	/*
	 * different data direction or already started, don't merge
	 */
	if (bio_data_dir(bio) != rq_data_dir(rq))
		return 0;

	/*
	 * same device and no special stuff set, merge is ok
	 */
	if (rq->rq_disk == bio->bi_bdev->bd_disk &&
	    !rq->waiting && !rq->special)
		return 1;

	return 0;
}

inline int elv_try_merge(struct request *__rq, struct bio *bio)
{
	int ret = ELEVATOR_NO_MERGE;

	/*
	 * we can merge and sequence is ok, check if it's possible
	 */
	if (elv_rq_merge_ok(__rq, bio)) {
		if (__rq->sector + __rq->nr_sectors == bio->bi_sector)
			ret = ELEVATOR_BACK_MERGE;
		else if (__rq->sector - bio_sectors(bio) == bio->bi_sector)
			ret = ELEVATOR_FRONT_MERGE;
	}

	return ret;
}

inline int elv_try_last_merge(request_queue_t *q, struct bio *bio)
{
	if (q->last_merge)
		return elv_try_merge(q->last_merge, bio);

	return ELEVATOR_NO_MERGE;
}

/*
 * general block -> elevator interface starts here
 */
int elevator_init(request_queue_t *q, elevator_t *type)
{
	elevator_t *e = &q->elevator;

	memcpy(e, type, sizeof(*e));

	INIT_LIST_HEAD(&q->queue_head);
	q->last_merge = NULL;

	if (e->elevator_init_fn)
		return e->elevator_init_fn(q, e);

	return 0;
}

void elevator_exit(request_queue_t *q)
{
	elevator_t *e = &q->elevator;

	if (e->elevator_exit_fn)
		e->elevator_exit_fn(q, e);
}

int elevator_global_init(void)
{
	return 0;
}

int elv_merge(request_queue_t *q, struct request **req, struct bio *bio)
{
	elevator_t *e = &q->elevator;

	if (e->elevator_merge_fn)
		return e->elevator_merge_fn(q, req, bio);

	return ELEVATOR_NO_MERGE;
}

void elv_merged_request(request_queue_t *q, struct request *rq)
{
	elevator_t *e = &q->elevator;

	if (e->elevator_merged_fn)
		e->elevator_merged_fn(q, rq);
}

void elv_merge_requests(request_queue_t *q, struct request *rq,
			     struct request *next)
{
	elevator_t *e = &q->elevator;

	if (q->last_merge == next)
		q->last_merge = NULL;

	if (e->elevator_merge_req_fn)
		e->elevator_merge_req_fn(q, rq, next);
}

void elv_requeue_request(request_queue_t *q, struct request *rq)
{
	/*
	 * it already went through dequeue, we need to decrement the
	 * in_flight count again
	 */
	if (blk_account_rq(rq))
		q->in_flight--;

	/*
	 * if iosched has an explicit requeue hook, then use that. otherwise
	 * just put the request at the front of the queue
	 */
	if (q->elevator.elevator_requeue_req_fn)
		q->elevator.elevator_requeue_req_fn(q, rq);
	else
		__elv_add_request(q, rq, ELEVATOR_INSERT_FRONT, 0);
}

void __elv_add_request(request_queue_t *q, struct request *rq, int where,
		       int plug)
{
	if (plug)
		blk_plug_device(q);

	rq->q = q;
	q->elevator.elevator_add_req_fn(q, rq, where);
}

void elv_add_request(request_queue_t *q, struct request *rq, int where,
		     int plug)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__elv_add_request(q, rq, where, plug);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static inline struct request *__elv_next_request(request_queue_t *q)
{
	return q->elevator.elevator_next_req_fn(q);
}

struct request *elv_next_request(request_queue_t *q)
{
	struct request *rq;
	int ret;

	while ((rq = __elv_next_request(q))) {
		/*
		 * just mark as started even if we don't start it, a request
		 * that has been delayed should not be passed by new incoming
		 * requests
		 */
		rq->flags |= REQ_STARTED;

		if (rq == q->last_merge)
			q->last_merge = NULL;

		if ((rq->flags & REQ_DONTPREP) || !q->prep_rq_fn)
			break;

		ret = q->prep_rq_fn(q, rq);
		if (ret == BLKPREP_OK) {
			break;
		} else if (ret == BLKPREP_DEFER) {
			rq = NULL;
			break;
		} else if (ret == BLKPREP_KILL) {
			int nr_bytes = rq->hard_nr_sectors << 9;

			if (!nr_bytes)
				nr_bytes = rq->data_len;

			blkdev_dequeue_request(rq);
			rq->flags |= REQ_QUIET;
			end_that_request_chunk(rq, 0, nr_bytes);
			end_that_request_last(rq);
		} else {
			printk("%s: bad return=%d\n", __FUNCTION__, ret);
			break;
		}
	}

	return rq;
}

void elv_remove_request(request_queue_t *q, struct request *rq)
{
	elevator_t *e = &q->elevator;

	/*
	 * the time frame between a request being removed from the lists
	 * and to it is freed is accounted as io that is in progress at
	 * the driver side. note that we only account requests that the
	 * driver has seen (REQ_STARTED set), to avoid false accounting
	 * for request-request merges
	 */
	if (blk_account_rq(rq))
		q->in_flight++;

	/*
	 * the main clearing point for q->last_merge is on retrieval of
	 * request by driver (it calls elv_next_request()), but it _can_
	 * also happen here if a request is added to the queue but later
	 * deleted without ever being given to driver (merged with another
	 * request).
	 */
	if (rq == q->last_merge)
		q->last_merge = NULL;

	if (e->elevator_remove_req_fn)
		e->elevator_remove_req_fn(q, rq);
}

int elv_queue_empty(request_queue_t *q)
{
	elevator_t *e = &q->elevator;

	if (e->elevator_queue_empty_fn)
		return e->elevator_queue_empty_fn(q);

	return list_empty(&q->queue_head);
}

struct request *elv_latter_request(request_queue_t *q, struct request *rq)
{
	struct list_head *next;

	elevator_t *e = &q->elevator;

	if (e->elevator_latter_req_fn)
		return e->elevator_latter_req_fn(q, rq);

	next = rq->queuelist.next;
	if (next != &q->queue_head && next != &rq->queuelist)
		return list_entry_rq(next);

	return NULL;
}

struct request *elv_former_request(request_queue_t *q, struct request *rq)
{
	struct list_head *prev;

	elevator_t *e = &q->elevator;

	if (e->elevator_former_req_fn)
		return e->elevator_former_req_fn(q, rq);

	prev = rq->queuelist.prev;
	if (prev != &q->queue_head && prev != &rq->queuelist)
		return list_entry_rq(prev);

	return NULL;
}

int elv_set_request(request_queue_t *q, struct request *rq, int gfp_mask)
{
	elevator_t *e = &q->elevator;

	if (e->elevator_set_req_fn)
		return e->elevator_set_req_fn(q, rq, gfp_mask);

	rq->elevator_private = NULL;
	return 0;
}

void elv_put_request(request_queue_t *q, struct request *rq)
{
	elevator_t *e = &q->elevator;

	if (e->elevator_put_req_fn)
		e->elevator_put_req_fn(q, rq);
}

int elv_may_queue(request_queue_t *q, int rw)
{
	elevator_t *e = &q->elevator;

	if (e->elevator_may_queue_fn)
		return e->elevator_may_queue_fn(q, rw);

	return 0;
}

void elv_completed_request(request_queue_t *q, struct request *rq)
{
	elevator_t *e = &q->elevator;

	/*
	 * request is released from the driver, io must be done
	 */
	if (blk_account_rq(rq))
		q->in_flight--;

	if (e->elevator_completed_req_fn)
		e->elevator_completed_req_fn(q, rq);
}

int elv_register_queue(struct request_queue *q)
{
	elevator_t *e;

	e = &q->elevator;

	e->kobj.parent = kobject_get(&q->kobj);
	if (!e->kobj.parent)
		return -EBUSY;

	snprintf(e->kobj.name, KOBJ_NAME_LEN, "%s", "iosched");
	e->kobj.ktype = e->elevator_ktype;

	return kobject_register(&e->kobj);
}

void elv_unregister_queue(struct request_queue *q)
{
	if (q) {
		elevator_t * e = &q->elevator;
		kobject_unregister(&e->kobj);
		kobject_put(&q->kobj);
	}
}

module_init(elevator_global_init);

EXPORT_SYMBOL(elv_add_request);
EXPORT_SYMBOL(__elv_add_request);
EXPORT_SYMBOL(elv_requeue_request);
EXPORT_SYMBOL(elv_next_request);
EXPORT_SYMBOL(elv_remove_request);
EXPORT_SYMBOL(elv_queue_empty);
EXPORT_SYMBOL(elv_completed_request);
EXPORT_SYMBOL(elevator_exit);
EXPORT_SYMBOL(elevator_init);
