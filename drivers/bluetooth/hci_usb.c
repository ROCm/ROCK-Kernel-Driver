/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * BlueZ HCI USB driver.
 * Based on original USB Bluetooth driver for Linux kernel
 *    Copyright (c) 2000 Greg Kroah-Hartman        <greg@kroah.com>
 *    Copyright (c) 2000 Mark Douglas Corner       <mcorner@umich.edu>
 *
 * $Id: hci_usb.c,v 1.5 2001/07/05 18:42:44 maxk Exp $    
 */
#define VERSION "1.0"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/bluez.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci_usb.h>

#ifndef HCI_USB_DEBUG
#undef  DBG
#define DBG( A... )
#undef  DMP
#define DMP( A... )
#endif

static struct usb_device_id usb_bluetooth_ids [] = {
	{ USB_DEVICE_INFO(HCI_DEV_CLASS, HCI_DEV_SUBCLASS, HCI_DEV_PROTOCOL) },
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_bluetooth_ids);

static int hci_usb_ctrl_msg(struct hci_usb *husb,  struct sk_buff *skb);
static int hci_usb_write_msg(struct hci_usb *husb, struct sk_buff *skb);

static void hci_usb_unlink_urbs(struct hci_usb *husb)
{
	usb_unlink_urb(husb->read_urb);
	usb_unlink_urb(husb->intr_urb);
	usb_unlink_urb(husb->ctrl_urb);
	usb_unlink_urb(husb->write_urb);
}

static void hci_usb_free_bufs(struct hci_usb *husb)
{
	if (husb->read_urb) {
		if (husb->read_urb->transfer_buffer)
			kfree(husb->read_urb->transfer_buffer);
		usb_free_urb(husb->read_urb);
	}

	if (husb->intr_urb) {
		if (husb->intr_urb->transfer_buffer)
			kfree(husb->intr_urb->transfer_buffer);
		usb_free_urb(husb->intr_urb);
	}

	if (husb->ctrl_urb)
		usb_free_urb(husb->ctrl_urb);

	if (husb->write_urb)
		usb_free_urb(husb->write_urb);

	if (husb->intr_skb)
		kfree_skb(husb->intr_skb);
}

/* ------- Interface to HCI layer ------ */
/* Initialize device */
int hci_usb_open(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;
	int status;

	DBG("%s", hdev->name);

	husb->read_urb->dev = husb->udev;
	if ((status = usb_submit_urb(husb->read_urb)))
		DBG("read submit failed. %d", status);

	husb->intr_urb->dev = husb->udev;
	if ((status = usb_submit_urb(husb->intr_urb)))
		DBG("interrupt submit failed. %d", status);

	hdev->flags |= HCI_RUNNING;

	return 0;
}

/* Reset device */
int hci_usb_flush(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;

	DBG("%s", hdev->name);

	/* Drop TX queues */
	skb_queue_purge(&husb->tx_ctrl_q);
	skb_queue_purge(&husb->tx_write_q);

	return 0;
}

/* Close device */
int hci_usb_close(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;

	DBG("%s", hdev->name);

	hdev->flags &= ~HCI_RUNNING;
	hci_usb_unlink_urbs(husb);

	hci_usb_flush(hdev);

	return 0;
}

void hci_usb_ctrl_wakeup(struct hci_usb *husb)
{
	struct sk_buff *skb;

	if (test_and_set_bit(HCI_TX_CTRL, &husb->tx_state))
		return;

	DBG("%s", husb->hdev.name);

	if (!(skb = skb_dequeue(&husb->tx_ctrl_q)))
		goto done;

	if (hci_usb_ctrl_msg(husb, skb)){
		kfree_skb(skb);
		goto done;
	}

	DMP(skb->data, skb->len);

	husb->hdev.stat.byte_tx += skb->len;
	return;

done:
	clear_bit(HCI_TX_CTRL, &husb->tx_state);
	return;
}

void hci_usb_write_wakeup(struct hci_usb *husb)
{
	struct sk_buff *skb;

	if (test_and_set_bit(HCI_TX_WRITE, &husb->tx_state))
		return;

	DBG("%s", husb->hdev.name);

	if (!(skb = skb_dequeue(&husb->tx_write_q)))
		goto done;

	if (hci_usb_write_msg(husb, skb)) {
		skb_queue_head(&husb->tx_write_q, skb);
		goto done;
	}

	DMP(skb->data, skb->len);

	husb->hdev.stat.byte_tx += skb->len;
	return;

done:
	clear_bit(HCI_TX_WRITE, &husb->tx_state);
	return;
}

