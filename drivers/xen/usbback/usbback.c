/*
 * usbback.c
 *
 * Xen USB backend driver
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

#include <linux/mm.h>
#include <xen/balloon.h>
#include "usbback.h"

#if 0
#include "../../usb/core/hub.h"
#endif

int usbif_reqs = USBIF_BACK_MAX_PENDING_REQS;
module_param_named(reqs, usbif_reqs, int, 0);
MODULE_PARM_DESC(reqs, "Number of usbback requests to allocate");

struct pending_req_segment {
	uint16_t offset;
	uint16_t length;
};

typedef struct {
	usbif_t *usbif;

	uint16_t id; /* request id */

	struct usbstub *stub;
	struct list_head urb_list;

	/* urb */
	struct urb *urb;
	void *buffer;
	dma_addr_t transfer_dma;
	struct usb_ctrlrequest *setup;
	dma_addr_t setup_dma;

	/* request segments */
	uint16_t nr_buffer_segs; /* number of urb->transfer_buffer segments */
	uint16_t nr_extra_segs; /* number of iso_frame_desc segments (ISO) */
	struct pending_req_segment *seg;

	struct list_head free_list;
} pending_req_t;

static pending_req_t *pending_reqs;
static struct list_head pending_free;
static DEFINE_SPINLOCK(pending_free_lock);
static DECLARE_WAIT_QUEUE_HEAD(pending_free_wq);

#define USBBACK_INVALID_HANDLE (~0)

static struct page **pending_pages;
static grant_handle_t *pending_grant_handles;

static inline int vaddr_pagenr(pending_req_t *req, int seg)
{
	return (req - pending_reqs) * USBIF_MAX_SEGMENTS_PER_REQUEST + seg;
}

static inline unsigned long vaddr(pending_req_t *req, int seg)
{
	unsigned long pfn = page_to_pfn(pending_pages[vaddr_pagenr(req, seg)]);
	return (unsigned long)pfn_to_kaddr(pfn);
}

#define pending_handle(_req, _seg) \
	(pending_grant_handles[vaddr_pagenr(_req, _seg)])

static pending_req_t *alloc_req(void)
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

static inline void add_req_to_submitting_list(struct usbstub *stub, pending_req_t *pending_req)
{
	unsigned long flags;

	spin_lock_irqsave(&stub->submitting_lock, flags);
	list_add_tail(&pending_req->urb_list, &stub->submitting_list);
	spin_unlock_irqrestore(&stub->submitting_lock, flags);
}

static inline void remove_req_from_submitting_list(struct usbstub *stub, pending_req_t *pending_req)
{
	unsigned long flags;

	spin_lock_irqsave(&stub->submitting_lock, flags);
	list_del_init(&pending_req->urb_list);
	spin_unlock_irqrestore(&stub->submitting_lock, flags);
}

void usbbk_unlink_urbs(struct usbstub *stub)
{
	pending_req_t *req, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&stub->submitting_lock, flags);
	list_for_each_entry_safe(req, tmp, &stub->submitting_list, urb_list) {
		usb_unlink_urb(req->urb);
	}
	spin_unlock_irqrestore(&stub->submitting_lock, flags);
}

static void fast_flush_area(pending_req_t *pending_req)
{
	struct gnttab_unmap_grant_ref unmap[USBIF_MAX_SEGMENTS_PER_REQUEST];
	unsigned int i, nr_segs, invcount = 0;
	grant_handle_t handle;
	int ret;

	nr_segs = pending_req->nr_buffer_segs + pending_req->nr_extra_segs;

	if (nr_segs) {
		for (i = 0; i < nr_segs; i++) {
			handle = pending_handle(pending_req, i);
			if (handle == USBBACK_INVALID_HANDLE)
				continue;
			gnttab_set_unmap_op(&unmap[invcount], vaddr(pending_req, i),
					    GNTMAP_host_map, handle);
			pending_handle(pending_req, i) = USBBACK_INVALID_HANDLE;
			invcount++;
		}

		ret = HYPERVISOR_grant_table_op(
			GNTTABOP_unmap_grant_ref, unmap, invcount);
		BUG_ON(ret);

		kfree(pending_req->seg);
	}

	return;
}

