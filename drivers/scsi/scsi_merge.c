/*
 *  scsi_merge.c Copyright (C) 1999 Eric Youngdale
 *
 *  SCSI queueing library.
 *      Initial versions: Eric Youngdale (eric@andante.org).
 *                        Based upon conversations with large numbers
 *                        of people at Linux Expo.
 *	Support for dynamic DMA mapping: Jakub Jelinek (jakub@redhat.com).
 *	Support for highmem I/O: Jens Axboe <axboe@suse.de>
 */

/*
 * This file contains queue management functions that are used by SCSI.
 * Typically this is used for several purposes.   First, we need to ensure
 * that commands do not grow so large that they cannot be handled all at
 * once by a host adapter.   The various flavors of merge functions included
 * here serve this purpose.
 *
 * Note that it would be quite trivial to allow the low-level driver the
 * flexibility to define it's own queue handling functions.  For the time
 * being, the hooks are not present.   Right now we are just using the
 * data in the host template as an indicator of how we should be handling
 * queues, and we select routines that are optimized for that purpose.
 *
 * Some hosts do not impose any restrictions on the size of a request.
 * In such cases none of the merge functions in this file are called,
 * and we allow ll_rw_blk to merge requests in the default manner.
 * This isn't guaranteed to be optimal, but it should be pretty darned
 * good.   If someone comes up with ideas of better ways of managing queues
 * to improve on the default behavior, then certainly fit it into this
 * scheme in whatever manner makes the most sense.   Please note that
 * since each device has it's own queue, we have considerable flexibility
 * in queue management.
 */

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>


#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/io.h>

#include "scsi.h"
#include "hosts.h"
#include "constants.h"
#include <scsi/scsi_ioctl.h>

/*
 * This means that bounce buffers cannot be allocated in chunks > PAGE_SIZE.
 * Ultimately we should get away from using a dedicated DMA bounce buffer
 * pool, and we should instead try and use kmalloc() instead.  If we can
 * eliminate this pool, then this restriction would no longer be needed.
 */
#define DMA_SEGMENT_SIZE_LIMITED

static void dma_exhausted(Scsi_Cmnd * SCpnt, int i)
{
	int jj;
	struct scatterlist *sgpnt;
	void **bbpnt;
	int consumed = 0;

	sgpnt = (struct scatterlist *) SCpnt->request_buffer;
	bbpnt = SCpnt->bounce_buffers;

	/*
	 * Now print out a bunch of stats.  First, start with the request
	 * size.
	 */
	printk("dma_free_sectors:%d\n", scsi_dma_free_sectors);
	printk("use_sg:%d\ti:%d\n", SCpnt->use_sg, i);
	printk("request_bufflen:%d\n", SCpnt->request_bufflen);
	/*
	 * Now dump the scatter-gather table, up to the point of failure.
	 */
	for(jj=0; jj < SCpnt->use_sg; jj++)
	{
		printk("[%d]\tlen:%d\taddr:%p\tbounce:%p\n",
		       jj,
		       sgpnt[jj].length,
		       sgpnt[jj].address,
		       (bbpnt ? bbpnt[jj] : NULL));
		if (bbpnt && bbpnt[jj])
			consumed += sgpnt[jj].length;
	}
	printk("Total %d sectors consumed\n", consumed);
	panic("DMA pool exhausted");
}

/*
 * This entire source file deals with the new queueing code.
 */

/*
 * Function:    __count_segments()
 *
 * Purpose:     Prototype for queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              dma_host - 1 if this host has ISA DMA issues (bus doesn't
 *                      expose all of the address lines, so that DMA cannot
 *                      be done from an arbitrary address).
 *		remainder - used to track the residual size of the last
 *			segment.  Comes in handy when we want to limit the 
 *			size of bounce buffer segments to PAGE_SIZE.
 *
 * Returns:     Count of the number of SG segments for the request.
 *
 * Lock status: 
 *
 * Notes:       This is only used for diagnostic purposes.
 */
