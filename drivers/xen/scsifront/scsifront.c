/*
 * Xen SCSI frontend driver
 *
 * Copyright (c) 2008, FUJITSU Limited
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
 
#include "common.h"
#include <linux/pfn.h>

#define PREFIX(lvl) KERN_##lvl "scsifront: "

static int get_id_from_freelist(struct vscsifrnt_info *info)
{
	unsigned long flags;
	uint32_t free;

	spin_lock_irqsave(&info->shadow_lock, flags);

	free = info->shadow_free;
	BUG_ON(free > VSCSIIF_MAX_REQS);
	info->shadow_free = info->shadow[free].next_free;
	info->shadow[free].next_free = 0x0fff;

	info->shadow[free].wait_reset = 0;

	spin_unlock_irqrestore(&info->shadow_lock, flags);

	return free;
}

static void _add_id_to_freelist(struct vscsifrnt_info *info, uint32_t id)
{
	info->shadow[id].next_free = info->shadow_free;
	info->shadow[id].sc = NULL;
	info->shadow_free = id;
}

static void add_id_to_freelist(struct vscsifrnt_info *info, uint32_t id)
{
	unsigned long flags;

	spin_lock_irqsave(&info->shadow_lock, flags);
	_add_id_to_freelist(info, id);
	spin_unlock_irqrestore(&info->shadow_lock, flags);
}


struct vscsiif_request * scsifront_pre_request(struct vscsifrnt_info *info)
{
	struct vscsiif_front_ring *ring = &(info->ring);
	vscsiif_request_t *ring_req;
	uint32_t id;

	ring_req = RING_GET_REQUEST(&(info->ring), ring->req_prod_pvt);

	ring->req_prod_pvt++;
	
	id = get_id_from_freelist(info);	/* use id by response */
	ring_req->rqid = (uint16_t)id;

	return ring_req;
}


static void scsifront_notify_work(struct vscsifrnt_info *info)
{
	info->waiting_resp = 1;
	wake_up(&info->wq);
}


static void scsifront_do_request(struct vscsifrnt_info *info)
{
	struct vscsiif_front_ring *ring = &(info->ring);
	unsigned int irq = info->irq;
	int notify;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(ring, notify);
	if (notify)
		notify_remote_via_irq(irq);
}

irqreturn_t scsifront_intr(int irq, void *dev_id)
{
	scsifront_notify_work((struct vscsifrnt_info *)dev_id);
	return IRQ_HANDLED;
}


static void scsifront_gnttab_done(struct vscsifrnt_info *info, uint32_t id)
{
	struct vscsifrnt_shadow *s = &info->shadow[id];
	int i;

	if (s->sc->sc_data_direction == DMA_NONE)
		return;

	for (i = 0; i < s->nr_segments; i++) {
		if (unlikely(gnttab_query_foreign_access(s->gref[i]) != 0)) {
			shost_printk(PREFIX(ALERT), info->host,
				     "grant still in use by backend\n");
			BUG();
		}
		gnttab_end_foreign_access(s->gref[i], 0UL);
	}
}


static void scsifront_cdb_cmd_done(struct vscsifrnt_info *info,
		       vscsiif_response_t *ring_res)
{
	struct scsi_cmnd *sc;
	uint32_t id;
	uint8_t sense_len;

	id = ring_res->rqid;
	sc = info->shadow[id].sc;

	if (sc == NULL)
		BUG();

	scsifront_gnttab_done(info, id);
	add_id_to_freelist(info, id);

	sc->result = ring_res->rslt;
	scsi_set_resid(sc, ring_res->residual_len);

	if (ring_res->sense_len > VSCSIIF_SENSE_BUFFERSIZE)
		sense_len = VSCSIIF_SENSE_BUFFERSIZE;
	else
		sense_len = ring_res->sense_len;

	if (sense_len)
		memcpy(sc->sense_buffer, ring_res->sense_buffer, sense_len);

	sc->scsi_done(sc);

	return;
}


static void scsifront_sync_cmd_done(struct vscsifrnt_info *info,
				vscsiif_response_t *ring_res)
{
	uint16_t id = ring_res->rqid;
	unsigned long flags;
	
	spin_lock_irqsave(&info->shadow_lock, flags);
	info->shadow[id].wait_reset = 1;
	switch (info->shadow[id].rslt_reset) {
	case 0:
		info->shadow[id].rslt_reset = ring_res->rslt;
		break;
	case -1:
		_add_id_to_freelist(info, id);
		break;
	default:
		shost_printk(PREFIX(ERR), info->host,
			     "bad reset state %d, possibly leaking %u\n",
			     info->shadow[id].rslt_reset, id);
		break;
	}
	spin_unlock_irqrestore(&info->shadow_lock, flags);

	wake_up(&(info->shadow[id].wq_reset));
}