static void copy_buff_to_pages(void *buff, pending_req_t *pending_req,
		int start, int nr_pages)
{
	unsigned long copied = 0;
	int i;

	for (i = start; i < start + nr_pages; i++) {
		memcpy((void *) vaddr(pending_req, i) + pending_req->seg[i].offset,
			buff + copied,
			pending_req->seg[i].length);
		copied += pending_req->seg[i].length;
	}
}

static void copy_pages_to_buff(void *buff, pending_req_t *pending_req,
		int start, int nr_pages)
{
	unsigned long copied = 0;
	int i;

	for (i = start; i < start + nr_pages; i++) {
		memcpy(buff + copied,
			(void *) vaddr(pending_req, i) + pending_req->seg[i].offset,
			pending_req->seg[i].length);
		copied += pending_req->seg[i].length;
	}
}

static int usbbk_alloc_urb(usbif_urb_request_t *req, pending_req_t *pending_req)
{
	int ret;

	if (usb_pipeisoc(req->pipe))
		pending_req->urb = usb_alloc_urb(req->u.isoc.number_of_packets, GFP_KERNEL);
	else
		pending_req->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pending_req->urb) {
		printk(KERN_ERR "usbback: can't alloc urb\n");
		ret = -ENOMEM;
		goto fail;
	}

	if (req->buffer_length) {
		pending_req->buffer = usb_buffer_alloc(pending_req->stub->udev,
				req->buffer_length, GFP_KERNEL,
				&pending_req->transfer_dma);
		if (!pending_req->buffer) {
			printk(KERN_ERR "usbback: can't alloc urb buffer\n");
			ret = -ENOMEM;
			goto fail_free_urb;
		}
	}

	if (usb_pipecontrol(req->pipe)) {
		pending_req->setup = usb_buffer_alloc(pending_req->stub->udev,
				sizeof(struct usb_ctrlrequest), GFP_KERNEL,
				&pending_req->setup_dma);
		if (!pending_req->setup) {
			printk(KERN_ERR "usbback: can't alloc usb_ctrlrequest\n");
			ret = -ENOMEM;
			goto fail_free_buffer;
		}
	}

	return 0;

fail_free_buffer:
	if (req->buffer_length)
		usb_buffer_free(pending_req->stub->udev, req->buffer_length,
				pending_req->buffer, pending_req->transfer_dma);
fail_free_urb:
	usb_free_urb(pending_req->urb);
fail:
	return ret;
}

static void usbbk_free_urb(struct urb *urb)
{
	if (usb_pipecontrol(urb->pipe))
		usb_buffer_free(urb->dev, sizeof(struct usb_ctrlrequest),
				urb->setup_packet, urb->setup_dma);
	if (urb->transfer_buffer_length)
		usb_buffer_free(urb->dev, urb->transfer_buffer_length,
				urb->transfer_buffer, urb->transfer_dma);
	barrier();
	usb_free_urb(urb);
}

static void usbbk_notify_work(usbif_t *usbif)
{
	usbif->waiting_reqs = 1;
	wake_up(&usbif->wq);
}

irqreturn_t usbbk_be_int(int irq, void *dev_id)
{
	usbbk_notify_work(dev_id);
	return IRQ_HANDLED;
}

static void usbbk_do_response(pending_req_t *pending_req, int32_t status,
					int32_t actual_length, int32_t error_count, uint16_t start_frame)
{
	usbif_t *usbif = pending_req->usbif;
	usbif_urb_response_t *res;
	unsigned long flags;
	int notify;

	spin_lock_irqsave(&usbif->urb_ring_lock, flags);
	res = RING_GET_RESPONSE(&usbif->urb_ring, usbif->urb_ring.rsp_prod_pvt);
	res->id = pending_req->id;
	res->status = status;
	res->actual_length = actual_length;
	res->error_count = error_count;
	res->start_frame = start_frame;
	usbif->urb_ring.rsp_prod_pvt++;
	barrier();
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&usbif->urb_ring, notify);
	spin_unlock_irqrestore(&usbif->urb_ring_lock, flags);

	if (notify)
		notify_remote_via_irq(usbif->irq);
}

