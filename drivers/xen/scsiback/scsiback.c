/*
 * Xen SCSI backend driver
 *
 * Copyright (c) 2008, FUJITSU Limited
 *
 * Based on the blkback driver code.
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
#include <linux/list.h>
#include <linux/delay.h>
#include <xen/balloon.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>
#include <asm/hypervisor.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <scsi/sg.h>

#include "common.h"


struct list_head pending_free;
DEFINE_SPINLOCK(pending_free_lock);
DECLARE_WAIT_QUEUE_HEAD(pending_free_wq);

static unsigned int vscsiif_reqs = 128;
module_param_named(reqs, vscsiif_reqs, uint, 0);
MODULE_PARM_DESC(reqs, "Number of scsiback requests to allocate");

unsigned int vscsiif_segs = VSCSIIF_SG_TABLESIZE;
module_param_named(segs, vscsiif_segs, uint, 0);
MODULE_PARM_DESC(segs, "Number of segments to allow per request");

static bool log_print_stat;
module_param(log_print_stat, bool, 0644);

#define SCSIBACK_INVALID_HANDLE (~0)

static pending_req_t *pending_reqs;
static struct page **pending_pages;
static grant_handle_t *pending_grant_handles;

static int vaddr_pagenr(pending_req_t *req, int seg)
{
	return (req - pending_reqs) * vscsiif_segs + seg;
}

static unsigned long vaddr(pending_req_t *req, int seg)
{
	unsigned long pfn = page_to_pfn(pending_pages[vaddr_pagenr(req, seg)]);
	return (unsigned long)pfn_to_kaddr(pfn);
}

#define pending_handle(_req, _seg) \
	(pending_grant_handles[vaddr_pagenr(_req, _seg)])


void scsiback_fast_flush_area(pending_req_t *req)
{
	struct gnttab_unmap_grant_ref *unmap = req->info->gunmap;
	unsigned int i, invcount = 0;
	grant_handle_t handle;
	int err;

	if (req->nr_segments) {
		for (i = 0; i < req->nr_segments; i++) {
			handle = pending_handle(req, i);
			if (handle == SCSIBACK_INVALID_HANDLE)
				continue;
			gnttab_set_unmap_op(&unmap[invcount], vaddr(req, i),
						GNTMAP_host_map, handle);
			pending_handle(req, i) = SCSIBACK_INVALID_HANDLE;
			invcount++;
		}

		err = HYPERVISOR_grant_table_op(
			GNTTABOP_unmap_grant_ref, unmap, invcount);
		BUG_ON(err);
		kfree(req->sgl);
	}

	return;
}


static pending_req_t * alloc_req(struct vscsibk_info *info)
{
	pending_req_t *req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pending_free_lock, flags);
	if (!list_empty(&pending_free)) {
		req = list_entry(pending_free.next, pending_req_t, free_list);
		list_del(&req->free_list);
		req->nr_segments = 0;
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


static void scsiback_notify_work(struct vscsibk_info *info)
{
	info->waiting_reqs = 1;
	wake_up(&info->wq);
}

void scsiback_do_resp_with_sense(char *sense_buffer, int32_t result,
				 uint32_t resid, pending_req_t *pending_req,
				 uint8_t act)
{
	vscsiif_response_t *ring_res;
	struct vscsibk_info *info = pending_req->info;
	int notify;
	struct scsi_sense_hdr sshdr;
	unsigned long flags;

	DPRINTK("%s\n", __func__);

	spin_lock_irqsave(&info->ring_lock, flags);

	ring_res = RING_GET_RESPONSE(&info->ring, info->ring.rsp_prod_pvt);
	info->ring.rsp_prod_pvt++;

	ring_res->act    = act;
	ring_res->rslt   = result;
	ring_res->rqid   = pending_req->rqid;

	if (sense_buffer != NULL) {
		if (scsi_normalize_sense(sense_buffer,
			sizeof(sense_buffer), &sshdr)) {

			int len = 8 + sense_buffer[7];

			if (len > VSCSIIF_SENSE_BUFFERSIZE)
				len = VSCSIIF_SENSE_BUFFERSIZE;

			memcpy(ring_res->sense_buffer, sense_buffer, len);
			ring_res->sense_len = len;
		}
	} else {
		ring_res->sense_len = 0;
	}

	ring_res->residual_len = resid;

	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&info->ring, notify);
	spin_unlock_irqrestore(&info->ring_lock, flags);

	if (notify)
		notify_remote_via_irq(info->irq);

	if (act != VSCSIIF_ACT_SCSI_SG_PRESET)
		free_req(pending_req);
}

static void scsiback_print_status(char *sense_buffer, int errors,
					pending_req_t *pending_req)
{
	struct scsi_device *sdev = pending_req->sdev;
	
	pr_err("scsiback[%u:%u:%u:%Lu] cmnd[0]=%02x -> st=%02x msg=%02x host=%02x drv=%02x\n",
	       sdev->host->host_no, sdev->channel, sdev->id, sdev->lun,
	       pending_req->cmnd[0], status_byte(errors), msg_byte(errors),
	       host_byte(errors), driver_byte(errors));

	if (CHECK_CONDITION & status_byte(errors))
		__scsi_print_sense(sdev, "scsiback", sense_buffer,
				   SCSI_SENSE_BUFFERSIZE);
}


static void scsiback_cmd_done(struct request *req, int uptodate)
{
	pending_req_t *pending_req = req->end_io_data;
	unsigned char *sense_buffer;
	unsigned int resid;
	int errors;

	sense_buffer = req->sense;
	resid        = blk_rq_bytes(req);
	errors       = req->errors;

	if (errors && log_print_stat)
		scsiback_print_status(sense_buffer, errors, pending_req);

	/* The Host mode is through as for Emulation. */
	if (pending_req->info->feature != VSCSI_TYPE_HOST)
		scsiback_rsp_emulation(pending_req);

	scsiback_fast_flush_area(pending_req);
	scsiback_do_resp_with_sense(sense_buffer, errors, resid, pending_req,
				    VSCSIIF_ACT_SCSI_CDB);
	scsiback_put(pending_req->info);

	__blk_put_request(req->q, req);
}


