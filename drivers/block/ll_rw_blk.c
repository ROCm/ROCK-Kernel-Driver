/*
 *  linux/drivers/block/ll_rw_blk.c
 *
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 * Elevator latency, (C) 2000  Andrea Arcangeli <andrea@suse.de> SuSE
 * Queue request tables / lock, selectable elevator, Jens Axboe <axboe@suse.de>
 * kernel-doc documentation started by NeilBrown <neilb@cse.unsw.edu.au> -  July2000
 * bio rewrite, highmem i/o, etc, Jens Axboe <axboe@suse.de> - may 2001
 */

/*
 * This handles all read/write requests to block devices
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/bootmem.h>
#include <linux/completion.h>
#include <linux/compiler.h>

#include <asm/system.h>
#include <asm/io.h>
#include <linux/blk.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/module.h>

/*
 * MAC Floppy IWM hooks
 */

#ifdef CONFIG_MAC_FLOPPY_IWM
extern int mac_floppy_init(void);
#endif

/*
 * For the allocated request tables
 */
static kmem_cache_t *request_cachep;

/*
 * The "disk" task queue is used to start the actual requests
 * after a plug
 */
DECLARE_TASK_QUEUE(tq_disk);

/* This specifies how many sectors to read ahead on the disk. */

int read_ahead[MAX_BLKDEV];

/* blk_dev_struct is:
 *	request_queue
 *	*queue
 */
struct blk_dev_struct blk_dev[MAX_BLKDEV]; /* initialized by blk_dev_init() */

/*
 * blk_size contains the size of all block-devices in units of 1024 byte
 * sectors:
 *
 * blk_size[MAJOR][MINOR]
 *
 * if (!blk_size[MAJOR]) then no minor size checking is done.
 */
int * blk_size[MAX_BLKDEV];

/*
 * blksize_size contains the size of all block-devices:
 *
 * blksize_size[MAJOR][MINOR]
 *
 * if (!blksize_size[MAJOR]) then 1024 bytes is assumed.
 */
int * blksize_size[MAX_BLKDEV];

/*
 * The following tunes the read-ahead algorithm in mm/filemap.c
 */
int * max_readahead[MAX_BLKDEV];

/*
 * How many reqeusts do we allocate per queue,
 * and how many do we "batch" on freeing them?
 */
int queue_nr_requests, batch_requests;
unsigned long blk_max_low_pfn, blk_max_pfn;
int blk_nohighio = 0;

/**
 * blk_get_queue: - return the queue that matches the given device
 * @dev:    device
 *
 * Description:
 *     Given a specific device, return the queue that will hold I/O
 *     for it. This is either a &struct blk_dev_struct lookup and a
 *     call to the ->queue() function defined, or the default queue
 *     stored in the same location.
 *
 **/
inline request_queue_t *blk_get_queue(kdev_t dev)
{
	struct blk_dev_struct *bdev = blk_dev + MAJOR(dev);

	if (bdev->queue)
		return bdev->queue(dev);
	else
		return &blk_dev[MAJOR(dev)].request_queue;
}

/**
 * blk_queue_make_request - define an alternate make_request function for a device
 * @q:  the request queue for the device to be affected
 * @mfn: the alternate make_request function
 *
 * Description:
 *    The normal way for &struct bios to be passed to a device
 *    driver is for them to be collected into requests on a request
 *    queue, and then to allow the device driver to select requests
 *    off that queue when it is ready.  This works well for many block
 *    devices. However some block devices (typically virtual devices
 *    such as md or lvm) do not benefit from the processing on the
 *    request queue, and are served best by having the requests passed
 *    directly to them.  This can be achieved by providing a function
 *    to blk_queue_make_request().
 *
 * Caveat:
 *    The driver that does this *must* be able to deal appropriately
 *    with buffers in "highmemory". This can be accomplished by either calling
 *    bio_kmap() to get a temporary kernel mapping, or by calling
 *    blk_queue_bounce() to create a buffer in normal memory.
 **/
void blk_queue_make_request(request_queue_t * q, make_request_fn * mfn)
{
	/*
	 * set defaults
	 */
	q->max_segments = MAX_SEGMENTS;
	q->make_request_fn = mfn;
	blk_queue_max_sectors(q, MAX_SECTORS);
	blk_queue_hardsect_size(q, 512);

	init_waitqueue_head(&q->queue_wait);
}

/**
 * blk_queue_bounce_limit - set bounce buffer limit for queue
 * @q:  the request queue for the device
 * @dma_addr:   bus address limit
 *
 * Description:
 *    Different hardware can have different requirements as to what pages
 *    it can do I/O directly to. A low level driver can call
 *    blk_queue_bounce_limit to have lower memory pages allocated as bounce
 *    buffers for doing I/O to pages residing above @page. By default
 *    the block layer sets this to the highest numbered "low" memory page.
 **/
void blk_queue_bounce_limit(request_queue_t *q, u64 dma_addr)
{
	unsigned long bounce_pfn = dma_addr >> PAGE_SHIFT;
	unsigned long mb = dma_addr >> 20;
	static request_queue_t *last_q;

	/*
	 * keep this for debugging for now...
	 */
	if (dma_addr != BLK_BOUNCE_HIGH && q != last_q) {
		printk("blk: queue %p, ", q);
		if (dma_addr == BLK_BOUNCE_ANY)
			printk("no I/O memory limit\n");
		else
			printk("I/O limit %luMb (mask 0x%Lx)\n", mb, (u64) dma_addr);
	}

	q->bounce_pfn = bounce_pfn;
	last_q = q;
}