static void usbbk_urb_complete(struct urb *urb)
{
	pending_req_t *pending_req = (pending_req_t *)urb->context;

	if (usb_pipein(urb->pipe) && urb->status == 0 && urb->actual_length > 0)
		copy_buff_to_pages(pending_req->buffer, pending_req,
					0, pending_req->nr_buffer_segs);

	if (usb_pipeisoc(urb->pipe))
		copy_buff_to_pages(&urb->iso_frame_desc[0], pending_req,
					pending_req->nr_buffer_segs, pending_req->nr_extra_segs);

	barrier();

	fast_flush_area(pending_req);

	usbbk_do_response(pending_req, urb->status, urb->actual_length,
					urb->error_count, urb->start_frame);

	remove_req_from_submitting_list(pending_req->stub, pending_req);

	barrier();
	usbbk_free_urb(urb);
	usbif_put(pending_req->usbif);
	free_req(pending_req);
}

static int usbbk_gnttab_map(usbif_t *usbif,
			usbif_urb_request_t *req, pending_req_t *pending_req)
{
	int i, ret;
	unsigned int nr_segs;
	uint32_t flags;
	struct gnttab_map_grant_ref map[USBIF_MAX_SEGMENTS_PER_REQUEST];

	nr_segs = pending_req->nr_buffer_segs + pending_req->nr_extra_segs;

	if (nr_segs > USBIF_MAX_SEGMENTS_PER_REQUEST) {
		printk(KERN_ERR "Bad number of segments in request\n");
		ret = -EINVAL;
		goto fail;
	}

	if (nr_segs) {
		pending_req->seg = kmalloc(sizeof(struct pending_req_segment)
				* nr_segs, GFP_KERNEL);
		if (!pending_req->seg) {
			ret = -ENOMEM;
			goto fail;
		}

		if (pending_req->nr_buffer_segs) {
			flags = GNTMAP_host_map;
			if (usb_pipeout(req->pipe))
				flags |= GNTMAP_readonly;
			for (i = 0; i < pending_req->nr_buffer_segs; i++)
				gnttab_set_map_op(&map[i], vaddr(
						pending_req, i), flags,
						req->seg[i].gref,
						usbif->domid);
		}

		if (pending_req->nr_extra_segs) {
			flags = GNTMAP_host_map;
			for (i = req->nr_buffer_segs; i < nr_segs; i++)
				gnttab_set_map_op(&map[i], vaddr(
						pending_req, i), flags,
						req->seg[i].gref,
						usbif->domid);
		}

		ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
					map, nr_segs);
		BUG_ON(ret);

		for (i = 0; i < nr_segs; i++) {
			if (unlikely(map[i].status != 0)) {
				printk(KERN_ERR "usbback: invalid buffer -- could not remap it\n");
				map[i].handle = USBBACK_INVALID_HANDLE;
				ret |= 1;
			}

			pending_handle(pending_req, i) = map[i].handle;

			if (ret)
				continue;

			set_phys_to_machine(__pa(vaddr(
				pending_req, i)) >> PAGE_SHIFT,
				FOREIGN_FRAME(map[i].dev_bus_addr >> PAGE_SHIFT));

			pending_req->seg[i].offset = req->seg[i].offset;
			pending_req->seg[i].length = req->seg[i].length;

			barrier();

			if (pending_req->seg[i].offset >= PAGE_SIZE ||
					pending_req->seg[i].length > PAGE_SIZE ||
					pending_req->seg[i].offset + pending_req->seg[i].length > PAGE_SIZE)
					ret |= 1;
		}

		if (ret)
			goto fail_flush;
	}

	return 0;

fail_flush:
	fast_flush_area(pending_req);
	ret = -ENOMEM;

fail:
	return ret;
}