static int scsiback_gnttab_data_map(const struct scsiif_request_segment *segs,
				    unsigned int nr_segs,
				    pending_req_t *pending_req)
{
	u32 flags;
	int write, err = 0;
	unsigned int i, j, data_len = 0;
	struct vscsibk_info *info   = pending_req->info;
	struct gnttab_map_grant_ref *map = info->gmap;
	int data_dir = (int)pending_req->sc_data_direction;
	unsigned int nr_segments = pending_req->nr_segments + nr_segs;

	write = (data_dir == DMA_TO_DEVICE);

	if (nr_segments) {
		struct scatterlist *sg;

		/* free of (sgl) in fast_flush_area()*/
		pending_req->sgl = kmalloc(sizeof(struct scatterlist) * nr_segments,
						GFP_KERNEL);
		if (!pending_req->sgl) {
			pr_err("scsiback: %s: kmalloc() error\n", __func__);
			return -ENOMEM;
		}

		sg_init_table(pending_req->sgl, nr_segments);

		flags = GNTMAP_host_map;
		if (write)
			flags |= GNTMAP_readonly;

		for (i = 0; i < pending_req->nr_segments; i++)
			gnttab_set_map_op(&map[i], vaddr(pending_req, i), flags,
						pending_req->segs[i].gref,
						info->domid);
		for (j = 0; i < nr_segments; i++, j++)
			gnttab_set_map_op(&map[i], vaddr(pending_req, i), flags,
						segs[j].gref,
						info->domid);


		err = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map, nr_segments);
		BUG_ON(err);

		j = 0;
		for_each_sg (pending_req->sgl, sg, nr_segments, i) {
			struct page *pg;

			/* Retry maps with GNTST_eagain */
			if (unlikely(map[i].status == GNTST_eagain))
				gnttab_check_GNTST_eagain_while(GNTTABOP_map_grant_ref, &map[i]);
			if (unlikely(map[i].status != GNTST_okay)) {
				pr_err("scsiback: invalid buffer -- could not remap it\n");
				map[i].handle = SCSIBACK_INVALID_HANDLE;
				err |= 1;
			}

			pending_handle(pending_req, i) = map[i].handle;

			if (err)
				continue;

			pg = pending_pages[vaddr_pagenr(pending_req, i)];

			set_phys_to_machine(page_to_pfn(pg),
				FOREIGN_FRAME(map[i].dev_bus_addr >> PAGE_SHIFT));

			if (i < pending_req->nr_segments)
				sg_set_page(sg, pg,
					    pending_req->segs[i].length,
					    pending_req->segs[i].offset);
			else {
				sg_set_page(sg, pg, segs[j].length,
					    segs[j].offset);
				++j;
			}
			data_len += sg->length;

			barrier();
			if (sg->offset >= PAGE_SIZE ||
			    sg->length > PAGE_SIZE ||
			    sg->offset + sg->length > PAGE_SIZE)
				err |= 1;

		}

		pending_req->nr_segments = nr_segments;

		if (err)
			goto fail_flush;
	}
	
	pending_req->request_bufflen = data_len;
	
	return 0;
	
