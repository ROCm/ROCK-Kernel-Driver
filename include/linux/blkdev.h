#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#include <linux/major.h>
#include <linux/sched.h>
#include <linux/genhd.h>
#include <linux/tqueue.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>

#include <asm/scatterlist.h>

struct request_queue;
typedef struct request_queue request_queue_t;
struct elevator_s;
typedef struct elevator_s elevator_t;

struct request_list {
	unsigned int count;
	struct list_head free;
	wait_queue_head_t wait;
};

struct request {
	struct list_head queuelist; /* looking for ->queue? you must _not_
				     * access it directly, use
				     * blkdev_dequeue_request! */
	void *elevator_private;

	unsigned char cmd[16];

	unsigned long flags;		/* see REQ_ bits below */

	int rq_status;	/* should split this into a few status bits */
	kdev_t rq_dev;
	int errors;
	sector_t sector;
	unsigned long nr_sectors;
	unsigned long hard_sector;	/* the hard_* are block layer
					 * internals, no driver should
					 * touch them
					 */
	unsigned long hard_nr_sectors;

	/* Number of scatter-gather DMA addr+len pairs after
	 * physical address coalescing is performed.
	 */
	unsigned short nr_phys_segments;

	/* Number of scatter-gather addr+len pairs after
	 * physical and DMA remapping hardware coalescing is performed.
	 * This is the number of scatter-gather entries the driver
	 * will actually have to deal with after DMA mapping is done.
	 */
	unsigned short nr_hw_segments;

	unsigned int current_nr_sectors;
	unsigned int hard_cur_sectors;
	int tag;
	void *special;
	char *buffer;
	struct completion *waiting;
	struct bio *bio, *biotail;
	request_queue_t *q;
	struct request_list *rl;
};

/*
 * first three bits match BIO_RW* bits, important
 */
enum rq_flag_bits {
	__REQ_RW,	/* not set, read. set, write */
	__REQ_RW_AHEAD,	/* READA */
	__REQ_BARRIER,	/* may not be passed */
	__REQ_CMD,	/* is a regular fs rw request */
	__REQ_NOMERGE,	/* don't touch this for merging */
	__REQ_STARTED,	/* drive already may have started this one */
	__REQ_DONTPREP,	/* don't call prep for this one */
	__REQ_QUEUED,	/* uses queueing */
	/*
	 * for ATA/ATAPI devices
	 */
	__REQ_PC,	/* packet command (special) */
	__REQ_BLOCK_PC,	/* queued down pc from block layer */
	__REQ_SENSE,	/* sense retrival */

	__REQ_SPECIAL,	/* driver suplied command */

	__REQ_NR_BITS,	/* stops here */
};

#define REQ_RW		(1 << __REQ_RW)
#define REQ_RW_AHEAD	(1 << __REQ_RW_AHEAD)
#define REQ_BARRIER	(1 << __REQ_BARRIER)
#define REQ_CMD		(1 << __REQ_CMD)
#define REQ_NOMERGE	(1 << __REQ_NOMERGE)
#define REQ_STARTED	(1 << __REQ_STARTED)
#define REQ_DONTPREP	(1 << __REQ_DONTPREP)
#define REQ_QUEUED	(1 << __REQ_QUEUED)
#define REQ_PC		(1 << __REQ_PC)
#define REQ_BLOCK_PC	(1 << __REQ_BLOCK_PC)
#define REQ_SENSE	(1 << __REQ_SENSE)
#define REQ_SPECIAL	(1 << __REQ_SPECIAL)

#include <linux/elevator.h>

typedef int (merge_request_fn) (request_queue_t *, struct request *,
				struct bio *);
typedef int (merge_requests_fn) (request_queue_t *, struct request *,
				 struct request *);
typedef void (request_fn_proc) (request_queue_t *q);
typedef request_queue_t * (queue_proc) (kdev_t dev);
typedef int (make_request_fn) (request_queue_t *q, struct bio *bio);
typedef int (prep_rq_fn) (request_queue_t *, struct request *);
typedef void (unplug_fn) (void *q);

enum blk_queue_state {
	Queue_down,
	Queue_up,
};

#define BLK_TAGS_PER_LONG	(sizeof(unsigned long) * 8)
#define BLK_TAGS_MASK		(BLK_TAGS_PER_LONG - 1)