static void usbbk_init_urb(usbif_urb_request_t *req, pending_req_t *pending_req)
{
	unsigned int pipe;
	struct usb_device *udev = pending_req->stub->udev;
	struct urb *urb = pending_req->urb;

	switch (usb_pipetype(req->pipe)) {
	case PIPE_ISOCHRONOUS:
		if (usb_pipein(req->pipe))
			pipe = usb_rcvisocpipe(udev, usb_pipeendpoint(req->pipe));
		else
			pipe = usb_sndisocpipe(udev, usb_pipeendpoint(req->pipe));

		urb->dev = udev;
		urb->pipe = pipe;
		urb->transfer_flags = req->transfer_flags;
		urb->transfer_flags |= URB_ISO_ASAP;
		urb->transfer_buffer = pending_req->buffer;
		urb->transfer_buffer_length = req->buffer_length;
		urb->complete = usbbk_urb_complete;
		urb->context = pending_req;
		urb->interval = req->u.isoc.interval;
		urb->start_frame = req->u.isoc.start_frame;
		urb->number_of_packets = req->u.isoc.number_of_packets;

		break;
	case PIPE_INTERRUPT:
		if (usb_pipein(req->pipe))
			pipe = usb_rcvintpipe(udev, usb_pipeendpoint(req->pipe));
		else
			pipe = usb_sndintpipe(udev, usb_pipeendpoint(req->pipe));

		usb_fill_int_urb(urb, udev, pipe,
				pending_req->buffer, req->buffer_length,
				usbbk_urb_complete,
				pending_req, req->u.intr.interval);
		/*
		 * high speed interrupt endpoints use a logarithmic encoding of
		 * the endpoint interval, and usb_fill_int_urb() initializes a
		 * interrupt urb with the encoded interval value.
		 *
		 * req->u.intr.interval is the interval value that already
		 * encoded in the frontend part, and the above usb_fill_int_urb()
		 * initializes the urb->interval with double encoded value.
		 *
		 * so, simply overwrite the urb->interval with original value.
		 */
		urb->interval = req->u.intr.interval;
		urb->transfer_flags = req->transfer_flags;

		break;
	case PIPE_CONTROL:
		if (usb_pipein(req->pipe))
			pipe = usb_rcvctrlpipe(udev, 0);
		else
			pipe = usb_sndctrlpipe(udev, 0);

		usb_fill_control_urb(urb, udev, pipe,
				(unsigned char *) pending_req->setup,
				pending_req->buffer, req->buffer_length,
				usbbk_urb_complete, pending_req);
		memcpy(pending_req->setup, req->u.ctrl, 8);
		urb->setup_dma = pending_req->setup_dma;
		urb->transfer_flags = req->transfer_flags;
		urb->transfer_flags |= URB_NO_SETUP_DMA_MAP;

		break;
	case PIPE_BULK:
		if (usb_pipein(req->pipe))
			pipe = usb_rcvbulkpipe(udev, usb_pipeendpoint(req->pipe));
		else
			pipe = usb_sndbulkpipe(udev, usb_pipeendpoint(req->pipe));

		usb_fill_bulk_urb(urb, udev, pipe,
				pending_req->buffer, req->buffer_length,
				usbbk_urb_complete, pending_req);
		urb->transfer_flags = req->transfer_flags;

		break;
	default:
		break;
	}

	if (req->buffer_length) {
		urb->transfer_dma = pending_req->transfer_dma;
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}
}

struct set_interface_request {
	pending_req_t *pending_req;
	int interface;
	int alternate;
	struct work_struct work;
};

static void usbbk_set_interface_work(struct work_struct *arg)
{
	struct set_interface_request *req
		= container_of(arg, struct set_interface_request, work);
	pending_req_t *pending_req = req->pending_req;
	struct usb_device *udev = req->pending_req->stub->udev;

	int ret;

	usb_lock_device(udev);
	ret = usb_set_interface(udev, req->interface, req->alternate);
	usb_unlock_device(udev);
	usb_put_dev(udev);

	usbbk_do_response(pending_req, ret, 0, 0, 0);
	usbif_put(pending_req->usbif);
	free_req(pending_req);
	kfree(req);
}

static int usbbk_set_interface(pending_req_t *pending_req, int interface, int alternate)
{
	struct set_interface_request *req;
	struct usb_device *udev = pending_req->stub->udev;

	req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	req->pending_req = pending_req;
	req->interface = interface;
	req->alternate = alternate;
	INIT_WORK(&req->work, usbbk_set_interface_work);
	usb_get_dev(udev);
	schedule_work(&req->work);
	return 0;
}

struct clear_halt_request {
	pending_req_t *pending_req;
	int pipe;
	struct work_struct work;
};

