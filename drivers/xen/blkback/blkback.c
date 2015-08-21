/******************************************************************************
 * arch/xen/drivers/blkif/backend/main.c
 * 
 * Back-end of the driver for virtual block devices. This portion of the
 * driver exports a 'unified' block-device interface that can be accessed
 * by any operating system that implements a compatible front end. A 
 * reference front-end implementation can be found in:
 *  arch/xen/drivers/blkif/frontend
 * 
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Copyright (c) 2005, Christopher Clark
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <xen/balloon.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>
#include <asm/hypervisor.h>
#include "common.h"

/*
 * These are rather arbitrary. They are fairly large because adjacent requests
 * pulled from a communication ring are quite likely to end up being part of
 * the same scatter/gather request at the disc.
 * 
 * ** TRY INCREASING 'blkif_reqs' IF WRITE SPEEDS SEEM TOO LOW **
 * 
 * This will increase the chances of being able to write whole tracks.
 * 64 should be enough to keep us competitive with Linux.
 * 128 is required to make blkif_max_ring_page_order = 2 a useful default.
 */
static unsigned int blkif_reqs = 4 * BITS_PER_LONG;
module_param_named(reqs, blkif_reqs, uint, 0444);
MODULE_PARM_DESC(reqs, "Number of blkback requests to allocate");

/* Run-time switchable: /sys/module/blkback/parameters/ */
static bool log_stats;
static unsigned int debug_lvl;
module_param(log_stats, bool, 0644);
module_param(debug_lvl, uint, 0644);

/* Order of maximum shared ring size advertised to the front end. */
unsigned int blkif_max_ring_page_order/* XXX = sizeof(long) / 4 */;

static int set_max_ring_order(const char *buf, struct kernel_param *kp)
{
	unsigned int order;
	int err = kstrtouint(buf, 0, &order);

	if (err || order > BLKIF_MAX_RING_PAGE_ORDER)
		return -EINVAL;

	if (blkif_reqs < BLK_RING_SIZE(order))
		pr_warn("WARNING: I/O request space (%u reqs) < ring order %u, "
			"consider increasing " KBUILD_MODNAME ".reqs to >= %lu.\n",
			blkif_reqs, order,
			roundup_pow_of_two(BLK_RING_SIZE(order)));

	blkif_max_ring_page_order = order;

	return 0;
}

module_param_call(max_ring_page_order,
		  set_max_ring_order, param_get_uint,
		  &blkif_max_ring_page_order, 0644);
MODULE_PARM_DESC(max_ring_page_order, "log2 of maximum ring size (in pages)");

/* Maximum number of indirect segments advertised to the front end. */
unsigned int blkif_max_segs_per_req = BITS_PER_LONG;
module_param_named(max_indirect_segments, blkif_max_segs_per_req, uint, 0444);
MODULE_PARM_DESC(max_indirect_segments, "maximum number of indirect segments");

/*
 * Each outstanding request that we've passed to the lower device layers has a 
 * 'pending_req' allocated to it. Each buffer_head that completes decrements 
 * the pendcnt towards zero. When it hits zero, the specified domain has a 
 * response queued for it, with the saved 'id' passed back.
 */
typedef struct {
	blkif_t       *blkif;
	u64            id;
	atomic_t       pendcnt;
	unsigned short nr_pages;
	unsigned short operation;
	struct list_head free_list;
} pending_req_t;

static pending_req_t *pending_reqs;
static struct list_head pending_free;
static DEFINE_SPINLOCK(pending_free_lock);
static DECLARE_WAIT_QUEUE_HEAD(pending_free_wq);

#define BLKBACK_INVALID_HANDLE (~0)

static struct page **pending_pages;
static grant_handle_t *pending_grant_handles;

static inline int vaddr_pagenr(pending_req_t *req, int seg)
{
	return (req - pending_reqs) * blkif_max_segs_per_req + seg;
}

#define pending_page(req, seg) pending_pages[vaddr_pagenr(req, seg)]

static inline unsigned long vaddr(pending_req_t *req, int seg)
{
	unsigned long pfn = page_to_pfn(pending_page(req, seg));
	return (unsigned long)pfn_to_kaddr(pfn);
}

