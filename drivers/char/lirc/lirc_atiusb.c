/* lirc_atiusb - USB remote support for LIRC
 * (currently only supports X10 USB remotes)
 * Version 0.3  [beta status]
 *
 * Copyright (C) 2003-2004 Paul Miller <pmiller9@users.sourceforge.net>
 *
 * This driver was derived from:
 *   Vladimir Dergachev <volodya@minspring.com>'s 2002
 *      "USB ATI Remote support" (input device)
 *   Adrian Dewhurst <sailor-lk@sailorfrag.net>'s 2002
 *      "USB StreamZap remote driver" (LIRC)
 *   Artur Lipowski <alipowski@kki.net.pl>'s 2002
 *      "lirc_dev" and "lirc_gpio" LIRC modules
 *
 * $Id: lirc_atiusb.c,v 1.21 2004/01/31 03:38:58 pmiller9 Exp $
 */

/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 2, 4)
#error "*******************************************************"
#error "Sorry, this driver needs kernel version 2.2.4 or higher"
#error "*******************************************************"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
#define KERNEL26		1
#else
#define KERNEL26		0
#endif

#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/poll.h>
#include <linux/wait.h>

#if KERNEL26
#include <linux/lirc.h>
#include "lirc_dev.h"
#else
#include "drivers/lirc.h"
#include "drivers/lirc_dev/lirc_dev.h"
#endif

#define DRIVER_VERSION		"0.3"
#define DRIVER_AUTHOR		"Paul Miller <pmiller9@users.sourceforge.net>"
#define DRIVER_DESC		"USB remote driver for LIRC"
#define DRIVER_NAME		"lirc_atiusb"

#define CODE_LENGTH		5
#define CODE_MIN_LENGTH		4
#define USB_BUFLEN		(CODE_LENGTH*4)

#ifdef CONFIG_USB_DEBUG
	static int debug = 1;
#else
	static int debug = 0;
#endif
#define dprintk			if (debug) printk

/* get hi and low bytes of a 16-bits int */
#define HI(a)			((unsigned char)((a) >> 8))
#define LO(a)			((unsigned char)((a) & 0xff))

/* lock irctl structure */
#define IRLOCK			down_interruptible(&ir->lock)
#define IRUNLOCK		up(&ir->lock)

/* general constants */
#define SUCCESS			0
#define SEND_FLAG_IN_PROGRESS	1
#define SEND_FLAG_COMPLETE	2


/* data structure for each usb remote */
struct irctl {

	/* usb */
	struct usb_device *usbdev;
	struct urb *urb_in;
	struct urb *urb_out;
	int devnum;

	/* buffers and dma */
	unsigned char *buf_in;
	unsigned char *buf_out;
	unsigned int len_in;
#if KERNEL26
	dma_addr_t dma_in;
	dma_addr_t dma_out;
#endif

	/* lirc */
	struct lirc_plugin *p;
	int connected;

	/* handle sending (init strings) */
	int send_flags;
	wait_queue_head_t wait_out;

	struct semaphore lock;
};

/* init strings */
static char init1[] = {0x01, 0x00, 0x20, 0x14};
static char init2[] = {0x01, 0x00, 0x20, 0x14, 0x20, 0x20, 0x20};

/* send packet - used to initialize remote */
static void send_packet(struct irctl *ir, u16 cmd, unsigned char *data)
{
	DECLARE_WAITQUEUE(wait, current);
	int timeout = HZ; /* 1 second */
	unsigned char buf[USB_BUFLEN];

	dprintk(DRIVER_NAME "[%d]: send called (%#x)\n", ir->devnum, cmd);

	IRLOCK;
	ir->urb_out->transfer_buffer_length = LO(cmd) + 1;
	ir->urb_out->dev = ir->usbdev;
	ir->send_flags = SEND_FLAG_IN_PROGRESS;

	memcpy(buf+1, data, LO(cmd));
	buf[0] = HI(cmd);
	memcpy(ir->buf_out, buf, LO(cmd)+1);

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&ir->wait_out, &wait);

