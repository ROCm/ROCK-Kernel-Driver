#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#include <linux/major.h>
#include <linux/sched.h>
#include <linux/genhd.h>
#include <linux/tqueue.h>
#include <linux/list.h>

struct request_queue;
typedef struct request_queue request_queue_t;
struct elevator_s;
typedef struct elevator_s elevator_t;

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and the semaphore is used to wait
 * for read/write completion.
 */
struct request {
	struct list_head queue;
	int elevator_sequence;
	struct list_head table;

	struct list_head *free_list;

	volatile int rq_status;	/* should split this into a few status bits */
#define RQ_INACTIVE		(-1)
#define RQ_ACTIVE		1
#define RQ_SCSI_BUSY		0xffff
#define RQ_SCSI_DONE		0xfffe
#define RQ_SCSI_DISCONNECTING	0xffe0

	kdev_t rq_dev;
	int cmd;		/* READ or WRITE */
	int errors;
	unsigned long sector;
	unsigned long nr_sectors;
	unsigned long hard_sector, hard_nr_sectors;
	unsigned int nr_segments;
	unsigned int nr_hw_segments;
	unsigned long current_nr_sectors;
	void * special;
	char * buffer;
	struct semaphore * sem;
	struct buffer_head * bh;
	struct buffer_head * bhtail;
	request_queue_t *q;
	elevator_t *e;
};

#include <linux/elevator.h>

typedef int (merge_request_fn) (request_queue_t *q, 
				struct request  *req,
				struct buffer_head *bh,
				int);
typedef int (merge_requests_fn) (request_queue_t *q, 
				 struct request  *req,
				 struct request  *req2,
				 int);
typedef void (request_fn_proc) (request_queue_t *q);
typedef request_queue_t * (queue_proc) (kdev_t dev);
typedef int (make_request_fn) (request_queue_t *q, int rw, struct buffer_head *bh);
typedef void (plug_device_fn) (request_queue_t *q, kdev_t device);
typedef void (unplug_device_fn) (void *q);

/*
 * Default nr free requests per queue
 */
#define QUEUE_NR_REQUESTS	256

struct request_queue
{
	/*
	 * the queue request freelist, one for reads and one for writes
	 */
	struct list_head	request_freelist[2];

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
	plug_device_fn		* plug_device_fn;
	/*
	 * The queue owner gets to use this for whatever they like.
	 * ll_rw_blk doesn't touch it.
	 */
	void			* queuedata;

	/*
	 * This is used to remove the plug when tq_disk runs.
	 */
	struct tq_struct	plug_tq;

	/*
	 * Boolean that indicates whether this queue is plugged or not.
	 */
	char			plugged;

	/*
	 * Boolean that indicates whether current_request is active or
	 * not.
	 */
	char			head_active;

	/*
	 * Is meant to protect the queue in the future instead of
	 * io_request_lock
	 */
	spinlock_t		request_lock;

	/*
	 * Tasks wait here for free request
	 */
	wait_queue_head_t	wait_for_request;
};

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
extern void grok_partitions(struct gendisk *dev, int drive, unsigned minors, long size);
extern void register_disk(struct gendisk *dev, kdev_t first, unsigned minors, struct block_device_operations *ops, long size);
extern void generic_make_request(int rw, struct buffer_head * bh);
extern request_queue_t *blk_get_queue(kdev_t dev);
extern void blkdev_release_request(struct request *);

/*
 * Access functions for manipulating queue properties
 */
extern void blk_init_queue(request_queue_t *, request_fn_proc *);
extern void blk_cleanup_queue(request_queue_t *);
extern void blk_queue_headactive(request_queue_t *, int);
extern void blk_queue_pluggable(request_queue_t *, plug_device_fn *);
extern void blk_queue_make_request(request_queue_t *, make_request_fn *);

extern int * blk_size[MAX_BLKDEV];

extern int * blksize_size[MAX_BLKDEV];

extern int * hardsect_size[MAX_BLKDEV];

extern int * max_readahead[MAX_BLKDEV];

extern int * max_sectors[MAX_BLKDEV];

extern int * max_segments[MAX_BLKDEV];

#define MAX_SECTORS 254

#define MAX_SEGMENTS MAX_SECTORS

#define PageAlignSize(size) (((size) + PAGE_SIZE -1) & PAGE_MASK)

/* read-ahead in pages.. */
#define MAX_READAHEAD	31
#define MIN_READAHEAD	3

#define blkdev_entry_to_request(entry) list_entry((entry), struct request, queue)
#define blkdev_entry_next_request(entry) blkdev_entry_to_request((entry)->next)
#define blkdev_entry_prev_request(entry) blkdev_entry_to_request((entry)->prev)
#define blkdev_next_request(req) blkdev_entry_to_request((req)->queue.next)
#define blkdev_prev_request(req) blkdev_entry_to_request((req)->queue.prev)

extern void drive_stat_acct (kdev_t dev, int rw,
					unsigned long nr_sectors, int new_io);

static inline int get_hardsect_size(kdev_t dev)
{
	extern int *hardsect_size[];
	if (hardsect_size[MAJOR(dev)] != NULL)
		return hardsect_size[MAJOR(dev)][MINOR(dev)];
	else
		return 512;
}


#endif