__inline static int __count_segments(struct request *req,
				     int dma_host,
				     int * remainder)
{
	int ret = 1;
	int reqsize = 0;
	int i;
	struct bio *bio;
	struct bio_vec *bvec;

	if (remainder)
		reqsize = *remainder;

	/*
	 * Add in the size increment for the first buffer.
	 */
	bio = req->bio;
#ifdef DMA_SEGMENT_SIZE_LIMITED
	if (reqsize + bio->bi_size > PAGE_SIZE)
		ret++;
#endif

	rq_for_each_bio(bio, req) {
		bio_for_each_segment(bvec, bio, i)
			ret++;

		reqsize += bio->bi_size;
	}

	if (remainder)
		*remainder = reqsize;

	return ret;
}

/*
 * Function:    recount_segments()
 *
 * Purpose:     Recount the number of scatter-gather segments for this request.
 *
 * Arguments:   req     - request that needs recounting.
 *
 * Returns:     Count of the number of SG segments for the request.
 *
 * Lock status: Irrelevant.
 *
 * Notes:	This is only used when we have partially completed requests
 *		and the bit that is leftover is of an indeterminate size.
 *		This can come up if you get a MEDIUM_ERROR, for example,
 *		as we will have "completed" all of the sectors up to and
 *		including the bad sector, and the leftover bit is what
 *		we have to do now.  This tends to be a rare occurrence, so
 *		we aren't busting our butts to instantiate separate versions
 *		of this function for the 4 different flag values.  We
 *		probably should, however.
 */
void
recount_segments(Scsi_Cmnd * SCpnt)
{
	struct request *req = &SCpnt->request;
	struct Scsi_Host *SHpnt = SCpnt->host;

	req->nr_segments = __count_segments(req, SHpnt->unchecked_isa_dma,NULL);
}

/*
 * IOMMU hackery for sparc64
 */
#ifdef DMA_CHUNK_SIZE

#define MERGEABLE_BUFFERS(X,Y) \
	((((bvec_to_phys(__BVEC_END((X))) + __BVEC_END((X))->bv_len) | bio_to_phys((Y))) & (DMA_CHUNK_SIZE - 1)) == 0)

static inline int scsi_new_mergeable(request_queue_t * q,
				     struct request * req,
				     struct bio *bio)
{
	int nr_segs = bio_hw_segments(q, bio);

	/*
	 * pci_map_sg will be able to merge these two
	 * into a single hardware sg entry, check if
	 * we'll have enough memory for the sg list.
	 * scsi.c allocates for this purpose
	 * min(64,sg_tablesize) entries.
	 */
	if (req->nr_segments + nr_segs > q->max_segments)
		return 0;

	req->nr_segments += nr_segs;
	return 1;
}

static inline int scsi_new_segment(request_queue_t * q,
				   struct request * req,
				   struct bio *bio)
{
	int nr_segs = bio_hw_segments(q, bio);
	/*
	 * pci_map_sg won't be able to map these two
	 * into a single hardware sg entry, so we have to
	 * check if things fit into sg_tablesize.
	 */
	if (req->nr_hw_segments + nr_segs > q->max_segments)
		return 0;
	else if (req->nr_segments + nr_segs > q->max_segments)
		return 0;

	req->nr_hw_segments += nr_segs;
	req->nr_segments += nr_segs;
	return 1;
}

#else /* DMA_CHUNK_SIZE */

static inline int scsi_new_segment(request_queue_t * q,
				   struct request * req,
				   struct bio *bio)
{
	int nr_segs = bio_hw_segments(q, bio);

	if (req->nr_segments + nr_segs > q->max_segments) {
		req->flags |= REQ_NOMERGE;
		return 0;
	}

	/*
	 * This will form the start of a new segment.  Bump the 
	 * counter.
	 */
	req->nr_segments += nr_segs;
	return 1;
}
#endif /* DMA_CHUNK_SIZE */