#define pending_handle(_req, _seg) \
	(pending_grant_handles[vaddr_pagenr(_req, _seg)])


static int do_block_io_op(blkif_t *blkif);
static void _dispatch_rw_block_io(blkif_t *, pending_req_t *,
				  unsigned int max_seg);
static void make_response(blkif_t *blkif, u64 id,
			  unsigned short op, int st);

/******************************************************************
 * misc small helpers
 */
static pending_req_t* alloc_req(void)
{
	pending_req_t *req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pending_free_lock, flags);
	if (!list_empty(&pending_free)) {
		req = list_entry(pending_free.next, pending_req_t, free_list);
		list_del(&req->free_list);
	}
	spin_unlock_irqrestore(&pending_free_lock, flags);
	return req;
}

static void free_req(pending_req_t *req)
{
	unsigned long flags;
	int was_empty;

	spin_lock_irqsave(&pending_free_lock, flags);
	was_empty = list_empty(&pending_free);
	list_add(&req->free_list, &pending_free);
	spin_unlock_irqrestore(&pending_free_lock, flags);
	if (was_empty)
		wake_up(&pending_free_wq);
}

static void fast_flush_area(pending_req_t *req)
{
	struct gnttab_unmap_grant_ref unmap[32];
	unsigned int i, invcount = 0;
	grant_handle_t handle;

	for (i = 0; i < req->nr_pages; i++) {
		handle = pending_handle(req, i);
		if (handle == BLKBACK_INVALID_HANDLE)
			continue;
		blkback_pagemap_clear(pending_page(req, i));
		gnttab_set_unmap_op(&unmap[invcount], vaddr(req, i),
				    GNTMAP_host_map, handle);
		pending_handle(req, i) = BLKBACK_INVALID_HANDLE;
		if (++invcount == ARRAY_SIZE(unmap)) {
			if (HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
						      unmap, invcount))
				BUG();
			invcount = 0;
		}
	}

	if (invcount &&
	    HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
				      unmap, invcount))
		BUG();
}

/******************************************************************
 * SCHEDULER FUNCTIONS
 */

static void print_stats(blkif_t *blkif)
{
	printk(KERN_DEBUG "%s: oo %3lu  |  rd %4lu  |  wr %4lu  |  br %4lu"
	       "  |  fl %4lu  |  ds %4lu  |  pk %4lu\n",
	       current->comm, blkif->st_oo_req,
	       blkif->st_rd_req, blkif->st_wr_req,
	       blkif->st_br_req, blkif->st_fl_req,
	       blkif->st_ds_req, blkif->st_pk_req);
	blkif->st_print = jiffies + msecs_to_jiffies(10 * 1000);
	blkif->st_rd_req = 0;
	blkif->st_wr_req = 0;
	blkif->st_oo_req = 0;
	blkif->st_br_req = 0;
	blkif->st_fl_req = 0;
	blkif->st_ds_req = 0;
	blkif->st_pk_req = 0;
}

int blkif_schedule(void *arg)
{
	blkif_t *blkif = arg;
	struct vbd *vbd = &blkif->vbd;

	blkif_get(blkif);

	if (debug_lvl)
		printk(KERN_DEBUG "%s: started\n", current->comm);

	while (!kthread_should_stop()) {
		if (try_to_freeze())
			continue;
		if (unlikely(vbd->size != vbd_size(vbd)))
			vbd_resize(blkif);

		wait_event_interruptible(
			blkif->wq,
			blkif->waiting_reqs || kthread_should_stop());
		wait_event_interruptible(
			pending_free_wq,
			!list_empty(&pending_free) || kthread_should_stop());

		blkif->waiting_reqs = 0;
		smp_mb(); /* clear flag *before* checking for work */

		switch (do_block_io_op(blkif)) {
		case 1:
			blkif->waiting_reqs = 1;
		case 0:
			break;
		case -EACCES:
			wait_event_interruptible(blkif->shutdown_wq,
						 kthread_should_stop());
			break;
		default:
			BUG();
		}

		if (log_stats && time_after(jiffies, blkif->st_print))
			print_stats(blkif);
	}

	if (log_stats)
		print_stats(blkif);
	if (debug_lvl)
		printk(KERN_DEBUG "%s: exiting\n", current->comm);

	blkif->xenblkd = NULL;
	blkif_put(blkif);

	return 0;
}