fail_flush:
	scsiback_fast_flush_area(pending_req);
	return -ENOMEM;
}

/* quoted scsi_lib.c/scsi_bi_endio */
static void scsiback_bi_endio(struct bio *bio, int error)
{
	bio_put(bio);
}



/* quoted scsi_lib.c/scsi_req_map_sg . */
static struct bio *request_map_sg(pending_req_t *pending_req)
{
	struct request_queue *q = pending_req->sdev->request_queue;
	unsigned int nsegs = (unsigned int)pending_req->nr_segments;
	unsigned int i, len, bytes, off, nr_pages, nr_vecs = 0;
	struct scatterlist *sg;
	struct page *page;
	struct bio *bio = NULL, *bio_first = NULL, *bio_last = NULL;
	int err;

	for_each_sg (pending_req->sgl, sg, nsegs, i) {
		page = sg_page(sg);
		off = sg->offset;
		len = sg->length;

		nr_pages = (len + off + PAGE_SIZE - 1) >> PAGE_SHIFT;
		while (len > 0) {
			bytes = min_t(unsigned int, len, PAGE_SIZE - off);

			if (!bio) {
				nr_vecs = min_t(unsigned int, BIO_MAX_PAGES,
						nr_pages);
				nr_pages -= nr_vecs;
				bio = bio_alloc(GFP_KERNEL, nr_vecs);
				if (!bio) {
					err = -ENOMEM;
					goto free_bios;
				}
				bio->bi_end_io = scsiback_bi_endio;
				if (bio_last)
					bio_last->bi_next = bio;
				else
					bio_first = bio;
				bio_last = bio;
			}

			if (bio_add_pc_page(q, bio, page, bytes, off) !=
						bytes) {
				err = -EINVAL;
				goto free_bios;
			}

			if (bio->bi_vcnt >= nr_vecs) {
				bio->bi_flags &= ~(1 << BIO_SEG_VALID);
				if (pending_req->sc_data_direction == WRITE)
					bio->bi_rw |= REQ_WRITE;
				bio = NULL;
			}

			page++;
			len -= bytes;
			off = 0;
		}
	}

	return bio_first;

free_bios:
	while ((bio = bio_first) != NULL) {
		bio_first = bio->bi_next;
		bio_put(bio);
	}

	return ERR_PTR(err);
}


int scsiback_cmd_exec(pending_req_t *pending_req)
{
	int cmd_len  = (int)pending_req->cmd_len;
	int data_dir = (int)pending_req->sc_data_direction;
	unsigned int timeout;
	struct bio *bio;
	struct request *rq;
	int write;

	DPRINTK("%s\n", __func__);

	/* because it doesn't timeout backend earlier than frontend.*/
	if (pending_req->timeout_per_command)
		timeout = pending_req->timeout_per_command * HZ;
	else
		timeout = VSCSIIF_TIMEOUT;

	write = (data_dir == DMA_TO_DEVICE);
	if (pending_req->nr_segments) {
		bio = request_map_sg(pending_req);
		if (IS_ERR(bio)) {
			pr_err("scsiback: SG Request Map Error %ld\n",
			       PTR_ERR(bio));
			return PTR_ERR(bio);
		}
	} else
		bio = NULL;

	if (bio) {
		rq = blk_make_request(pending_req->sdev->request_queue, bio,
				      GFP_KERNEL);
		if (IS_ERR(rq)) {
			do {
				struct bio *b = bio->bi_next;

				bio_put(bio);
				bio = b;
			} while (bio);
			pr_err("scsiback: Make Request Error %ld\n",
			       PTR_ERR(rq));
			return PTR_ERR(rq);
		}
	} else {
		rq = blk_get_request(pending_req->sdev->request_queue, write,
				     GFP_KERNEL);
		if (unlikely(!rq)) {
			pr_err("scsiback: Get Request Error\n");
			return -ENOMEM;
		}
	}

	rq->cmd_type = REQ_TYPE_BLOCK_PC;
	rq->cmd_len = cmd_len;
	memcpy(rq->cmd, pending_req->cmnd, cmd_len);

	memset(pending_req->sense_buffer, 0, VSCSIIF_SENSE_BUFFERSIZE);
	rq->sense       = pending_req->sense_buffer;
	rq->sense_len = 0;

	/* not allowed to retry in backend.                   */
	rq->retries   = 0;
	rq->timeout   = timeout;
	rq->end_io_data = pending_req;

	scsiback_get(pending_req->info);
	blk_execute_rq_nowait(rq->q, NULL, rq, 1, scsiback_cmd_done);

	return 0;
}