static void usbbk_clear_halt_work(struct work_struct *arg)
{
	struct clear_halt_request *req
		= container_of(arg, struct clear_halt_request, work);
	pending_req_t *pending_req = req->pending_req;
	struct usb_device *udev = req->pending_req->stub->udev;
	int ret;

	usb_lock_device(udev);
	ret = usb_clear_halt(req->pending_req->stub->udev, req->pipe);
	usb_unlock_device(udev);
	usb_put_dev(udev);

	usbbk_do_response(pending_req, ret, 0, 0, 0);
	usbif_put(pending_req->usbif);
	free_req(pending_req);
	kfree(req);
}

static int usbbk_clear_halt(pending_req_t *pending_req, int pipe)
{
	struct clear_halt_request *req;
	struct usb_device *udev = pending_req->stub->udev;

	req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	req->pending_req = pending_req;
	req->pipe = pipe;
	INIT_WORK(&req->work, usbbk_clear_halt_work);

	usb_get_dev(udev);
	schedule_work(&req->work);
	return 0;
}

#if 0
struct port_reset_request {
	pending_req_t *pending_req;
	struct work_struct work;
};

static void usbbk_port_reset_work(struct work_struct *arg)
{
	struct port_reset_request *req
		= container_of(arg, struct port_reset_request, work);
	pending_req_t *pending_req = req->pending_req;
	struct usb_device *udev = pending_req->stub->udev;
	int ret, ret_lock;

	ret = ret_lock = usb_lock_device_for_reset(udev, NULL);
	if (ret_lock >= 0) {
		ret = usb_reset_device(udev);
		if (ret_lock)
			usb_unlock_device(udev);
	}
	usb_put_dev(udev);

	usbbk_do_response(pending_req, ret, 0, 0, 0);
	usbif_put(pending_req->usbif);
	free_req(pending_req);
	kfree(req);
}

static int usbbk_port_reset(pending_req_t *pending_req)
{
	struct port_reset_request *req;
	struct usb_device *udev = pending_req->stub->udev;

	req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->pending_req = pending_req;
	INIT_WORK(&req->work, usbbk_port_reset_work);

	usb_get_dev(udev);
	schedule_work(&req->work);
	return 0;
}
#endif

static void usbbk_set_address(usbif_t *usbif, struct usbstub *stub, int cur_addr, int new_addr)
{
	unsigned long flags;

	spin_lock_irqsave(&usbif->addr_lock, flags);
	if (cur_addr)
		usbif->addr_table[cur_addr] = NULL;
	if (new_addr)
		usbif->addr_table[new_addr] = stub;
	stub->addr = new_addr;
	spin_unlock_irqrestore(&usbif->addr_lock, flags);
}

struct usbstub *find_attached_device(usbif_t *usbif, int portnum)
{
	struct usbstub *stub;
	int found = 0;
	unsigned long flags;

	spin_lock_irqsave(&usbif->stub_lock, flags);
	list_for_each_entry(stub, &usbif->stub_list, dev_list) {
		if (stub->portid->portnum == portnum) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&usbif->stub_lock, flags);

	if (found)
		return stub;

	return NULL;
}

static void process_unlink_req(usbif_t *usbif,
		usbif_urb_request_t *req, pending_req_t *pending_req)
{
	pending_req_t *unlink_req = NULL;
	int devnum;
	int ret = 0;
	unsigned long flags;

	devnum = usb_pipedevice(req->pipe);
	if (unlikely(devnum == 0)) {
		pending_req->stub = find_attached_device(usbif, usbif_pipeportnum(req->pipe));
		if (unlikely(!pending_req->stub)) {
			ret = -ENODEV;
			goto fail_response;
		}
	} else {
		if (unlikely(!usbif->addr_table[devnum])) {
			ret = -ENODEV;
			goto fail_response;
		}
		pending_req->stub = usbif->addr_table[devnum];
	}

	spin_lock_irqsave(&pending_req->stub->submitting_lock, flags);
	list_for_each_entry(unlink_req, &pending_req->stub->submitting_list, urb_list) {
		if (unlink_req->id == req->u.unlink.unlink_id) {
			ret = usb_unlink_urb(unlink_req->urb);
			break;
		}
	}
	spin_unlock_irqrestore(&pending_req->stub->submitting_lock, flags);

fail_response:
	usbbk_do_response(pending_req, ret, 0, 0, 0);
	usbif_put(usbif);
	free_req(pending_req);
	return;
}

