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
 * - added merge by hash-lookup
 * - loose bi_dev comparisons, partition handling is right now
 * - completely modularize elevator setup and teardown
 *
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
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
	if (next == head)
		return 0;

	next_rq = list_entry(next, struct request, queuelist);

	BUG_ON(!next_rq->inactive);

	/*
	 * if the device is different (not a normal case) just check if
	 * bio is after rq
	 */
	if (next_rq->rq_dev != rq->rq_dev)
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
inline int elv_rq_merge_ok(request_queue_t *q, struct request *rq,
			   struct bio *bio)
{
	if (bio_data_dir(bio) == rq->cmd) {
		if (rq->rq_dev == bio->bi_dev && !rq->waiting
		    && !rq->special && rq->inactive && rq->q == q)
			return 1;
	}

	return 0;
}

/*
 * find a struct request that has a bio linked that we can merge with
 */
inline struct request *bio_get_hash_rq(kdev_t dev, sector_t sector, int vc)
{
	struct bio *bio = bio_hash_find(dev, sector, vc);
	struct request *rq = NULL;

	/*
	 * bio is pinned until we bio_put it
	 */
	if (bio) {
		rq = bio->bi_hash_desc;

		BUG_ON(!rq);

		bio_put(bio);
	}

	return rq;
}

int elevator_linus_merge(request_queue_t *q, struct request **req,
			 struct list_head *head, struct bio *bio)
{
	unsigned int count = bio_sectors(bio);
	struct elv_linus_data *edat = q->elevator.elevator_data;
	unsigned int vc = q->hash_valid_counter;
	struct list_head *entry;
	struct request *__rq;

	/*
	 * first try a back merge, then front, then give up and scan. this
	 * will of course fail for different size bios on the same queue,
	 * however that isn't really an issue
	 */
	if (likely(edat->flags & ELV_LINUS_BACK_MERGE)) {
		__rq = bio_get_hash_rq(bio->bi_dev, bio->bi_sector - count, vc);
		if (__rq) {
			if (!elv_rq_merge_ok(q, __rq, bio))
				goto front;

			/*
			 * looks ok to merge
			 */
			if (__rq->sector + __rq->nr_sectors == bio->bi_sector) {
				*req = __rq;
				return ELEVATOR_BACK_MERGE;
			}
		}
	}

front:
	if (likely(edat->flags & ELV_LINUS_FRONT_MERGE)) {
		__rq = bio_get_hash_rq(bio->bi_dev, bio->bi_sector + count, vc);
		if (__rq) {
			if (!elv_rq_merge_ok(q, __rq, bio))
				goto scan;

			/*
			 * looks ok to merge
			 */
			if (__rq->sector - count == bio->bi_sector) {
				*req = __rq;
				return ELEVATOR_FRONT_MERGE;
			}
		}
	}

	/*
	 * no merge possible, scan for insertion
	 */
scan:
	entry = &q->queue_head;
	while ((entry = entry->prev) != head) {
		__rq = list_entry_rq(entry);

		prefetch(list_entry_rq(entry->prev));

		if (unlikely(__rq->waiting || __rq->special))
			continue;
		if (unlikely(!__rq->inactive))
			break;
		if (!*req && bio_rq_in_between(bio, __rq, &q->queue_head))
			*req = __rq;

		/*
		 * simple "aging" of requests in queue
		 */
		if (__rq->elevator_sequence-- <= 0)
			break;
		else if (__rq->elevator_sequence < count)
			break;
	}

	return ELEVATOR_NO_MERGE;
}

void elevator_linus_merge_cleanup(request_queue_t *q, struct request *req, int count)
{
	struct list_head *entry;

	BUG_ON(req->q != q);

	/*
	 * second pass scan of requests that got passed over, if any
	 */
	entry = &req->queuelist;
	while ((entry = entry->next) != &q->queue_head) {
		struct request *tmp;
		prefetch(list_entry_rq(entry->next));
		tmp = list_entry_rq(entry);
		tmp->elevator_sequence -= count;
	}
}

void elevator_linus_merge_req(struct request *req, struct request *next)
{
	if (next->elevator_sequence < req->elevator_sequence)
		req->elevator_sequence = next->elevator_sequence;
}

void elv_add_request_fn(request_queue_t *q, struct request *rq,
			       struct list_head *insert_here)
{
	/*
	 * insert into queue pending list, merge hash, and possible latency
	 * list
	 */
	list_add(&rq->queuelist, insert_here);
}

struct request *elv_next_request_fn(request_queue_t *q)
{
	if (!blk_queue_empty(q))
		return list_entry(q->queue_head.next, struct request, queuelist);

	return NULL;
}

int elv_linus_init(request_queue_t *q, elevator_t *e)
{
	struct elv_linus_data *edata;

	edata = kmalloc(sizeof(struct elv_linus_data), GFP_ATOMIC);
	if (!edata)
		return -ENOMEM;

	/*
	 * default to doing both front and back merges
	 */
	edata->flags = ELV_LINUS_BACK_MERGE | ELV_LINUS_FRONT_MERGE;
	e->elevator_data = edata;
	return 0;
}

void elv_linus_exit(request_queue_t *q, elevator_t *e)
{
	kfree(e->elevator_data);
}

/*
 * See if we can find a request that this buffer can be coalesced with.
 */
int elevator_noop_merge(request_queue_t *q, struct request **req,
			struct list_head * head, struct bio *bio)
{
	struct request *__rq;
	int count, ret;
	unsigned int vc;

	count = bio_sectors(bio);
	ret = ELEVATOR_NO_MERGE;
	vc = q->hash_valid_counter;

	__rq = bio_get_hash_rq(bio->bi_dev, bio->bi_sector - count, vc);
	if (__rq) {
		if (!elv_rq_merge_ok(q, __rq, bio))
			goto front;

		if (__rq->sector + __rq->nr_sectors == bio->bi_sector) {
			ret = ELEVATOR_BACK_MERGE;
			*req = __rq;
			goto out;
		}
	}

front:
	__rq = bio_get_hash_rq(bio->bi_dev, bio->bi_sector + count, vc);
	if (__rq) {
		if (!elv_rq_merge_ok(q, __rq, bio))
			goto out;

		if (__rq->sector - count == bio->bi_sector) {
			ret = ELEVATOR_FRONT_MERGE;
			*req = __rq;
			goto out;
		}
	}

out:
	return ret;
}

void elevator_noop_merge_cleanup(request_queue_t *q, struct request *req, int count) {}

void elevator_noop_merge_req(struct request *req, struct request *next) {}

int elevator_init(request_queue_t *q, elevator_t *e, elevator_t type,char *name)
{
	*e = type;

	INIT_LIST_HEAD(&q->queue_head);
	strncpy(e->queue_name, name, 15);

	if (e->elevator_init_fn)
		return e->elevator_init_fn(q, e);

	return 0;
}

void elevator_exit(request_queue_t *q, elevator_t *e)
{
	if (e->elevator_exit_fn)
		e->elevator_exit_fn(q, e);
}

int elevator_global_init(void)
{
	return 0;
}

module_init(elevator_global_init);
