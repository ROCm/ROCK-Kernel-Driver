/*
 *  linux/drivers/block/ll_rw_blk.c
 *
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 * Elevator latency, (C) 2000  Andrea Arcangeli <andrea@suse.de> SuSE
 * Queue request tables / lock, selectable elevator, Jens Axboe <axboe@suse.de>
 * kernel-doc documentation started by NeilBrown <neilb@cse.unsw.edu.au> -  July2000
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
#include <linux/init.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/io.h>
#include <linux/blk.h>
#include <linux/highmem.h>
#include <linux/raid/md.h>

#include <linux/module.h>

/*
 * MAC Floppy IWM hooks
 */

#ifdef CONFIG_MAC_FLOPPY_IWM
extern int mac_floppy_init(void);
#endif

extern int lvm_init(void);

/*
 * For the allocated request tables
 */
static kmem_cache_t *request_cachep;

/*
 * The "disk" task queue is used to start the actual requests
 * after a plug
 */
DECLARE_TASK_QUEUE(tq_disk);

/*
 * Protect the request list against multiple users..
 *
 * With this spinlock the Linux block IO subsystem is 100% SMP threaded
 * from the IRQ event side, and almost 100% SMP threaded from the syscall
 * side (we still have protect against block device array operations, and
 * the do_request() side is casually still unsafe. The kernel lock protects
 * this part currently.).
 *
 * there is a fair chance that things will work just OK if these functions
 * are called with no global kernel lock held ...
 */
spinlock_t io_request_lock = SPIN_LOCK_UNLOCKED;

/* This specifies how many sectors to read ahead on the disk. */

int read_ahead[MAX_BLKDEV];

/* blk_dev_struct is:
 *	*request_fn
 *	*current_request
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
 * hardsect_size contains the size of the hardware sector of a device.
 *
 * hardsect_size[MAJOR][MINOR]
 *
 * if (!hardsect_size[MAJOR])
 *		then 512 bytes is assumed.
 * else
 *		sector_size is hardsect_size[MAJOR][MINOR]
 * This is currently set by some scsi devices and read by the msdos fs driver.
 * Other uses may appear later.
 */
int * hardsect_size[MAX_BLKDEV];

/*
 * The following tunes the read-ahead algorithm in mm/filemap.c
 */
int * max_readahead[MAX_BLKDEV];

/*
 * Max number of sectors per request
 */
int * max_sectors[MAX_BLKDEV];

static inline int get_max_sectors(kdev_t dev)
{
	if (!max_sectors[MAJOR(dev)])
		return MAX_SECTORS;
	return max_sectors[MAJOR(dev)][MINOR(dev)];
}

static inline request_queue_t *__blk_get_queue(kdev_t dev)
{
	struct blk_dev_struct *bdev = blk_dev + MAJOR(dev);

	if (bdev->queue)
		return bdev->queue(dev);
	else
		return &blk_dev[MAJOR(dev)].request_queue;
}

/*
 * NOTE: the device-specific queue() functions
 * have to be atomic!
 */
request_queue_t *blk_get_queue(kdev_t dev)
{
	request_queue_t *ret;
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock,flags);
	ret = __blk_get_queue(dev);
	spin_unlock_irqrestore(&io_request_lock,flags);

	return ret;
}

static int __blk_cleanup_queue(struct list_head *head)
{
	struct list_head *entry;
	struct request *rq;
	int i = 0;

	if (list_empty(head))
		return 0;

	entry = head->next;
	do {
		rq = list_entry(entry, struct request, table);
		entry = entry->next;
		list_del(&rq->table);
		kmem_cache_free(request_cachep, rq);
		i++;
	} while (!list_empty(head));

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
	int count = QUEUE_NR_REQUESTS;

	count -= __blk_cleanup_queue(&q->request_freelist[READ]);
	count -= __blk_cleanup_queue(&q->request_freelist[WRITE]);

	if (count)
		printk("blk_cleanup_queue: leaked requests (%d)\n", count);

	memset(q, 0, sizeof(*q));
}