/**
 * blk_queue_max_sectors - set max sectors for a request for this queue
 * @q:  the request queue for the device
 * @max_sectors:  max sectors in the usual 512b unit
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the size of
 *    received requests.
 **/
void blk_queue_max_sectors(request_queue_t *q, unsigned short max_sectors)
{
	q->max_sectors = max_sectors;
}

/**
 * blk_queue_max_segments - set max segments for a request for this queue
 * @q:  the request queue for the device
 * @max_segments:  max number of segments
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the number of
 *    data segments in a request
 **/
void blk_queue_max_segments(request_queue_t *q, unsigned short max_segments)
{
	q->max_segments = max_segments;
}

/**
 * blk_queue_max_segment_size - set max segment size for blk_rq_map_sg
 * @q:  the request queue for the device
 * @max_size:  max size of segment in bytes
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the size of a
 *    coalesced segment
 **/
void blk_queue_max_segment_size(request_queue_t *q, unsigned int max_size)
{
	q->max_segment_size = max_size;
}

/**
 * blk_queue_hardsect_size - set hardware sector size for the queue
 * @q:  the request queue for the device
 * @size:  the hardware sector size, in bytes
 *
 * Description:
 *   This should typically be set to the lowest possible sector size
 *   that the hardware can operate on (possible without reverting to
 *   even internal read-modify-write operations). Usually the default
 *   of 512 covers most hardware.
 **/
void blk_queue_hardsect_size(request_queue_t *q, unsigned short size)
{
	q->hardsect_size = size;
}

/**
 * blk_queue_segment_boundary - set boundary rules for segment merging
 * @q:  the request queue for the device
 * @mask:  the memory boundary mask
 **/
void blk_queue_segment_boundary(request_queue_t *q, unsigned long mask)
{
	q->seg_boundary_mask = mask;
}

/*
 * can we merge the two segments, or do we need to start a new one?
 */
inline int blk_same_segment(request_queue_t *q, struct bio *bio,
			    struct bio *nxt)
{
	/*
	 * not contigous, just forget it
	 */
	if (!BIO_CONTIG(bio, nxt))
		return 0;

	/*
	 * bio and nxt are contigous in memory, check if the queue allows
	 * these two to be merged into one
	 */
	if (BIO_SEG_BOUNDARY(q, bio, nxt))
		return 1;

	return 0;
}

/*
 * map a request to scatterlist, return number of sg entries setup. Caller
 * must make sure sg can hold rq->nr_segments entries
 */
int blk_rq_map_sg(request_queue_t *q, struct request *rq, struct scatterlist *sg)
{
	unsigned long long lastend;
	struct bio_vec *bvec;
	struct bio *bio;
	int nsegs, i, cluster;

	nsegs = 0;
	bio = rq->bio;
	lastend = ~0ULL;
	cluster = q->queue_flags & (1 << QUEUE_FLAG_CLUSTER);

	/*
	 * for each bio in rq
	 */
	rq_for_each_bio(bio, rq) {
		/*
		 * for each segment in bio
		 */
		bio_for_each_segment(bvec, bio, i) {
			int nbytes = bvec->bv_len;

			BIO_BUG_ON(i > bio->bi_vcnt);

			if (!cluster)
				goto new_segment;

			if (bvec_to_phys(bvec) == lastend) {
				if (sg[nsegs - 1].length + nbytes > q->max_segment_size)
					goto new_segment;

				/*
				 * make sure to not map a segment across a
				 * boundary that the queue doesn't want
				 */
				if (!__BIO_SEG_BOUNDARY(lastend, lastend + nbytes, q->seg_boundary_mask))
					lastend = ~0ULL;
				else
					lastend += nbytes;

				sg[nsegs - 1].length += nbytes;
			} else {
new_segment:
				if (nsegs >= q->max_segments) {
					printk("map: %d >= %d\n", nsegs, q->max_segments);
					BUG();
				}

				sg[nsegs].address = NULL;
				sg[nsegs].page = bvec->bv_page;
				sg[nsegs].length = nbytes;
				sg[nsegs].offset = bvec->bv_offset;

				lastend = bvec_to_phys(bvec) + nbytes;
				nsegs++;
			}
		} /* segments in bio */
	} /* bios in rq */

	return nsegs;
}

/*
 * the standard queue merge functions, can be overridden with device
 * specific ones if so desired
 */
static inline int ll_new_segment(request_queue_t *q, struct request *req)
{
	if (req->nr_segments < q->max_segments) {
		req->nr_segments++;
		return 1;
	}
	return 0;
}

static int ll_back_merge_fn(request_queue_t *q, struct request *req, 
			    struct bio *bio)
{
	if (req->nr_sectors + bio_sectors(bio) > q->max_sectors)
		return 0;
	if (blk_same_segment(q, req->biotail, bio))
		return 1;

	return ll_new_segment(q, req);
}

static int ll_front_merge_fn(request_queue_t *q, struct request *req, 
			     struct bio *bio)
{
	if (req->nr_sectors + bio_sectors(bio) > q->max_sectors)
		return 0;
	if (blk_same_segment(q, bio, req->bio))
		return 1;

	return ll_new_segment(q, req);
}

static int ll_merge_requests_fn(request_queue_t *q, struct request *req,
				struct request *next)
{
	int total_segments = req->nr_segments + next->nr_segments;

	if (blk_same_segment(q, req->biotail, next->bio))
		total_segments--;
    