struct blk_queue_tag {
	struct request **tag_index;	/* map of busy tags */
	unsigned long *tag_map;		/* bit map of free/busy tags */
	struct list_head busy_list;	/* fifo list of busy tags */
	int busy;			/* current depth */
	int max_depth;
};

/*
 * Default nr free requests per queue, ll_rw_blk will scale it down
 * according to available RAM at init time
 */
#define QUEUE_NR_REQUESTS	8192

struct request_queue
{
	/*
	 * Together with queue_head for cacheline sharing
	 */
	struct list_head	queue_head;
	struct list_head	*last_merge;
	elevator_t		elevator;

	/*
	 * the queue request freelist, one for reads and one for writes
	 */
	struct request_list	rq[2];

	request_fn_proc		*request_fn;
	merge_request_fn	*back_merge_fn;
	merge_request_fn	*front_merge_fn;
	merge_requests_fn	*merge_requests_fn;
	make_request_fn		*make_request_fn;
	prep_rq_fn		*prep_rq_fn;
	unplug_fn		*unplug_fn;

	struct backing_dev_info	backing_dev_info;

	/*
	 * The queue owner gets to use this for whatever they like.
	 * ll_rw_blk doesn't touch it.
	 */
	void			*queuedata;

	/*
	 * queue needs bounce pages for pages above this limit
	 */
	unsigned long		bounce_pfn;
	int			bounce_gfp;

	struct list_head	plug_list;

	/*
	 * various queue flags, see QUEUE_* below
	 */
	unsigned long		queue_flags;

	/*
	 * protects queue structures from reentrancy
	 */
	spinlock_t		*queue_lock;

	/*
	 * queue settings
	 */
	unsigned short		max_sectors;
	unsigned short		max_phys_segments;
	unsigned short		max_hw_segments;
	unsigned short		hardsect_size;
	unsigned int		max_segment_size;

	unsigned long		seg_boundary_mask;

	wait_queue_head_t	queue_wait;

	struct blk_queue_tag	*queue_tags;
};

#define RQ_INACTIVE		(-1)
#define RQ_ACTIVE		1
#define RQ_SCSI_BUSY		0xffff
#define RQ_SCSI_DONE		0xfffe
#define RQ_SCSI_DISCONNECTING	0xffe0

#define QUEUE_FLAG_CLUSTER	0	/* cluster several segments into 1 */
#define QUEUE_FLAG_QUEUED	1	/* uses generic tag queueing */
#define QUEUE_FLAG_STOPPED	2	/* queue is stopped */

#define blk_queue_plugged(q)	!list_empty(&(q)->plug_list)
#define blk_queue_tagged(q)	test_bit(QUEUE_FLAG_QUEUED, &(q)->queue_flags)
#define blk_queue_empty(q)	elv_queue_empty(q)
#define list_entry_rq(ptr)	list_entry((ptr), struct request, queuelist)

#define rq_data_dir(rq)		((rq)->flags & 1)

/*
 * mergeable request must not have _NOMERGE or _BARRIER bit set, nor may
 * it already be started by driver.
 */
#define rq_mergeable(rq)	\
	(!((rq)->flags & (REQ_NOMERGE | REQ_STARTED | REQ_BARRIER))	\
	&& ((rq)->flags & REQ_CMD))

/*
 * noop, requests are automagically marked as active/inactive by I/O
 * scheduler -- see elv_next_request
 */
#define blk_queue_headactive(q, head_active)

extern unsigned long blk_max_low_pfn, blk_max_pfn;

/*
 * standard bounce addresses:
 *
 * BLK_BOUNCE_HIGH	: bounce all highmem pages
 * BLK_BOUNCE_ANY	: don't bounce anything
 * BLK_BOUNCE_ISA	: bounce pages above ISA DMA boundary
 */
#define BLK_BOUNCE_HIGH		(blk_max_low_pfn << PAGE_SHIFT)
#define BLK_BOUNCE_ANY		(blk_max_pfn << PAGE_SHIFT)
#define BLK_BOUNCE_ISA		(ISA_DMA_THRESHOLD)

extern int init_emergency_isa_pool(void);
void blk_queue_bounce(request_queue_t *q, struct bio **bio);