static int check_and_submit_special_ctrlreq(usbif_t *usbif,
		usbif_urb_request_t *req, pending_req_t *pending_req)
{
	int devnum;
	struct usbstub *stub = NULL;
	struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *) req->u.ctrl;
	int ret;
	int done = 0;

	devnum = usb_pipedevice(req->pipe);

	/*
	 * When the device is first connected or reseted, USB device has no address.
	 * In this initial state, following requests are send to device address (#0),
	 *
	 *  1. GET_DESCRIPTOR (with Descriptor Type is "DEVICE") is send,
	 *     and OS knows what device is connected to.
	 *
	 *  2. SET_ADDRESS is send, and then, device has its address.
	 *
	 * In the next step, SET_CONFIGURATION is send to addressed device, and then,
	 * the device is finally ready to use.
	 */
	if (unlikely(devnum == 0)) {
		stub = find_attached_device(usbif, usbif_pipeportnum(req->pipe));
		if (unlikely(!stub)) {
			ret = -ENODEV;
			goto fail_response;
		}

		switch (ctrl->bRequest) {
		case USB_REQ_GET_DESCRIPTOR:
			/*
			 * GET_DESCRIPTOR request to device #0.
			 * through to normal urb transfer.
			 */
			pending_req->stub = stub;
			return 0;
			break;
		case USB_REQ_SET_ADDRESS:
			/*
			 * SET_ADDRESS request to device #0.
			 * add attached device to addr_table.
			 */
			{
				__u16 addr = le16_to_cpu(ctrl->wValue);
				usbbk_set_address(usbif, stub, 0, addr);
			}
			ret = 0;
			goto fail_response;
			break;
		default:
			ret = -EINVAL;
			goto fail_response;
		}
	} else {
		if (unlikely(!usbif->addr_table[devnum])) {
			ret = -ENODEV;
			goto fail_response;
		}
		pending_req->stub = usbif->addr_table[devnum];
	}

	/*
	 * Check special request
	 */
	switch (ctrl->bRequest) {
	case USB_REQ_SET_ADDRESS:
		/*
		 * SET_ADDRESS request to addressed device.
		 * change addr or remove from addr_table.
		 */
		{
			__u16 addr = le16_to_cpu(ctrl->wValue);
			usbbk_set_address(usbif, stub, devnum, addr);
		}
		ret = 0;
		goto fail_response;
		break;
#if 0
	case USB_REQ_SET_CONFIGURATION:
		/*
		 * linux 2.6.27 or later version only!
		 */
		if (ctrl->RequestType == USB_RECIP_DEVICE) {
			__u16 config = le16_to_cpu(ctrl->wValue);
			usb_driver_set_configuration(pending_req->stub->udev, config);
			done = 1;
		}
		break;
#endif
	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType == USB_RECIP_INTERFACE) {
			__u16 alt = le16_to_cpu(ctrl->wValue);
			__u16 intf = le16_to_cpu(ctrl->wIndex);
			usbbk_set_interface(pending_req, intf, alt);
			done = 1;
		}
		break;
	case USB_REQ_CLEAR_FEATURE:
		if (ctrl->bRequestType == USB_RECIP_ENDPOINT
			&& ctrl->wValue == USB_ENDPOINT_HALT) {
			int pipe;
			int ep = le16_to_cpu(ctrl->wIndex) & 0x0f;
			int dir = le16_to_cpu(ctrl->wIndex)
					& USB_DIR_IN;
			if (dir)
				pipe = usb_rcvctrlpipe(pending_req->stub->udev, ep);
			else
				pipe = usb_sndctrlpipe(pending_req->stub->udev, ep);
			usbbk_clear_halt(pending_req, pipe);
			done = 1;
		}
		break;
#if 0 /* not tested yet */
	case USB_REQ_SET_FEATURE:
		if (ctrl->bRequestType == USB_RT_PORT) {
			__u16 feat = le16_to_cpu(ctrl->wValue);
			if (feat == USB_PORT_FEAT_RESET) {
				usbbk_port_reset(pending_req);
				done = 1;
			}
		}
		break;
#endif
	default:
		break;
	}

	return done;

fail_response:
	usbbk_do_response(pending_req, ret, 0, 0, 0);
	usbif_put(usbif);
	free_req(pending_req);
	return 1;
}