static void drain_io(blkif_t *blkif)
{
	atomic_set(&blkif->drain, 1);
	do {
		/* The initial value is one, and one refcnt taken at the
		 * start of the blkif_schedule thread. */
		if (atomic_read(&blkif->refcnt) <= 2)
			break;

		wait_for_completion_interruptible_timeout(
				&blkif->drain_complete, HZ);

		if (!atomic_read(&blkif->drain))
			break;
	} while (!kthread_should_stop());
	atomic_set(&blkif->drain, 0);
}

/******************************************************************
 * COMPLETION CALLBACK -- Called as bh->b_end_io()
 */

static void __end_block_io_op(pending_req_t *pending_req, int error)
{
	blkif_t *blkif = pending_req->blkif;
	int status = BLKIF_RSP_OKAY;

	/* An error fails the entire request. */
	if ((pending_req->operation == BLKIF_OP_WRITE_BARRIER) &&
	    (error == -EOPNOTSUPP)) {
		DPRINTK("blkback: write barrier op failed, not supported\n");
		blkback_barrier(XBT_NIL, blkif->be, 0);
		status = BLKIF_RSP_EOPNOTSUPP;
	} else if ((pending_req->operation == BLKIF_OP_FLUSH_DISKCACHE) &&
		   (error == -EOPNOTSUPP)) {
		DPRINTK("blkback: flush diskcache op failed, not supported\n");
		blkback_flush_diskcache(XBT_NIL, blkif->be, 0);
		status = BLKIF_RSP_EOPNOTSUPP;
	} else if (error) {
		DPRINTK("Buffer not up-to-date at end of operation, "
			"error=%d\n", error);
		status = BLKIF_RSP_ERROR;
	}

	if (atomic_dec_and_test(&pending_req->pendcnt)) {
		fast_flush_area(pending_req);
		make_response(blkif, pending_req->id,
			      pending_req->operation, status);
		free_req(pending_req);
		if (atomic_read(&blkif->drain)
		    && atomic_read(&blkif->refcnt) <= 2)
			complete(&blkif->drain_complete);
		blkif_put(blkif);
	}
}

static void end_block_io_op(struct bio *bio, int error)
{
	__end_block_io_op(bio->bi_private, error);
	bio_put(bio);
}


/******************************************************************************
 * NOTIFICATION FROM GUEST OS.
 */

static void blkif_notify_work(blkif_t *blkif)
{
	blkif->waiting_reqs = 1;
	wake_up(&blkif->wq);
}

irqreturn_t blkif_be_int(int irq, void *dev_id)
{
	blkif_notify_work(dev_id);
	return IRQ_HANDLED;
}



/******************************************************************
 * DOWNWARD CALLS -- These interface with the block-device layer proper.
 */

static void dispatch_discard(blkif_t *blkif, struct blkif_request_discard *req)
{
	unsigned long secure = (blkif->vbd.discard_secure &&
				(req->flag & BLKIF_DISCARD_SECURE)) ?
			       BLKDEV_DISCARD_SECURE : 0;
	struct phys_req preq;
	int status;

	blkif->st_ds_req++;

	preq.sector_number = req->sector_number;
	preq.nr_sects      = req->nr_sectors;

	if (vbd_translate(&preq, blkif, REQ_DISCARD) != 0) {
		DPRINTK("access denied: discard of [%Lu,%Lu) on dev=%04x\n",
			preq.sector_number,
			preq.sector_number + preq.nr_sects,
			blkif->vbd.pdevice);
		make_response(blkif, req->id, req->operation, BLKIF_RSP_ERROR);
		msleep(1); /* back off a bit */
		return;
	}

	switch (blkdev_issue_discard(preq.bdev, preq.sector_number,
				     preq.nr_sects, GFP_KERNEL, secure)) {
	case 0:
		status = BLKIF_RSP_OKAY;
		break;
	case -EOPNOTSUPP:
		DPRINTK("discard op failed, not supported\n");
		status = BLKIF_RSP_EOPNOTSUPP;
		break;
	default:
		status = BLKIF_RSP_ERROR;
		break;
	}

	make_response(blkif, req->id, req->operation, status);
}