#if KERNEL26
	if (usb_submit_urb(ir->urb_out, SLAB_ATOMIC)) {
#else
	if (usb_submit_urb(ir->urb_out)) {
#endif
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&ir->wait_out, &wait);
		IRUNLOCK;
		return;
	}
	IRUNLOCK;

	while (timeout && (ir->urb_out->status == -EINPROGRESS)
		&& !(ir->send_flags & SEND_FLAG_COMPLETE)) {
		timeout = schedule_timeout(timeout);
		rmb();
	}

	dprintk(DRIVER_NAME "[%d]: send complete (%#x)\n", ir->devnum, cmd);

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ir->wait_out, &wait);
	usb_unlink_urb(ir->urb_out);
}

static int unregister_from_lirc(struct irctl *ir)
{
	struct lirc_plugin *p = ir->p;
	int devnum;
	int rtn;

	devnum = ir->devnum;
	dprintk(DRIVER_NAME "[%d]: unregister from lirc called\n", devnum);

	if ((rtn = lirc_unregister_plugin(p->minor)) > 0) {
		printk(DRIVER_NAME "[%d]: error in lirc_unregister minor: %d\n"
			"Trying again...\n", devnum, p->minor);
		if (rtn == -EBUSY) {
			printk(DRIVER_NAME
				"[%d]: device is opened, will unregister"
				" on close\n", devnum);
			return -EAGAIN;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);

		if ((rtn = lirc_unregister_plugin(p->minor)) > 0) {
			printk(DRIVER_NAME "[%d]: lirc_unregister failed\n",
			devnum);
		}
	}

	if (rtn != SUCCESS) {
		printk(DRIVER_NAME "[%d]: didn't free resources\n", devnum);
		return -EAGAIN;
	}

	printk(DRIVER_NAME "[%d]: usb remote disconnected\n", devnum);

	lirc_buffer_free(p->rbuf);
	kfree(p->rbuf);
	kfree(p);
	kfree(ir);
	return SUCCESS;
}

