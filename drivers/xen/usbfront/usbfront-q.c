/*
 * usbfront-q.c
 *
 * Xen USB Virtual Host Controller - RING operations.
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * or, by your choice,
 *
 * When distributed separately from the Linux kernel or incorporated into
 * other software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

struct kmem_cache *xenhcd_urbp_cachep;

static struct urb_priv *alloc_urb_priv(struct urb *urb)
{
	struct urb_priv *urbp;

	urbp = kmem_cache_zalloc(xenhcd_urbp_cachep, GFP_ATOMIC);
	if (!urbp) {
		return NULL;
	}

	urbp->urb = urb;
	urb->hcpriv = urbp;
	urbp->req_id = ~0;
	INIT_LIST_HEAD(&urbp->list);

	return urbp;
}

static void free_urb_priv(struct urb_priv *urbp)
{
	urbp->urb->hcpriv = NULL;
	kmem_cache_free(xenhcd_urbp_cachep, urbp);
}

static inline int get_id_from_freelist(
	struct usbfront_info *info)
{
	unsigned long free;
	free = info->shadow_free;
	BUG_ON(free > USB_RING_SIZE);
	info->shadow_free = info->shadow[free].req.id;
	info->shadow[free].req.id = (unsigned int)0x0fff; /* debug */
	return free;
}

static inline void add_id_to_freelist(
	struct usbfront_info *info, unsigned long id)
{
	info->shadow[id].req.id  = info->shadow_free;
	info->shadow[id].urb = NULL;
	info->shadow_free = id;
}

static inline int count_pages(void *addr, int length)
{
	unsigned long start = (unsigned long) addr >> PAGE_SHIFT;
	unsigned long end = (unsigned long) (addr + length + PAGE_SIZE -1) >> PAGE_SHIFT;
	return end - start;
}

static inline void xenhcd_gnttab_map(struct usbfront_info *info,
		void *addr, int length, grant_ref_t *gref_head,
		struct usbif_request_segment *seg, int nr_pages, int flags)
{
	grant_ref_t ref;
	struct page *page;
	unsigned long buffer_pfn;
	unsigned int offset;
	unsigned int len;
	unsigned int bytes;
	int i;

	len = length;

	for(i = 0;i < nr_pages;i++){
		BUG_ON(!len);

		page = virt_to_page(addr);
		buffer_pfn = page_to_phys(page) >> PAGE_SHIFT;
		offset = offset_in_page(addr);

		bytes = PAGE_SIZE - offset;
		if(bytes > len)
			bytes = len;

		ref = gnttab_claim_grant_reference(gref_head);
		BUG_ON(ref == -ENOSPC);
		gnttab_grant_foreign_access_ref(ref, info->xbdev->otherend_id, buffer_pfn, flags);
		seg[i].gref = ref;
		seg[i].offset = (uint16_t)offset;
		seg[i].length = (uint16_t)bytes;

		addr += bytes;
		len -= bytes;
	}
}