static int scsifront_cmd_done(struct vscsifrnt_info *info)
{
	vscsiif_response_t *ring_res;

	RING_IDX i, rp;
	int more_to_do = 0;
	unsigned long flags;

	spin_lock_irqsave(info->host->host_lock, flags);

	rp = info->ring.sring->rsp_prod;
	rmb();
	for (i = info->ring.rsp_cons; i != rp; i++) {
		
		ring_res = RING_GET_RESPONSE(&info->ring, i);

		if (info->shadow[ring_res->rqid].act == VSCSIIF_ACT_SCSI_CDB)
			scsifront_cdb_cmd_done(info, ring_res);
		else
			scsifront_sync_cmd_done(info, ring_res);
	}

	info->ring.rsp_cons = i;

	if (i != info->ring.req_prod_pvt) {
		RING_FINAL_CHECK_FOR_RESPONSES(&info->ring, more_to_do);
	} else {
		info->ring.sring->rsp_event = i + 1;
	}

	info->waiting_sync = 0;

	spin_unlock_irqrestore(info->host->host_lock, flags);

	wake_up(&info->wq_sync);

	/* Yield point for this unbounded loop. */
	cond_resched();

	return more_to_do;
}




int scsifront_schedule(void *data)
{
	struct vscsifrnt_info *info = (struct vscsifrnt_info *)data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(
			info->wq,
			info->waiting_resp || kthread_should_stop());

		info->waiting_resp = 0;
		smp_mb();

		if (scsifront_cmd_done(info))
			info->waiting_resp = 1;
	}

	return 0;
}



static int map_data_for_request(struct vscsifrnt_info *info,
		struct scsi_cmnd *sc, vscsiif_request_t *ring_req, uint32_t id)
{
	grant_ref_t gref_head;
	struct page *page;
	int err, ref, ref_cnt = 0;
	int write = (sc->sc_data_direction == DMA_TO_DEVICE);
	unsigned int i, nr_pages, off, len, bytes;
	unsigned int data_len = scsi_bufflen(sc);

	if (sc->sc_data_direction == DMA_NONE || !data_len)
		return 0;

	err = gnttab_alloc_grant_references(VSCSIIF_SG_TABLESIZE, &gref_head);
	if (err) {
		shost_printk(PREFIX(ERR), info->host,
			     "gnttab_alloc_grant_references() error\n");
		return -ENOMEM;
	}

	/* quoted scsi_lib.c/scsi_req_map_sg . */
	nr_pages = PFN_UP(data_len + scsi_sglist(sc)->offset);
	if (nr_pages > VSCSIIF_SG_TABLESIZE) {
		shost_printk(PREFIX(ERR), info->host,
			     "Unable to map request_buffer for command!\n");
		ref_cnt = -E2BIG;
	} else {
		struct scatterlist *sg, *sgl = scsi_sglist(sc);

		for_each_sg (sgl, sg, scsi_sg_count(sc), i) {
			page = sg_page(sg);
			off = sg->offset;
			len = sg->length;

			while (len > 0 && data_len > 0) {
				/*
				 * sg sends a scatterlist that is larger than
				 * the data_len it wants transferred for certain
				 * IO sizes
				 */
				bytes = min_t(unsigned int, len, PAGE_SIZE - off);
				bytes = min(bytes, data_len);
				
				ref = gnttab_claim_grant_reference(&gref_head);
				BUG_ON(ref == -ENOSPC);

				gnttab_grant_foreign_access_ref(ref, info->dev->otherend_id,
					page_to_phys(page) >> PAGE_SHIFT, write);

				info->shadow[id].gref[ref_cnt]  = ref;
				ring_req->seg[ref_cnt].gref     = ref;
				ring_req->seg[ref_cnt].offset   = (uint16_t)off;
				ring_req->seg[ref_cnt].length   = (uint16_t)bytes;

				page++;
				len -= bytes;
				data_len -= bytes;
				off = 0;
				ref_cnt++;
			}
		}
	}

	gnttab_free_grant_references(gref_head);

	return ref_cnt;
}

static int scsifront_queuecommand(struct Scsi_Host *shost,
				  struct scsi_cmnd *sc)
{
	struct vscsifrnt_info *info = shost_priv(shost);
	vscsiif_request_t *ring_req;
	unsigned long flags;
	int ref_cnt;
	uint16_t rqid;

/* debug printk to identify more missing scsi commands
	shost_printk(KERN_INFO "scsicmd: ", sc->device->host,
		     "len=%u %#x,%#x,%#x,%#x,%#x,%#x,%#x,%#x,%#x,%#x\n",
		     sc->cmd_len, sc->cmnd[0], sc->cmnd[1],
		     sc->cmnd[2], sc->cmnd[3], sc->cmnd[4], sc->cmnd[5],
		     sc->cmnd[6], sc->cmnd[7], sc->cmnd[8], sc->cmnd[9]);
*/
	spin_lock_irqsave(shost->host_lock, flags);
	scsi_cmd_get_serial(shost, sc);
	if (RING_FULL(&info->ring)) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	sc->result    = 0;

	ring_req          = scsifront_pre_request(info);
	rqid              = ring_req->rqid;
	ring_req->act     = VSCSIIF_ACT_SCSI_CDB;

