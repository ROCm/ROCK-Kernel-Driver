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
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/blk.h>
#include <linux/module.h>
#include <asm/uaccess.h>

int elevator_linus_merge(request_queue_t *q, struct request **req,
			 struct list_head * head,
			 struct buffer_head *bh, int rw,
			 int max_sectors, int max_segments)
{
	struct list_head *entry = &q->queue_head;
	unsigned int count = bh->b_size >> 9, ret = ELEVATOR_NO_MERGE;

	while ((entry = entry->prev) != head) {
		struct request *__rq = blkdev_entry_to_request(entry);

		/*
		 * simply "aging" of requests in queue
		 */
		if (__rq->elevator_sequence-- <= 0) {
			*req = __rq;
			break;
		}

		if (__rq->sem)
			continue;
		if (__rq->cmd != rw)
			continue;
		if (__rq->rq_dev != bh->b_rdev)
			continue;
		if (__rq->nr_sectors + count > max_sectors)
			continue;
		if (__rq->elevator_sequence < count)
			break;
		if (__rq->sector + __rq->nr_sectors == bh->b_rsector) {
			ret = ELEVATOR_BACK_MERGE;
			*req = __rq;
			break;
		} else if (__rq->sector - count == bh->b_rsector) {
			ret = ELEVATOR_FRONT_MERGE;
			__rq->elevator_sequence -= count;
			*req = __rq;
			break;
		} else if (!*req && BHRQ_IN_ORDER(bh, __rq))
			*req = __rq;
	}

	return ret;
}

void elevator_linus_merge_cleanup(request_queue_t *q, struct request *req, int count)
{
	struct list_head *entry = &req->queue, *head = &q->queue_head;

	/*
	 * second pass scan of requests that got passed over, if any
	 */
	while ((entry = entry->next) != head) {
		struct request *tmp = blkdev_entry_to_request(entry);
		tmp->elevator_sequence -= count;
	}
}

void elevator_linus_merge_req(struct request *req, struct request *next)
{
	if (next->elevator_sequence < req->elevator_sequence)
		req->elevator_sequence = next->elevator_sequence;
}

/*
 * See if we can find a request that this buffer can be coalesced with.
 */
int elevator_noop_merge(request_queue_t *q, struct request **req,
			struct list_head * head,
			struct buffer_head *bh, int rw,
			int max_sectors, int max_segments)
{
	struct list_head *entry;
	unsigned int count = bh->b_size >> 9;

	if (list_empty(&q->queue_head))
		return ELEVATOR_NO_MERGE;

	entry = &q->queue_head;
	while ((entry = entry->prev) != head) {
		struct request *__rq = blkdev_entry_to_request(entry);

		if (__rq->cmd != rw)
			continue;
		if (__rq->rq_dev != bh->b_rdev)
			continue;
		if (__rq->nr_sectors + count > max_sectors)
			continue;
		if (__rq->sem)
			continue;
		if (__rq->sector + __rq->nr_sectors == bh->b_rsector) {
			*req = __rq;
			return ELEVATOR_BACK_MERGE;
		} else if (__rq->sector - count == bh->b_rsector) {
			*req = __rq;
			return ELEVATOR_FRONT_MERGE;
		}
	}

	*req = blkdev_entry_to_request(q->queue_head.prev);
	return ELEVATOR_NO_MERGE;
}

void elevator_noop_merge_cleanup(request_queue_t *q, struct request *req, int count) {}

void elevator_noop_merge_req(struct request *req, struct request *next) {}

int blkelvget_ioctl(elevator_t * elevator, blkelv_ioctl_arg_t * arg)
{
	blkelv_ioctl_arg_t output;

	output.queue_ID			= elevator->queue_ID;
	output.read_latency		= elevator->read_latency;
	output.write_latency		= elevator->write_latency;
	output.max_bomb_segments	= 0;

	if (copy_to_user(arg, &output, sizeof(blkelv_ioctl_arg_t)))
		return -EFAULT;

	return 0;
}

int blkelvset_ioctl(elevator_t * elevator, const blkelv_ioctl_arg_t * arg)
{
	blkelv_ioctl_arg_t input;

	if (copy_from_user(&input, arg, sizeof(blkelv_ioctl_arg_t)))
		return -EFAULT;

	if (input.read_latency < 0)
		return -EINVAL;
	if (input.write_latency < 0)
		return -EINVAL;

	elevator->read_latency		= input.read_latency;
	elevator->write_latency		= input.write_latency;
	return 0;
}

void elevator_init(elevator_t * elevator, elevator_t type)
{
	static unsigned int queue_ID;

	*elevator = type;
	elevator->queue_ID = queue_ID++;
}