static void dispatch_rw_block_io(blkif_t *blkif,
				 blkif_request_t *greq,
				 pending_req_t *pending_req)
{
	struct blkbk_request *req = blkif->req;

	req->operation     = greq->operation;
	req->handle        = greq->handle;
	req->nr_segments   = greq->nr_segments;
	req->id            = greq->id;
	req->sector_number = greq->sector_number;
	if (likely(req->nr_segments <= BLKIF_MAX_SEGMENTS_PER_REQUEST))
		memcpy(req->seg, greq->seg, req->nr_segments * sizeof(*req->seg));
	_dispatch_rw_block_io(blkif, pending_req,
			      BLKIF_MAX_SEGMENTS_PER_REQUEST);
}

static void dispatch_indirect(blkif_t *blkif,
			      struct blkif_request_indirect *greq,
			      pending_req_t *pending_req)
{
	struct blkbk_request *req = blkif->req;

	req->operation     = greq->indirect_op;
	req->handle        = greq->handle;
	req->nr_segments   = greq->nr_segments;
	req->id            = greq->id;
	req->sector_number = greq->sector_number;
	if (likely(req->nr_segments <= blkif_max_segs_per_req)) {
		struct gnttab_copy gc[BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST];
		unsigned int i, n = BLKIF_INDIRECT_PAGES(req->nr_segments);

		for (i = 0; i < n; ++i) {
			gc[i].source.u.ref = greq->indirect_grefs[i];
			gc[i].source.domid = blkif->domid;
			gc[i].source.offset = 0;
			gc[i].dest.u.gmfn = blkif->seg_mfn[i];
			gc[i].dest.domid = DOMID_SELF;
			gc[i].dest.offset = blkif->seg_offs;
			gc[i].len = PAGE_SIZE;
			gc[i].flags = GNTCOPY_source_gref;
		}
		if ((req->nr_segments * sizeof(*req->seg)) & ~PAGE_MASK)
			gc[n - 1].len = (req->nr_segments * sizeof(*req->seg))
					& ~PAGE_MASK;
		if (HYPERVISOR_grant_table_op(GNTTABOP_copy, gc, n))
			BUG();
		for (i = 0; i < n; ++i) {
			if (unlikely(gc[i].status == GNTST_eagain))
				gnttab_check_GNTST_eagain_do_while(GNTTABOP_copy,
								   &gc[i]);
			if (gc[i].status != GNTST_okay)
				/* force failure in _dispatch_rw_block_io() */
				req->operation = BLKIF_OP_INDIRECT;
		}
	}
	_dispatch_rw_block_io(blkif, pending_req, blkif_max_segs_per_req);
}

