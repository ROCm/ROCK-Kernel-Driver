#ifndef _BLK_H
#define _BLK_H

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>

extern void set_device_ro(kdev_t dev,int flag);
extern void add_blkdev_randomness(int major);

#ifdef CONFIG_BLK_DEV_RAM

extern int rd_doload;          /* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;          /* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;     /* starting block # of image */

#ifdef CONFIG_BLK_DEV_INITRD

#define INITRD_MINOR 250 /* shouldn't collide with /dev/ram* too soon ... */

extern unsigned long initrd_start,initrd_end;
extern int initrd_below_start_ok; /* 1 if it is not an error if initrd_start < memory_start */
void initrd_init(void);

#endif /* CONFIG_BLK_DEV_INITRD */

#endif

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
struct request *elv_next_request(request_queue_t *q);

static inline void blkdev_dequeue_request(struct request *req)
{
	BUG_ON(list_empty(&req->queuelist));

	list_del_init(&req->queuelist);

	if (req->q)
		elv_remove_request(req->q, req);
}

/*
 * If we have our own end_request, we do not want to include this mess
 */
#ifndef LOCAL_END_REQUEST
static inline void end_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, req->hard_cur_sectors))
		return;

	add_blkdev_randomness(major(req->rq_dev));
	blkdev_dequeue_request(req);
	end_that_request_last(req);
}
#endif /* !LOCAL_END_REQUEST */

#endif /* _BLK_H */
