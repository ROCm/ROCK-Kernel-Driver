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
#include <linux/blk.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>

#include <asm/uaccess.h>

/*
 * This is a bit tricky. It's given that bio and rq are for the same
 * device, but the next request might of course not be. Run through
 * the tests below to check if we want to insert here if we can't merge
 * bio into an existing request
 */
inline int bio_rq_in_between(struct bio *bio, struct request *rq,
			     struct list_head *head)
{
	struct list_head *next;
	struct request *next_rq;

	/*
	 * if .next is a valid request
	 */
	next = rq->queuelist.next;
	if (unlikely(next == head))
		return 0;

	next_rq = list_entry(next, struct request, queuelist);

	/*
	 * not a sector based request
	 */
	if (!(next_rq->flags & REQ_CMD))
		return 0;

	/*
	 * if the device is different (not a normal case) just check if
	 * bio is after rq
	 */
	if (next_rq->rq_disk != rq->rq_disk)
		return bio->bi_sector > rq->sector;

	/*
	 * ok, rq, next_rq and bio are on the same device. if bio is in between
	 * the two, this is the sweet spot
	 */
	if (bio->bi_sector < next_rq->sector && bio->bi_sector > rq->sector)
		return 1;

	/*
	 * next_rq is ordered wrt rq, but bio is not in between the two
	 */
	if (next_rq->sector > rq->sector)
		return 0;

	/*
	 * next_rq and rq not ordered, if we happen to be either before
	 * next_rq or after rq insert here anyway
	 */
	if (bio->bi_sector > rq->sector || bio->bi_sector < next_rq->sector)
		return 1;

	return 0;
}

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
	int ret = ELEVATOR_NO_MERGE;

	/*
	 * give a one-shot try to merging with the last touched
	 * request
	 */
	if (q->last_merge) {
		struct request *__rq = list_entry_rq(q->last_merge);

		if (!rq_mergeable(__rq))
			q->last_merge = NULL;
		else
			ret = elv_try_merge(__rq, bio);
	}

	return ret;
}

/*
 * elevator noop
 *
 * See if we can find a request that this buffer can be coalesced with.
 */
int elevator_noop_merge(request_queue_t *q, struct list_head **insert,
			struct bio *bio)
{
	struct list_head *entry = &q->queue_head;
	struct request *__rq;
	int ret;

	if ((ret = elv_try_last_merge(q, bio))) {
		*insert = q->last_merge;
		return ret;
	}

	while ((entry = entry->prev) != &q->queue_head) {
		__rq = list_entry_rq(entry);

		if (__rq->flags & (REQ_SOFTBARRIER | REQ_HARDBARRIER))
			break;
		else if (__rq->flags & REQ_STARTED)
			break;

		if (!(__rq->flags & REQ_CMD))
			continue;

		if ((ret = elv_try_merge(__rq, bio))) {
			*insert = &__rq->queuelist;
			q->last_merge = &__rq->queuelist;
			return ret;
		}
	}

	return ELEVATOR_NO_MERGE;
}

void elevator_noop_merge_requests(request_queue_t *q, struct request *req,
				  struct request *next)
{
	list_del_init(&next->queuelist);
}

void elevator_noop_add_request(request_queue_t *q, struct request *rq,
			       struct list_head *insert_here)
{
	list_add_tail(&rq->queuelist, &q->queue_head);

	/*
	 * new merges must not precede this barrier
	 */
	if (rq->flags & REQ_HARDBARRIER)
		q->last_merge = NULL;
	else if (!q->last_merge)
		q->last_merge = &rq->queuelist;
}

struct request *elevator_noop_next_request(request_queue_t *q)
{
	if (!list_empty(&q->queue_head))
		return list_entry_rq(q->queue_head.next);

	return NULL;
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

int elv_merge(request_queue_t *q, struct list_head **entry, struct bio *bio)
{
	elevator_t *e = &q->elevator;

	if (e->elevator_merge_fn)
		return e->elevator_merge_fn(q, entry, bio);

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

	if (e->elevator_merge_req_fn)
		e->elevator_merge_req_fn(q, rq, next);
}

void __elv_add_request(request_queue_t *q, struct request *rq, int at_end,
		       int plug)
{
	struct list_head *insert = &q->queue_head;

	if (at_end)
		insert = insert->prev;
	if (plug)
		blk_plug_device(q);

	q->elevator.elevator_add_req_fn(q, rq, insert);
}

void elv_add_request(request_queue_t *q, struct request *rq, int at_end,
		     int plug)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__elv_add_request(q, rq, at_end, plug);
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

		if (&rq->queuelist == q->last_merge)
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
			blkdev_dequeue_request(rq);
			rq->flags |= REQ_QUIET;
			while (end_that_request_first(rq, 0, rq->nr_sectors))
				;
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
	 * the main clearing point for q->last_merge is on retrieval of
	 * request by driver (it calls elv_next_request()), but it _can_
	 * also happen here if a request is added to the queue but later
	 * deleted without ever being given to driver (merged with another
	 * request).
	 */
	if (&rq->queuelist == q->last_merge)
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

int elv_register_queue(struct gendisk *disk)
{
	request_queue_t *q = disk->queue;
	elevator_t *e;

	if (!q)
		return -ENXIO;

	e = &q->elevator;

	e->kobj.parent = kobject_get(&disk->kobj);
	if (!e->kobj.parent)
		return -EBUSY;

	snprintf(e->kobj.name, KOBJ_NAME_LEN, "%s", "iosched");
	e->kobj.ktype = e->elevator_ktype;

	return kobject_register(&e->kobj);
}

void elv_unregister_queue(struct gendisk *disk)
{
	request_queue_t *q = disk->queue;

	if (q) {
		elevator_t * e = &q->elevator;
		kobject_unregister(&e->kobj);
		kobject_put(&disk->kobj);
	}
}

elevator_t elevator_noop = {
	.elevator_merge_fn		= elevator_noop_merge,
	.elevator_merge_req_fn		= elevator_noop_merge_requests,
	.elevator_next_req_fn		= elevator_noop_next_request,
	.elevator_add_req_fn		= elevator_noop_add_request,
};

module_init(elevator_global_init);

EXPORT_SYMBOL(elevator_noop);

EXPORT_SYMBOL(elv_add_request);
EXPORT_SYMBOL(__elv_add_request);
EXPORT_SYMBOL(elv_next_request);
EXPORT_SYMBOL(elv_remove_request);
EXPORT_SYMBOL(elv_queue_empty);
EXPORT_SYMBOL(elevator_exit);
EXPORT_SYMBOL(elevator_init);