/**
 * blk_queue_headactive - indicate whether head of request queue may be active
 * @q:       The queue which this applies to.
 * @active:  A flag indication where the head of the queue is active.
 *
 * Description:
 *    The driver for a block device may choose to leave the currently active
 *    request on the request queue, removing it only when it has completed.
 *    The queue handling routines assume this by default for safety reasons
 *    and will not involve the head of the request queue in any merging or
 *    reordering of requests when the queue is unplugged (and thus may be
 *    working on this particular request).
 *
 *    If a driver removes requests from the queue before processing them, then
 *    it may indicate that it does so, there by allowing the head of the queue
 *    to be involved in merging and reordering.  This is done be calling
 *    blk_queue_headactive() with an @active flag of %0.
 *
 *    If a driver processes several requests at once, it must remove them (or
 *    at least all but one of them) from the request queue.
 *
 *    When a queue is plugged (see blk_queue_pluggable()) the head will be
 *    assumed to be inactive.
 **/
 
void blk_queue_headactive(request_queue_t * q, int active)
{
	q->head_active = active;
}

/**
 * blk_queue_pluggable - define a plugging function for a request queue
 * @q:   the request queue to which the function will apply
 * @plug: the function to be called to plug a queue
 *
 * Description:
 *   A request queue will be "plugged" if a request is added to it
 *   while it is empty.  This allows a number of requests to be added
 *   before any are processed, thus providing an opportunity for these
 *   requests to be merged or re-ordered.
 *   The default plugging function (generic_plug_device()) sets the
 *   "plugged" flag for the queue and adds a task to the $tq_disk task
 *   queue to unplug the queue and call the request function at a
 *   later time.
 *
 *   A device driver may provide an alternate plugging function by
 *   passing it to blk_queue_pluggable().  This function should set
 *   the "plugged" flag if it want calls to the request_function to be
 *   blocked, and should place a task on $tq_disk which will unplug
 *   the queue.  Alternately it can simply do nothing and there-by
 *   disable plugging of the device.
 **/

void blk_queue_pluggable (request_queue_t * q, plug_device_fn *plug)
{
	q->plug_device_fn = plug;
}


/**
 * blk_queue_make_request - define an alternate make_request function for a device
 * @q:  the request queue for the device to be affected
 * @mfn: the alternate make_request function
 *
 * Description:
 *    The normal way for &struct buffer_heads to be passed to a device
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
 *    with buffers in "highmemory", either by calling bh_kmap() to get
 *    a kernel mapping, to by calling create_bounce() to create a
 *    buffer in normal memory.
 **/

void blk_queue_make_request(request_queue_t * q, make_request_fn * mfn)
{
	q->make_request_fn = mfn;
}

static inline int ll_new_segment(request_queue_t *q, struct request *req, int max_segments)
{
	if (req->nr_segments < max_segments) {
		req->nr_segments++;
		q->elevator.nr_segments++;
		return 1;
	}
	return 0;
}

static int ll_back_merge_fn(request_queue_t *q, struct request *req, 
			    struct buffer_head *bh, int max_segments)
{
	if (req->bhtail->b_data + req->bhtail->b_size == bh->b_data)
		return 1;
	return ll_new_segment(q, req, max_segments);
}

static int ll_front_merge_fn(request_queue_t *q, struct request *req, 
			     struct buffer_head *bh, int max_segments)
{
	if (bh->b_data + bh->b_size == req->bh->b_data)
		return 1;
	return ll_new_segment(q, req, max_segments);
}

static int ll_merge_requests_fn(request_queue_t *q, struct request *req,
				struct request *next, int max_segments)
{
	int total_segments = req->nr_segments + next->nr_segments;
	int same_segment;

	same_segment = 0;
	if (req->bhtail->b_data + req->bhtail->b_size == next->bh->b_data) {
		total_segments--;
		same_segment = 1;
	}
    
	if (total_segments > max_segments)
		return 0;

