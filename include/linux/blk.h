#ifndef _BLK_H
#define _BLK_H

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>

extern void set_device_ro(struct block_device *bdev, int flag);
extern void set_disk_ro(struct gendisk *disk, int flag);
extern void add_disk_randomness(struct gendisk *disk);
extern void rand_initialize_disk(struct gendisk *disk);

/*
 * end_request() and friends. Must be called with the request queue spinlock
 * acquired. All functions called within end_request() _must_be_ atomic.
 *
 * Several drivers define their own end_request and call
 * end_that_request_first() and end_that_request_last()
 * for parts of the original function. This prevents
 * code duplication in drivers.
 */

extern int end_that_request_first(struct request *, int, int);
extern int end_that_request_chunk(struct request *, int, int);
extern void end_that_request_last(struct request *);
extern void end_request(struct request *req, int uptodate);
struct request *elv_next_request(request_queue_t *q);

static inline void blkdev_dequeue_request(struct request *req)
{
	BUG_ON(list_empty(&req->queuelist));

	list_del_init(&req->queuelist);

	if (req->q)
		elv_remove_request(req->q, req);
}

#endif /* _BLK_H */
