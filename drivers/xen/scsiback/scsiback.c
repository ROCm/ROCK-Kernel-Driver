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
#include <asm/hypervisor.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>

#include "common.h"


struct list_head pending_free;
DEFINE_SPINLOCK(pending_free_lock);
DECLARE_WAIT_QUEUE_HEAD(pending_free_wq);

int vscsiif_reqs = VSCSIIF_BACK_MAX_PENDING_REQS;
module_param_named(reqs, vscsiif_reqs, int, 0);
MODULE_PARM_DESC(reqs, "Number of scsiback requests to allocate");

static unsigned int log_print_stat = 0;
module_param(log_print_stat, int, 0644);

#define SCSIBACK_INVALID_HANDLE (~0)

static pending_req_t *pending_reqs;
static struct page **pending_pages;
static grant_handle_t *pending_grant_handles;

static int vaddr_pagenr(pending_req_t *req, int seg)
{
	return (req - pending_reqs) * VSCSIIF_SG_TABLESIZE + seg;
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
	struct gnttab_unmap_grant_ref unmap[VSCSIIF_SG_TABLESIZE];
	unsigned int i, invcount = 0;
	grant_handle_t handle;
	int err;

	if (req->nr_segments) {
		for (i = 0; i < req->nr_segments; i++) {
			handle = pending_handle(req, i);
			if (handle == SCSIBACK_INVALID_HANDLE)
				continue;
			gnttab_set_unmap_op(&unmap[i], vaddr(req, i),
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
			uint32_t resid, pending_req_t *pending_req)
{
	vscsiif_response_t *ring_res;
	struct vscsibk_info *info = pending_req->info;
	int notify;
	int more_to_do = 1;
	struct scsi_sense_hdr sshdr;
	unsigned long flags;

	DPRINTK("%s\n",__FUNCTION__);

	spin_lock_irqsave(&info->ring_lock, flags);

	ring_res = RING_GET_RESPONSE(&info->ring, info->ring.rsp_prod_pvt);
	info->ring.rsp_prod_pvt++;

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
	if (info->ring.rsp_prod_pvt == info->ring.req_cons) {
		RING_FINAL_CHECK_FOR_REQUESTS(&info->ring, more_to_do);
	} else if (RING_HAS_UNCONSUMED_REQUESTS(&info->ring)) {
		more_to_do = 1;
	}
	
	spin_unlock_irqrestore(&info->ring_lock, flags);

	if (more_to_do)
		scsiback_notify_work(info);

	if (notify)
		notify_remote_via_irq(info->irq);

	free_req(pending_req);
}

static void scsiback_print_status(char *sense_buffer, int errors,
					pending_req_t *pending_req)
{
	struct scsi_device *sdev = pending_req->sdev;
	
	printk(KERN_ERR "scsiback: %d:%d:%d:%d ",sdev->host->host_no,
			sdev->channel, sdev->id, sdev->lun);
	printk(KERN_ERR "status = 0x%02x, message = 0x%02x, host = 0x%02x, driver = 0x%02x\n",
			status_byte(errors), msg_byte(errors),
			host_byte(errors), driver_byte(errors));

	printk(KERN_ERR "scsiback: cmnd[0]=0x%02X\n",
			pending_req->cmnd[0]);

	if (CHECK_CONDITION & status_byte(errors))
		__scsi_print_sense("scsiback", sense_buffer, SCSI_SENSE_BUFFERSIZE);
}


static void scsiback_cmd_done(struct request *req, int uptodate)
{
	pending_req_t *pending_req = req->end_io_data;
	unsigned char *sense_buffer;
	unsigned int resid;
	int errors;

	sense_buffer = req->sense;
	resid        = req->data_len;
	errors       = req->errors;

	if (errors != 0) {
		if (log_print_stat)
			scsiback_print_status(sense_buffer, errors, pending_req);
	}

	/* The Host mode is through as for Emulation. */
	if (pending_req->info->feature != VSCSI_TYPE_HOST)
		scsiback_rsp_emulation(pending_req);

	scsiback_fast_flush_area(pending_req);
	scsiback_do_resp_with_sense(sense_buffer, errors, resid, pending_req);
	scsiback_put(pending_req->info);

	__blk_put_request(req->q, req);
}


static int scsiback_gnttab_data_map(vscsiif_request_t *ring_req,
					pending_req_t *pending_req)
{
	u32 flags;
	int write;
	int i, err = 0;
	unsigned int data_len = 0;
	struct gnttab_map_grant_ref map[VSCSIIF_SG_TABLESIZE];
	struct vscsibk_info *info   = pending_req->info;

	int data_dir = (int)pending_req->sc_data_direction;
	unsigned int nr_segments = (unsigned int)pending_req->nr_segments;

	write = (data_dir == DMA_TO_DEVICE);

	if (nr_segments) {
		struct scatterlist *sg;

		/* free of (sgl) in fast_flush_area()*/
		pending_req->sgl = kmalloc(sizeof(struct scatterlist) * nr_segments,
						GFP_KERNEL);
		if (!pending_req->sgl) {
			printk(KERN_ERR "scsiback: %s: kmalloc() error.\n", __FUNCTION__);
			return -ENOMEM;
		}

		sg_init_table(pending_req->sgl, nr_segments);

		for (i = 0; i < nr_segments; i++) {
			flags = GNTMAP_host_map;
			if (write)
				flags |= GNTMAP_readonly;
			gnttab_set_map_op(&map[i], vaddr(pending_req, i), flags,
						ring_req->seg[i].gref,
						info->domid);
		}

		err = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map, nr_segments);
		BUG_ON(err);

		for_each_sg (pending_req->sgl, sg, nr_segments, i) {
			if (unlikely(map[i].status != 0)) {
				printk(KERN_ERR "scsiback: invalid buffer -- could not remap it\n");
				map[i].handle = SCSIBACK_INVALID_HANDLE;
				err |= 1;
			}

			pending_handle(pending_req, i) = map[i].handle;

			if (err)
				continue;

			set_phys_to_machine(__pa(vaddr(
				pending_req, i)) >> PAGE_SHIFT,
				FOREIGN_FRAME(map[i].dev_bus_addr >> PAGE_SHIFT));

			sg_set_page(sg, virt_to_page(vaddr(pending_req, i)),
				    ring_req->seg[i].length,
				    ring_req->seg[i].offset);
			data_len += sg->length;

			barrier();
			if (sg->offset >= PAGE_SIZE ||
			    sg->length > PAGE_SIZE ||
			    sg->offset + sg->length > PAGE_SIZE)
				err |= 1;

		}

		if (err)
			goto fail_flush;
	}
	
	pending_req->request_bufflen = data_len;
	
	return 0;
	
fail_flush:
	scsiback_fast_flush_area(pending_req);
	return -ENOMEM;
}

/* quoted scsi_lib.c/scsi_merge_bio */
static int scsiback_merge_bio(struct request *rq, struct bio *bio)
{
	struct request_queue *q = rq->q;

	bio->bi_flags &= ~(1 << BIO_SEG_VALID);
	if (rq_data_dir(rq) == WRITE)
		bio->bi_rw |= (1 << BIO_RW);

	blk_queue_bounce(q, &bio);

	return blk_rq_append_bio(q, rq, bio);
}


/* quoted scsi_lib.c/scsi_bi_endio */
static void scsiback_bi_endio(struct bio *bio, int error)
{
	bio_put(bio);
}



/* quoted scsi_lib.c/scsi_req_map_sg . */
static int request_map_sg(struct request *rq, pending_req_t *pending_req, unsigned int count)
{
	struct request_queue *q = rq->q;
	int nr_pages;
	unsigned int nsegs = count;
	unsigned int data_len = 0, len, bytes, off;
	struct scatterlist *sg;
	struct page *page;
	struct bio *bio = NULL;
	int i, err, nr_vecs = 0;

	for_each_sg (pending_req->sgl, sg, nsegs, i) {
		page = sg_page(sg);
		off = sg->offset;
		len = sg->length;
		data_len += len;

		nr_pages = (len + off + PAGE_SIZE - 1) >> PAGE_SHIFT;
		while (len > 0) {
			bytes = min_t(unsigned int, len, PAGE_SIZE - off);

			if (!bio) {
				nr_vecs = min_t(int, BIO_MAX_PAGES, nr_pages);
				nr_pages -= nr_vecs;
				bio = bio_alloc(GFP_KERNEL, nr_vecs);
				if (!bio) {
					err = -ENOMEM;
					goto free_bios;
				}
				bio->bi_end_io = scsiback_bi_endio;
			}

			if (bio_add_pc_page(q, bio, page, bytes, off) !=
						bytes) {
				bio_put(bio);
				err = -EINVAL;
				goto free_bios;
			}

			if (bio->bi_vcnt >= nr_vecs) {
				err = scsiback_merge_bio(rq, bio);
				if (err) {
					bio_endio(bio, 0);
					goto free_bios;
				}
				bio = NULL;
			}

			page++;
			len -= bytes;
			off = 0;
		}
	}

	rq->buffer   = rq->data = NULL;
	rq->data_len = data_len;

	return 0;

free_bios:
	while ((bio = rq->bio) != NULL) {
		rq->bio = bio->bi_next;
		/*
		 * call endio instead of bio_put incase it was bounced
		 */
		bio_endio(bio, 0);
	}

	return err;
}


void scsiback_cmd_exec(pending_req_t *pending_req)
{
	int cmd_len  = (int)pending_req->cmd_len;
	int data_dir = (int)pending_req->sc_data_direction;
	unsigned int nr_segments = (unsigned int)pending_req->nr_segments;
	unsigned int timeout;
	struct request *rq;
	int write;

	DPRINTK("%s\n",__FUNCTION__);

	/* because it doesn't timeout backend earlier than frontend.*/
	if (pending_req->timeout_per_command)
		timeout = pending_req->timeout_per_command * HZ;
	else
		timeout = VSCSIIF_TIMEOUT;

	write = (data_dir == DMA_TO_DEVICE);
	rq = blk_get_request(pending_req->sdev->request_queue, write, GFP_KERNEL);

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

	if (nr_segments) {

		if (request_map_sg(rq, pending_req, nr_segments)) {
			printk(KERN_ERR "scsiback: SG Request Map Error\n");
			return;
		}
	}

	scsiback_get(pending_req->info);
	blk_execute_rq_nowait(rq->q, NULL, rq, 1, scsiback_cmd_done);

	return ;
}


static void scsiback_device_reset_exec(pending_req_t *pending_req)
{
	struct vscsibk_info *info = pending_req->info;
	int err;
	struct scsi_device *sdev = pending_req->sdev;

	scsiback_get(info);
	err = scsi_reset_provider(sdev, SCSI_TRY_RESET_DEVICE);

	scsiback_do_resp_with_sense(NULL, err, 0, pending_req);
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
	int err = -EINVAL;

	DPRINTK("%s\n",__FUNCTION__);

	pending_req->rqid       = ring_req->rqid;
	pending_req->act        = ring_req->act;

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

	pending_req->nr_segments = ring_req->nr_segments;
	barrier();
	if (pending_req->nr_segments > VSCSIIF_SG_TABLESIZE) {
		DPRINTK("scsiback: invalid parameter nr_seg = %d\n",
			pending_req->nr_segments);
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

	if(scsiback_gnttab_data_map(ring_req, pending_req)) {
		DPRINTK("scsiback: invalid buffer\n");
		err = -EINVAL;
		goto invalid_value;
	}

	return 0;

invalid_value:
	return err;
}


static int scsiback_do_cmd_fn(struct vscsibk_info *info)
{
	struct vscsiif_back_ring *ring = &info->ring;
	vscsiif_request_t  *ring_req;

	pending_req_t *pending_req;
	RING_IDX rc, rp;
	int err, more_to_do = 0;

	DPRINTK("%s\n",__FUNCTION__);

	rc = ring->req_cons;
	rp = ring->sring->req_prod;
	rmb();

	while ((rc != rp)) {
		if (RING_REQUEST_CONS_OVERFLOW(ring, rc))
			break;
		pending_req = alloc_req(info);
		if (NULL == pending_req) {
			more_to_do = 1;
			break;
		}

		ring_req = RING_GET_REQUEST(ring, rc);
		ring->req_cons = ++rc;

		err = prepare_pending_reqs(info, ring_req,
						pending_req);
		if (err == -EINVAL) {
			scsiback_do_resp_with_sense(NULL, (DRIVER_ERROR << 24),
				0, pending_req);
			continue;
		} else if (err == -ENODEV) {
			scsiback_do_resp_with_sense(NULL, (DID_NO_CONNECT << 16),
				0, pending_req);
			continue;
		}

		if (pending_req->act == VSCSIIF_ACT_SCSI_CDB) {

			/* The Host mode is through as for Emulation. */
			if (info->feature == VSCSI_TYPE_HOST)
				scsiback_cmd_exec(pending_req);
			else
				scsiback_req_emulation_or_cmdexec(pending_req);

		} else if (pending_req->act == VSCSIIF_ACT_SCSI_RESET) {
			scsiback_device_reset_exec(pending_req);
		} else {
			printk(KERN_ERR "scsiback: invalid parameter for request\n");
			scsiback_do_resp_with_sense(NULL, (DRIVER_ERROR << 24),
				0, pending_req);
			continue;
		}
	}

	if (RING_HAS_UNCONSUMED_REQUESTS(ring))
		more_to_do = 1;

	/* Yield point for this unbounded loop. */
	cond_resched();

	return more_to_do;
}


int scsiback_schedule(void *data)
{
	struct vscsibk_info *info = (struct vscsibk_info *)data;

	DPRINTK("%s\n",__FUNCTION__);

	while (!kthread_should_stop()) {
		wait_event_interruptible(
			info->wq,
			info->waiting_reqs || kthread_should_stop());
		wait_event_interruptible(
			pending_free_wq,
			!list_empty(&pending_free) || kthread_should_stop());

		info->waiting_reqs = 0;
		smp_mb();

		if (scsiback_do_cmd_fn(info))
			info->waiting_reqs = 1;
	}

	return 0;
}


static int __init scsiback_init(void)
{
	int i, mmap_pages;

	if (!is_running_on_xen())
		return -ENODEV;

	mmap_pages = vscsiif_reqs * VSCSIIF_SG_TABLESIZE;

	pending_reqs          = kmalloc(sizeof(pending_reqs[0]) *
					vscsiif_reqs, GFP_KERNEL);
	pending_grant_handles = kmalloc(sizeof(pending_grant_handles[0]) *
					mmap_pages, GFP_KERNEL);
	pending_pages         = alloc_empty_pages_and_pagevec(mmap_pages);

	if (!pending_reqs || !pending_grant_handles || !pending_pages)
		goto out_of_memory;

	for (i = 0; i < mmap_pages; i++)
		pending_grant_handles[i] = SCSIBACK_INVALID_HANDLE;

	if (scsiback_interface_init() < 0)
		goto out_of_kmem;

	memset(pending_reqs, 0, sizeof(pending_reqs));
	INIT_LIST_HEAD(&pending_free);

	for (i = 0; i < vscsiif_reqs; i++)
		list_add_tail(&pending_reqs[i].free_list, &pending_free);

	if (scsiback_xenbus_init())
		goto out_of_xenbus;

	scsiback_emulation_init();

	return 0;

out_of_xenbus:
	scsiback_xenbus_unregister();
out_of_kmem:
	scsiback_interface_exit();
out_of_memory:
	kfree(pending_reqs);
	kfree(pending_grant_handles);
	free_empty_pages_and_pagevec(pending_pages, mmap_pages);
	printk(KERN_ERR "scsiback: %s: out of memory\n", __FUNCTION__);
	return -ENOMEM;
}

#if 0
static void __exit scsiback_exit(void)
{
	scsiback_xenbus_unregister();
	scsiback_interface_exit();
	kfree(pending_reqs);
	kfree(pending_grant_handles);
	free_empty_pages_and_pagevec(pending_pages, (vscsiif_reqs * VSCSIIF_SG_TABLESIZE));

}
#endif

module_init(scsiback_init);

#if 0
module_exit(scsiback_exit);
#endif

MODULE_DESCRIPTION("Xen SCSI backend driver");
MODULE_LICENSE("Dual BSD/GPL");