static int map_urb_for_request(struct usbfront_info *info, struct urb *urb,
		usbif_request_t *req)
{
	grant_ref_t gref_head;
	int nr_buff_pages = 0;
	int nr_isodesc_pages = 0;
	int ret = 0;

	if (urb->transfer_buffer_length) {
		nr_buff_pages = count_pages(urb->transfer_buffer, urb->transfer_buffer_length);

		if (usb_pipeisoc(urb->pipe))
			nr_isodesc_pages = count_pages(&urb->iso_frame_desc[0],
					sizeof(struct usb_iso_packet_descriptor) * urb->number_of_packets);

		if (nr_buff_pages + nr_isodesc_pages > USBIF_MAX_SEGMENTS_PER_REQUEST)
			return -E2BIG;

		ret = gnttab_alloc_grant_references(USBIF_MAX_SEGMENTS_PER_REQUEST, &gref_head);
		if (ret) {
			printk(KERN_ERR "usbfront: gnttab_alloc_grant_references() error\n");
			return -ENOMEM;
		}

		xenhcd_gnttab_map(info, urb->transfer_buffer,
				urb->transfer_buffer_length,
				&gref_head, &req->seg[0], nr_buff_pages,
				usb_pipein(urb->pipe) ? 0 : GTF_readonly);

		if (!usb_pipeisoc(urb->pipe))
			gnttab_free_grant_references(gref_head);
	}

	req->pipe = usbif_setportnum_pipe(urb->pipe, urb->dev->portnum);
	req->transfer_flags = urb->transfer_flags;
	req->buffer_length = urb->transfer_buffer_length;
	req->nr_buffer_segs = nr_buff_pages;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		req->u.isoc.interval = urb->interval;
		req->u.isoc.start_frame = urb->start_frame;
		req->u.isoc.number_of_packets = urb->number_of_packets;
		req->u.isoc.nr_frame_desc_segs = nr_isodesc_pages;
		/*
		 * urb->number_of_packets must be > 0
		 */
		if (unlikely(urb->number_of_packets <= 0))
			BUG();
		xenhcd_gnttab_map(info, &urb->iso_frame_desc[0],
				sizeof(struct usb_iso_packet_descriptor) * urb->number_of_packets,
				&gref_head, &req->seg[nr_buff_pages], nr_isodesc_pages, 0);
		gnttab_free_grant_references(gref_head);
		break;
	case PIPE_INTERRUPT:
		req->u.intr.interval = urb->interval;
		break;
	case PIPE_CONTROL:
		if (urb->setup_packet)
			memcpy(req->u.ctrl, urb->setup_packet, 8);
		break;
	case PIPE_BULK:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void xenhcd_gnttab_done(struct usb_shadow *shadow)
{
	int nr_segs = 0;
	int i;

	nr_segs = shadow->req.nr_buffer_segs;

	if (usb_pipeisoc(shadow->req.pipe))
		nr_segs +=  shadow->req.u.isoc.nr_frame_desc_segs;

	for (i = 0; i < nr_segs; i++)
		gnttab_end_foreign_access(shadow->req.seg[i].gref, 0UL);
}

static void xenhcd_giveback_urb(struct usbfront_info *info, struct urb *urb)
__releases(info->lock)
__acquires(info->lock)
{
	struct urb_priv *urbp = (struct urb_priv *) urb->hcpriv;

	list_del_init(&urbp->list);
	free_urb_priv(urbp);
	switch (urb->status) {
	case -ECONNRESET:
	case -ENOENT:
		COUNT(info->stats.unlink);
		break;
	default:
		COUNT(info->stats.complete);
	}
	spin_unlock(&info->lock);
	usb_hcd_giveback_urb(info_to_hcd(info), urb,
			     urbp->status <= 0 ? urbp->status : urb->status);
	spin_lock(&info->lock);
}

static inline int xenhcd_do_request(struct usbfront_info *info, struct urb_priv *urbp)
{
	usbif_request_t *ring_req;
	struct urb *urb = urbp->urb;
	uint16_t id;
	int notify;
	int ret = 0;

	ring_req = RING_GET_REQUEST(&info->ring, info->ring.req_prod_pvt);
	id = get_id_from_freelist(info);
	ring_req->id = id;

	ret = map_urb_for_request(info, urb, ring_req);
	if (ret < 0) {
		add_id_to_freelist(info, id);
		return ret;
	}

	info->ring.req_prod_pvt++;
	info->shadow[id].urb = urb;
	info->shadow[id].req = *ring_req;
	urbp->req_id = id;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&info->ring, notify);
	if (notify)
		notify_remote_via_irq(info->irq);

	return ret;
}

static void xenhcd_kick_pending_urbs(struct usbfront_info *info)
{
	struct urb_priv *urbp;
	int ret;

	while (!list_empty(&info->pending_urbs)) {
		if (RING_FULL(&info->ring)) {
			COUNT(info->stats.ring_full);
			timer_action(info, TIMER_RING_WATCHDOG);
			goto done;
		}

		urbp = list_entry(info->pending_urbs.next, struct urb_priv, list);
		ret = xenhcd_do_request(info, urbp);
		if (ret == 0)
			list_move_tail(&urbp->list, &info->inprogress_urbs);
		else
			xenhcd_giveback_urb(info, urbp->urb);
	}
	timer_action_done(info, TIMER_SCAN_PENDING_URBS);

done:
	return;
}

