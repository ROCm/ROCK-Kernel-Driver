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
extern void end_that_request_last(struct request *);

static inline void blkdev_dequeue_request(struct request *req)
{
	list_del(&req->queuelist);

	if (req->q)
		elv_remove_request(req->q, req);
}

extern inline struct request *elv_next_request(request_queue_t *q)
{
	struct request *rq;

	while ((rq = __elv_next_request(q))) {
		rq->flags |= REQ_STARTED;

		if (&rq->queuelist == q->last_merge)
			q->last_merge = NULL;

		if ((rq->flags & REQ_DONTPREP) || !q->prep_rq_fn)
			break;

		/*
		 * all ok, break and return it
		 */
		if (!q->prep_rq_fn(q, rq))
			break;

		/*
		 * prep said no-go, kill it
		 */
		blkdev_dequeue_request(rq);
		if (end_that_request_first(rq, 0, rq->nr_sectors))
			BUG();

		end_that_request_last(rq);
	}

	return rq;
}

#define _elv_add_request_core(q, rq, where, plug)			\
	do {								\
		if ((plug))						\
			blk_plug_device((q));				\
		(q)->elevator.elevator_add_req_fn((q), (rq), (where));	\
	} while (0)

#define _elv_add_request(q, rq, back, p) do {				      \
	if ((back))							      \
		_elv_add_request_core((q), (rq), (q)->queue_head.prev, (p));  \
	else								      \
		_elv_add_request_core((q), (rq), &(q)->queue_head, (p));      \
} while (0)

#define elv_add_request(q, rq, back) _elv_add_request((q), (rq), (back), 1)

#if defined(MAJOR_NR) || defined(IDE_DRIVER)
#if (MAJOR_NR != SCSI_TAPE_MAJOR) && (MAJOR_NR != OSST_MAJOR)
#if !defined(IDE_DRIVER)

#ifndef QUEUE
# define QUEUE (&blk_dev[MAJOR_NR].request_queue)
#endif
#ifndef CURRENT
# define CURRENT elv_next_request(QUEUE)
#endif

#endif /* !defined(IDE_DRIVER) */

/*
 * If we have our own end_request, we do not want to include this mess
 */
#ifndef LOCAL_END_REQUEST
static inline void end_request(int uptodate)
{
	struct request *req = CURRENT;

	if (end_that_request_first(req, uptodate, CURRENT->hard_cur_sectors))
		return;

	add_blkdev_randomness(major(req->rq_dev));
	blkdev_dequeue_request(req);
	end_that_request_last(req);
}
#endif /* !LOCAL_END_REQUEST */
#endif /* (MAJOR_NR != SCSI_TAPE_MAJOR) */
#endif /* defined(MAJOR_NR) || defined(IDE_DRIVER) */

#endif /* _BLK_H */
