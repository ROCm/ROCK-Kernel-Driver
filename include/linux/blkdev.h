#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#include <linux/major.h>
#include <linux/sched.h>
#include <linux/genhd.h>
#include <linux/tqueue.h>
#include <linux/list.h>
#include <linux/mm.h>

#include <asm/scatterlist.h>

struct request_queue;
typedef struct request_queue request_queue_t;
struct elevator_s;
typedef struct elevator_s elevator_t;

struct request {
	struct list_head queuelist; /* looking for ->queue? you must _not_
				     * access it directly, use
				     * blkdev_dequeue_request! */
	int elevator_sequence;

	int inactive; /* driver hasn't seen it yet */

	int rq_status;	/* should split this into a few status bits */
	kdev_t rq_dev;
	int cmd;		/* READ or WRITE */
	int errors;
	sector_t sector;
	unsigned long nr_sectors;
	unsigned long hard_sector, hard_nr_sectors;
	unsigned short nr_segments;
	unsigned short nr_hw_segments;
	unsigned int current_nr_sectors;
	unsigned int hard_cur_sectors;
	void *special;
	char *buffer;
	struct completion *waiting;
	struct bio *bio, *biotail;
	request_queue_t *q;
};

#include <linux/elevator.h>

typedef int (merge_request_fn) (request_queue_t *, struct request *,
				struct bio *);
typedef int (merge_requests_fn) (request_queue_t *, struct request *,
				 struct request *);
typedef void (request_fn_proc) (request_queue_t *q);
typedef request_queue_t * (queue_proc) (kdev_t dev);
typedef int (make_request_fn) (request_queue_t *q, struct bio *bio);
typedef void (unplug_device_fn) (void *q);

enum blk_queue_state {
	Queue_down,
	Queue_up,
};

/*
 * Default nr free requests per queue, ll_rw_blk will scale it down
 * according to available RAM at init time
 */
#define QUEUE_NR_REQUESTS	8192

struct request_list {
	unsigned int count;
	struct list_head free;
	wait_queue_head_t wait;
};

struct request_queue
{
	/*
	 * the queue request freelist, one for reads and one for writes
	 */
	struct request_list	rq[2];

	/*
	 * Together with queue_head for cacheline sharing
	 */
	struct list_head	queue_head;
	elevator_t		elevator;

	request_fn_proc		* request_fn;
	merge_request_fn	* back_merge_fn;
	merge_request_fn	* front_merge_fn;
	merge_requests_fn	* merge_requests_fn;
	make_request_fn		* make_request_fn;

	/*
	 * The queue owner gets to use this for whatever they like.
	 * ll_rw_blk doesn't touch it.
	 */
	void			* queuedata;

	/*
	 * queue needs bounce pages for pages above this limit
	 */
	unsigned long		bounce_pfn;

	/*
	 * This is used to remove the plug when tq_disk runs.
	 */
	struct tq_struct	plug_tq;

	/*
	 * various queue flags, see QUEUE_* below
	 */
	unsigned long		queue_flags;

	/*
	 * protects queue structures from reentrancy
	 */
	spinlock_t		queue_lock;

	/*
	 * queue settings
	 */
	unsigned short		max_sectors;
	unsigned short		max_segments;
	unsigned short		hardsect_size;
	unsigned int		max_segment_size;

	unsigned long		seg_boundary_mask;

	wait_queue_head_t	queue_wait;
};

#define RQ_INACTIVE		(-1)
#define RQ_ACTIVE		1
#define RQ_SCSI_BUSY		0xffff
#define RQ_SCSI_DONE		0xfffe
#define RQ_SCSI_DISCONNECTING	0xffe0

#define QUEUE_FLAG_PLUGGED	0	/* queue is plugged */
#define QUEUE_FLAG_NOSPLIT	1	/* can process bio over several goes */
#define QUEUE_FLAG_CLUSTER	2	/* cluster several segments into 1 */

#define blk_queue_plugged(q)	test_bit(QUEUE_FLAG_PLUGGED, &(q)->queue_flags)

#define blk_mark_plugged(q)	set_bit(QUEUE_FLAG_PLUGGED, &(q)->queue_flags)

#define blk_queue_empty(q)	elv_queue_empty(q)

#define list_entry_rq(ptr)	list_entry((ptr), struct request, queuelist)

/*
 * noop, requests are automagically marked as active/inactive by I/O
 * scheduler -- see elv_next_request
 */
#define blk_queue_headactive(q, head_active)

extern unsigned long blk_max_low_pfn, blk_max_pfn;

#define __elv_next_request(q)	(q)->elevator.elevator_next_req_fn((q))

extern inline struct request *elv_next_request(request_queue_t *q)
{
	struct request *rq = __elv_next_request(q);

	if (rq) {
		rq->inactive = 0;
		wmb();
	}

	return rq;
}