static void xenhcd_giveback_unlinked_urbs(struct usbfront_info *info)
{
	struct urb_priv *urbp, *tmp;

	list_for_each_entry_safe(urbp, tmp, &info->unlinked_urbs, list) {
		xenhcd_giveback_urb(info, urbp->urb);
	}
}

static int xenhcd_submit_urb(struct usbfront_info *info, struct urb_priv *urbp)
{
	int ret = 0;

	if (RING_FULL(&info->ring)) {
		list_add_tail(&urbp->list, &info->pending_urbs);
		COUNT(info->stats.ring_full);
		timer_action(info, TIMER_RING_WATCHDOG);
		goto done;
	}

	if (!list_empty(&info->pending_urbs)) {
		list_add_tail(&urbp->list, &info->pending_urbs);
		timer_action(info, TIMER_SCAN_PENDING_URBS);
		goto done;
	}

	ret = xenhcd_do_request(info, urbp);
	if (ret == 0)
		list_add_tail(&urbp->list, &info->inprogress_urbs);

done:
	return ret;
}

static int xenhcd_unlink_urb(struct usbfront_info *info, struct urb_priv *urbp)
{
	if (urbp->unlinked)
		return -EBUSY;
	urbp->unlinked = 1;

	/* if the urb is in pending_urbs */
	if (urbp->req_id == ~0) {
		list_move_tail(&urbp->list, &info->unlinked_urbs);
		timer_action(info, TIMER_SCAN_PENDING_URBS);
	}

	/* TODO: send cancel request to backend */

	return 0;
}

static int xenhcd_end_submit_urb(struct usbfront_info *info)
{
	usbif_response_t *ring_res;
	struct urb *urb;
	struct urb_priv *urbp;

	RING_IDX i, rp;
	uint16_t id;
	int more_to_do = 0;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	rp = info->ring.sring->rsp_prod;
	rmb(); /* ensure we see queued responses up to "rp" */

	for (i = info->ring.rsp_cons; i != rp; i++) {
		ring_res = RING_GET_RESPONSE(&info->ring, i);
		id = ring_res->id;
		xenhcd_gnttab_done(&info->shadow[id]);
		urb = info->shadow[id].urb;
		barrier();
		add_id_to_freelist(info, id);

		urbp = (struct urb_priv *)urb->hcpriv;
		if (likely(!urbp->unlinked)) {
			urb->status = ring_res->status;
			urb->actual_length = ring_res->actual_length;
			urb->error_count = ring_res->error_count;
			urb->start_frame = ring_res->start_frame;
		}
		barrier();
		xenhcd_giveback_urb(info, urb);
	}
	info->ring.rsp_cons = i;

	if (i != info->ring.req_prod_pvt)
		RING_FINAL_CHECK_FOR_RESPONSES(&info->ring, more_to_do);
	else
		info->ring.sring->rsp_event = i + 1;

	spin_unlock_irqrestore(&info->lock, flags);

	cond_resched();

	return more_to_do;
}

int xenhcd_schedule(void *arg)
{
	struct usbfront_info *info = (struct usbfront_info *) arg;

	while (!kthread_should_stop()) {
		wait_event_interruptible(
				info->wq,
				info->waiting_resp || kthread_should_stop());
		info->waiting_resp = 0;
		smp_mb();

		if (xenhcd_end_submit_urb(info))
			info->waiting_resp = 1;
	}

	return 0;
}

static void xenhcd_notify_work(struct usbfront_info *info)
{
	info->waiting_resp = 1;
	wake_up(&info->wq);
}

irqreturn_t xenhcd_int(int irq, void *dev_id)
{
	xenhcd_notify_work((struct usbfront_info *) dev_id);
	return IRQ_HANDLED;
}