	q->elevator.nr_segments -= same_segment;
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
static void generic_plug_device(request_queue_t *q, kdev_t dev)
{
	/*
	 * no need to replug device
	 */
	if (!list_empty(&q->queue_head) || q->plugged)
		return;

	q->plugged = 1;
	queue_task(&q->plug_tq, &tq_disk);
}

/*
 * remove the plug and let it rip..
 */
static inline void __generic_unplug_device(request_queue_t *q)
{
	if (q->plugged) {
		q->plugged = 0;
		if (!list_empty(&q->queue_head))
			q->request_fn(q);
	}
}

static void generic_unplug_device(void *data)
{
	request_queue_t *q = (request_queue_t *) data;
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock, flags);
	__generic_unplug_device(q);
	spin_unlock_irqrestore(&io_request_lock, flags);
}

static void blk_init_free_list(request_queue_t *q)
{
	struct request *rq;
	int i;

	/*
	 * Divide requests in half between read and write. This used to
	 * be a 2/3 advantage for reads, but now reads can steal from
	 * the write free list.
	 */
	for (i = 0; i < QUEUE_NR_REQUESTS; i++) {
		rq = kmem_cache_alloc(request_cachep, SLAB_KERNEL);
		rq->rq_status = RQ_INACTIVE;
		list_add(&rq->table, &q->request_freelist[i & 1]);
	}

	init_waitqueue_head(&q->wait_for_request);
	spin_lock_init(&q->request_lock);
}

static int __make_request(request_queue_t * q, int rw, struct buffer_head * bh);

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
 *    A global spin lock $io_request_lock must be held while manipulating the
 *    requests on the request queue.
 *
 *    The request on the head of the queue is by default assumed to be
 *    potentially active, and it is not considered for re-ordering or merging
 *    whenever the given queue is unplugged. This behaviour can be changed with
 *    blk_queue_headactive().
 *
 * Note:
 *    blk_init_queue() must be paired with a blk_cleanup-queue() call
 *    when the block device is deactivated (such as at module unload).
 **/
void blk_init_queue(request_queue_t * q, request_fn_proc * rfn)
{
	INIT_LIST_HEAD(&q->queue_head);
	INIT_LIST_HEAD(&q->request_freelist[READ]);
	INIT_LIST_HEAD(&q->request_freelist[WRITE]);
	elevator_init(&q->elevator, ELEVATOR_LINUS);
	blk_init_free_list(q);
	q->request_fn     	= rfn;
	q->back_merge_fn       	= ll_back_merge_fn;
	q->front_merge_fn      	= ll_front_merge_fn;
	q->merge_requests_fn	= ll_merge_requests_fn;
	q->make_request_fn	= __make_request;
	q->plug_tq.sync		= 0;
	q->plug_tq.routine	= &generic_unplug_device;
	q->plug_tq.data		= q;
	q->plugged        	= 0;
	/*
	 * These booleans describe the queue properties.  We set the
	 * default (and most common) values here.  Other drivers can
	 * use the appropriate functions to alter the queue properties.
	 * as appropriate.
	 */
	q->plug_device_fn 	= generic_plug_device;
	q->head_active    	= 1;
}


#define blkdev_free_rq(list) list_entry((list)->next, struct request, table);
/*
 * Get a free request. io_request_lock must be held and interrupts
 * disabled on the way in.
 */
static inline struct request *get_request(request_queue_t *q, int rw)
{
	struct list_head *list = &q->request_freelist[rw];
	struct request *rq;

	/*
	 * Reads get preferential treatment and are allowed to steal
	 * from the write free list if necessary.
	 */
	if (!list_empty(list)) {
		rq = blkdev_free_rq(list);
		goto got_rq;
	}

	/*
	 * if the WRITE list is non-empty, we know that rw is READ
	 * and that the READ list is empty. allow reads to 'steal'
	 * from the WRITE list.
	 */
	if (!list_empty(&q->request_freelist[WRITE])) {
		list = &q->request_freelist[WRITE];
		rq = blkdev_free_rq(list);
		goto got_rq;
	}

	return NULL;

got_rq:
	list_del(&rq->table);
	rq->free_list = list;
	rq->rq_status = RQ_ACTIVE;
	rq->special = NULL;
	rq->q = q;
	return rq;
}

/*
 * No available requests for this queue, unplug the device.
 */