	if (total_segments > q->max_segments)
		return 0;

	req->nr_segments = total_segments;
	return 1;
}

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 *
 * This is called with interrupts off and no requests on the queue.
 * (and with the request spinlock acquired)
 */
static void blk_plug_device(request_queue_t *q)
{
	/*
	 * common case
	 */
	if (!elv_queue_empty(q))
		return;

	if (!test_and_set_bit(QUEUE_FLAG_PLUGGED, &q->queue_flags))
		queue_task(&q->plug_tq, &tq_disk);
}

/*
 * remove the plug and let it rip..
 */
static inline void __generic_unplug_device(request_queue_t *q)
{
	if (test_and_clear_bit(QUEUE_FLAG_PLUGGED, &q->queue_flags))
		if (!elv_queue_empty(q))
			q->request_fn(q);
}

/**
 * generic_unplug_device - fire a request queue
 * @q:    The &request_queue_t in question
 *
 * Description:
 *   Linux uses plugging to build bigger requests queues before letting
 *   the device have at them. If a queue is plugged, the I/O scheduler
 *   is still adding and merging requests on the queue. Once the queue
 *   gets unplugged (either by manually calling this function, or by
 *   running the tq_disk task queue), the request_fn defined for the
 *   queue is invoked and transfers started.
 **/
void generic_unplug_device(void *data)
{
	request_queue_t *q = (request_queue_t *) data;
	unsigned long flags;

	spin_lock_irqsave(&q->queue_lock, flags);
	__generic_unplug_device(q);
	spin_unlock_irqrestore(&q->queue_lock, flags);
}

static int __blk_cleanup_queue(struct request_list *list)
{
	struct list_head *head = &list->free;
	struct request *rq;
	int i = 0;

	while (!list_empty(head)) {
		rq = list_entry(head->next, struct request, queuelist);
		list_del(&rq->queuelist);
		kmem_cache_free(request_cachep, rq);
		i++;
	}

	if (i != list->count)
		printk("request list leak!\n");

	list->count = 0;
	return i;
}

/**
 * blk_cleanup_queue: - release a &request_queue_t when it is no longer needed
 * @q:    the request queue to be released
 *
 * Description:
 *     blk_cleanup_queue is the pair to blk_init_queue().  It should
 *     be called when a request queue is being released; typically
 *     when a block device is being de-registered.  Currently, its
 *     primary task it to free all the &struct request structures that
 *     were allocated to the queue.
 * Caveat: 
 *     Hopefully the low level driver will have finished any
 *     outstanding requests first...
 **/
void blk_cleanup_queue(request_queue_t * q)
{
	int count = queue_nr_requests;

	count -= __blk_cleanup_queue(&q->rq[READ]);
	count -= __blk_cleanup_queue(&q->rq[WRITE]);

	if (count)
		printk("blk_cleanup_queue: leaked requests (%d)\n", count);

	elevator_exit(q, &q->elevator);

	memset(q, 0, sizeof(*q));
}

static int blk_init_free_list(request_queue_t *q)
{
	struct request *rq;
	int i;

	INIT_LIST_HEAD(&q->rq[READ].free);
	INIT_LIST_HEAD(&q->rq[WRITE].free);
	q->rq[READ].count = 0;
	q->rq[WRITE].count = 0;

	/*
	 * Divide requests in half between read and write
	 */
	for (i = 0; i < queue_nr_requests; i++) {
		rq = kmem_cache_alloc(request_cachep, SLAB_KERNEL);
		if (!rq)
			goto nomem;

		memset(rq, 0, sizeof(struct request));
		rq->rq_status = RQ_INACTIVE;
		if (i < queue_nr_requests >> 1) {
			list_add(&rq->queuelist, &q->rq[READ].free);
			q->rq[READ].count++;
		} else {
			list_add(&rq->queuelist, &q->rq[WRITE].free);
			q->rq[WRITE].count++;
		}
	}

	init_waitqueue_head(&q->rq[READ].wait);
	init_waitqueue_head(&q->rq[WRITE].wait);
	spin_lock_init(&q->queue_lock);
	return 0;
nomem:
	blk_cleanup_queue(q);
	return 1;
}

static int __make_request(request_queue_t *, struct bio *);

/**
 * blk_init_queue  - prepare a request queue for use with a block device
 * @q:    The &request_queue_t to be initialised
 * @rfn:  The function to be called to process requests that have been
 *        placed on the queue.
 *
 * Description:
 *    If a block device wishes to use the standard request handling procedures,
 *    which sorts requests and coalesces adjacent requests, then it must
 *    call blk_init_queue().  The function @rfn will be called when there
 *    are requests on the queue that need to be processed.  If the device
 *    supports plugging, then @rfn may not be called immediately when requests
 *    are available on the queue, but may be called at some time later instead.
 *    Plugged queues are generally unplugged when a buffer belonging to one
 *    of the requests on the queue is needed, or due to memory pressure.
 *
 *    @rfn is not required, or even expected, to remove all requests off the
 *    queue, but only as many as it can handle at a time.  If it does leave
 *    requests on the queue, it is responsible for arranging that the requests
 *    get dealt with eventually.
 *
 *    The queue spin lock must be held while manipulating the requests on the
 *    request queue.
 *
 * Note:
 *    blk_init_queue() must be paired with a blk_cleanup_queue() call
 *    when the block device is deactivated (such as at module unload).
 **/