/* Send frames from HCI layer */
int hci_usb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct hci_usb *husb;

	if (!hdev) {
		ERR("frame for uknown device (hdev=NULL)");
		return -ENODEV;
	}

	if (!(hdev->flags & HCI_RUNNING))
		return 0;

	husb = (struct hci_usb *) hdev->driver_data;

	DBG("%s type %d len %d", hdev->name, skb->pkt_type, skb->len);

	switch (skb->pkt_type) {
		case HCI_COMMAND_PKT:
			skb_queue_tail(&husb->tx_ctrl_q, skb);
			hci_usb_ctrl_wakeup(husb);
			hdev->stat.cmd_tx++;
			return 0;

		case HCI_ACLDATA_PKT:
			skb_queue_tail(&husb->tx_write_q, skb);
			hci_usb_write_wakeup(husb);
			hdev->stat.acl_tx++;
			return 0;

		case HCI_SCODATA_PKT:
			return -EOPNOTSUPP;
	};

	return 0;
}

/* ---------- USB ------------- */

static void hci_usb_ctrl(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct hci_dev *hdev;
	struct hci_usb *husb;

	if (!skb)
		return;
	hdev = (struct hci_dev *) skb->dev;
	husb = (struct hci_usb *) hdev->driver_data;

	DBG("%s", hdev->name);

	if (urb->status)
		DBG("%s ctrl status: %d", hdev->name, urb->status);

	clear_bit(HCI_TX_CTRL, &husb->tx_state);
	kfree_skb(skb);

	/* Wake up device */
	hci_usb_ctrl_wakeup(husb);
}

static void hci_usb_bulk_write(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct hci_dev *hdev;
	struct hci_usb *husb;

	if (!skb)
		return;
	hdev = (struct hci_dev *) skb->dev;
	husb = (struct hci_usb *) hdev->driver_data;

	DBG("%s", hdev->name);

	if (urb->status)
		DBG("%s bulk write status: %d", hdev->name, urb->status);

	clear_bit(HCI_TX_WRITE, &husb->tx_state);
	kfree_skb(skb);

	/* Wake up device */
	hci_usb_write_wakeup(husb);

	return;
}

static void hci_usb_intr(struct urb *urb)
{
	struct hci_usb *husb = (struct hci_usb *) urb->context;
	unsigned char *data = urb->transfer_buffer;
	register int count  = urb->actual_length;
	register struct sk_buff *skb = husb->intr_skb;
	hci_event_hdr *eh;
	register int len;

	if (!husb)
		return;

	DBG("%s count %d", husb->hdev.name, count);

	if (urb->status || !count) {
		DBG("%s intr status %d, count %d", husb->hdev.name, urb->status, count);
		return;
	}

	/* Do we really have to handle continuations here ? */
	if (!skb) {
		/* New frame */
		if (count < HCI_EVENT_HDR_SIZE) {
			DBG("%s bad frame len %d", husb->hdev.name, count);
			return;
		}

		eh = (hci_event_hdr *) data;
		len = eh->plen + HCI_EVENT_HDR_SIZE;

		if (count > len) {
			DBG("%s corrupted frame, len %d", husb->hdev.name, count);
			return;
		}

		/* Allocate skb */
		if (!(skb = bluez_skb_alloc(len, GFP_ATOMIC))) {
			ERR("Can't allocate mem for new packet");
			return;
		}
		skb->dev = (void *) &husb->hdev;
		skb->pkt_type = HCI_EVENT_PKT;

		husb->intr_skb = skb;
		husb->intr_count = len;
	} else {
		/* Continuation */
		if (count > husb->intr_count) {
			ERR("%s bad frame len %d (expected %d)", husb->hdev.name, count, husb->intr_count);

			kfree_skb(skb);
			husb->intr_skb = NULL;
			husb->intr_count = 0;
			return;
		}
	}

	memcpy(skb_put(skb, count), data, count);
	husb->intr_count -= count;

	DMP(data, count);

	if (!husb->intr_count) {
		/* Got complete frame */

		husb->hdev.stat.byte_rx += skb->len;
		hci_recv_frame(skb);

		husb->intr_skb = NULL;
	}
}