static void scsiback_device_reset_exec(pending_req_t *pending_req)
{
	struct vscsibk_info *info = pending_req->info;
	int err, op = SG_SCSI_RESET_DEVICE;
	struct scsi_device *sdev = pending_req->sdev;
	mm_segment_t old_fs = get_fs();

	scsiback_get(info);

	set_fs(KERNEL_DS);
	err = scsi_ioctl_reset(sdev, (typeof(op) __force __user *)&op);
	set_fs(old_fs);

	scsiback_do_resp_with_sense(NULL, err ? FAILED : SUCCESS, 0,
				    pending_req, VSCSIIF_ACT_SCSI_RESET);
	scsiback_put(info);

	return;
}


irqreturn_t scsiback_intr(int irq, void *dev_id)
{
	scsiback_notify_work((struct vscsibk_info *)dev_id);
	return IRQ_HANDLED;
}

static int prepare_pending_reqs(struct vscsibk_info *info,
		vscsiif_request_t *ring_req, pending_req_t *pending_req)
{
	struct scsi_device *sdev;
	struct ids_tuple vir;
	unsigned int nr_segs;
	int err = -EINVAL;

	DPRINTK("%s\n", __func__);

	pending_req->info       = info;

	pending_req->v_chn = vir.chn = ring_req->channel;
	pending_req->v_tgt = vir.tgt = ring_req->id;
	vir.lun = ring_req->lun;

	rmb();
	sdev = scsiback_do_translation(info, &vir);
	if (!sdev) {
		pending_req->sdev = NULL;
		DPRINTK("scsiback: doesn't exist.\n");
		err = -ENODEV;
		goto invalid_value;
	}
	pending_req->sdev = sdev;

	/* request range check from frontend */
	pending_req->sc_data_direction = ring_req->sc_data_direction;
	barrier();
	if ((pending_req->sc_data_direction != DMA_BIDIRECTIONAL) &&
		(pending_req->sc_data_direction != DMA_TO_DEVICE) &&
		(pending_req->sc_data_direction != DMA_FROM_DEVICE) &&
		(pending_req->sc_data_direction != DMA_NONE)) {
		DPRINTK("scsiback: invalid parameter data_dir = %d\n",
			pending_req->sc_data_direction);
		err = -EINVAL;
		goto invalid_value;
	}

	nr_segs = ring_req->nr_segments;
	barrier();
	if (pending_req->nr_segments + nr_segs > vscsiif_segs) {
		DPRINTK("scsiback: invalid nr_segs = %u\n", nr_segs);
		err = -EINVAL;
		goto invalid_value;
	}

	pending_req->cmd_len = ring_req->cmd_len;
	barrier();
	if (pending_req->cmd_len > VSCSIIF_MAX_COMMAND_SIZE) {
		DPRINTK("scsiback: invalid parameter cmd_len = %d\n",
			pending_req->cmd_len);
		err = -EINVAL;
		goto invalid_value;
	}
	memcpy(pending_req->cmnd, ring_req->cmnd, pending_req->cmd_len);
	
	pending_req->timeout_per_command = ring_req->timeout_per_command;

	if (scsiback_gnttab_data_map(ring_req->seg, nr_segs, pending_req)) {
		DPRINTK("scsiback: invalid buffer\n");
		err = -EINVAL;
		goto invalid_value;
	}

	return 0;

invalid_value:
	return err;
}