static void dispatch_request_to_pending_reqs(usbif_t *usbif,
		usbif_urb_request_t *req,
		pending_req_t *pending_req)
{
	int ret;

	pending_req->id = req->id;
	pending_req->usbif = usbif;

	barrier();

	usbif_get(usbif);

	/* unlink request */
	if (unlikely(usbif_pipeunlink(req->pipe))) {
		process_unlink_req(usbif, req, pending_req);
		return;
	}

	if (usb_pipecontrol(req->pipe)) {
		if (check_and_submit_special_ctrlreq(usbif, req, pending_req))
			return;
	} else {
		int devnum = usb_pipedevice(req->pipe);
		if (unlikely(!usbif->addr_table[devnum])) {
			ret = -ENODEV;
			goto fail_response;
		}
		pending_req->stub = usbif->addr_table[devnum];
	}

	barrier();

	ret = usbbk_alloc_urb(req, pending_req);
	if (ret) {
		ret = -ESHUTDOWN;
		goto fail_response;
	}

	add_req_to_submitting_list(pending_req->stub, pending_req);

	barrier();

	usbbk_init_urb(req, pending_req);

	barrier();

	pending_req->nr_buffer_segs = req->nr_buffer_segs;
	if (usb_pipeisoc(req->pipe))
		pending_req->nr_extra_segs = req->u.isoc.nr_frame_desc_segs;
	else
		pending_req->nr_extra_segs = 0;

	barrier();

	ret = usbbk_gnttab_map(usbif, req, pending_req);
	if (ret) {
		printk(KERN_ERR "usbback: invalid buffer\n");
		ret = -ESHUTDOWN;
		goto fail_free_urb;
	}

	barrier();

	if (usb_pipeout(req->pipe) && req->buffer_length)
		copy_pages_to_buff(pending_req->buffer,
					pending_req,
					0,
					pending_req->nr_buffer_segs);
	if (usb_pipeisoc(req->pipe)) {
		copy_pages_to_buff(&pending_req->urb->iso_frame_desc[0],
			pending_req,
			pending_req->nr_buffer_segs,
			pending_req->nr_extra_segs);
	}

	barrier();

	ret = usb_submit_urb(pending_req->urb, GFP_KERNEL);
	if (ret) {
		printk(KERN_ERR "usbback: failed submitting urb, error %d\n", ret);
		ret = -ESHUTDOWN;
		goto fail_flush_area;
	}
	return;

fail_flush_area:
	fast_flush_area(pending_req);
fail_free_urb:
	remove_req_from_submitting_list(pending_req->stub, pending_req);
	barrier();
	usbbk_free_urb(pending_req->urb);
fail_response:
	usbbk_do_response(pending_req, ret, 0, 0, 0);
	usbif_put(usbif);
	free_req(pending_req);
}

static int usbbk_start_submit_urb(usbif_t *usbif)
{
	usbif_urb_back_ring_t *urb_ring = &usbif->urb_ring;
	usbif_urb_request_t *req;
	pending_req_t *pending_req;
	RING_IDX rc, rp;
	int more_to_do = 0;

	rc = urb_ring->req_cons;
	rp = urb_ring->sring->req_prod;
	rmb();

	while (rc != rp) {
		if (RING_REQUEST_CONS_OVERFLOW(urb_ring, rc)) {
			printk(KERN_WARNING "RING_REQUEST_CONS_OVERFLOW\n");
			break;
		}

		pending_req = alloc_req();
		if (NULL == pending_req) {
			more_to_do = 1;
			break;
		}

		req = RING_GET_REQUEST(urb_ring, rc);
		urb_ring->req_cons = ++rc;

		dispatch_request_to_pending_reqs(usbif, req,
							pending_req);
	}

	RING_FINAL_CHECK_FOR_REQUESTS(&usbif->urb_ring, more_to_do);

	cond_resched();

	return more_to_do;
}