/*
 * Function:    __scsi_merge_fn()
 *
 * Purpose:     Prototype for queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              bio     - Block which we may wish to merge into request
 *              dma_host - 1 if this host has ISA DMA issues (bus doesn't
 *                      expose all of the address lines, so that DMA cannot
 *                      be done from an arbitrary address).
 *
 * Returns:     1 if it is OK to merge the block into the request.  0
 *              if it is not OK.
 *
 * Lock status: queue lock is assumed to be held here.
 *
 * Notes:       Some drivers have limited scatter-gather table sizes, and
 *              thus they cannot queue an infinitely large command.  This
 *              function is called from ll_rw_blk before it attempts to merge
 *              a new block into a request to make sure that the request will
 *              not become too large.
 *
 *              This function is not designed to be directly called.  Instead
 *              it should be referenced from other functions where the
 *              dma_host parameter should be an integer constant. The
 *              compiler should thus be able to properly optimize the code,
 *              eliminating stuff that is irrelevant.
 *              It is more maintainable to do this way with a single function
 *              than to have 4 separate functions all doing roughly the
 *              same thing.
 */
__inline static int __scsi_back_merge_fn(request_queue_t * q,
					 struct request *req,
					 struct bio *bio)
{
	if (req->nr_sectors + bio_sectors(bio) > q->max_sectors) {
		req->flags |= REQ_NOMERGE;
		return 0;
	}

#ifdef DMA_CHUNK_SIZE
	if (MERGEABLE_BUFFERS(bio, req->bio))
		return scsi_new_mergeable(q, req, bio);
#endif

	return scsi_new_segment(q, req, bio);
}

__inline static int __scsi_front_merge_fn(request_queue_t * q,
					  struct request *req,
					  struct bio *bio)
{
	if (req->nr_sectors + bio_sectors(bio) > q->max_sectors) {
		req->flags |= REQ_NOMERGE;
		return 0;
	}

#ifdef DMA_CHUNK_SIZE
	if (MERGEABLE_BUFFERS(bio, req->bio))
		return scsi_new_mergeable(q, req, bio);
#endif
	return scsi_new_segment(q, req, bio);
}

/*
 * Function:    scsi_merge_fn_()
 *
 * Purpose:     queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              bio     - Block which we may wish to merge into request
 *
 * Returns:     1 if it is OK to merge the block into the request.  0
 *              if it is not OK.
 *
 * Lock status: queue lock is assumed to be held here.
 *
 * Notes:       Optimized for different cases depending upon whether
 *              ISA DMA is in use and whether clustering should be used.
 */
#define MERGEFCT(_FUNCTION, _BACK_FRONT)				\
static int _FUNCTION(request_queue_t * q,				\
		     struct request * req,				\
		     struct bio *bio)					\
{									\
    int ret;								\
    ret =  __scsi_ ## _BACK_FRONT ## _merge_fn(q,			\
					       req,			\
					       bio);			\
    return ret;								\
}

MERGEFCT(scsi_back_merge_fn, back)
MERGEFCT(scsi_front_merge_fn, front)

/*
 * Function:    scsi_merge_requests_fn_()
 *
 * Purpose:     queue merge function.
 *
 * Arguments:   q       - Queue for which we are merging request.
 *              req     - request into which we wish to merge.
 *              next    - Block which we may wish to merge into request
 *
 * Returns:     1 if it is OK to merge the block into the request.  0
 *              if it is not OK.
 *
 * Lock status: queue lock is assumed to be held here.
 *
 */
inline static int scsi_merge_requests_fn(request_queue_t * q,
					 struct request *req,
					 struct request *next)
{
	int bio_segs;

	/*
	 * First check if the either of the requests are re-queued
	 * requests.  Can't merge them if they are.
	 */
	if (req->special || next->special)
		return 0;

	/*
	 * will become to large?
	 */
	if ((req->nr_sectors + next->nr_sectors) > q->max_sectors)
		return 0;

	bio_segs = req->nr_segments + next->nr_segments;
	if (blk_contig_segment(q, req->biotail, next->bio))
		bio_segs--;

	/*
	 * exceeds our max allowed segments?
	 */
	if (bio_segs > q->max_segments)
		return 0;

#ifdef DMA_CHUNK_SIZE
	bio_segs = req->nr_hw_segments + next->nr_hw_segments;
	if (blk_contig_segment(q, req->biotail, next->bio))
		bio_segs--;

	/* If dynamic DMA mapping can merge last segment in req with
	 * first segment in next, then the check for hw segments was
	 * done above already, so we can always merge.
	 */
	if (bio_segs > q->max_segments)
		return 0;

	req->nr_hw_segments = bio_segs;
#endif