int blk_init_queue(request_queue_t *q, request_fn_proc *rfn)
{
	int ret;

	if (blk_init_free_list(q))
		return -ENOMEM;

	if ((ret = elevator_init(q, &q->elevator, ELEVATOR_LINUS))) {
		blk_cleanup_queue(q);
		return ret;
	}

	q->request_fn     	= rfn;
	q->back_merge_fn       	= ll_back_merge_fn;
	q->front_merge_fn      	= ll_front_merge_fn;
	q->merge_requests_fn	= ll_merge_requests_fn;
	q->plug_tq.sync		= 0;
	q->plug_tq.routine	= &generic_unplug_device;
	q->plug_tq.data		= q;
	q->queue_flags		= (1 << QUEUE_FLAG_CLUSTER);
	
	/*
	 * by default assume old behaviour and bounce for any highmem page
	 */
	blk_queue_bounce_limit(q, BLK_BOUNCE_HIGH);

	blk_queue_segment_boundary(q, 0xffffffff);

	blk_queue_make_request(q, __make_request);
	blk_queue_max_segment_size(q, MAX_SEGMENT_SIZE);
	return 0;
}

#define blkdev_free_rq(list) list_entry((list)->next, struct request, queuelist)
/*
 * Get a free request. queue lock must be held and interrupts
 * disabled on the way in.
 */
static inline struct request *get_request(request_queue_t *q, int rw)
{
	struct request *rq = NULL;
	struct request_list *rl = q->rq + rw;

	if (!list_empty(&rl->free)) {
		rq = blkdev_free_rq(&rl->free);
		list_del(&rq->queuelist);
		rl->count--;
		rq->inactive = 1;
		rq->rq_status = RQ_ACTIVE;
		rq->special = NULL;
		rq->q = q;
	}

	return rq;
}

/*
 * No available requests for this queue, unplug the device.
 */
static struct request *get_request_wait(request_queue_t *q, int rw)
{
	DECLARE_WAITQUEUE(wait, current);
	struct request *rq;

	spin_lock_prefetch(&q->queue_lock);

	generic_unplug_device(q);
	add_wait_queue(&q->rq[rw].wait, &wait);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (q->rq[rw].count < batch_requests)
			schedule();
		spin_lock_irq(&q->queue_lock);
		rq = get_request(q, rw);
		spin_unlock_irq(&q->queue_lock);
	} while (rq == NULL);
	remove_wait_queue(&q->rq[rw].wait, &wait);
	current->state = TASK_RUNNING;
	return rq;
}

/* RO fail safe mechanism */

static long ro_bits[MAX_BLKDEV][8];

int is_read_only(kdev_t dev)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return 0;
	return ro_bits[major][minor >> 5] & (1 << (minor & 31));
}

void set_device_ro(kdev_t dev,int flag)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return;
	if (flag) ro_bits[major][minor >> 5] |= 1 << (minor & 31);
	else ro_bits[major][minor >> 5] &= ~(1 << (minor & 31));
}

void drive_stat_acct (kdev_t dev, int rw, unsigned long nr_sectors, int new_io)
{
	unsigned int major = MAJOR(dev);
	unsigned int index;

	index = disk_index(dev);
	if ((index >= DK_MAX_DISK) || (major >= DK_MAX_MAJOR))
		return;

	kstat.dk_drive[major][index] += new_io;
	if (rw == READ) {
		kstat.dk_drive_rio[major][index] += new_io;
		kstat.dk_drive_rblk[major][index] += nr_sectors;
	} else if (rw == WRITE) {
		kstat.dk_drive_wio[major][index] += new_io;
		kstat.dk_drive_wblk[major][index] += nr_sectors;
	} else
		printk(KERN_ERR "drive_stat_acct: cmd not R/W?\n");
}

/*
 * add-request adds a request to the linked list.
 * queue lock is held and interrupts disabled, as we muck with the
 * request queue list.
 */
static inline void add_request(request_queue_t * q, struct request * req,
			       struct list_head *insert_here)
{
	drive_stat_acct(req->rq_dev, req->cmd, req->nr_sectors, 1);

	{
		struct request *__rq = __elv_next_request(q);

		if (__rq && !__rq->inactive && insert_here == &q->queue_head)
			BUG();
	}

	/*
	 * elevator indicated where it wants this request to be
	 * inserted at elevator_merge time
	 */
	q->elevator.elevator_add_req_fn(q, req, insert_here);
}

/*
 * Must be called with queue lock held and interrupts disabled
 */
void blkdev_release_request(struct request *req)
{
	request_queue_t *q = req->q;
	int rw = req->cmd;

	req->rq_status = RQ_INACTIVE;
	req->q = NULL;

	/*
	 * Request may not have originated from ll_rw_blk. if not,
	 * assume it has free buffers and check waiters
	 */
	if (q) {
		list_add(&req->queuelist, &q->rq[rw].free);
		if (++q->rq[rw].count >= batch_requests
		    && waitqueue_active(&q->rq[rw].wait))
			wake_up(&q->rq[rw].wait);
	}
}

/*
 * Has to be called with the request spinlock acquired
 */