static void latch_segments(pending_req_t *pending_req,
			   const struct vscsiif_sg_list *sgl)
{
	unsigned int nr_segs = sgl->nr_segments;

	barrier();
	if (pending_req->nr_segments + nr_segs <= vscsiif_segs) {
		memcpy(pending_req->segs + pending_req->nr_segments,
		       sgl->seg, nr_segs * sizeof(*sgl->seg));
		pending_req->nr_segments += nr_segs;
	}
	else
		DPRINTK("scsiback: invalid nr_segs = %u\n", nr_segs);
}

static int _scsiback_do_cmd_fn(struct vscsibk_info *info)
{
	struct vscsiif_back_ring *ring = &info->ring;
	vscsiif_request_t  *ring_req;

	pending_req_t *pending_req;
	RING_IDX rc, rp;
	int err, more_to_do = 0;

	DPRINTK("%s\n", __func__);

	rc = ring->req_cons;
	rp = ring->sring->req_prod;
	rmb();

	if (RING_REQUEST_PROD_OVERFLOW(ring, rp)) {
		rc = ring->rsp_prod_pvt;
		pr_warning("scsiback:"
			   " Dom%d provided bogus ring requests (%#x - %#x = %u)."
			   " Halting ring processing\n",
			   info->domid, rp, rc, rp - rc);
		return -EACCES;
	}

	while ((rc != rp)) {
		int act, rqid;

		if (RING_REQUEST_CONS_OVERFLOW(ring, rc))
			break;
		pending_req = info->preq ?: alloc_req(info);
		if (NULL == pending_req) {
			more_to_do = 1;
			break;
		}

		ring_req = RING_GET_REQUEST(ring, rc);
		ring->req_cons = ++rc;

		act = ring_req->act;
		rqid = ring_req->rqid;
		barrier();
		if (!pending_req->nr_segments)
			pending_req->rqid = rqid;
		else if (pending_req->rqid != rqid)
			DPRINTK("scsiback: invalid rqid %04x, expected %04x\n",
				rqid, pending_req->rqid);

		info->preq = NULL;
		if (pending_req->rqid != rqid)
			err = -EINVAL;
		else if (act == VSCSIIF_ACT_SCSI_SG_PRESET) {
			latch_segments(pending_req, (void *)ring_req);
			info->preq = pending_req;
			err = 0;
		} else
			err = prepare_pending_reqs(info, ring_req,
						   pending_req);
		switch (err ?: act) {
		case VSCSIIF_ACT_SCSI_CDB:
			/* The Host mode is through as for Emulation. */
			if (info->feature == VSCSI_TYPE_HOST ?
			    scsiback_cmd_exec(pending_req) :
			    scsiback_req_emulation_or_cmdexec(pending_req)) {
				scsiback_fast_flush_area(pending_req);
				scsiback_do_resp_with_sense(NULL,
							    DRIVER_ERROR << 24,
							    0, pending_req,
							    act);
			}
			break;
		case VSCSIIF_ACT_SCSI_RESET:
			/* Just for pointlessly specified segments: */
			scsiback_fast_flush_area(pending_req);
			scsiback_device_reset_exec(pending_req);
			break;
		case VSCSIIF_ACT_SCSI_SG_PRESET:
			scsiback_do_resp_with_sense(NULL, 0, 0, pending_req,
						    act);
			break;
		default:
			if(!err) {
				scsiback_fast_flush_area(pending_req);
				if (printk_ratelimit())
					pr_err("scsiback: invalid request %#x\n",
					       act);
			}
			scsiback_do_resp_with_sense(NULL, DRIVER_ERROR << 24,
						    0, pending_req, act);
			break;
		case -EINVAL:
			scsiback_do_resp_with_sense(NULL, DRIVER_INVALID << 24,
						    0, pending_req, act);
			break;
		case -ENODEV:
			scsiback_do_resp_with_sense(NULL, DID_NO_CONNECT << 16,
						    0, pending_req, act);
			break;
		}

		/* Yield point for this unbounded loop. */
		cond_resched();
	}

	if (RING_HAS_UNCONSUMED_REQUESTS(ring))
		more_to_do = 1;

	return more_to_do;
}