	/*
	 * This will form the start of a new segment.  Bump the 
	 * counter.
	 */
	req->nr_segments = bio_segs;
	return 1;
}

/*
 * Function:    __init_io()
 *
 * Purpose:     Prototype for io initialize function.
 *
 * Arguments:   SCpnt   - Command descriptor we wish to initialize
 *              sg_count_valid  - 1 if the sg count in the req is valid.
 *              dma_host - 1 if this host has ISA DMA issues (bus doesn't
 *                      expose all of the address lines, so that DMA cannot
 *                      be done from an arbitrary address).
 *
 * Returns:     1 on success.
 *
 * Lock status: 
 *
 * Notes:       Only the SCpnt argument should be a non-constant variable.
 *              This function is designed in such a way that it will be
 *              invoked from a series of small stubs, each of which would
 *              be optimized for specific circumstances.
 *
 *              The advantage of this is that hosts that don't do DMA
 *              get versions of the function that essentially don't have
 *              any of the DMA code.  Same goes for clustering - in the
 *              case of hosts with no need for clustering, there is no point
 *              in a whole bunch of overhead.
 *
 *              Finally, in the event that a host has set can_queue to SG_ALL
 *              implying that there is no limit to the length of a scatter
 *              gather list, the sg count in the request won't be valid
 *              (mainly because we don't need queue management functions
 *              which keep the tally uptodate.
 */