static void hci_usb_bulk_read(struct urb *urb)
{
	struct hci_usb *husb = (struct hci_usb *) urb->context;
	unsigned char *data = urb->transfer_buffer;
	int count = urb->actual_length, status;
	struct sk_buff *skb;
	hci_acl_hdr *ah;
	register __u16 dlen;

	if (!husb)
		return;

	DBG("%s status %d, count %d, flags %x", husb->hdev.name, urb->status, count, urb->transfer_flags);

	if (urb->status) {
		/* Do not re-submit URB on critical errors */
		switch (urb->status) {
			case -ENOENT:
				return;
			default:
				goto resubmit;
		};
	}
	if (!count)
		goto resubmit;

	DMP(data, count);

	ah = (hci_acl_hdr *) data;
	dlen = le16_to_cpu(ah->dlen);

	/* Verify frame len and completeness */
	if ((count - HCI_ACL_HDR_SIZE) != dlen) {
		ERR("%s corrupted ACL packet: count %d, plen %d", husb->hdev.name, count, dlen);
		goto resubmit;
	}

	/* Allocate packet */
	if (!(skb = bluez_skb_alloc(count, GFP_ATOMIC))) {
		ERR("Can't allocate mem for new packet");
		goto resubmit;
	}

	memcpy(skb_put(skb, count), data, count);
	skb->dev = (void *) &husb->hdev;
	skb->pkt_type = HCI_ACLDATA_PKT;

	husb->hdev.stat.byte_rx += skb->len;

	hci_recv_frame(skb);

resubmit:
	husb->read_urb->dev = husb->udev;
	if ((status = usb_submit_urb(husb->read_urb)))
		DBG("%s read URB submit failed %d", husb->hdev.name, status);

	DBG("%s read URB re-submited", husb->hdev.name);
}

static int hci_usb_ctrl_msg(struct hci_usb *husb, struct sk_buff *skb)
{
	struct urb *urb = husb->ctrl_urb;
	devrequest *dr  = &husb->dev_req;
	int pipe, status;

	DBG("%s len %d", husb->hdev.name, skb->len);

	pipe = usb_sndctrlpipe(husb->udev, 0);

	dr->requesttype = HCI_CTRL_REQ;
	dr->request = 0;
	dr->index   = 0;
	dr->value   = 0;
	dr->length  = cpu_to_le16(skb->len);

	FILL_CONTROL_URB(urb, husb->udev, pipe, (void*)dr, skb->data, skb->len,
	                 hci_usb_ctrl, skb);

	if ((status = usb_submit_urb(urb))) {
		DBG("%s control URB submit failed %d", husb->hdev.name, status);
		return status;
	}

	return 0;
}

static int hci_usb_write_msg(struct hci_usb *husb, struct sk_buff *skb)
{
	struct urb *urb = husb->write_urb;
	int pipe, status;

	DBG("%s len %d", husb->hdev.name, skb->len);

	pipe = usb_sndbulkpipe(husb->udev, husb->bulk_out_ep_addr);

	FILL_BULK_URB(urb, husb->udev, pipe, skb->data, skb->len,
	              hci_usb_bulk_write, skb);
	urb->transfer_flags |= USB_QUEUE_BULK;

	if ((status = usb_submit_urb(urb))) {
		DBG("%s write URB submit failed %d", husb->hdev.name, status);
		return status;
	}

	return 0;
}

