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
#include <asm/uaccess.h>

/*
 * Order ascending, but only allow a request to be skipped a certain
 * number of times
 */
void elevator_linus(struct request *req, elevator_t *elevator,
		    struct list_head *real_head,
		    struct list_head *head, int orig_latency)
{
	struct list_head *entry = real_head;
	struct request *tmp;

	req->elevator_sequence = orig_latency;

	while ((entry = entry->prev) != head) {
		tmp = blkdev_entry_to_request(entry);
		if (IN_ORDER(tmp, req))
			break;
		if (!tmp->elevator_sequence)
			break;
		tmp->elevator_sequence--;
	}
	list_add(&req->queue, entry);
}

int elevator_linus_merge(request_queue_t *q, struct request **req,
			 struct buffer_head *bh, int rw,
			 int *max_sectors, int *max_segments)
{
	struct list_head *entry, *head = &q->queue_head;
	unsigned int count = bh->b_size >> 9, ret = ELEVATOR_NO_MERGE;

	entry = head;
	if (q->head_active && !q->plugged)
		head = head->next;

	while ((entry = entry->prev) != head) {
		struct request *__rq = *req = blkdev_entry_to_request(entry);
		if (__rq->sem)
			continue;
		if (__rq->cmd != rw)
			continue;
		if (__rq->nr_sectors + count > *max_sectors)
			continue;
		if (__rq->rq_dev != bh->b_rdev)
			continue;
		if (__rq->sector + __rq->nr_sectors == bh->b_rsector) {
			ret = ELEVATOR_BACK_MERGE;
			break;
		}
		if (!__rq->elevator_sequence)
			break;
		if (__rq->sector - count == bh->b_rsector) {
			__rq->elevator_sequence--;
			ret = ELEVATOR_FRONT_MERGE;
			break;
		}
	}

	/*
	 * second pass scan of requests that got passed over, if any
	 */
	if (ret != ELEVATOR_NO_MERGE && *req) {
		while ((entry = entry->next) != &q->queue_head) {
			struct request *tmp = blkdev_entry_to_request(entry);
			tmp->elevator_sequence--;
		}
	}

	return ret;
}

/*
 * No request sorting, just add it to the back of the list
 */
void elevator_noop(struct request *req, elevator_t *elevator,
		   struct list_head *real_head, struct list_head *head,
		   int orig_latency)
{
	list_add_tail(&req->queue, real_head);
}

/*
 * See if we can find a request that is buffer can be coalesced with.
 */
int elevator_noop_merge(request_queue_t *q, struct request **req,
			struct buffer_head *bh, int rw,
			int *max_sectors, int *max_segments)
{
	struct list_head *entry, *head = &q->queue_head;
	unsigned int count = bh->b_size >> 9;

	if (q->head_active && !q->plugged)
		head = head->next;

	entry = head;
	while ((entry = entry->prev) != head) {
		struct request *__rq = *req = blkdev_entry_to_request(entry);
		if (__rq->sem)
			continue;
		if (__rq->cmd != rw)
			continue;
		if (__rq->nr_sectors + count > *max_sectors)
			continue;
		if (__rq->rq_dev != bh->b_rdev)
			continue;
		if (__rq->sector + __rq->nr_sectors == bh->b_rsector)
			return ELEVATOR_BACK_MERGE;
		if (__rq->sector - count == bh->b_rsector)
			return ELEVATOR_FRONT_MERGE;
	}
	return ELEVATOR_NO_MERGE;
}

/*
 * The noop "elevator" does not do any accounting
 */
void elevator_noop_dequeue(struct request *req) {}

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