static int _do_block_io_op(blkif_t *blkif)
{
	blkif_back_rings_t *blk_rings = &blkif->blk_rings;
	blkif_request_t req;
	pending_req_t *pending_req;
	RING_IDX rc, rp;

	rc = blk_rings->common.req_cons;
	rp = blk_rings->common.sring->req_prod;
	rmb(); /* Ensure we see queued requests up to 'rp'. */

	if (RING_REQUEST_PROD_OVERFLOW(&blk_rings->common, rp)) {
		rc = blk_rings->common.rsp_prod_pvt;
		pr_warning("blkback:"
			   " Dom%d provided bogus ring requests (%#x - %#x = %u)."
			   " Halting ring processing on dev=%04x\n",
			   blkif->domid, rp, rc, rp - rc, blkif->vbd.pdevice);
		return -EACCES;
	}

	while (rc != rp) {
		if (RING_REQUEST_CONS_OVERFLOW(&blk_rings->common, rc))
			break;

		if (kthread_should_stop())
			return 1;

		switch (blkif->blk_protocol) {
		case BLKIF_PROTOCOL_NATIVE:
			req = *RING_GET_REQUEST(&blk_rings->native, rc);
			break;
		case BLKIF_PROTOCOL_X86_32:
			blkif_get_x86_32_req(&req, RING_GET_REQUEST(&blk_rings->x86_32, rc));
			break;
		case BLKIF_PROTOCOL_X86_64:
			blkif_get_x86_64_req(&req, RING_GET_REQUEST(&blk_rings->x86_64, rc));
			break;
		default:
			BUG();
			return 0; /* make compiler happy */
		}

		++rc;

		switch (req.operation) {
		case BLKIF_OP_READ:
		case BLKIF_OP_WRITE:
		case BLKIF_OP_WRITE_BARRIER:
		case BLKIF_OP_FLUSH_DISKCACHE:
			pending_req = alloc_req();
			if (!pending_req) {
				blkif->st_oo_req++;
				return 1;
			}

			/* before make_response() */
			blk_rings->common.req_cons = rc;

			/* Apply all sanity checks to /private copy/ of request. */
			barrier();

			dispatch_rw_block_io(blkif, &req, pending_req);
			break;
		case BLKIF_OP_DISCARD:
			blk_rings->common.req_cons = rc;
			barrier();
			dispatch_discard(blkif, (void *)&req);
			break;
		case BLKIF_OP_INDIRECT:
			pending_req = alloc_req();
			if (!pending_req) {
				blkif->st_oo_req++;
				return 1;
			}
			blk_rings->common.req_cons = rc;
			barrier();
			dispatch_indirect(blkif, (void *)&req, pending_req);
			break;
		case BLKIF_OP_PACKET:
			blk_rings->common.req_cons = rc;
			barrier();
			blkif->st_pk_req++;
			DPRINTK("error: block operation BLKIF_OP_PACKET not implemented\n");
			make_response(blkif, req.id, req.operation,
				      BLKIF_RSP_ERROR);
			break;
		default:
			/* A good sign something is wrong: sleep for a while to
			 * avoid excessive CPU consumption by a bad guest. */
			msleep(1);
			blk_rings->common.req_cons = rc;
			barrier();
			DPRINTK("error: unknown block io operation [%d]\n",
				req.operation);
			make_response(blkif, req.id, req.operation,
				      BLKIF_RSP_ERROR);
			break;
		}

		/* Yield point for this unbounded loop. */
		cond_resched();
	}

	return 0;
}

static int
do_block_io_op(blkif_t *blkif)
{
	blkif_back_rings_t *blk_rings = &blkif->blk_rings;
	int more_to_do;

	do {
		more_to_do = _do_block_io_op(blkif);
		if (more_to_do)
			break;

		RING_FINAL_CHECK_FOR_REQUESTS(&blk_rings->common, more_to_do);
	} while (more_to_do);

	return more_to_do;
}