#define BLK_BOUNCE_HIGH	(blk_max_low_pfn << PAGE_SHIFT)
#define BLK_BOUNCE_ANY	(blk_max_pfn << PAGE_SHIFT)

#ifdef CONFIG_HIGHMEM

extern void create_bounce(unsigned long pfn, struct bio **bio_orig);

extern inline void blk_queue_bounce(request_queue_t *q, struct bio **bio)
{
	create_bounce(q->bounce_pfn, bio);
}

#else /* CONFIG_HIGHMEM */

#define blk_queue_bounce(q, bio)	do { } while (0)

#endif /* CONFIG_HIGHMEM */

#define rq_for_each_bio(bio, rq)	\
	for (bio = (rq)->bio; bio; bio = bio->bi_next)

struct blk_dev_struct {
	/*
	 * queue_proc has to be atomic
	 */
	request_queue_t		request_queue;
	queue_proc		*queue;
	void			*data;
};

struct sec_size {
	unsigned block_size;
	unsigned block_size_bits;
};

/*
 * Used to indicate the default queue for drivers that don't bother
 * to implement multiple queues.  We have this access macro here
 * so as to eliminate the need for each and every block device
 * driver to know about the internal structure of blk_dev[].
 */
#define BLK_DEFAULT_QUEUE(_MAJOR)  &blk_dev[_MAJOR].request_queue

extern struct sec_size * blk_sec[MAX_BLKDEV];
extern struct blk_dev_struct blk_dev[MAX_BLKDEV];
extern void grok_partitions(kdev_t dev, long size);
extern int wipe_partitions(kdev_t dev);
extern void register_disk(struct gendisk *dev, kdev_t first, unsigned minors, struct block_device_operations *ops, long size);
extern void generic_make_request(struct bio *bio);
extern inline request_queue_t *blk_get_queue(kdev_t dev);
extern void blkdev_release_request(struct request *);
extern void blk_attempt_remerge(request_queue_t *, struct request *);

/*
 * Access functions for manipulating queue properties
 */
extern int blk_init_queue(request_queue_t *, request_fn_proc *);
extern void blk_cleanup_queue(request_queue_t *);
extern void blk_queue_make_request(request_queue_t *, make_request_fn *);
extern void blk_queue_bounce_limit(request_queue_t *, u64);
extern void blk_queue_max_sectors(request_queue_t *q, unsigned short);
extern void blk_queue_max_segments(request_queue_t *q, unsigned short);
extern void blk_queue_max_segment_size(request_queue_t *q, unsigned int);
extern void blk_queue_hardsect_size(request_queue_t *q, unsigned short);
extern void blk_queue_segment_boundary(request_queue_t *q, unsigned long);
extern int blk_rq_map_sg(request_queue_t *, struct request *, struct scatterlist *);
extern void generic_unplug_device(void *);

extern int * blk_size[MAX_BLKDEV];

extern int * blksize_size[MAX_BLKDEV];

extern int * max_readahead[MAX_BLKDEV];

#define MAX_SEGMENTS 128
#define MAX_SECTORS 255

#define MAX_SEGMENT_SIZE	65536

/* read-ahead in pages.. */
#define MAX_READAHEAD	31
#define MIN_READAHEAD	3

#define blkdev_entry_to_request(entry) list_entry((entry), struct request, queuelist)
#define blkdev_entry_next_request(entry) blkdev_entry_to_request((entry)->next)
#define blkdev_entry_prev_request(entry) blkdev_entry_to_request((entry)->prev)
#define blkdev_next_request(req) blkdev_entry_to_request((req)->queuelist.next)
#define blkdev_prev_request(req) blkdev_entry_to_request((req)->queuelist.prev)

extern void drive_stat_acct (kdev_t dev, int rw,
					unsigned long nr_sectors, int new_io);

extern inline void blk_clear(int major)
{
	blk_size[major] = NULL;
#if 0
	blk_size_in_bytes[major] = NULL;
#endif
	blksize_size[major] = NULL;
	max_readahead[major] = NULL;
	read_ahead[major] = 0;
}

extern inline int get_hardsect_size(kdev_t dev)
{
	request_queue_t *q = blk_get_queue(dev);
	int retval = 512;

	if (q && q->hardsect_size)
		retval = q->hardsect_size;

	return retval;
}

#define blk_finished_io(nsects)	do { } while (0)
#define blk_started_io(nsects)	do { } while (0)

extern inline unsigned int blksize_bits(unsigned int size)
{
	unsigned int bits = 8;
	do {
		bits++;
		size >>= 1;
	} while (size > 256);
	return bits;
}

extern inline unsigned int block_size(kdev_t dev)
{
	int retval = BLOCK_SIZE;
	int major = MAJOR(dev);

	if (blksize_size[major]) {
		int minor = MINOR(dev);
		if (blksize_size[major][minor])
			retval = blksize_size[major][minor];
	}
	return retval;
}

#endif
