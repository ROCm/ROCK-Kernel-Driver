/* lirc_atiusb - USB remote support for LIRC
 * (currently only supports X10 USB remotes)
 * (supports ATI Remote Wonder and ATI Remote Wonder II, too)
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
 * $Id: lirc_atiusb.c,v 1.38 2004/10/08 15:17:12 pmiller9 Exp $
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 4, 0)
#error "*******************************************************"
#error "Sorry, this driver needs kernel version 2.4.0 or higher"
#error "*******************************************************"
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

#include "lirc.h"
#include "kcompat.h"
#include "lirc_dev.h"

#define DRIVER_VERSION		"0.4"
#define DRIVER_AUTHOR		"Paul Miller <pmiller9@users.sourceforge.net>"
#define DRIVER_DESC		"USB remote driver for LIRC"
#define DRIVER_NAME		"lirc_atiusb"

#define CODE_LENGTH		5
#define CODE_MIN_LENGTH		3
#define USB_BUFLEN		(CODE_LENGTH*4)

/* module parameters */
#ifdef CONFIG_USB_DEBUG
	static int debug = 1;
#else
	static int debug = 0;
#endif
#define dprintk(fmt, args...)                                 \
	do{                                                   \
		if(debug) printk(KERN_DEBUG fmt, ## args);    \
	}while(0)

static int mask = 0xFFFF;	// channel acceptance bit mask
static int unique = 0;		// enable channel-specific codes
static int repeat = 10;		// repeat time in 1/100 sec
static unsigned long repeat_jiffies; // repeat timeout

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

#define VENDOR_ATI1		0x0bc7
#define VENDOR_ATI2		0x0471

static struct usb_device_id usb_remote_table [] = {
	{ USB_DEVICE(VENDOR_ATI1, 0x0002) },	/* X10 USB Firecracker Interface */
	{ USB_DEVICE(VENDOR_ATI1, 0x0003) },	/* X10 VGA Video Sender */
	{ USB_DEVICE(VENDOR_ATI1, 0x0004) },	/* ATI Wireless Remote Receiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x0005) },	/* NVIDIA Wireless Remote Receiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x0006) },	/* ATI Wireless Remote Receiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x0007) },	/* X10 USB Wireless Transceiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x0008) },	/* X10 USB Wireless Transceiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x0009) },	/* X10 USB Wireless Transceiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x000A) },	/* X10 USB Wireless Transceiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x000B) },	/* X10 USB Transceiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x000C) },	/* X10 USB Transceiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x000D) },	/* X10 USB Transceiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x000E) },	/* X10 USB Transceiver */
	{ USB_DEVICE(VENDOR_ATI1, 0x000F) },	/* X10 USB Transceiver */

	{ USB_DEVICE(VENDOR_ATI2, 0x0602) },	/* ATI Remote Wonder 2 */

	{ }					/* Terminating entry */
};

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
#ifdef KERNEL_2_5
	dma_addr_t dma_in;
	dma_addr_t dma_out;
#endif

	/* remote type based on usb_device_id tables */
	enum {
		ATI1_COMPATIBLE,
		ATI2_COMPATIBLE
	} remote_type;

	/* handle repeats */
	unsigned char old[CODE_LENGTH];
	unsigned long old_jiffies;

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

#ifdef KERNEL_2_5
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

	MOD_INC_USE_COUNT;

	if (!ir->connected) {
		if (!ir->usbdev)
			return -ENOENT;
		ir->urb_in->dev = ir->usbdev;
#ifdef KERNEL_2_5
		if (usb_submit_urb(ir->urb_in, SLAB_ATOMIC)) {
#else
		if (usb_submit_urb(ir->urb_in)) {
#endif
			printk(DRIVER_NAME "[%d]: open result = -EIO error "
				"submitting urb\n", ir->devnum);
			MOD_DEC_USE_COUNT;
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
	MOD_DEC_USE_COUNT;
}

static void usb_remote_printdata(struct irctl *ir, char *buf, int len)
{
	char codes[USB_BUFLEN*3 + 1];
	int i;

	if (len <= 0)
		return;

	for (i = 0; i < len && i < USB_BUFLEN; i++) {
		snprintf(codes+i*3, 4, "%02x ", buf[i] & 0xFF);
	}
	printk(DRIVER_NAME "[%d]: data received %s (length=%d)\n",
		ir->devnum, codes, len);
}

static inline int usb_remote_check(struct irctl *ir, int len)
{
	int chan;

	/* ATI RW1: some remotes emit both 4 and 5 byte length codes. */
	/* ATI RW2: emit 3 byte codes */
	if (len < CODE_MIN_LENGTH || len > CODE_LENGTH)
		return -1;

	switch (ir->remote_type) {

	case ATI1_COMPATIBLE:

		// *** channel not tested with 4/5-byte Dutch remotes ***
		chan = ((ir->buf_in[len-1]>>4) & 0x0F);

		/* strip channel code */
		if (!unique) {
			ir->buf_in[len-1] &= 0x0F;
			ir->buf_in[len-3] -= (chan<<4);
		}
		break;

	case ATI2_COMPATIBLE:
		chan = ir->buf_in[0];
		if (!unique) ir->buf_in[0] = 0;
		break;

	default:
		chan = 0;
	}
	return chan;
}


#ifdef KERNEL_2_5
static void usb_remote_recv(struct urb *urb, struct pt_regs *regs)
#else
static void usb_remote_recv(struct urb *urb)
#endif
{
	struct irctl *ir;
	int i, len, chan;

	if (!urb)
		return;

	if (!(ir = urb->context)) {
#ifdef KERNEL_2_5
		urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
		usb_unlink_urb(urb);
		return;
	}

	len = urb->actual_length;
	if (debug)
		usb_remote_printdata(ir,urb->transfer_buffer,len);

	switch (urb->status) {

	/* success */
	case SUCCESS:

		if ((chan = usb_remote_check(ir, len)) < 0)
			break;

		if ( !((1U<<chan) & mask) ) {
			dprintk(DRIVER_NAME "[%d]: ignore channel %d\n",
				ir->devnum, chan+1);
			break;
		}
		dprintk(DRIVER_NAME "[%d]: accept channel %d\n",
			ir->devnum, chan+1);

		/* check for repeats */
		if (memcmp(ir->old, ir->buf_in, len) == 0) {
			if (ir->old_jiffies + repeat_jiffies > jiffies) {
				break;
			}
		} else {
			memcpy(ir->old, ir->buf_in, len);
			for (i = len; i < CODE_LENGTH; i++) ir->old[i] = 0;
		}
		ir->old_jiffies = jiffies;

		lirc_buffer_write_1(ir->p->rbuf, ir->old);
		wake_up(&ir->p->rbuf->wait_poll);
		break;

	/* unlink */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
#ifdef KERNEL_2_5
		urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
		usb_unlink_urb(urb);
		return;

	case -EPIPE:
	default:
		break;
	}

	/* resubmit urb */
#ifdef KERNEL_2_5
	usb_submit_urb(urb, SLAB_ATOMIC);
#else
//	kernel 2.4 resubmits automagically???
//	usb_submit_urb(urb);
#endif
}

#ifdef KERNEL_2_5
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


#ifdef KERNEL_2_5
static int usb_remote_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *idesc;
#else
static void *usb_remote_probe(struct usb_device *dev, unsigned int ifnum,
				const struct usb_device_id *id)
{
	struct usb_interface *intf = &dev->actconfig->interface[ifnum];
	struct usb_interface_descriptor *idesc;
#endif
	struct usb_endpoint_descriptor *ep=NULL, *ep_in=NULL, *ep_out=NULL;
	struct irctl *ir = NULL;
	struct lirc_plugin *plugin = NULL;
	struct lirc_buffer *rbuf = NULL;
	int devnum, pipe, maxp, len, buf_len, bytes_in_key;
	int minor = 0;
	int i, type;
	char buf[63], name[128]="";
	int mem_failure = 0;

	dprintk(DRIVER_NAME ": usb probe called\n");

	/* determine remote type */
	switch (dev->descriptor.idVendor) {
	case VENDOR_ATI1:
		type = ATI1_COMPATIBLE;
		break;
	case VENDOR_ATI2:
		type = ATI2_COMPATIBLE;
		break;
	default:
		dprintk(DRIVER_NAME ": unknown id\n");
#ifdef KERNEL_2_5
		return -ENODEV;
#else
		return NULL;
#endif
	}


#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,4)
	idesc = intf->cur_altsetting;
#else
	idesc = &intf->altsetting[intf->act_altsetting];
#endif

	// step through the endpoints to find first in and first out endpoint
	// of type interrupt transfer
#ifdef KERNEL_2_5
	for (i = 0; i < idesc->desc.bNumEndpoints; ++i) {
		ep = &idesc->endpoint[i].desc;
#else
	for (i = 0; i < idesc->bNumEndpoints; ++i) {
		ep = &idesc->endpoint[i];
#endif

		if ((ep_in == NULL)
			&& ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
			&& ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)) {

			// ATI2's ep 1 only reports mouse emulation, ep 2 reports both
			if ((type != ATI2_COMPATIBLE)
				|| ((ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK) == 0x02)) {

				dprintk(DRIVER_NAME ": acceptable inbound endpoint found\n");
				ep_in = ep;
			}
		}

		if ((ep_out == NULL)
			&& ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
			&& ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)) {

			dprintk(DRIVER_NAME ": acceptable outbound endpoint found\n");
			ep_out = ep;
		}
	}
	if (ep_in == NULL) {
		dprintk(DRIVER_NAME ": inbound endpoint not found\n");
#ifdef KERNEL_2_5
		return -ENODEV;
#else
		return NULL;
#endif
	}

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
#ifdef KERNEL_2_5
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
#ifdef KERNEL_2_5
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
#ifdef KERNEL_2_5
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
	ir->remote_type = type;

	if (dev->descriptor.iManufacturer
		&& usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0)
		strncpy(name, buf, 128);
	if (dev->descriptor.iProduct
		&& usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0)
		snprintf(name, 128, "%s %s", name, buf);
	printk(DRIVER_NAME "[%d]: %s on usb%d:%d\n", devnum, name,
	       dev->bus->busnum, devnum);

	/* inbound data */
	usb_fill_int_urb(ir->urb_in, dev, pipe, ir->buf_in,
		buf_len, usb_remote_recv, ir, ep_in->bInterval);

	/* outbound data (initialization) */
	if (ep_out != NULL) {
		usb_fill_int_urb(ir->urb_out, dev,
			usb_sndintpipe(dev, ep_out->bEndpointAddress), ir->buf_out,
			USB_BUFLEN, usb_remote_send, ir, ep_out->bInterval);

		send_packet(ir, 0x8004, init1);
		send_packet(ir, 0x8007, init2);
	}

#ifdef KERNEL_2_5
	usb_set_intfdata(intf, ir);
	return SUCCESS;
#else
	return ir;
#endif
}


#ifdef KERNEL_2_5
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
#ifdef KERNEL_2_5
	usb_buffer_free(dev, ir->len_in, ir->buf_in, ir->dma_in);
	usb_buffer_free(dev, USB_BUFLEN, ir->buf_out, ir->dma_out);
#else
	kfree(ir->buf_in);
	kfree(ir->buf_out);
#endif
	IRUNLOCK;

	unregister_from_lirc(ir);
}

static struct usb_driver usb_remote_driver = {
	.owner =	THIS_MODULE,
	.name =		DRIVER_NAME,
	.probe =	usb_remote_probe,
	.disconnect =	usb_remote_disconnect,
	.id_table =	usb_remote_table
};

static int __init usb_remote_init(void)
{
	int i;

	printk("\n" DRIVER_NAME ": " DRIVER_DESC " v" DRIVER_VERSION "\n");
	printk(DRIVER_NAME ": " DRIVER_AUTHOR "\n");
	dprintk(DRIVER_NAME ": debug mode enabled\n");

	request_module("lirc_dev");

	repeat_jiffies = repeat*HZ/100;

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

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, usb_remote_table);

module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug enabled or not");

module_param(mask, int, 0444);
MODULE_PARM_DESC(mask, "Set channel acceptance bit mask");

module_param(unique, int, 0444);
MODULE_PARM_DESC(unique, "Enable channel-specific codes");

module_param(repeat, int, 0444);
MODULE_PARM_DESC(repeat, "Repeat timeout (1/100 sec)");

EXPORT_NO_SYMBOLS;