#define rq_for_each_bio(bio, rq)	\
	if ((rq->bio))			\
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
extern inline request_queue_t *bdev_get_queue(struct block_device *bdev);
extern void blkdev_release_request(struct request *);
extern void blk_attempt_remerge(request_queue_t *, struct request *);
extern void __blk_attempt_remerge(request_queue_t *, struct request *);
extern struct request *blk_get_request(request_queue_t *, int, int);
extern struct request *__blk_get_request(request_queue_t *, int);
extern void blk_put_request(struct request *);
extern void blk_plug_device(request_queue_t *);
extern int blk_remove_plug(request_queue_t *);
extern void blk_recount_segments(request_queue_t *, struct bio *);
extern inline int blk_phys_contig_segment(request_queue_t *q, struct bio *, struct bio *);
extern inline int blk_hw_contig_segment(request_queue_t *q, struct bio *, struct bio *);
extern int block_ioctl(struct block_device *, unsigned int, unsigned long);
extern void blk_start_queue(request_queue_t *q);
extern void blk_stop_queue(request_queue_t *q);
extern void __blk_stop_queue(request_queue_t *q);

/*
 * get ready for proper ref counting
 */
#define blk_put_queue(q)	do { } while (0)

/*
 * Access functions for manipulating queue properties
 */
extern int blk_init_queue(request_queue_t *, request_fn_proc *, spinlock_t *);
extern void blk_cleanup_queue(request_queue_t *);
extern void blk_queue_make_request(request_queue_t *, make_request_fn *);
extern void blk_queue_bounce_limit(request_queue_t *, u64);
extern void blk_queue_max_sectors(request_queue_t *q, unsigned short);
extern void blk_queue_max_phys_segments(request_queue_t *q, unsigned short);
extern void blk_queue_max_hw_segments(request_queue_t *q, unsigned short);
extern void blk_queue_max_segment_size(request_queue_t *q, unsigned int);
extern void blk_queue_hardsect_size(request_queue_t *q, unsigned short);
extern void blk_queue_segment_boundary(request_queue_t *q, unsigned long);
extern void blk_queue_assign_lock(request_queue_t *q, spinlock_t *);
extern void blk_queue_prep_rq(request_queue_t *q, prep_rq_fn *pfn);
extern struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev);

extern int blk_rq_map_sg(request_queue_t *, struct request *, struct scatterlist *);
extern void blk_dump_rq_flags(struct request *, char *);
extern void generic_unplug_device(void *);

/*
 * tag stuff
 */
#define blk_queue_tag_depth(q)		((q)->queue_tags->busy)
#define blk_queue_tag_queue(q)		((q)->queue_tags->busy < (q)->queue_tags->max_depth)
#define blk_rq_tagged(rq)		((rq)->flags & REQ_QUEUED)
extern int blk_queue_start_tag(request_queue_t *, struct request *);
extern struct request *blk_queue_find_tag(request_queue_t *, int);
extern void blk_queue_end_tag(request_queue_t *, struct request *);
extern int blk_queue_init_tags(request_queue_t *, int);
extern void blk_queue_free_tags(request_queue_t *);
extern void blk_queue_invalidate_tags(request_queue_t *);

extern int * blk_size[MAX_BLKDEV];	/* in units of 1024 bytes */

#define MAX_PHYS_SEGMENTS 128
#define MAX_HW_SEGMENTS 128
#define MAX_SECTORS 255

#define MAX_SEGMENT_SIZE	65536

#define blkdev_entry_to_request(entry) list_entry((entry), struct request, queuelist)

extern void drive_stat_acct(struct request *, int, int);

extern inline void blk_clear(int major)
{
	blk_size[major] = NULL;
#if 0
	blk_size_in_bytes[major] = NULL;
#endif
}

extern inline int queue_hardsect_size(request_queue_t *q)
{
	int retval = 512;

	if (q && q->hardsect_size)
		retval = q->hardsect_size;

	return retval;
}

extern inline int bdev_hardsect_size(struct block_device *bdev)
{
	return queue_hardsect_size(bdev_get_queue(bdev));
}

#define blk_finished_io(nsects)	do { } while (0)
#define blk_started_io(nsects)	do { } while (0)

/* assumes size > 256 */
extern inline unsigned int blksize_bits(unsigned int size)
{
	unsigned int bits = 8;
	do {
		bits++;
		size >>= 1;
	} while (size > 256);
	return bits;
}

extern inline unsigned int block_size(struct block_device *bdev)
{
	return bdev->bd_block_size;
}

typedef struct {struct page *v;} Sector;

unsigned char *read_dev_sector(struct block_device *, unsigned long, Sector *);

static inline void put_dev_sector(Sector p)
{
	page_cache_release(p.v);
}

#endif