static void _dispatch_rw_block_io(blkif_t *blkif,
				  pending_req_t *pending_req,
				  unsigned int max_seg)
{
	struct blkbk_request *req = blkif->req;
	struct gnttab_map_grant_ref *map = blkif->map;
	struct phys_req preq;
	union blkif_seg *seg = blkif->seg;
	unsigned int nseg, i, nbio = 0;
	struct bio *bio = NULL;
	uint32_t flags;
	int ret, operation;
	struct blk_plug plug;

	switch (req->operation) {
	case BLKIF_OP_READ:
		blkif->st_rd_req++;
		operation = READ;
		break;
	case BLKIF_OP_WRITE:
		blkif->st_wr_req++;
		operation = WRITE;
		break;
	case BLKIF_OP_WRITE_BARRIER:
		blkif->st_br_req++;
		operation = WRITE_FLUSH_FUA;
		break;
	case BLKIF_OP_FLUSH_DISKCACHE:
		blkif->st_fl_req++;
		operation = WRITE_FLUSH;
		break;
	default:
		goto fail_response;
	}

	/* Check that number of segments is sane. */
	nseg = req->nr_segments;
	if (unlikely(nseg == 0 && !(operation & REQ_FLUSH)) ||
	    unlikely(nseg > max_seg)) {
		DPRINTK("Bad number of segments in request (%d)\n", nseg);
		goto fail_response;
	}

	preq.sector_number = req->sector_number;
	preq.nr_sects      = 0;

	pending_req->blkif     = blkif;
	pending_req->id        = req->id;
	pending_req->operation = req->operation;
	pending_req->nr_pages  = nseg;

	flags = GNTMAP_host_map;
	if (operation != READ)
		flags |= GNTMAP_readonly;

	for (i = 0; i < nseg; i++) {
		seg[i].nsec = req->seg[i].last_sect -
			req->seg[i].first_sect + 1;

		if ((req->seg[i].last_sect >= (PAGE_SIZE >> 9)) ||
		    (req->seg[i].last_sect < req->seg[i].first_sect))
			goto fail_response;
		preq.nr_sects += seg[i].nsec;

		gnttab_set_map_op(&map[i], vaddr(pending_req, i), flags,
				  req->seg[i].gref, blkif->domid);
	}

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map, nseg);
	BUG_ON(ret);

	for (i = 0; i < nseg; i++) {
		if (unlikely(map[i].status == GNTST_eagain))
			gnttab_check_GNTST_eagain_do_while(GNTTABOP_map_grant_ref, &map[i])
		if (unlikely(map[i].status != GNTST_okay)) {
			DPRINTK("invalid buffer -- could not remap it\n");
			map[i].handle = BLKBACK_INVALID_HANDLE;
			ret = 1;
		} else {
			blkback_pagemap_set(vaddr_pagenr(pending_req, i),
					    pending_page(pending_req, i),
					    blkif->domid, req->handle,
					    req->seg[i].gref);
		}

		pending_handle(pending_req, i) = map[i].handle;

		if (ret)
			continue;

		set_phys_to_machine(
			page_to_pfn(pending_page(pending_req, i)),
			FOREIGN_FRAME(map[i].dev_bus_addr >> PAGE_SHIFT));
	}

	if (ret)
		goto fail_flush;

	if (vbd_translate(&preq, blkif, operation) != 0) {
		DPRINTK("access denied: %s of [%llu,%llu] on dev=%04x\n", 
			operation == READ ? "read" : "write",
			preq.sector_number,
			preq.sector_number + preq.nr_sects,
			blkif->vbd.pdevice);
		goto fail_flush;
	}

	/* Wait on all outstanding I/O's and once that has been completed
	 * issue the WRITE_FLUSH.
	 */
	if (req->operation == BLKIF_OP_WRITE_BARRIER)
		drain_io(blkif);

	blkif_get(blkif);

	for (i = 0; i < nseg; i++) {
		if (((int)preq.sector_number|(int)seg[i].nsec) &
		    ((bdev_logical_block_size(preq.bdev) >> 9) - 1)) {
			DPRINTK("Misaligned I/O request from domain %d",
				blkif->domid);
			goto fail_put_bio;
		}

		while ((bio == NULL) ||
		       (bio_add_page(bio,
				     pending_page(pending_req, i),
				     seg[i].nsec << 9,
				     req->seg[i].first_sect << 9) == 0)) {
			bio = bio_alloc(GFP_KERNEL, nseg-i);
			if (unlikely(bio == NULL))
				goto fail_put_bio;

			bio->bi_bdev    = preq.bdev;
			bio->bi_private = pending_req;
			bio->bi_end_io  = end_block_io_op;
			bio->bi_iter.bi_sector = preq.sector_number;
		}

		preq.sector_number += seg[i].nsec;

		BUG_ON(nbio > i);
		if (!nbio || bio != seg[nbio - 1].bio)
			seg[nbio++].bio = bio;
	}

	if (!bio) {
		BUG_ON(!(operation & (REQ_FLUSH|REQ_FUA)));
		bio = bio_alloc(GFP_KERNEL, 0);
		if (unlikely(bio == NULL))
			goto fail_put_bio;
		seg[0].bio = bio;
		nbio = 1;

		bio->bi_bdev    = preq.bdev;
		bio->bi_private = pending_req;
		bio->bi_end_io  = end_block_io_op;
		bio->bi_iter.bi_sector = -1;
	}

	atomic_set(&pending_req->pendcnt, nbio);
	blk_start_plug(&plug);

	for (i = 0; i < nbio; ++i)
		submit_bio(operation, seg[i].bio);

	blk_finish_plug(&plug);

	if (operation == READ)
		blkif->st_rd_sect += preq.nr_sects;
	else
		blkif->st_wr_sect += preq.nr_sects;

	return;

 fail_flush:
	fast_flush_area(pending_req);
 fail_response:
	make_response(blkif, req->id, req->operation, BLKIF_RSP_ERROR);
	free_req(pending_req);
	msleep(1); /* back off a bit */
	return;

 fail_put_bio:
	for (i = 0; i < nbio; ++i)
		bio_put(seg[i].bio);
	atomic_set(&pending_req->pendcnt, 1);
	__end_block_io_op(pending_req, -EINVAL);
	msleep(1); /* back off a bit */
	return;
}