static void attempt_merge(request_queue_t *q, struct request *req)
{
	struct request *next = blkdev_next_request(req);

	if (req->sector + req->nr_sectors != next->sector)
		return;

	if (req->cmd != next->cmd
	    || req->rq_dev != next->rq_dev
	    || req->nr_sectors + next->nr_sectors > q->max_sectors
	    || next->waiting || next->special || !next->inactive)
		return;

	/*
	 * If we are not allowed to merge these requests, then
	 * return.  If we are allowed to merge, then the count
	 * will have been updated to the appropriate number,
	 * and we shouldn't do it here too.
	 */
	if (q->merge_requests_fn(q, req, next)) {
		q->elevator.elevator_merge_req_fn(req, next);

		blkdev_dequeue_request(next);

		req->biotail->bi_next = next->bio;
		req->biotail = next->biotail;

		next->bio = next->biotail = NULL;

		req->nr_sectors = req->hard_nr_sectors += next->hard_nr_sectors;

		blkdev_release_request(next);
	}
}

static inline void attempt_back_merge(request_queue_t *q, struct request *rq)
{
	if (&rq->queuelist != q->queue_head.prev)
		attempt_merge(q, rq);
}

static inline void attempt_front_merge(request_queue_t *q,
				       struct list_head *head,
				       struct request *rq)
{
	struct list_head *prev = rq->queuelist.prev;

	if (prev != head)
		attempt_merge(q, blkdev_entry_to_request(prev));
}

static inline void __blk_attempt_remerge(request_queue_t *q, struct request *rq)
{
	if (rq->queuelist.next != &q->queue_head)
		attempt_merge(q, rq);
}

/**
 * blk_attempt_remerge  - attempt to remerge active head with next request
 * @q:    The &request_queue_t belonging to the device
 * @rq:   The head request (usually)
 *
 * Description:
 *    For head-active devices, the queue can easily be unplugged so quickly
 *    that proper merging is not done on the front request. This may hurt
 *    performance greatly for some devices. The block layer cannot safely
 *    do merging on that first request for these queues, but the driver can
 *    call this function and make it happen any way. Only the driver knows
 *    when it is safe to do so.
 **/
void blk_attempt_remerge(request_queue_t *q, struct request *rq)
{
	unsigned long flags;

	spin_lock_irqsave(&q->queue_lock, flags);
	__blk_attempt_remerge(q, rq);
	spin_unlock_irqrestore(&q->queue_lock, flags);
}

static int __make_request(request_queue_t *q, struct bio *bio)
{
	struct request *req, *freereq = NULL;
	int el_ret, latency = 0, rw, nr_sectors, cur_nr_sectors, barrier;
	struct list_head *head, *insert_here;
	elevator_t *elevator = &q->elevator;
	sector_t sector;

	sector = bio->bi_sector;
	nr_sectors = bio_sectors(bio);
	cur_nr_sectors = bio_iovec(bio)->bv_len >> 9;
	rw = bio_data_dir(bio);

	/*
	 * low level driver can indicate that it wants pages above a
	 * certain limit bounced to low memory (ie for highmem, or even
	 * ISA dma in theory)
	 */
	blk_queue_bounce(q, &bio);

	spin_lock_prefetch(&q->queue_lock);

	latency = elevator_request_latency(elevator, rw);

	barrier = test_bit(BIO_BARRIER, &bio->bi_flags);

again:
	req = NULL;
	head = &q->queue_head;

	spin_lock_irq(&q->queue_lock);

	/*
	 * barrier write must not be passed - so insert with 0 latency at
	 * the back of the queue and invalidate the entire existing merge hash
	 * for this device
	 */
	if (barrier && !freereq)
		latency = 0;

	insert_here = head->prev;
	if (blk_queue_empty(q) || barrier) {
		blk_plug_device(q);
		goto get_rq;
#if 0
	} else if (test_bit(QUEUE_FLAG_PLUGGED, &q->queue_flags)) {
		head = head->next;
#else
	} else if ((req = __elv_next_request(q))) {
		if (!req->inactive)
			head = head->next;

		req = NULL;
#endif
	}

	el_ret = elevator->elevator_merge_fn(q, &req, head, bio);
	switch (el_ret) {
		case ELEVATOR_BACK_MERGE:
			if (&req->queuelist == head && !req->inactive)
				BUG();
			if (!q->back_merge_fn(q, req, bio))
				break;
			elevator->elevator_merge_cleanup_fn(q, req, nr_sectors);

			req->biotail->bi_next = bio;
			req->biotail = bio;
			req->nr_sectors = req->hard_nr_sectors += nr_sectors;
			drive_stat_acct(req->rq_dev, req->cmd, nr_sectors, 0);
			attempt_back_merge(q, req);
			goto out;

		case ELEVATOR_FRONT_MERGE:
			if (&req->queuelist == head && !req->inactive)
				BUG();
			if (!q->front_merge_fn(q, req, bio))
				break;
			elevator->elevator_merge_cleanup_fn(q, req, nr_sectors);

			bio->bi_next = req->bio;
			req->bio = bio;
			/*
			 * may not be valid. if the low level driver said
			 * it didn't need a bounce buffer then it better
			 * not touch req->buffer either...
			 */
			req->buffer = bio_data(bio);
			req->current_nr_sectors = cur_nr_sectors;
			req->hard_cur_sectors = cur_nr_sectors;
			req->sector = req->hard_sector = sector;
			req->nr_sectors = req->hard_nr_sectors += nr_sectors;
			drive_stat_acct(req->rq_dev, req->cmd, nr_sectors, 0);
			attempt_front_merge(q, head, req);
			goto out;

		/*
		 * elevator says don't/can't merge. get new request
		 */
		case ELEVATOR_NO_MERGE:
			/*
			 * use elevator hints as to where to insert the
			 * request. if no hints, just add it to the back
			 * of the queue
			 */
			if (req)
				insert_here = &req->queuelist;
			break;

		default:
			printk("elevator returned crap (%d)\n", el_ret);
			BUG();
	}

	/*
	 * Grab a free request from the freelist - if that is empty, check
	 * if we are doing read ahead and abort instead of blocking for
	 * a free slot.
	 */
get_rq:
	if (freereq) {
		req = freereq;
		freereq = NULL;
	} else if ((req = get_request(q, rw)) == NULL) {

		spin_unlock_irq(&q->queue_lock);

		/*
		 * READA bit set
		 */
		if (bio->bi_rw & RWA_MASK) {
			set_bit(BIO_RW_BLOCK, &bio->bi_flags);
			goto end_io;
		}

		freereq = get_request_wait(q, rw);
		goto again;
	}

	/*
	 * fill up the request-info, and add it to the queue
	 */
	req->elevator_sequence = latency;
	req->cmd = rw;
	req->errors = 0;
	req->hard_sector = req->sector = sector;
	req->hard_nr_sectors = req->nr_sectors = nr_sectors;
	req->current_nr_sectors = req->hard_cur_sectors = cur_nr_sectors;
	req->nr_segments = bio->bi_vcnt;
	req->nr_hw_segments = req->nr_segments;
	req->buffer = bio_data(bio);	/* see ->buffer comment above */
	req->waiting = NULL;
	req->bio = req->biotail = bio;
	req->rq_dev = bio->bi_dev;
	add_request(q, req, insert_here);
out:
	if (freereq) {
		freereq->bio = freereq->biotail = NULL;
		blkdev_release_request(freereq);
	}

	spin_unlock_irq(&q->queue_lock);
	return 0;

end_io:
	bio->bi_end_io(bio, nr_sectors);
	return 0;
}