static int scsiback_do_cmd_fn(struct vscsibk_info *info)
{
	int more_to_do;

	do {
		more_to_do = _scsiback_do_cmd_fn(info);
		if (more_to_do)
			break;

		RING_FINAL_CHECK_FOR_REQUESTS(&info->ring, more_to_do);
	} while (more_to_do);

	return more_to_do;
}


int scsiback_schedule(void *data)
{
	struct vscsibk_info *info = (struct vscsibk_info *)data;

	DPRINTK("%s\n", __func__);

	while (!kthread_should_stop()) {
		wait_event_interruptible(
			info->wq,
			info->waiting_reqs || kthread_should_stop());
		wait_event_interruptible(
			pending_free_wq,
			!list_empty(&pending_free) || kthread_should_stop());

		info->waiting_reqs = 0;
		smp_mb();

		switch (scsiback_do_cmd_fn(info)) {
		case 1:
			info->waiting_reqs = 1;
		case 0:
			break;
		case -EACCES:
			wait_event_interruptible(info->shutdown_wq,
						 kthread_should_stop());
			break;
		default:
			BUG();
		}
	}

	return 0;
}


static int __init scsiback_init(void)
{
	int i, mmap_pages;

	if (!is_running_on_xen())
		return -ENODEV;

	if (vscsiif_segs < VSCSIIF_SG_TABLESIZE)
		vscsiif_segs = VSCSIIF_SG_TABLESIZE;
	if (vscsiif_segs != (uint8_t)vscsiif_segs)
		return -EINVAL;
	mmap_pages = vscsiif_reqs * vscsiif_segs;

	pending_reqs          = kzalloc(sizeof(pending_reqs[0]) *
					vscsiif_reqs, GFP_KERNEL);
	if (!pending_reqs)
		return -ENOMEM;
	pending_grant_handles = kmalloc(sizeof(pending_grant_handles[0]) *
					mmap_pages, GFP_KERNEL);
	pending_pages         = alloc_empty_pages_and_pagevec(mmap_pages);

	if (!pending_grant_handles || !pending_pages)
		goto out_of_memory;

	for (i = 0; i < vscsiif_reqs; ++i) {
		pending_reqs[i].gref = kcalloc(sizeof(*pending_reqs->gref),
					       vscsiif_segs, GFP_KERNEL);
		pending_reqs[i].segs = kcalloc(sizeof(*pending_reqs->segs),
					       vscsiif_segs, GFP_KERNEL);
		if (!pending_reqs[i].gref || !pending_reqs[i].segs)
			goto out_of_memory;
	}

	for (i = 0; i < mmap_pages; i++)
		pending_grant_handles[i] = SCSIBACK_INVALID_HANDLE;

	if (scsiback_interface_init() < 0)
		goto out_of_memory;

	INIT_LIST_HEAD(&pending_free);

	for (i = 0; i < vscsiif_reqs; i++)
		list_add_tail(&pending_reqs[i].free_list, &pending_free);

	if (scsiback_xenbus_init())
		goto out_interface;

	scsiback_emulation_init();

	return 0;

out_interface:
	scsiback_interface_exit();
out_of_memory:
	for (i = 0; i < vscsiif_reqs; ++i) {
		kfree(pending_reqs[i].gref);
		kfree(pending_reqs[i].segs);
	}
	kfree(pending_reqs);
	kfree(pending_grant_handles);
	free_empty_pages_and_pagevec(pending_pages, mmap_pages);
	pr_err("scsiback: %s: out of memory\n", __func__);
	return -ENOMEM;
}

#if 0
static void __exit scsiback_exit(void)
{
	unsigned int i;

	scsiback_xenbus_unregister();
	scsiback_interface_exit();
	for (i = 0; i < vscsiif_reqs; ++i) {
		kfree(pending_reqs[i].gref);
		kfree(pending_reqs[i].segs);
	}
	kfree(pending_reqs);
	kfree(pending_grant_handles);
	free_empty_pages_and_pagevec(pending_pages, vscsiif_reqs * vscsiif_segs);
}
#endif

module_init(scsiback_init);

#if 0
module_exit(scsiback_exit);
#endif

MODULE_DESCRIPTION("Xen SCSI backend driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("xen-backend:vscsi");