static int set_use_inc(void *data)
{
	struct irctl *ir = data;

	if (!ir) {
		printk(DRIVER_NAME "[?]: set_use_inc called with no context\n");
		return -EIO;
	}
	dprintk(DRIVER_NAME "[%d]: set use inc\n", ir->devnum);

	if (!ir->connected) {
		if (!ir->usbdev)
			return -ENOENT;
		ir->urb_in->dev = ir->usbdev;
#if KERNEL26
		if (usb_submit_urb(ir->urb_in, SLAB_ATOMIC)) {
#else
		if (usb_submit_urb(ir->urb_in)) {
#endif
			printk(DRIVER_NAME "[%d]: open result = -EIO error "
				"submitting urb\n", ir->devnum);
			return -EIO;
		}
		ir->connected = 1;
	}

	return SUCCESS;
}

static void set_use_dec(void *data)
{
	struct irctl *ir = data;

	if (!ir) {
		printk(DRIVER_NAME "[?]: set_use_dec called with no context\n");
		return;
	}
	dprintk(DRIVER_NAME "[%d]: set use dec\n", ir->devnum);

	if (ir->connected) {
		IRLOCK;
		usb_unlink_urb(ir->urb_in);
		ir->connected = 0;
		IRUNLOCK;
	}
}


#if KERNEL26
static void usb_remote_recv(struct urb *urb, struct pt_regs *regs)
#else
static void usb_remote_recv(struct urb *urb)
#endif
{
	struct irctl *ir;
	char buf[CODE_LENGTH];
	int i, len;

	if (!urb)
		return;

	if (!(ir = urb->context)) {
		usb_unlink_urb(urb);
		return;
	}

	dprintk(DRIVER_NAME "[%d]: data received (length %d)\n",
		ir->devnum, urb->actual_length);

	switch (urb->status) {

	/* success */
	case SUCCESS:
		/* some remotes emit both 4 and 5 byte length codes. */
		len = urb->actual_length;
		if (len < CODE_MIN_LENGTH || len > CODE_LENGTH) return;

		memcpy(buf,urb->transfer_buffer,len);
		for (i = len; i < CODE_LENGTH; i++) buf[i] = 0;

		lirc_buffer_write_1(ir->p->rbuf, buf);
		wake_up(&ir->p->rbuf->wait_poll);
		break;

	/* unlink */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;
	}

	/* resubmit urb */
#if KERNEL26
	usb_submit_urb(urb, SLAB_ATOMIC);
#else
	usb_submit_urb(urb);
#endif
}

#if KERNEL26
static void usb_remote_send(struct urb *urb, struct pt_regs *regs)
#else
static void usb_remote_send(struct urb *urb)
#endif
{
	struct irctl *ir;

	if (!urb)
		return;

	if (!(ir = urb->context)) {
		usb_unlink_urb(urb);
		return;
	}

	dprintk(DRIVER_NAME "[%d]: usb out called\n", ir->devnum);

	if (urb->status)
		return;

	ir->send_flags |= SEND_FLAG_COMPLETE;
	wmb();
	if (waitqueue_active(&ir->wait_out))
		wake_up(&ir->wait_out);
}

#if KERNEL26
static int usb_remote_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *dev = NULL;
	struct usb_host_interface *idesc = NULL;
#else
static void *usb_remote_probe(struct usb_device *dev, unsigned int ifnum,
				const struct usb_device_id *id)
{
	struct usb_interface *intf;
	struct usb_interface_descriptor *idesc;
#endif
	struct usb_endpoint_descriptor *ep_in, *ep_out;
	struct irctl *ir = NULL;
	struct lirc_plugin *plugin = NULL;
	struct lirc_buffer *rbuf = NULL;
	int devnum, pipe, maxp, len, buf_len, bytes_in_key;
	int minor = 0;
	char buf[63], name[128]="";
	int mem_failure = 0;

	dprintk(DRIVER_NAME ": usb probe called\n");

#if KERNEL26
	dev = interface_to_usbdev(intf);
	idesc = intf->cur_altsetting;
	if (idesc->desc.bNumEndpoints != 2)
		return -ENODEV;
	ep_in = &idesc->endpoint[0].desc;
	ep_out = &idesc->endpoint[1].desc;
	if (((ep_in->bEndpointAddress & USB_ENDPOINT_DIR_MASK) != USB_DIR_IN)
		|| (ep_in->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		!= USB_ENDPOINT_XFER_INT)
		return -ENODEV;
#else
	intf = &dev->actconfig->interface[ifnum];
	idesc = &intf->altsetting[intf->act_altsetting];
	if (idesc->bNumEndpoints != 2)
		return NULL;
	ep_in = idesc->endpoint + 0;
	ep_out = idesc->endpoint + 1;
	if (((ep_in->bEndpointAddress & USB_ENDPOINT_DIR_MASK) != USB_DIR_IN)
		|| (ep_in->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		!= USB_ENDPOINT_XFER_INT)
		return NULL;
#endif
	devnum = dev->devnum;
	pipe = usb_rcvintpipe(dev, ep_in->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	bytes_in_key = CODE_LENGTH;
	len = (maxp > USB_BUFLEN) ? USB_BUFLEN : maxp;
	buf_len = len - (len % bytes_in_key);

	dprintk(DRIVER_NAME "[%d]: bytes_in_key=%d len=%d maxp=%d buf_len=%d\n",
		devnum, bytes_in_key, len, maxp, buf_len);


	/* allocate kernel memory */
	mem_failure = 0;
	if (!(ir = kmalloc(sizeof(struct irctl), GFP_KERNEL))) {
		mem_failure = 1;
	} else {
		memset(ir, 0, sizeof(struct irctl));

		if (!(plugin = kmalloc(sizeof(struct lirc_plugin), GFP_KERNEL))) {
			mem_failure = 2;
		} else if (!(rbuf = kmalloc(sizeof(struct lirc_buffer), GFP_KERNEL))) {
			mem_failure = 3;
		} else if (lirc_buffer_init(rbuf, bytes_in_key, USB_BUFLEN/bytes_in_key)) {
			mem_failure = 4;
#if KERNEL26
		} else if (!(ir->buf_in = usb_buffer_alloc(dev, buf_len, SLAB_ATOMIC, &ir->dma_in))) {
			mem_failure = 5;
		} else if (!(ir->buf_out = usb_buffer_alloc(dev, USB_BUFLEN, SLAB_ATOMIC, &ir->dma_out))) {
			mem_failure = 6;
		} else if (!(ir->urb_in = usb_alloc_urb(0, GFP_KERNEL))) {
			mem_failure = 7;
		} else if (!(ir->urb_out = usb_alloc_urb(0, GFP_KERNEL))) {
			mem_failure = 8;
#else
		} else if (!(ir->buf_in = kmalloc(buf_len, GFP_KERNEL))) {
			mem_failure = 5;
		} else if (!(ir->buf_out = kmalloc(USB_BUFLEN, GFP_KERNEL))) {
			mem_failure = 6;
		} else if (!(ir->urb_in = usb_alloc_urb(0))) {
			mem_failure = 7;
		} else if (!(ir->urb_out = usb_alloc_urb(0))) {
			mem_failure = 8;
#endif
		} else {

			memset(plugin, 0, sizeof(struct lirc_plugin));

			strcpy(plugin->name, DRIVER_NAME " ");
			plugin->minor = -1;
			plugin->code_length = bytes_in_key*8;
			plugin->features = LIRC_CAN_REC_LIRCCODE;
			plugin->data = ir;
			plugin->rbuf = rbuf;
			plugin->set_use_inc = &set_use_inc;
			plugin->set_use_dec = &set_use_dec;

			init_MUTEX(&ir->lock);
			init_waitqueue_head(&ir->wait_out);

			if ((minor = lirc_register_plugin(plugin)) < 0) {
				mem_failure = 9;
			}
		}
	}

	/* free allocated memory incase of failure */
	switch (mem_failure) {
	case 9:
		lirc_buffer_free(rbuf);
	case 8:
		usb_free_urb(ir->urb_out);
	case 7:
		usb_free_urb(ir->urb_in);
#if KERNEL26
	case 6:
		usb_buffer_free(dev, USB_BUFLEN, ir->buf_out, ir->dma_out);
	case 5:
		usb_buffer_free(dev, buf_len, ir->buf_in, ir->dma_in);
#else
	case 6:
		kfree(ir->buf_out);
	case 5:
		kfree(ir->buf_in);
#endif
	case 4:
		kfree(rbuf);
	case 3:
		kfree(plugin);
	case 2:
		kfree(ir);
	case 1:
		printk(DRIVER_NAME "[%d]: out of memory (code=%d)\n",
			devnum, mem_failure);
#if KERNEL26
		return -ENOMEM;
#else
		return NULL;
#endif
	}

	plugin->minor = minor;
	ir->p = plugin;
	ir->devnum = devnum;
	ir->usbdev = dev;
	ir->len_in = buf_len;
	ir->connected = 0;

	usb_fill_int_urb(ir->urb_in, dev, pipe, ir->buf_in,
		buf_len, usb_remote_recv, ir, ep_in->bInterval);
	usb_fill_int_urb(ir->urb_out, dev,
		usb_sndintpipe(dev, ep_out->bEndpointAddress), ir->buf_out,
		USB_BUFLEN, usb_remote_send, ir, ep_out->bInterval);

	if (dev->descriptor.iManufacturer
		&& usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0)
		strncpy(name, buf, 128);
	if (dev->descriptor.iProduct
		&& usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0)
		snprintf(name, 128, "%s %s", name, buf);
	printk(DRIVER_NAME "[%d]: %s on usb%d:%d\n", devnum, name,
	       dev->bus->busnum, devnum);

	send_packet(ir, 0x8004, init1);
	send_packet(ir, 0x8007, init2);

#if KERNEL26
	usb_set_intfdata(intf, ir);
	return SUCCESS;
#else
	return ir;
#endif
}


#if KERNEL26
static void usb_remote_disconnect(struct usb_interface *intf)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct irctl *ir = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
#else
static void usb_remote_disconnect(struct usb_device *dev, void *ptr)
{
	struct irctl *ir = ptr;
#endif

	if (!ir || !ir->p)
		return;

	ir->usbdev = NULL;
	wake_up_all(&ir->wait_out);

	IRLOCK;
	usb_unlink_urb(ir->urb_in);
	usb_unlink_urb(ir->urb_out);
	usb_free_urb(ir->urb_in);
	usb_free_urb(ir->urb_out);
#if KERNEL26
	usb_buffer_free(dev, ir->len_in, ir->buf_in, ir->dma_in);
	usb_buffer_free(dev, USB_BUFLEN, ir->buf_out, ir->dma_out);
#else
	kfree(ir->buf_in);
	kfree(ir->buf_out);
#endif
	IRUNLOCK;

	unregister_from_lirc(ir);
}

static struct usb_device_id usb_remote_id_table [] = {
	{ USB_DEVICE(0x0bc7, 0x0002) },		/* X10 USB Firecracker Interface */
	{ USB_DEVICE(0x0bc7, 0x0003) },		/* X10 VGA Video Sender */
	{ USB_DEVICE(0x0bc7, 0x0004) },		/* ATI Wireless Remote Receiver */
	{ USB_DEVICE(0x0bc7, 0x0005) },		/* NVIDIA Wireless Remote Receiver */
	{ USB_DEVICE(0x0bc7, 0x0006) },		/* ATI Wireless Remote Receiver */
	{ USB_DEVICE(0x0bc7, 0x0007) },		/* X10 USB Wireless Transceiver */
	{ USB_DEVICE(0x0bc7, 0x0008) },		/* X10 USB Wireless Transceiver */
	{ USB_DEVICE(0x0bc7, 0x0009) },		/* X10 USB Wireless Transceiver */
	{ USB_DEVICE(0x0bc7, 0x000A) },		/* X10 USB Wireless Transceiver */
	{ USB_DEVICE(0x0bc7, 0x000B) },		/* X10 USB Transceiver */
	{ USB_DEVICE(0x0bc7, 0x000C) },		/* X10 USB Transceiver */
	{ USB_DEVICE(0x0bc7, 0x000D) },		/* X10 USB Transceiver */
	{ USB_DEVICE(0x0bc7, 0x000E) },		/* X10 USB Transceiver */
	{ USB_DEVICE(0x0bc7, 0x000F) },		/* X10 USB Transceiver */

	{ }					/* Terminating entry */
};

static struct usb_driver usb_remote_driver = {
	.owner =	THIS_MODULE,
	.name =		DRIVER_NAME,
	.probe =	usb_remote_probe,
	.disconnect =	usb_remote_disconnect,
	.id_table =	usb_remote_id_table
};

static int __init usb_remote_init(void)
{
	int i;

	printk("\n" DRIVER_NAME ": " DRIVER_DESC " v" DRIVER_VERSION "\n");
	printk(DRIVER_NAME ": " DRIVER_AUTHOR "\n");
	dprintk(DRIVER_NAME ": debug mode enabled\n");

	request_module("lirc_dev");

	if ((i = usb_register(&usb_remote_driver)) < 0) {
		printk(DRIVER_NAME ": usb register failed, result = %d\n", i);
		return -ENODEV;
	}

	return SUCCESS;
}

static void __exit usb_remote_exit(void)
{
	usb_deregister(&usb_remote_driver);
}

module_init(usb_remote_init);
module_exit(usb_remote_exit);

MODULE_AUTHOR (DRIVER_AUTHOR);
MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_LICENSE ("GPL");
MODULE_DEVICE_TABLE (usb, usb_remote_id_table);

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "enable driver debug mode");

#if !KERNEL26
EXPORT_NO_SYMBOLS;
#endif