/*
 * If bio->bi_dev is a partition, remap the location
 */
static inline void blk_partition_remap(struct bio *bio)
{
	int major, minor, drive, minor0;
	struct gendisk *g;
	kdev_t dev0;

	major = MAJOR(bio->bi_dev);
	if ((g = get_gendisk(bio->bi_dev))) {
		minor = MINOR(bio->bi_dev);
		drive = (minor >> g->minor_shift);
		minor0 = (drive << g->minor_shift); /* whole disk device */
		/* that is, minor0 = (minor & ~((1<<g->minor_shift)-1)); */
		dev0 = MKDEV(major, minor0);
		if (dev0 != bio->bi_dev) {
			bio->bi_dev = dev0;
			bio->bi_sector += g->part[minor].start_sect;
		}
		/* lots of checks are possible */
	}
}

/**
 * generic_make_request: hand a buffer to it's device driver for I/O
 * @bio:  The bio describing the location in memory and on the device.
 *
 * generic_make_request() is used to make I/O requests of block
 * devices. It is passed a &struct bio, which describes the I/O that needs
 * to be done.
 *
 * generic_make_request() does not return any status.  The
 * success/failure status of the request, along with notification of
 * completion, is delivered asynchronously through the bio->bi_end_io
 * function described (one day) else where.
 *
 * The caller of generic_make_request must make sure that bi_io_vec
 * are set to describe the memory buffer, and that bi_dev and bi_sector are
 * set to describe the device address, and the
 * bi_end_io and optionally bi_private are set to describe how
 * completion notification should be signaled.
 *
 * generic_make_request and the drivers it calls may use bi_next if this
 * bio happens to be merged with someone else, and may change bi_dev and
 * bi_sector for remaps as it sees fit.  So the values of these fields
 * should NOT be depended on after the call to generic_make_request.
 *
 * */
void generic_make_request(struct bio *bio)
{
	int major = MAJOR(bio->bi_dev);
	int minor = MINOR(bio->bi_dev);
	request_queue_t *q;
	sector_t minorsize = 0;
	int nr_sectors = bio_sectors(bio);

	/* Test device or partition size, when known. */
	if (blk_size[major])
		minorsize = blk_size[major][minor];
	if (minorsize) {
		unsigned long maxsector = (minorsize << 1) + 1;
		unsigned long sector = bio->bi_sector;

		if (maxsector < nr_sectors || maxsector - nr_sectors < sector) {
			if (blk_size[major][minor]) {
				
				/* This may well happen - the kernel calls
				 * bread() without checking the size of the
				 * device, e.g., when mounting a device. */
				printk(KERN_INFO
				       "attempt to access beyond end of device\n");
				printk(KERN_INFO "%s: rw=%ld, want=%ld, limit=%Lu\n",
				       kdevname(bio->bi_dev), bio->bi_rw,
				       (sector + nr_sectors)>>1,
				       (u64) blk_size[major][minor]);
			}
			set_bit(BIO_EOF, &bio->bi_flags);
			goto end_io;
		}
	}

	/*
	 * Resolve the mapping until finished. (drivers are
	 * still free to implement/resolve their own stacking
	 * by explicitly returning 0)
	 *
	 * NOTE: we don't repeat the blk_size check for each new device.
	 * Stacking drivers are expected to know what they are doing.
	 */
	do {
		q = blk_get_queue(bio->bi_dev);
		if (!q) {
			printk(KERN_ERR
			       "generic_make_request: Trying to access nonexistent block-device %s (%Lu)\n",
			       kdevname(bio->bi_dev), (u64) bio->bi_sector);
end_io:
			bio->bi_end_io(bio, nr_sectors);
			break;
		}

		/*
		 * uh oh, need to split this bio... not implemented yet
		 */
		if (bio_sectors(bio) > q->max_sectors)
			BUG();

		/*
		 * If this device has partitions, remap block n
		 * of partition p to block n+start(p) of the disk.
		 */
		blk_partition_remap(bio);

	} while (q->make_request_fn(q, bio));
}