static void * hci_usb_probe(struct usb_device *udev, unsigned int ifnum, const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *bulk_out_ep, *intr_in_ep, *bulk_in_ep;
	struct usb_interface_descriptor *uif;
	struct usb_endpoint_descriptor *ep;
	struct hci_usb *husb;
	struct hci_dev *hdev;
	int i, size, pipe;
	__u8 * buf;

	DBG("udev %p ifnum %d", udev, ifnum);

	/* Check device signature */
	if ((udev->descriptor.bDeviceClass    != HCI_DEV_CLASS)   ||
	    (udev->descriptor.bDeviceSubClass != HCI_DEV_SUBCLASS)||
	    (udev->descriptor.bDeviceProtocol != HCI_DEV_PROTOCOL) )
		return NULL;

	MOD_INC_USE_COUNT;

	uif = &udev->actconfig->interface[ifnum].altsetting[0];

	if (uif->bNumEndpoints != 3) {
		DBG("Wrong number of endpoints %d", uif->bNumEndpoints);
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	bulk_out_ep = intr_in_ep = bulk_in_ep = NULL;

	/* Find endpoints that we need */
	for ( i = 0; i < uif->bNumEndpoints; ++i) {
		ep = &uif->endpoint[i];

		switch (ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
			case USB_ENDPOINT_XFER_BULK:
				if (ep->bEndpointAddress & USB_DIR_IN)
					bulk_in_ep  = ep;
				else
					bulk_out_ep = ep;
				break;

			case USB_ENDPOINT_XFER_INT:
				intr_in_ep = ep;
				break;
		};
	}

	if (!bulk_in_ep || !bulk_out_ep || !intr_in_ep) {
		DBG("Endpoints not found: %p %p %p", bulk_in_ep, bulk_out_ep, intr_in_ep);
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	if (!(husb = kmalloc(sizeof(struct hci_usb), GFP_KERNEL))) {
		ERR("Can't allocate: control structure");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	memset(husb, 0, sizeof(struct hci_usb));

	husb->udev = udev;
	husb->bulk_out_ep_addr = bulk_out_ep->bEndpointAddress;

	if (!(husb->ctrl_urb = usb_alloc_urb(0))) {
		ERR("Can't allocate: control URB");
		goto probe_error;
	}

	if (!(husb->write_urb = usb_alloc_urb(0))) {
		ERR("Can't allocate: write URB");
		goto probe_error;
	}

	if (!(husb->read_urb = usb_alloc_urb(0))) {
		ERR("Can't allocate: read URB");
		goto probe_error;
	}

	ep = bulk_in_ep;
	pipe = usb_rcvbulkpipe(udev, ep->bEndpointAddress);
	size = HCI_MAX_FRAME_SIZE;

	if (!(buf = kmalloc(size, GFP_KERNEL))) {
		ERR("Can't allocate: read buffer");
		goto probe_error;
	}

	FILL_BULK_URB(husb->read_urb, udev, pipe, buf, size, hci_usb_bulk_read, husb);
	husb->read_urb->transfer_flags |= USB_QUEUE_BULK;

	ep = intr_in_ep;
	pipe = usb_rcvintpipe(udev, ep->bEndpointAddress);
	size = usb_maxpacket(udev, pipe, usb_pipeout(pipe));

	if (!(husb->intr_urb = usb_alloc_urb(0))) {
		ERR("Can't allocate: interrupt URB");
		goto probe_error;
	}

	if (!(buf = kmalloc(size, GFP_KERNEL))) {
		ERR("Can't allocate: interrupt buffer");
		goto probe_error;
	}

	FILL_INT_URB(husb->intr_urb, udev, pipe, buf, size, hci_usb_intr, husb, ep->bInterval);

	skb_queue_head_init(&husb->tx_ctrl_q);
	skb_queue_head_init(&husb->tx_write_q);

	/* Initialize and register HCI device */
	hdev = &husb->hdev;

	hdev->type = HCI_USB;
	hdev->driver_data = husb;

	hdev->open  = hci_usb_open;
	hdev->close = hci_usb_close;
	hdev->flush = hci_usb_flush;
	hdev->send	= hci_usb_send_frame;

	if (hci_register_dev(hdev) < 0) {
		ERR("Can't register HCI device %s", hdev->name);
		goto probe_error;
	}

	return husb;

probe_error:
	hci_usb_free_bufs(husb);
	kfree(husb);
	MOD_DEC_USE_COUNT;
	return NULL;
}

static void hci_usb_disconnect(struct usb_device *udev, void *ptr)
{
	struct hci_usb *husb = (struct hci_usb *) ptr;
	struct hci_dev *hdev = &husb->hdev;

	if (!husb)
		return;

	DBG("%s", hdev->name);

	hci_usb_close(hdev);

	if (hci_unregister_dev(hdev) < 0) {
		ERR("Can't unregister HCI device %s", hdev->name);
	}

	hci_usb_free_bufs(husb);
	kfree(husb);

	MOD_DEC_USE_COUNT;
}

static struct usb_driver hci_usb_driver =
{
	name:           "hci_usb",
	probe:          hci_usb_probe,
	disconnect:     hci_usb_disconnect,
	id_table:       usb_bluetooth_ids,
};

int hci_usb_init(void)
{
	int err;

	INF("BlueZ HCI USB driver ver %s Copyright (C) 2000,2001 Qualcomm Inc",  
		VERSION);
	INF("Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>");

	if ((err = usb_register(&hci_usb_driver)) < 0)
		ERR("Failed to register HCI USB driver");

	return err;
}

void hci_usb_cleanup(void)
{
	usb_deregister(&hci_usb_driver);
}

module_init(hci_usb_init);
module_exit(hci_usb_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("BlueZ HCI USB driver ver " VERSION);
MODULE_LICENSE("GPL");