__inline static int __init_io(Scsi_Cmnd * SCpnt,
			      int sg_count_valid,
			      int dma_host)
{
	struct bio	   * bio;
	char		   * buff;
	int		     count;
	int		     i;
	struct request     * req;
	int		     sectors;
	struct scatterlist * sgpnt;
	int		     this_count;
	void		   ** bbpnt;

	req = &SCpnt->request;

	/*
	 * First we need to know how many scatter gather segments are needed.
	 */
	if (!sg_count_valid) {
		count = __count_segments(req, dma_host, NULL);
	} else {
		count = req->nr_segments;
	}

	/*
	 * If the dma pool is nearly empty, then queue a minimal request
	 * with a single segment.  Typically this will satisfy a single
	 * buffer.
	 */
	if (dma_host && scsi_dma_free_sectors <= 10) {
		this_count = req->current_nr_sectors;
		goto single_segment;
	}

	/*
	 * we used to not use scatter-gather for single segment request,
	 * but now we do (it makes highmem I/O easier to support without
	 * kmapping pages)
	 */
	SCpnt->use_sg = count;

	/* 
	 * Allocate the actual scatter-gather table itself.
	 */
	SCpnt->sglist_len = (SCpnt->use_sg * sizeof(struct scatterlist));

	/* If we could potentially require ISA bounce buffers, allocate
	 * space for this array here.
	 */
	if (dma_host)
		SCpnt->sglist_len += (SCpnt->use_sg * sizeof(void *));

	/* scsi_malloc can only allocate in chunks of 512 bytes so
	 * round it up.
	 */
	SCpnt->sglist_len = (SCpnt->sglist_len + 511) & ~511;
 
	sgpnt = (struct scatterlist *) scsi_malloc(SCpnt->sglist_len);

	if (!sgpnt) {
		struct Scsi_Host *SHpnt = SCpnt->host;

		/*
		 * If we cannot allocate the scatter-gather table, then
		 * simply write the first buffer all by itself.
		 */
		printk("Warning - running *really* short on DMA buffers\n");
		this_count = req->current_nr_sectors;
		printk("SCSI: depth is %d, # segs %d, # hw segs %d\n", SHpnt->host_busy, req->nr_segments, req->nr_hw_segments);
		goto single_segment;
	}

	memset(sgpnt, 0, SCpnt->sglist_len);
	SCpnt->request_buffer = (char *) sgpnt;
	SCpnt->request_bufflen = 0;
	req->buffer = NULL;

	if (dma_host)
		bbpnt = (void **) ((char *)sgpnt +
			 (SCpnt->use_sg * sizeof(struct scatterlist)));
	else
		bbpnt = NULL;

	SCpnt->bounce_buffers = bbpnt;

	/* 
	 * Next, walk the list, and fill in the addresses and sizes of
	 * each segment.
	 */
	SCpnt->request_bufflen = req->nr_sectors << 9;
	count = blk_rq_map_sg(req->q, req, SCpnt->request_buffer);

	/*
	 * Verify that the count is correct.
	 */
	if (count > SCpnt->use_sg) {
		printk("Incorrect number of segments after building list\n");
		printk("counted %d, received %d\n", count, SCpnt->use_sg);
		printk("req nr_sec %lu, cur_nr_sec %u\n", req->nr_sectors, req->current_nr_sectors);
		scsi_free(SCpnt->request_buffer, SCpnt->sglist_len);
		this_count = req->current_nr_sectors;
		goto single_segment;
	}

	SCpnt->use_sg = count;

	if (!dma_host)
		return 1;

	/*
	 * Now allocate bounce buffers, if needed.
	 */
	SCpnt->request_bufflen = 0;
	for (i = 0; i < count; i++) {
		sectors = (sgpnt[i].length >> 9);
		SCpnt->request_bufflen += sgpnt[i].length;
		if (virt_to_phys(sgpnt[i].address) + sgpnt[i].length - 1 >
                    ISA_DMA_THRESHOLD) {
			if( scsi_dma_free_sectors - sectors <= 10  ) {
				/*
				 * If this would nearly drain the DMA
				 * pool empty, then let's stop here.
				 * Don't make this request any larger.
				 * This is kind of a safety valve that
				 * we use - we could get screwed later
				 * on if we run out completely.  
				 */
				SCpnt->request_bufflen -= sgpnt[i].length;
				SCpnt->use_sg = i;
				if (i == 0) {
					goto big_trouble;
				}
				break;
			}

			/*
			 * this is not a dma host, so it will never
			 * be a highmem page
			 */
			bbpnt[i] = page_address(sgpnt[i].page) +sgpnt[i].offset;
			sgpnt[i].address = (char *)scsi_malloc(sgpnt[i].length);
			/*
			 * If we cannot allocate memory for this DMA bounce
			 * buffer, then queue just what we have done so far.
			 */
			if (sgpnt[i].address == NULL) {
				printk("Warning - running low on DMA memory\n");
				SCpnt->request_bufflen -= sgpnt[i].length;
				SCpnt->use_sg = i;
				if (i == 0) {
					goto big_trouble;
				}
				break;
			}
			if (rq_data_dir(req) == WRITE)
				memcpy(sgpnt[i].address, bbpnt[i],
				       sgpnt[i].length);
		}
	}
	return 1;

      big_trouble:
	/*
	 * We come here in the event that we get one humongous
	 * request, where we need a bounce buffer, and the buffer is
	 * more than we can allocate in a single call to
	 * scsi_malloc().  In addition, we only come here when it is
	 * the 0th element of the scatter-gather table that gets us
	 * into this trouble.  As a fallback, we fall back to
	 * non-scatter-gather, and ask for a single segment.  We make
	 * a half-hearted attempt to pick a reasonably large request
	 * size mainly so that we don't thrash the thing with
	 * iddy-biddy requests.
	 */

	/*
	 * The original number of sectors in the 0th element of the
	 * scatter-gather table.  
	 */
	sectors = sgpnt[0].length >> 9;

	/* 
	 * Free up the original scatter-gather table.  Note that since
	 * it was the 0th element that got us here, we don't have to
	 * go in and free up memory from the other slots.  
	 */
	SCpnt->request_bufflen = 0;
	SCpnt->use_sg = 0;
	scsi_free(SCpnt->request_buffer, SCpnt->sglist_len);

	/*
	 * Make an attempt to pick up as much as we reasonably can.
	 * Just keep adding sectors until the pool starts running kind of
	 * low.  The limit of 30 is somewhat arbitrary - the point is that
	 * it would kind of suck if we dropped down and limited ourselves to
	 * single-block requests if we had hundreds of free sectors.
	 */
	if( scsi_dma_free_sectors > 30 ) {
		for (this_count = 0, bio = req->bio; bio; bio = bio->bi_next) {
			if( scsi_dma_free_sectors - this_count < 30 
			    || this_count == sectors )
			{
				break;
			}
			this_count += bio_sectors(bio);
		}

	} else {
		/*
		 * Yow!   Take the absolute minimum here.
		 */
		this_count = req->current_nr_sectors;
	}

	/*
	 * Now drop through into the single-segment case.
	 */
	
      single_segment:
	/*
	 * Come here if for any reason we choose to do this as a single
	 * segment.  Possibly the entire request, or possibly a small
	 * chunk of the entire request.
	 */

	bio = req->bio;
	buff = req->buffer = bio_data(bio);

	if (dma_host || PageHighMem(bio_page(bio))) {
		/*
		 * Allocate a DMA bounce buffer.  If the allocation fails, fall
		 * back and allocate a really small one - enough to satisfy
		 * the first buffer.
		 */
		if (bio_to_phys(bio) + bio->bi_size - 1 > ISA_DMA_THRESHOLD) {
			buff = (char *) scsi_malloc(this_count << 9);
			if (!buff) {
				printk("Warning - running low on DMA memory\n");
				this_count = req->current_nr_sectors;
				buff = (char *) scsi_malloc(this_count << 9);
				if (!buff) {
					dma_exhausted(SCpnt, 0);
					return 0;
				}
			}
			if (rq_data_dir(req) == WRITE) {
				unsigned long flags;
				char *buf = bio_kmap_irq(bio, &flags);
				memcpy(buff, buf, this_count << 9);
				bio_kunmap_irq(buf, &flags);
			}
		}
	}
	SCpnt->request_bufflen = this_count << 9;
	SCpnt->request_buffer = buff;
	SCpnt->use_sg = 0;
	return 1;
}