/*
 * our default bio end_io callback handler for a buffer_head mapping.
 */
static int end_bio_bh_io_sync(struct bio *bio, int nr_sectors)
{
	struct buffer_head *bh = bio->bi_private;

	BIO_BUG_ON(nr_sectors != (bh->b_size >> 9));

	bh->b_end_io(bh, test_bit(BIO_UPTODATE, &bio->bi_flags));
	bio_put(bio);

	return 0;
}

/**
 * submit_bio: submit a bio to the block device layer for I/O
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bio: The &struct bio which describes the I/O
 *
 * submit_bio() is very similar in purpose to generic_make_request(), and
 * uses that function to do most of the work. Both are fairly rough
 * interfaces, @bio must be presetup and ready for I/O.
 *
 */
int submit_bio(int rw, struct bio *bio)
{
	int count = bio_sectors(bio);

	/*
	 * do some validity checks...
	 */
	BUG_ON(!bio->bi_end_io);

	BIO_BUG_ON(bio_offset(bio) > PAGE_SIZE);
	BIO_BUG_ON(!bio_size(bio));
	BIO_BUG_ON(!bio->bi_io_vec);

	bio->bi_rw = rw;

	if (rw & WRITE)
		kstat.pgpgout += count;
	else
		kstat.pgpgin += count;

	generic_make_request(bio);
	return 1;
}

/**
 * submit_bh: submit a buffer_head to the block device layer for I/O
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bh: The &struct buffer_head which describes the I/O
 *
 **/
int submit_bh(int rw, struct buffer_head * bh)
{
	struct bio *bio;

	BUG_ON(!test_bit(BH_Lock, &bh->b_state));
	BUG_ON(!buffer_mapped(bh));
	BUG_ON(!bh->b_end_io);

	set_bit(BH_Req, &bh->b_state);

	/*
	 * from here on down, it's all bio -- do the initial mapping,
	 * submit_bio -> generic_make_request may further map this bio around
	 */
	bio = bio_alloc(GFP_NOIO, 1);

	bio->bi_sector = bh->b_blocknr * (bh->b_size >> 9);
	bio->bi_next = NULL;
	bio->bi_dev = bh->b_dev;
	bio->bi_private = bh;
	bio->bi_end_io = end_bio_bh_io_sync;

	bio->bi_io_vec[0].bv_page = bh->b_page;
	bio->bi_io_vec[0].bv_len = bh->b_size;
	bio->bi_io_vec[0].bv_offset = bh_offset(bh);

	bio->bi_vcnt = 1;
	bio->bi_idx = 0;
	bio->bi_size = bh->b_size;

	return submit_bio(rw, bio);
}

/**
 * ll_rw_block: low-level access to block devices
 * @rw: whether to %READ or %WRITE or maybe %READA (readahead)
 * @nr: number of &struct buffer_heads in the array
 * @bhs: array of pointers to &struct buffer_head
 *
 * ll_rw_block() takes an array of pointers to &struct buffer_heads,
 * and requests an I/O operation on them, either a %READ or a %WRITE.
 * The third %READA option is described in the documentation for
 * generic_make_request() which ll_rw_block() calls.
 *
 * This function provides extra functionality that is not in
 * generic_make_request() that is relevant to buffers in the buffer
 * cache or page cache.  In particular it drops any buffer that it
 * cannot get a lock on (with the BH_Lock state bit), any buffer that
 * appears to be clean when doing a write request, and any buffer that
 * appears to be up-to-date when doing read request.  Further it marks
 * as clean buffers that are processed for writing (the buffer cache
 * wont assume that they are actually clean until the buffer gets
 * unlocked).
 *
 * ll_rw_block sets b_end_io to simple completion handler that marks
 * the buffer up-to-date (if approriate), unlocks the buffer and wakes
 * any waiters.  As client that needs a more interesting completion
 * routine should call submit_bh() (or generic_make_request())
 * directly.
 *
 * Caveat:
 *  All of the buffers must be for the same device, and must also be
 *  a multiple of the current approved size for the device.
 *
 **/
void ll_rw_block(int rw, int nr, struct buffer_head * bhs[])
{
	unsigned int major;
	int correct_size;
	int i;

	if (!nr)
		return;

	major = MAJOR(bhs[0]->b_dev);

	/* Determine correct block size for this device. */
	correct_size = get_hardsect_size(bhs[0]->b_dev);

	/* Verify requested block sizes. */
	for (i = 0; i < nr; i++) {
		struct buffer_head *bh = bhs[i];
		if (bh->b_size & (correct_size - 1)) {
			printk(KERN_NOTICE "ll_rw_block: device %s: "
			       "only %d-char blocks implemented (%u)\n",
			       kdevname(bhs[0]->b_dev),
			       correct_size, bh->b_size);
			goto sorry;
		}
	}

	if ((rw & WRITE) && is_read_only(bhs[0]->b_dev)) {
		printk(KERN_NOTICE "Can't write to read-only device %s\n",
		       kdevname(bhs[0]->b_dev));
		goto sorry;
	}

	for (i = 0; i < nr; i++) {
		struct buffer_head *bh = bhs[i];

		/* Only one thread can actually submit the I/O. */
		if (test_and_set_bit(BH_Lock, &bh->b_state))
			continue;

		/* We have the buffer lock */
		atomic_inc(&bh->b_count);
		bh->b_end_io = end_buffer_io_sync;

		switch(rw) {
		case WRITE:
			if (!atomic_set_buffer_clean(bh))
				/* Hmmph! Nothing to write */
				goto end_io;
			__mark_buffer_clean(bh);
			break;

		case READA:
		case READ:
			if (buffer_uptodate(bh))
				/* Hmmph! Already have it */
				goto end_io;
			break;
		default:
			BUG();
	end_io:
			bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
			continue;
		}

		submit_bh(rw, bh);
	}
	return;

sorry:
	/* Make sure we don't get infinite dirty retries.. */
	for (i = 0; i < nr; i++)
		mark_buffer_clean(bhs[i]);
}