/******************************************************************
 * MISCELLANEOUS SETUP / TEARDOWN / DEBUGGING
 */


static void make_response(blkif_t *blkif, u64 id,
			  unsigned short op, int st)
{
	blkif_response_t  resp;
	unsigned long     flags;
	blkif_back_rings_t *blk_rings = &blkif->blk_rings;
	int notify;

	resp.id        = id;
	resp.operation = op;
	resp.status    = st;

	spin_lock_irqsave(&blkif->blk_ring_lock, flags);
	/* Place on the response ring for the relevant domain. */
	switch (blkif->blk_protocol) {
	case BLKIF_PROTOCOL_NATIVE:
		memcpy(RING_GET_RESPONSE(&blk_rings->native, blk_rings->native.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	case BLKIF_PROTOCOL_X86_32:
		memcpy(RING_GET_RESPONSE(&blk_rings->x86_32, blk_rings->x86_32.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	case BLKIF_PROTOCOL_X86_64:
		memcpy(RING_GET_RESPONSE(&blk_rings->x86_64, blk_rings->x86_64.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	default:
		BUG();
	}
	blk_rings->common.rsp_prod_pvt++;
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&blk_rings->common, notify);
	spin_unlock_irqrestore(&blkif->blk_ring_lock, flags);

	if (notify)
		notify_remote_via_irq(blkif->irq);
}

static int __init blkif_init(void)
{
	unsigned long i, mmap_pages;

	if (!is_running_on_xen())
		return -ENODEV;

	if (blkif_max_segs_per_req < BLKIF_MAX_SEGMENTS_PER_REQUEST)
		blkif_max_segs_per_req = BLKIF_MAX_SEGMENTS_PER_REQUEST;
	else if (BLKIF_INDIRECT_PAGES(blkif_max_segs_per_req) >
		 BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST)
		blkif_max_segs_per_req = BLKIF_MAX_INDIRECT_PAGES_PER_REQUEST *
					 BLKIF_SEGS_PER_INDIRECT_FRAME;
	mmap_pages = blkif_reqs * 1UL * blkif_max_segs_per_req;

	pending_reqs          = kzalloc(sizeof(pending_reqs[0]) *
					blkif_reqs, GFP_KERNEL);
	pending_grant_handles = vmalloc(sizeof(pending_grant_handles[0]) *
					mmap_pages);
	pending_pages         = alloc_empty_pages_and_pagevec(mmap_pages);

	if (blkback_pagemap_init(mmap_pages))
		goto out_of_memory;

	if (!pending_reqs || !pending_grant_handles || !pending_pages)
		goto out_of_memory;

	for (i = 0; i < mmap_pages; i++)
		pending_grant_handles[i] = BLKBACK_INVALID_HANDLE;

	blkif_interface_init();

	INIT_LIST_HEAD(&pending_free);

	for (i = 0; i < blkif_reqs; i++)
		list_add_tail(&pending_reqs[i].free_list, &pending_free);

	blkif_xenbus_init();

	return 0;

 out_of_memory:
	kfree(pending_reqs);
	kfree(pending_grant_handles);
	free_empty_pages_and_pagevec(pending_pages, mmap_pages);
	pr_warning("%s: out of memory\n", __func__);
	return -ENOMEM;
}

module_init(blkif_init);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:vbd");