#define INITIO(_FUNCTION, _VALID, _DMA)		\
static int _FUNCTION(Scsi_Cmnd * SCpnt)		\
{						\
    return __init_io(SCpnt, _VALID, _DMA);	\
}

/*
 * ll_rw_blk.c now keeps track of the number of segments in
 * a request.  Thus we don't have to do it any more here.
 * We always force "_VALID" to 1.  Eventually clean this up
 * and get rid of the extra argument.
 */
INITIO(scsi_init_io_v, 1, 0)
INITIO(scsi_init_io_vd, 1, 1)

/*
 * Function:    initialize_merge_fn()
 *
 * Purpose:     Initialize merge function for a host
 *
 * Arguments:   SHpnt   - Host descriptor.
 *
 * Returns:     Nothing.
 *
 * Lock status: 
 *
 * Notes:
 */
void initialize_merge_fn(Scsi_Device * SDpnt)
{
	struct Scsi_Host *SHpnt = SDpnt->host;
	request_queue_t *q = &SDpnt->request_queue;
	dma64_addr_t bounce_limit;

	/*
	 * If this host has an unlimited tablesize, then don't bother with a
	 * merge manager.  The whole point of the operation is to make sure
	 * that requests don't grow too large, and this host isn't picky.
	 *
	 * Note that ll_rw_blk.c is effectively maintaining a segment
	 * count which is only valid if clustering is used, and it obviously
	 * doesn't handle the DMA case.   In the end, it
	 * is simply easier to do it ourselves with our own functions
	 * rather than rely upon the default behavior of ll_rw_blk.
	 */
	q->back_merge_fn = scsi_back_merge_fn;
	q->front_merge_fn = scsi_front_merge_fn;
	q->merge_requests_fn = scsi_merge_requests_fn;

	if (SHpnt->unchecked_isa_dma == 0) {
		SDpnt->scsi_init_io_fn = scsi_init_io_v;
	} else {
		SDpnt->scsi_init_io_fn = scsi_init_io_vd;
	}

	/*
	 * now enable highmem I/O, if appropriate
	 */
	bounce_limit = BLK_BOUNCE_HIGH;
	if (SHpnt->highmem_io && (SDpnt->type == TYPE_DISK)) {
		if (!PCI_DMA_BUS_IS_PHYS)
			/* Platforms with virtual-DMA translation
 			 * hardware have no practical limit.
			 */
			bounce_limit = BLK_BOUNCE_ANY;
		else
			bounce_limit = SHpnt->pci_dev->dma_mask;
	}

	blk_queue_bounce_limit(q, bounce_limit);
}