	ring_req->id      = sc->device->id;
	ring_req->lun     = sc->device->lun;
	ring_req->channel = sc->device->channel;
	ring_req->cmd_len = sc->cmd_len;

	BUG_ON(sc->cmd_len > VSCSIIF_MAX_COMMAND_SIZE);

	if ( sc->cmd_len )
		memcpy(ring_req->cmnd, sc->cmnd, sc->cmd_len);
	else
		memset(ring_req->cmnd, 0, VSCSIIF_MAX_COMMAND_SIZE);

	ring_req->sc_data_direction   = (uint8_t)sc->sc_data_direction;
	ring_req->timeout_per_command = (sc->request->timeout / HZ);

	info->shadow[rqid].sc  = sc;
	info->shadow[rqid].act = VSCSIIF_ACT_SCSI_CDB;

	ref_cnt = map_data_for_request(info, sc, ring_req, rqid);
	if (ref_cnt < 0) {
		add_id_to_freelist(info, rqid);
		spin_unlock_irqrestore(shost->host_lock, flags);
		if (ref_cnt == (-ENOMEM))
			return SCSI_MLQUEUE_HOST_BUSY;
		sc->result = (DID_ERROR << 16);
		sc->scsi_done(sc);
		return 0;
	}

	ring_req->nr_segments          = (uint8_t)ref_cnt;
	info->shadow[rqid].nr_segments = ref_cnt;

	scsifront_do_request(info);
	spin_unlock_irqrestore(shost->host_lock, flags);

	return 0;
}


static int scsifront_eh_abort_handler(struct scsi_cmnd *sc)
{
	return (FAILED);
}

/* vscsi supports only device_reset, because it is each of LUNs */
static int scsifront_dev_reset_handler(struct scsi_cmnd *sc)
{
	struct Scsi_Host *host = sc->device->host;
	struct vscsifrnt_info *info = shost_priv(host);

	vscsiif_request_t *ring_req;
	uint16_t rqid;
	int err = 0;

	for (;;) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12)
		spin_lock_irq(host->host_lock);
#endif
		if (!RING_FULL(&info->ring))
			break;
		if (err) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12)
			spin_unlock_irq(host->host_lock);
#endif
			return FAILED;
		}
		info->waiting_sync = 1;
		spin_unlock_irq(host->host_lock);
		err = wait_event_interruptible(info->wq_sync,
					       !info->waiting_sync);
		spin_lock_irq(host->host_lock);
	}

	ring_req      = scsifront_pre_request(info);
	ring_req->act = VSCSIIF_ACT_SCSI_RESET;

	rqid          = ring_req->rqid;
	info->shadow[rqid].act = VSCSIIF_ACT_SCSI_RESET;
	info->shadow[rqid].rslt_reset = 0;

	ring_req->channel = sc->device->channel;
	ring_req->id      = sc->device->id;
	ring_req->lun     = sc->device->lun;
	ring_req->cmd_len = sc->cmd_len;

	if ( sc->cmd_len )
		memcpy(ring_req->cmnd, sc->cmnd, sc->cmd_len);
	else
		memset(ring_req->cmnd, 0, VSCSIIF_MAX_COMMAND_SIZE);

	ring_req->sc_data_direction   = (uint8_t)sc->sc_data_direction;
	ring_req->timeout_per_command = (sc->request->timeout / HZ);
	ring_req->nr_segments         = 0;

	scsifront_do_request(info);	

	spin_unlock_irq(host->host_lock);
	err = wait_event_interruptible(info->shadow[rqid].wq_reset,
				       info->shadow[rqid].wait_reset);
	spin_lock_irq(host->host_lock);

	if (!err) {
		err = info->shadow[rqid].rslt_reset;
		add_id_to_freelist(info, rqid);
	} else {
		spin_lock(&info->shadow_lock);
		info->shadow[rqid].rslt_reset = -1;
		spin_unlock(&info->shadow_lock);
		err = FAILED;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12)
	spin_unlock_irq(host->host_lock);
#endif
	return (err);
}


struct scsi_host_template scsifront_sht = {
	.module			= THIS_MODULE,
	.name			= "Xen SCSI frontend driver",
	.queuecommand		= scsifront_queuecommand,
	.eh_abort_handler	= scsifront_eh_abort_handler,
	.eh_device_reset_handler= scsifront_dev_reset_handler,
	.cmd_per_lun		= VSCSIIF_DEFAULT_CMD_PER_LUN,
	.can_queue		= VSCSIIF_MAX_REQS,
	.this_id 		= -1,
	.sg_tablesize		= VSCSIIF_SG_TABLESIZE,
	.use_clustering		= DISABLE_CLUSTERING,
	.proc_name		= "scsifront",
};


static int __init scsifront_init(void)
{
	int err;

	if (!is_running_on_xen())
		return -ENODEV;

	err = scsifront_xenbus_init();

	return err;
}

static void __exit scsifront_exit(void)
{
	scsifront_xenbus_unregister();
}

module_init(scsifront_init);
module_exit(scsifront_exit);

MODULE_DESCRIPTION("Xen SCSI frontend driver");
MODULE_LICENSE("GPL");