static struct request *__get_request_wait(request_queue_t *q, int rw)
{
	register struct request *rq;
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue_exclusive(&q->wait_for_request, &wait);
	for (;;) {
		__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_lock_irq(&io_request_lock);
		rq = get_request(q, rw);
		spin_unlock_irq(&io_request_lock);
		if (rq)
			break;
		generic_unplug_device(q);
		schedule();
	}
	remove_wait_queue(&q->wait_for_request, &wait);
	current->state = TASK_RUNNING;
	return rq;
}

static inline struct request *get_request_wait(request_queue_t *q, int rw)
{
	register struct request *rq;

	spin_lock_irq(&io_request_lock);
	rq = get_request(q, rw);
	spin_unlock_irq(&io_request_lock);
	if (rq)
		return rq;
	return __get_request_wait(q, rw);
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

inline void drive_stat_acct (kdev_t dev, int rw,
				unsigned long nr_sectors, int new_io)
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
 * It disables interrupts (acquires the request spinlock) so that it can muck
 * with the request-lists in peace. Thus it should be called with no spinlocks
 * held.
 *
 * By this point, req->cmd is always either READ/WRITE, never READA,
 * which is important for drive_stat_acct() above.
 */

static inline void add_request(request_queue_t * q, struct request * req,
			       struct list_head *head, int lat)
{
	int major;

	drive_stat_acct(req->rq_dev, req->cmd, req->nr_sectors, 1);

	/*
	 * let selected elevator insert the request
	 */
	q->elevator.elevator_fn(req, &q->elevator, &q->queue_head, head, lat);

        /*
	 * FIXME(eric) I don't understand why there is a need for this
	 * special case code.  It clearly doesn't fit any more with
	 * the new queueing architecture, and it got added in 2.3.10.
	 * I am leaving this in here until I hear back from the COMPAQ
	 * people.
         */
	major = MAJOR(req->rq_dev);
	if (major >= COMPAQ_SMART2_MAJOR+0 && major <= COMPAQ_SMART2_MAJOR+7)
		(q->request_fn)(q);
	if (major >= COMPAQ_CISS_MAJOR+0 && major <= COMPAQ_CISS_MAJOR+7)
                (q->request_fn)(q);
	if (major >= DAC960_MAJOR+0 && major <= DAC960_MAJOR+7)
		(q->request_fn)(q);
}

/*
 * Must be called with io_request_lock held and interrupts disabled
 */
void inline blkdev_release_request(struct request *req)
{
	req->rq_status = RQ_INACTIVE;

	/*
	 * Request may not have originated from ll_rw_blk
	 */
	if (req->free_list) {
		list_add(&req->table, req->free_list);
		req->free_list = NULL;
		wake_up(&req->q->wait_for_request);
	}
}

/*
 * Has to be called with the request spinlock acquired
 */
static void attempt_merge(request_queue_t * q,
			  struct request *req,
			  int max_sectors,
			  int max_segments)
{
	struct request *next;
  
	next = blkdev_next_request(req);
	if (req->sector + req->nr_sectors != next->sector)
		return;
	if (req->cmd != next->cmd
	    || req->rq_dev != next->rq_dev
	    || req->nr_sectors + next->nr_sectors > max_sectors
	    || next->sem)
		return;
	/*
	 * If we are not allowed to merge these requests, then
	 * return.  If we are allowed to merge, then the count
	 * will have been updated to the appropriate number,
	 * and we shouldn't do it here too.
	 */
	if(!(q->merge_requests_fn)(q, req, next, max_segments))
		return;

	req->bhtail->b_reqnext = next->bh;
	req->bhtail = next->bhtail;
	req->nr_sectors = req->hard_nr_sectors += next->hard_nr_sectors;
	list_del(&next->queue);
	blkdev_release_request(next);
}

static inline void attempt_back_merge(request_queue_t * q,
				      struct request *req,
				      int max_sectors,
				      int max_segments)
{
	if (&req->queue == q->queue_head.prev)
		return;
	attempt_merge(q, req, max_sectors, max_segments);
}

static inline void attempt_front_merge(request_queue_t * q,
				       struct list_head * head,
				       struct request *req,
				       int max_sectors,
				       int max_segments)
{
	struct list_head * prev;

	prev = req->queue.prev;
	if (head == prev)
		return;
	attempt_merge(q, blkdev_entry_to_request(prev), max_sectors, max_segments);
}

static int __make_request(request_queue_t * q, int rw,
				  struct buffer_head * bh)
{
	unsigned int sector, count;
	int max_segments = MAX_SEGMENTS;
	struct request * req = NULL, *freereq = NULL;
	int rw_ahead, max_sectors, el_ret;
	struct list_head *head;
	int latency;
	elevator_t *elevator = &q->elevator;

	count = bh->b_size >> 9;
	sector = bh->b_rsector;

	rw_ahead = 0;	/* normal case; gets changed below for READA */
	switch (rw) {
		case READA:
			rw_ahead = 1;
			rw = READ;	/* drop into READ */
		case READ:
		case WRITE:
			break;
		default:
			BUG();
			goto end_io;
	}

	/* We'd better have a real physical mapping!
	   Check this bit only if the buffer was dirty and just locked
	   down by us so at this point flushpage will block and
	   won't clear the mapped bit under us. */
	if (!buffer_mapped(bh))
		BUG();

	/*
	 * Temporary solution - in 2.5 this will be done by the lowlevel
	 * driver. Create a bounce buffer if the buffer data points into
	 * high memory - keep the original buffer otherwise.
	 */
#if CONFIG_HIGHMEM
	bh = create_bounce(rw, bh);
#endif

/* look for a free request. */
	/*
	 * Try to coalesce the new request with old requests
	 */
	max_sectors = get_max_sectors(bh->b_rdev);

	latency = elevator_request_latency(elevator, rw);

	/*
	 * Now we acquire the request spinlock, we have to be mega careful
	 * not to schedule or do something nonatomic
	 */
again:
	spin_lock_irq(&io_request_lock);

	/*
	 * skip first entry, for devices with active queue head
	 */
	head = &q->queue_head;
	if (q->head_active && !q->plugged)
		head = head->next;

	if (list_empty(head)) {
		q->plug_device_fn(q, bh->b_rdev); /* is atomic */
		goto get_rq;
	}

	el_ret = elevator->elevator_merge_fn(q, &req, bh, rw,
					     &max_sectors, &max_segments);
	switch (el_ret) {

		case ELEVATOR_BACK_MERGE:
			if (!q->back_merge_fn(q, req, bh, max_segments))
				break;
			req->bhtail->b_reqnext = bh;
			req->bhtail = bh;
			req->nr_sectors = req->hard_nr_sectors += count;
			req->e = elevator;
			drive_stat_acct(req->rq_dev, req->cmd, count, 0);
			attempt_back_merge(q, req, max_sectors, max_segments);
			goto out;

		case ELEVATOR_FRONT_MERGE:
			if (!q->front_merge_fn(q, req, bh, max_segments))
				break;
			bh->b_reqnext = req->bh;
			req->bh = bh;
			req->buffer = bh->b_data;
			req->current_nr_sectors = count;
			req->sector = req->hard_sector = sector;
			req->nr_sectors = req->hard_nr_sectors += count;
			req->e = elevator;
			drive_stat_acct(req->rq_dev, req->cmd, count, 0);
			attempt_front_merge(q, head, req, max_sectors, max_segments);
			goto out;
		/*
		 * elevator says don't/can't merge. get new request
		 */
		case ELEVATOR_NO_MERGE:
			break;

		default:
			printk("elevator returned crap (%d)\n", el_ret);
			BUG();
	}
		
	/*
	 * Grab a free request from the freelist. Read first try their
	 * own queue - if that is empty, we steal from the write list.
	 * Writes must block if the write list is empty, and read aheads
	 * are not crucial.
	 */
get_rq:
	if (freereq) {
		req = freereq;
		freereq = NULL;
	} else if ((req = get_request(q, rw)) == NULL) {
		spin_unlock_irq(&io_request_lock);
		if (rw_ahead)
			goto end_io;

		freereq = __get_request_wait(q, rw);
		goto again;
	}

/* fill up the request-info, and add it to the queue */
	req->cmd = rw;
	req->errors = 0;
	req->hard_sector = req->sector = sector;
	req->hard_nr_sectors = req->nr_sectors = count;
	req->current_nr_sectors = count;
	req->nr_segments = 1; /* Always 1 for a new request. */
	req->nr_hw_segments = 1; /* Always 1 for a new request. */
	req->buffer = bh->b_data;
	req->sem = NULL;
	req->bh = bh;
	req->bhtail = bh;
	req->rq_dev = bh->b_rdev;
	req->e = elevator;
	add_request(q, req, head, latency);
out:
	if (!q->plugged)
		(q->request_fn)(q);
	if (freereq)
		blkdev_release_request(freereq);
	spin_unlock_irq(&io_request_lock);
	return 0;
end_io:
	bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
	return 0;
}

/**
 * generic_make_request: hand a buffer head to it's device driver for I/O
 * @rw:  READ, WRITE, or READA - what sort of I/O is desired.
 * @bh:  The buffer head describing the location in memory and on the device.
 *
 * generic_make_request() is used to make I/O requests of block
 * devices. It is passed a &struct buffer_head and a &rw value.  The
 * %READ and %WRITE options are (hopefully) obvious in meaning.  The
 * %READA value means that a read is required, but that the driver is
 * free to fail the request if, for example, it cannot get needed
 * resources immediately.
 *
 * generic_make_request() does not return any status.  The
 * success/failure status of the request, along with notification of
 * completion, is delivered asynchronously through the bh->b_end_io
 * function described (one day) else where.
 *
 * The caller of generic_make_request must make sure that b_page,
 * b_addr, b_size are set to describe the memory buffer, that b_rdev
 * and b_rsector are set to describe the device address, and the
 * b_end_io and optionally b_private are set to describe how
 * completion notification should be signaled.  BH_Mapped should also
 * be set (to confirm that b_dev and b_blocknr are valid).
 *
 * generic_make_request and the drivers it calls may use b_reqnext,
 * and may change b_rdev and b_rsector.  So the values of these fields
 * should NOT be depended on after the call to generic_make_request.
 * Because of this, the caller should record the device address
 * information in b_dev and b_blocknr.
 *
 * Apart from those fields mentioned above, no other fields, and in
 * particular, no other flags, are changed by generic_make_request or
 * any lower level drivers.
 * */
void generic_make_request (int rw, struct buffer_head * bh)
{
	int major = MAJOR(bh->b_rdev);
	request_queue_t *q;

	if (!bh->b_end_io) BUG();
	if (blk_size[major]) {
		unsigned long maxsector = (blk_size[major][MINOR(bh->b_rdev)] << 1) + 1;
		unsigned int sector, count;

		count = bh->b_size >> 9;
		sector = bh->b_rsector;

		if (maxsector < count || maxsector - count < sector) {
			bh->b_state &= (1 << BH_Lock) | (1 << BH_Mapped);
			if (blk_size[major][MINOR(bh->b_rdev)]) {
				
				/* This may well happen - the kernel calls bread()
				   without checking the size of the device, e.g.,
				   when mounting a device. */
				printk(KERN_INFO
				       "attempt to access beyond end of device\n");
				printk(KERN_INFO "%s: rw=%d, want=%d, limit=%d\n",
				       kdevname(bh->b_rdev), rw,
				       (sector + count)>>1,
				       blk_size[major][MINOR(bh->b_rdev)]);
			}
			bh->b_end_io(bh, 0);
			return;
		}
	}

	/*
	 * Resolve the mapping until finished. (drivers are
	 * still free to implement/resolve their own stacking
	 * by explicitly returning 0)
	 */
	/* NOTE: we don't repeat the blk_size check for each new device.
	 * Stacking drivers are expected to know what they are doing.
	 */
	do {
		q = blk_get_queue(bh->b_rdev);
		if (!q) {
			printk(KERN_ERR
			       "generic_make_request: Trying to access nonexistent block-device %s (%ld)\n",
			       kdevname(bh->b_rdev), bh->b_rsector);
			buffer_IO_error(bh);
			break;
		}

	}
	while (q->make_request_fn(q, rw, bh));
}


/**
 * submit_bh: submit a buffer_head to the block device later for I/O
 * @rw: whether to %READ or %WRITE, or mayve to %READA (read ahead)
 * @bh: The &struct buffer_head which describes the I/O
 *
 * submit_bh() is very similar in purpose to generic_make_request(), and
 * uses that function to do most of the work.
 *
 * The extra functionality provided by submit_bh is to determine
 * b_rsector from b_blocknr and b_size, and to set b_rdev from b_dev.
 * This is is appropriate for IO requests that come from the buffer
 * cache and page cache which (currently) always use aligned blocks.
 */
void submit_bh(int rw, struct buffer_head * bh)
{
	if (!test_bit(BH_Lock, &bh->b_state))
		BUG();

	set_bit(BH_Req, &bh->b_state);

	/*
	 * First step, 'identity mapping' - RAID or LVM might
	 * further remap this.
	 */
	bh->b_rdev = bh->b_dev;
	bh->b_rsector = bh->b_blocknr * (bh->b_size>>9);

	generic_make_request(rw, bh);

	switch (rw) {
		case WRITE:
			kstat.pgpgout++;
			break;
		default:
			kstat.pgpgin++;
			break;
	}
}

/*
 * Default IO end handler, used by "ll_rw_block()".
 */
static void end_buffer_io_sync(struct buffer_head *bh, int uptodate)
{
	mark_buffer_uptodate(bh, uptodate);
	unlock_buffer(bh);
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
 *  of the current approved size for the device.  */

void ll_rw_block(int rw, int nr, struct buffer_head * bhs[])
{
	unsigned int major;
	int correct_size;
	int i;

	major = MAJOR(bhs[0]->b_dev);

	/* Determine correct block size for this device. */
	correct_size = BLOCK_SIZE;
	if (blksize_size[major]) {
		i = blksize_size[major][MINOR(bhs[0]->b_dev)];
		if (i)
			correct_size = i;
	}

	/* Verify requested block sizes. */
	for (i = 0; i < nr; i++) {
		struct buffer_head *bh;
		bh = bhs[i];
		if (bh->b_size != correct_size) {
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
		struct buffer_head *bh;
		bh = bhs[i];

		/* Only one thread can actually submit the I/O. */
		if (test_and_set_bit(BH_Lock, &bh->b_state))
			continue;

		/* We have the buffer lock */
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

/*
 * First step of what used to be end_request
 *
 * 0 means continue with end_that_request_last,
 * 1 means we are done
 */

int end_that_request_first (struct request *req, int uptodate, char *name)
{
	struct buffer_head * bh;
	int nsect;

	req->errors = 0;
	if (!uptodate)
		printk("end_request: I/O error, dev %s (%s), sector %lu\n",
			kdevname(req->rq_dev), name, req->sector);

	if ((bh = req->bh) != NULL) {
		nsect = bh->b_size >> 9;
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		bh->b_end_io(bh, uptodate);
		if ((bh = req->bh) != NULL) {
			req->hard_sector += nsect;
			req->hard_nr_sectors -= nsect;
			req->sector = req->hard_sector;
			req->nr_sectors = req->hard_nr_sectors;

			req->current_nr_sectors = bh->b_size >> 9;
			if (req->nr_sectors < req->current_nr_sectors) {
				req->nr_sectors = req->current_nr_sectors;
				printk("end_request: buffer-list destroyed\n");
			}
			req->buffer = bh->b_data;
			return 1;
		}
	}
	return 0;
}

void end_that_request_last(struct request *req)
{
	if (req->e) {
		printk("end_that_request_last called with non-dequeued req\n");
		BUG();
	}
	if (req->sem != NULL)
		up(req->sem);

	blkdev_release_request(req);
}

int __init blk_dev_init(void)
{
	struct blk_dev_struct *dev;

	request_cachep = kmem_cache_create("blkdev_requests",
					   sizeof(struct request),
					   0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!request_cachep)
		panic("Can't create request pool slab cache\n");

	for (dev = blk_dev + MAX_BLKDEV; dev-- != blk_dev;)
		dev->queue = NULL;

	memset(ro_bits,0,sizeof(ro_bits));
	memset(max_readahead, 0, sizeof(max_readahead));
	memset(max_sectors, 0, sizeof(max_sectors));
#ifdef CONFIG_AMIGA_Z2RAM
	z2_init();
#endif
#ifdef CONFIG_STRAM_SWAP
	stram_device_init();
#endif
#ifdef CONFIG_BLK_DEV_RAM
	rd_init();
#endif
#ifdef CONFIG_BLK_DEV_LOOP
	loop_init();
#endif
#ifdef CONFIG_ISP16_CDI
	isp16_init();
#endif
#if defined(CONFIG_IDE) && defined(CONFIG_BLK_DEV_IDE)
	ide_init();		/* this MUST precede hd_init */
#endif
#if defined(CONFIG_IDE) && defined(CONFIG_BLK_DEV_HD)
	hd_init();
#endif
#ifdef CONFIG_BLK_DEV_PS2
	ps2esdi_init();
#endif
#ifdef CONFIG_BLK_DEV_XD
	xd_init();
#endif
#ifdef CONFIG_BLK_DEV_MFM
	mfm_init();
#endif
#ifdef CONFIG_PARIDE
	{ extern void paride_init(void); paride_init(); };
#endif
#ifdef CONFIG_MAC_FLOPPY
	swim3_init();
#endif
#ifdef CONFIG_BLK_DEV_SWIM_IOP
	swimiop_init();
#endif
#ifdef CONFIG_AMIGA_FLOPPY
	amiga_floppy_init();
#endif
#ifdef CONFIG_ATARI_FLOPPY
	atari_floppy_init();
#endif
#ifdef CONFIG_BLK_DEV_FD
	floppy_init();
#else
#if defined(__i386__)	/* Do we even need this? */
	outb_p(0xc, 0x3f2);
#endif
#endif
#ifdef CONFIG_CDU31A
	cdu31a_init();
#endif
#ifdef CONFIG_ATARI_ACSI
	acsi_init();
#endif
#ifdef CONFIG_MCD
	mcd_init();
#endif
#ifdef CONFIG_MCDX
	mcdx_init();
#endif
#ifdef CONFIG_SBPCD
	sbpcd_init();
#endif
#ifdef CONFIG_AZTCD
	aztcd_init();
#endif
#ifdef CONFIG_CDU535
	sony535_init();
#endif
#ifdef CONFIG_GSCD
	gscd_init();
#endif
#ifdef CONFIG_CM206
	cm206_init();
#endif
#ifdef CONFIG_OPTCD
	optcd_init();
#endif
#ifdef CONFIG_SJCD
	sjcd_init();
#endif
#ifdef CONFIG_APBLOCK
	ap_init();
#endif
#ifdef CONFIG_DDV
	ddv_init();
#endif
#ifdef CONFIG_BLK_DEV_NBD
	nbd_init();
#endif
#ifdef CONFIG_MDISK
	mdisk_init();
#endif
#ifdef CONFIG_DASD
	dasd_init();
#endif
#ifdef CONFIG_SUN_JSFLASH
	jsfd_init();
#endif
#ifdef CONFIG_BLK_DEV_LVM
	lvm_init();
#endif
	return 0;
};

EXPORT_SYMBOL(io_request_lock);
EXPORT_SYMBOL(end_that_request_first);
EXPORT_SYMBOL(end_that_request_last);
EXPORT_SYMBOL(blk_init_queue);
EXPORT_SYMBOL(blk_get_queue);
EXPORT_SYMBOL(blk_cleanup_queue);
EXPORT_SYMBOL(blk_queue_headactive);
EXPORT_SYMBOL(blk_queue_pluggable);
EXPORT_SYMBOL(blk_queue_make_request);
EXPORT_SYMBOL(generic_make_request);
EXPORT_SYMBOL(blkdev_release_request);