void usbbk_hotplug_notify(usbif_t *usbif, int portnum, int speed)
{
	usbif_conn_back_ring_t *ring = &usbif->conn_ring;
	usbif_conn_request_t *req;
	usbif_conn_response_t *res;
	unsigned long flags;
	u16 id;
	int notify;

	spin_lock_irqsave(&usbif->conn_ring_lock, flags);

	req = RING_GET_REQUEST(ring, ring->req_cons);;
	id = req->id;
	ring->req_cons++;
	ring->sring->req_event = ring->req_cons + 1;

	res = RING_GET_RESPONSE(ring, ring->rsp_prod_pvt);
	res->id = id;
	res->portnum = portnum;
	res->speed = speed;
	ring->rsp_prod_pvt++;
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(ring, notify);

	spin_unlock_irqrestore(&usbif->conn_ring_lock, flags);

	if (notify)
		notify_remote_via_irq(usbif->irq);
}

int usbbk_schedule(void *arg)
{
	usbif_t *usbif = (usbif_t *) arg;

	usbif_get(usbif);

	while (!kthread_should_stop()) {
		wait_event_interruptible(
			usbif->wq,
			usbif->waiting_reqs || kthread_should_stop());
		wait_event_interruptible(
			pending_free_wq,
			!list_empty(&pending_free) || kthread_should_stop());
		usbif->waiting_reqs = 0;
		smp_mb();

		if (usbbk_start_submit_urb(usbif))
			usbif->waiting_reqs = 1;
	}

	usbif->xenusbd = NULL;
	usbif_put(usbif);

	return 0;
}

/*
 * attach usbstub device to usbif.
 */
void usbbk_attach_device(usbif_t *usbif, struct usbstub *stub)
{
	unsigned long flags;

	spin_lock_irqsave(&usbif->stub_lock, flags);
	list_add(&stub->dev_list, &usbif->stub_list);
	spin_unlock_irqrestore(&usbif->stub_lock, flags);
	stub->usbif = usbif;
}

/*
 * detach usbstub device from usbif.
 */
void usbbk_detach_device(usbif_t *usbif, struct usbstub *stub)
{
	unsigned long flags;

	if (stub->addr)
		usbbk_set_address(usbif, stub, stub->addr, 0);
	spin_lock_irqsave(&usbif->stub_lock, flags);
	list_del(&stub->dev_list);
	spin_unlock_irqrestore(&usbif->stub_lock, flags);
	stub->usbif = NULL;
}

void detach_device_without_lock(usbif_t *usbif, struct usbstub *stub)
{
	if (stub->addr)
		usbbk_set_address(usbif, stub, stub->addr, 0);
	list_del(&stub->dev_list);
	stub->usbif = NULL;
}

static int __init usbback_init(void)
{
	int i, mmap_pages;
	int err = 0;

	if (!is_running_on_xen())
		return -ENODEV;

	mmap_pages = usbif_reqs * USBIF_MAX_SEGMENTS_PER_REQUEST;
	pending_reqs = kmalloc(sizeof(pending_reqs[0]) *
			usbif_reqs, GFP_KERNEL);
	pending_grant_handles = kmalloc(sizeof(pending_grant_handles[0]) *
			mmap_pages, GFP_KERNEL);
	pending_pages = alloc_empty_pages_and_pagevec(mmap_pages);

	if (!pending_reqs || !pending_grant_handles || !pending_pages) {
		err = -ENOMEM;
		goto out_mem;
	}

	for (i = 0; i < mmap_pages; i++)
		pending_grant_handles[i] = USBBACK_INVALID_HANDLE;

	memset(pending_reqs, 0, sizeof(pending_reqs));
	INIT_LIST_HEAD(&pending_free);

	for (i = 0; i < usbif_reqs; i++)
		list_add_tail(&pending_reqs[i].free_list, &pending_free);

	err = usbstub_init();
	if (err)
		goto out_mem;

	err = usbback_xenbus_init();
	if (err)
		goto out_xenbus;

	return 0;

out_xenbus:
	usbstub_exit();
out_mem:
	kfree(pending_reqs);
	kfree(pending_grant_handles);
	free_empty_pages_and_pagevec(pending_pages, mmap_pages);
	return err;
}

static void __exit usbback_exit(void)
{
	usbback_xenbus_exit();
	usbstub_exit();
	kfree(pending_reqs);
	kfree(pending_grant_handles);
	free_empty_pages_and_pagevec(pending_pages, usbif_reqs * USBIF_MAX_SEGMENTS_PER_REQUEST);
}

module_init(usbback_init);
module_exit(usbback_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("Xen USB backend driver (usbback)");
MODULE_LICENSE("Dual BSD/GPL");