#ifdef CONFIG_STRAM_SWAP
extern int stram_device_init (void);
#endif

/**
 * end_that_request_first - end I/O on one buffer.
 * @req:      the request being processed
 * @uptodate: 0 for I/O error
 * @nr_sectors: number of sectors to end I/O on
 *
 * Description:
 *     Ends I/O on the first buffer attached to @req, and sets it up
 *     for the next buffer_head (if any) in the cluster.
 *     
 * Return:
 *     0 - we are done with this request, call end_that_request_last()
 *     1 - still buffers pending for this request
 **/

int end_that_request_first(struct request *req, int uptodate, int nr_sectors)
{
	struct bio *bio, *nxt;
	int nsect;

	req->errors = 0;
	if (!uptodate)
		printk("end_request: I/O error, dev %s, sector %lu\n",
			kdevname(req->rq_dev), req->sector);

	if ((bio = req->bio) != NULL) {
next_chunk:
		nsect = bio_iovec(bio)->bv_len >> 9;

		nr_sectors -= nsect;

		nxt = bio->bi_next;
		bio->bi_next = NULL;
		if (!bio_endio(bio, uptodate, nsect))
			req->bio = nxt;
		else
			bio->bi_next = nxt;

		if ((bio = req->bio) != NULL) {
			req->hard_sector += nsect;
			req->hard_nr_sectors -= nsect;
			req->sector = req->hard_sector;
			req->nr_sectors = req->hard_nr_sectors;

			req->current_nr_sectors = bio_iovec(bio)->bv_len >> 9;
			req->hard_cur_sectors = req->current_nr_sectors;
			if (req->nr_sectors < req->current_nr_sectors) {
				printk("end_request: buffer-list destroyed\n");
				req->nr_sectors = req->current_nr_sectors;
			}

			req->buffer = bio_data(bio);
			/*
			 * end more in this run, or just return 'not-done'
			 */
			if (nr_sectors > 0)
				goto next_chunk;

			return 1;
		}
	}
	return 0;
}

void end_that_request_last(struct request *req)
{
	if (req->waiting)
		complete(req->waiting);

	blkdev_release_request(req);
}

#define MB(kb)	((kb) << 10)

int __init blk_dev_init(void)
{
	struct blk_dev_struct *dev;
	int total_ram;

	request_cachep = kmem_cache_create("blkdev_requests",
					   sizeof(struct request),
					   0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!request_cachep)
		panic("Can't create request pool slab cache\n");

	for (dev = blk_dev + MAX_BLKDEV; dev-- != blk_dev;)
		dev->queue = NULL;

	memset(ro_bits,0,sizeof(ro_bits));
	memset(max_readahead, 0, sizeof(max_readahead));

	total_ram = nr_free_pages() << (PAGE_SHIFT - 10);

	/*
	 * Free request slots per queue.
	 * (Half for reads, half for writes)
	 */
	queue_nr_requests = 64;
	if (total_ram > MB(32))
		queue_nr_requests = 256;

	/*
	 * Batch frees according to queue length
	 */
	if ((batch_requests = queue_nr_requests / 4) > 32)
		batch_requests = 32;
	printk("block: %d slots per queue, batch=%d\n", queue_nr_requests, batch_requests);

	blk_max_low_pfn = max_low_pfn;
	blk_max_pfn = max_pfn;

#if defined(CONFIG_IDE) && defined(CONFIG_BLK_DEV_IDE)
	ide_init();		/* this MUST precede hd_init */
#endif
#if defined(CONFIG_IDE) && defined(CONFIG_BLK_DEV_HD)
	hd_init();
#endif
#if defined(__i386__)	/* Do we even need this? */
	outb_p(0xc, 0x3f2);
#endif

	return 0;
};

EXPORT_SYMBOL(end_that_request_first);
EXPORT_SYMBOL(end_that_request_last);
EXPORT_SYMBOL(blk_init_queue);
EXPORT_SYMBOL(blk_get_queue);
EXPORT_SYMBOL(blk_cleanup_queue);
EXPORT_SYMBOL(blk_queue_make_request);
EXPORT_SYMBOL(blk_queue_bounce_limit);
EXPORT_SYMBOL(generic_make_request);
EXPORT_SYMBOL(blkdev_release_request);
EXPORT_SYMBOL(generic_unplug_device);
EXPORT_SYMBOL(blk_attempt_remerge);
EXPORT_SYMBOL(blk_max_low_pfn);
EXPORT_SYMBOL(blk_queue_max_sectors);
EXPORT_SYMBOL(blk_queue_max_segments);
EXPORT_SYMBOL(blk_queue_max_segment_size);
EXPORT_SYMBOL(blk_queue_hardsect_size);
EXPORT_SYMBOL(blk_rq_map_sg);
EXPORT_SYMBOL(blk_nohighio);
