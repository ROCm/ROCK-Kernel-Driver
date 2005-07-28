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
 * $Id: lirc_atiusb.c,v 1.49 2005/03/12 11:32:14 lirc Exp $
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
#include <linux/list.h>

#include "lirc.h"
#include "kcompat.h"
#include "lirc_dev.h"

#define DRIVER_VERSION		"0.4"
#define DRIVER_AUTHOR		"Paul Miller <pmiller9@users.sourceforge.net>"
#define DRIVER_DESC		"USB remote driver for LIRC"
#define DRIVER_NAME		"lirc_atiusb"

#define CODE_LENGTH		5
#define CODE_LENGTH_ATI2	3
#define CODE_MIN_LENGTH		3

#define RW2_MODENAV_KEYCODE	0x3F
#define RW2_NULL_MODE		0xFF
/* Fake (virtual) keycode indicating compass mouse usage */
#define RW2_MOUSE_KEYCODE	0xFF
#define RW2_PRESSRELEASE_KEYCODE	0xFE

#define RW2_PRESS_CODE		1
#define RW2_HOLD_CODE		2
#define RW2_RELEASE_CODE	0

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
static int emit_updown = 0;	// send seperate press/release codes (rw2)
static int emit_modekeys = 0;	// send keycodes for aux1-aux4, pc, and mouse (rw2)
static unsigned long repeat_jiffies; // repeat timeout
static int mdeadzone = 0;	// mouse sensitivity >= 0
static int mgradient = 375;	// 1000*gradient from cardinal direction

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
#define FREE_ALL		0xFF

/* endpoints */
#define EP_KEYS			0
#define EP_MOUSE		1
#define EP_MOUSE_ADDR		0x81
#define EP_KEYS_ADDR		0x82

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

	{ USB_DEVICE(VENDOR_ATI2, 0x0602) },	/* ATI Remote Wonder 2: Input Device */
	{ USB_DEVICE(VENDOR_ATI2, 0x0603) },	/* ATI Remote Wonder 2: Controller (???) */

	{ }					/* Terminating entry */
};


/* init strings */
#define USB_OUTLEN		7

static char init1[] = {0x01, 0x00, 0x20, 0x14};
static char init2[] = {0x01, 0x00, 0x20, 0x14, 0x20, 0x20, 0x20};



struct in_endpt {
	/* inner link in list of endpoints for the remote specified by ir */
	struct list_head iep_list_link;
	struct irctl *ir;
	struct urb *urb;
	struct usb_endpoint_descriptor *ep;
	int type;

	/* buffers and dma */
	unsigned char *buf;
	unsigned int len;
#ifdef KERNEL_2_5
	dma_addr_t dma;
#endif

	/* handle repeats */
	unsigned char old[CODE_LENGTH];
	unsigned long old_jiffies;
};

struct out_endpt {
	struct irctl *ir;
	struct urb *urb;
	struct usb_endpoint_descriptor *ep;

	/* buffers and dma */
	unsigned char *buf;
#ifdef KERNEL_2_5
	dma_addr_t dma;
#endif

	/* handle sending (init strings) */
	int send_flags;
	wait_queue_head_t wait;
};


/* data structure for each usb remote */
struct irctl {
	/* inner link in list of all remotes managed by this module */
	struct list_head remote_list_link;
	/* Number of usb interfaces associated with this device */
	int dev_refcount;

	/* usb */
	struct usb_device *usbdev;
	/* Head link to list of all inbound endpoints in this remote */
	struct list_head iep_listhead;
	struct out_endpt *out_init;
	int devnum;

	/* remote type based on usb_device_id tables */
	enum {
		ATI1_COMPATIBLE,
		ATI2_COMPATIBLE
	} remote_type;

	/* rw2 current mode (mirror's remote's state) */
	int mode;

	/* lirc */
	struct lirc_plugin *p;
	int connected;

	/* locking */
	struct semaphore lock;
};

/* list of all registered devices via the remote_list_link in irctl */
static struct list_head remote_list;

/* Convenience macros to retrieve a pointer to the surrounding struct from
 * the given list_head reference within, pointed at by link. */
#define get_iep_from_link(link)  list_entry((link), struct in_endpt, iep_list_link);
#define get_irctl_from_link(link)  list_entry((link), struct irctl, remote_list_link);

/* send packet - used to initialize remote */
static void send_packet(struct out_endpt *oep, u16 cmd, unsigned char *data)
{
	struct irctl *ir = oep->ir;
	DECLARE_WAITQUEUE(wait, current);
	int timeout = HZ; /* 1 second */
	unsigned char buf[USB_OUTLEN];

	dprintk(DRIVER_NAME "[%d]: send called (%#x)\n", ir->devnum, cmd);

	IRLOCK;
	oep->urb->transfer_buffer_length = LO(cmd) + 1;
	oep->urb->dev = oep->ir->usbdev;
	oep->send_flags = SEND_FLAG_IN_PROGRESS;

	memcpy(buf+1, data, LO(cmd));
	buf[0] = HI(cmd);
	memcpy(oep->buf, buf, LO(cmd)+1);

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&oep->wait, &wait);

#ifdef KERNEL_2_5
	if (usb_submit_urb(oep->urb, SLAB_ATOMIC)) {
#else
	if (usb_submit_urb(oep->urb)) {
#endif
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&oep->wait, &wait);
		IRUNLOCK;
		return;
	}
	IRUNLOCK;

	while (timeout && (oep->urb->status == -EINPROGRESS)
		&& !(oep->send_flags & SEND_FLAG_COMPLETE)) {
		timeout = schedule_timeout(timeout);
		rmb();
	}

	dprintk(DRIVER_NAME "[%d]: send complete (%#x)\n", ir->devnum, cmd);

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&oep->wait, &wait);
#ifdef KERNEL_2_5
	oep->urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
	usb_unlink_urb(oep->urb);
}

static int unregister_from_lirc(struct irctl *ir)
{
	struct lirc_plugin *p = ir->p;
	int devnum;

	devnum = ir->devnum;
	dprintk(DRIVER_NAME "[%d]: unregister from lirc called\n", devnum);

	lirc_unregister_plugin(p->minor);

	printk(DRIVER_NAME "[%d]: usb remote disconnected\n", devnum);
	return SUCCESS;
}


static int set_use_inc(void *data)
{
	struct irctl *ir = data;
	struct list_head *pos, *n;
	struct in_endpt *iep;

	if (!ir) {
		printk(DRIVER_NAME "[?]: set_use_inc called with no context\n");
		return -EIO;
	}
	dprintk(DRIVER_NAME "[%d]: set use inc\n", ir->devnum);

	MOD_INC_USE_COUNT;

	if (!ir->connected) {
		if (!ir->usbdev)
			return -ENOENT;

		IRLOCK;
		/* Iterate through the inbound endpoints */
		list_for_each_safe(pos, n, &ir->iep_listhead) {
			/* extract the current in_endpt */
			iep = get_iep_from_link(pos);
			iep->urb->dev = ir->usbdev;
#ifdef KERNEL_2_5
			if (usb_submit_urb(iep->urb, SLAB_ATOMIC)) {
#else
			if (usb_submit_urb(iep->urb)) {
#endif
				printk(DRIVER_NAME "[%d]: open result = -EIO error "
					"submitting urb\n", ir->devnum);
				IRUNLOCK;
				MOD_DEC_USE_COUNT;
				return -EIO;
			}
		}
		ir->connected = 1;
		IRUNLOCK;
	}

	return SUCCESS;
}

static void set_use_dec(void *data)
{
	struct irctl *ir = data;
	struct list_head *pos, *n;
	struct in_endpt *iep;

	if (!ir) {
		printk(DRIVER_NAME "[?]: set_use_dec called with no context\n");
		return;
	}
	dprintk(DRIVER_NAME "[%d]: set use dec\n", ir->devnum);

	if (ir->connected) {
		IRLOCK;
		/* Free inbound usb urbs */
		list_for_each_safe(pos, n, &ir->iep_listhead) {
			iep = get_iep_from_link(pos);
#ifdef KERNEL_2_5
			iep->urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
			usb_unlink_urb(iep->urb);
		}
		ir->connected = 0;
		IRUNLOCK;
	}
	MOD_DEC_USE_COUNT;
}

static void print_data(struct in_endpt *iep, char *buf, int len)
{
	char codes[CODE_LENGTH*3 + 1];
	int i;

	if (len <= 0)
		return;

	for (i = 0; i < len && i < CODE_LENGTH; i++) {
		snprintf(codes+i*3, 4, "%02x ", buf[i] & 0xFF);
	}
	printk(DRIVER_NAME "[%d]: data received %s (ep=0x%x length=%d)\n",
		iep->ir->devnum, codes, iep->ep->bEndpointAddress, len);
}

static int code_check_ati1(struct in_endpt *iep, int len)
{
	struct irctl *ir = iep->ir;
	int i, chan;

	/* ATI RW1: some remotes emit both 4 and 5 byte length codes. */
	/* ATI RW2: emit 3 byte codes */
	if (len < CODE_MIN_LENGTH || len > CODE_LENGTH)
		return -1;

	// *** channel not tested with 4/5-byte Dutch remotes ***
	chan = ((iep->buf[len-1]>>4) & 0x0F);

	/* strip channel code */
	if (!unique) {
		iep->buf[len-1] &= 0x0F;
		iep->buf[len-3] -= (chan<<4);
	}

	if ( !((1U<<chan) & mask) ) {
		dprintk(DRIVER_NAME "[%d]: ignore channel %d\n", ir->devnum, chan+1);
		return -1;
	}
	dprintk(DRIVER_NAME "[%d]: accept channel %d\n", ir->devnum, chan+1);

	if (ir->remote_type == ATI1_COMPATIBLE) {
		/* check for repeats */
		if (memcmp(iep->old, iep->buf, len) == 0) {
			if (iep->old_jiffies + repeat_jiffies > jiffies) {
				return -1;
			}
		} else {
			for (i = len; i < CODE_LENGTH; i++) iep->buf[i] = 0;
			memcpy(iep->old, iep->buf, CODE_LENGTH);
		}
		iep->old_jiffies = jiffies;
	}

	return SUCCESS;
}

/*
 * Since the ATI Remote Wonder II has quite a different structure from the
 * prior version, this function was seperated out to clarify the sanitization
 * process.
 *
 * Here is a summary of the main differences:
 *
 * a. The rw2 has no sense of a transmission channel.  But, it does have an
 *    auxilliary mode state, which is set by the mode buttons Aux1 through
 *    Aux4 and "PC".  These map respectively to 0-4 in the first byte of the
 *    recv buffer.  Any subsequent button press sends this mode number as its
 *    "channel code."  Annoyingly enough, the mode setting buttons all send
 *    the same key code (0x3f), and can only be distinguished via their mode
 *    byte.
 *
 *    Because of this, old-style "unique"-parameter-enabled channel squashing
 *    kills the functionality of the aux1-aux4 and PC buttons.  However, to
 *    not do so would cause each remote key to send a different code depending
 *    on the active aux.  Further complicating matters, using the mouse norb
 *    also sends an identical code as would pushing the active aux button.  To
 *    handle this we need a seperate parameter, like rw2modes, with the
 *    following values and meanings:
 *
 *    	0: Don't squash any channel info
 *    	1: Only squash channel data for non-mode setting keys
 *    	2: Ignore aux keypresses, but don't squash channel
 *    	3: Ignore aux keypresses and squash channel data
 *
 *    Option 1 may seem useless since the mouse sends the same code, but one
 *    need only ignore in userspace any press of a mode-setting code that only
 *    reaffirms the current mode.  The 3rd party lirccd should be able to
 *    handle this easily enough, but lircd doesn't keep the state necessary
 *    for this.  TODO We could work around this in the driver by emitting a
 *    single 02 (press) code for a mode key only if that mode is not currently
 *    active.
 *
 *    Option 2 would be useful for those wanting super configurability,
 *    offering the ability to program 5 times the number actions based on the
 *    current mode.
 *
 * b. The rw2 has its own built in repeat handling; the keys endpoint
 *    encodes this in the second byte as 1 for press, 2 for hold, and 0 for
 *    release.  This is generally much more responsive than lirc's built-in
 *    timeout handling.
 *
 *    The problem is that the remote can send the release-recieve pair
 *    (0,1) while one is still holding down the same button if the
 *    transmission is momentarilly interrupted.  (It seems that the receiver
 *    manages this count instead of the remote.)  By default, this information
 *    is squashed to 2.
 *
 *    In order to expose the built-in repeat code, set the emit_updown
 *    parameter as described below.
 *
 * c. The mouse norb is much more sensitive than on the rw1.  It emulates
 *    a joystick-like controller with the second byte representing the x-axis
 *    and the third, the y-axis.  Treated as signed integers, these axes range
 *    approximately as follows:
 *
 *    	x: (left) -46 ... 46 (right) (0xd2..0x2e)
 *    	y: (up)   -46 ... 46 (down)  (0xd2..0x2e)
 *
 *    NB these values do not correspond to the pressure with which the mouse
 *    norb is pushed in a given direction, but rather seems to indicate the
 *    duration for which a given direction is held.
 *
 *    These are normalized to 8 cardinal directions for easy configuration via
 *    lircd.conf.  The normalization can be fined tuned with the mdeadzone and
 *    mgradient parameters as described below.
 *
 * d. The interrupt rate of the mouse vs. the normal keys is different.
 *
 * 	mouse: ~27Hz (37ms between interrupts)
 * 	keys:  ~10Hz (100ms between interrupts)
 *
 *    This means that the normal gap mechanism for lircd won't work as
 *    expected; is emit_updown>0 if you can get away with it.
 */
static int code_check_ati2(struct in_endpt *iep, int len) {
	struct irctl *ir = iep->ir;
	int mode, i;
	char *buf = iep->buf;

	if (len != CODE_LENGTH_ATI2) {
		dprintk(DRIVER_NAME
			"[%d]: Huh?  Abnormal length (%d) buffer recieved.\n",
			ir->devnum, len);
		return -1;
	}
	for (i = len; i < CODE_LENGTH; i++) iep->buf[i] = 0;

	mode = buf[0];

	/* Squash the mode indicator if unique wasn't set non-zero */
	if (!unique) buf[0] = 0;

	if (iep->ep->bEndpointAddress == EP_KEYS_ADDR) {
		/* ignore mouse navigation indicator key and mode-set (aux) keys */
		if (buf[2] == RW2_MODENAV_KEYCODE) {
			if (emit_modekeys >= 2) { /* emit raw */
				buf[0] = mode;
			} else if (emit_modekeys == 1) { /* translate */
				buf[0] = mode;
				if (ir->mode != mode) {
					buf[1] = 0x03;
					ir->mode = mode;
					return SUCCESS;
				}
			} else {
				dprintk(DRIVER_NAME "[%d]: ignore dummy code 0x%x (ep=0x%x)\n",
					ir->devnum, buf[2], iep->ep->bEndpointAddress);
				return -1;
			}
		}

		if (buf[1] != 2) {
			/* handle press/release codes */
			if (emit_updown == 0) /* ignore */
				return -1;
			else if(emit_updown == 1) /* normalize keycode */
				buf[2] = RW2_PRESSRELEASE_KEYCODE;
			/* else emit raw */
		}

	} else {
		int x = (signed char)buf[1];
		int y = (signed char)buf[2];
		int code = 0x00;
		int dir_ew, dir_ns;

		buf[2] = RW2_MOUSE_KEYCODE;

		/* sensitivity threshold (use L2norm^2) */
		if (mdeadzone > (x*x+y*y)) {
			buf[1] = 0x00;
			return SUCCESS;
		}

/* Nybble encoding: xy, 2 is -1 (S or W); 1 (N or E) */
#define MOUSE_N		0x01
#define MOUSE_NE	0x11
#define MOUSE_E		0x10
#define MOUSE_SE	0x12
#define MOUSE_S		0x02
#define MOUSE_SW	0x22
#define MOUSE_W		0x20
#define MOUSE_NW	0x21

		/* cardinal leanings: positive x -> E, positive y -> S */
		dir_ew = (x > 0) ? MOUSE_E : MOUSE_W;
		dir_ns = (y > 0) ? MOUSE_S : MOUSE_N;

		/* convert coordintes(angle) into compass direction */
		if (x == 0) {
			code = dir_ns;
		} else if (y == 0) {
			code = dir_ew;
		} else {
			if (abs(1000*y/x) > mgradient)
				code = dir_ns;
			if (abs(1000*x/y) > mgradient)
				code |= dir_ew;
		}

		buf[1] = code;
		dprintk(DRIVER_NAME "[%d]: mouse compass=0x%x %s%s (%d,%d)\n",
			ir->devnum, code,
			(code & MOUSE_S ? "S" : (code & MOUSE_N ? "N" : "")),
			(code & MOUSE_E ? "E" : (code & MOUSE_W ? "W" : "")),
			x, y);
	}

	return SUCCESS;
}


#ifdef KERNEL_2_5
static void usb_remote_recv(struct urb *urb, struct pt_regs *regs)
#else
static void usb_remote_recv(struct urb *urb)
#endif
{
	struct in_endpt *iep;
	int len, result;

	if (!urb)
		return;
	if (!(iep = urb->context)) {
#ifdef KERNEL_2_5
		urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
		usb_unlink_urb(urb);
		return;
	}
	if (!iep->ir->usbdev)
		return;

	len = urb->actual_length;
	if (debug)
		print_data(iep,urb->transfer_buffer,len);

	switch (urb->status) {

	/* success */
	case SUCCESS:

		switch (iep->ir->remote_type) {
		case ATI2_COMPATIBLE:
			result = code_check_ati2(iep, len);
			break;
		case ATI1_COMPATIBLE:
		default:
			result = code_check_ati1(iep, len);
		}
		if (result < 0) break;
		lirc_buffer_write_1(iep->ir->p->rbuf, iep->buf);
		wake_up(&iep->ir->p->rbuf->wait_poll);
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
#endif
}

#ifdef KERNEL_2_5
static void usb_remote_send(struct urb *urb, struct pt_regs *regs)
#else
static void usb_remote_send(struct urb *urb)
#endif
{
	struct out_endpt *oep;

	if (!urb)
		return;
	if (!(oep = urb->context)) {
#ifdef KERNEL_2_5
		urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
		usb_unlink_urb(urb);
		return;
	}
	if (!oep->ir->usbdev)
		return;

	dprintk(DRIVER_NAME "[%d]: usb out called\n", oep->ir->devnum);

	if (urb->status)
		return;

	oep->send_flags |= SEND_FLAG_COMPLETE;
	wmb();
	if (waitqueue_active(&oep->wait))
		wake_up(&oep->wait);
}


/***************************************************************************
 * Initialization and removal
 ***************************************************************************/

/*
 * Free iep according to mem_failure which specifies a checkpoint into the
 * initialization sequence for rollback recovery.
 */
static void free_in_endpt(struct in_endpt *iep, int mem_failure)
{
	struct irctl *ir;
	dprintk(DRIVER_NAME ": free_in_endpt(%p, %d)\n", iep, mem_failure);
	if (!iep) return;

	ir = iep->ir;
	if (!ir) {
		dprintk(DRIVER_NAME ": free_in_endpt: WARNING! null ir\n");
		return;
	}
	IRLOCK;
	switch (mem_failure) {
	case FREE_ALL:
	case 5:
		list_del(&iep->iep_list_link);
		dprintk(DRIVER_NAME "[%d]: free_in_endpt removing ep=0x%0x from list\n", ir->devnum, iep->ep->bEndpointAddress);
	case 4:
		if (iep->urb) {
#ifdef KERNEL_2_5
			iep->urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
			usb_unlink_urb(iep->urb);
			usb_free_urb(iep->urb);
			iep->urb = 0;
		} else {
			dprintk(DRIVER_NAME "[%d]: free_in_endpt null urb!\n", ir->devnum);
		}
	case 3:
#ifdef KERNEL_2_5
		usb_buffer_free(iep->ir->usbdev, iep->len, iep->buf, iep->dma);
#else
		kfree(iep->buf);
#endif
		iep->buf = 0;
	case 2:
		kfree(iep);
	}
	IRUNLOCK;
}

/*
 * Construct a new inbound endpoint for this remote, and add it to the list of
 * in_epts in ir.
 */
static struct in_endpt *new_in_endpt(struct irctl *ir, struct usb_endpoint_descriptor *ep)
{
	struct usb_device *dev = ir->usbdev;
	struct in_endpt *iep;
	int pipe, maxp, len, addr;
	int mem_failure;

	addr = ep->bEndpointAddress;
	pipe = usb_rcvintpipe(dev, addr);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

//	len = (maxp > USB_BUFLEN) ? USB_BUFLEN : maxp;
//	len -= (len % CODE_LENGTH);
	len = CODE_LENGTH;

	dprintk(DRIVER_NAME "[%d]: acceptable inbound endpoint (0x%x) found (maxp=%d len=%d)\n", ir->devnum, addr, maxp, len);

	mem_failure = 0;
	if ( !(iep = kmalloc(sizeof(*iep), GFP_KERNEL)) ) {
		mem_failure = 1;
	} else {
		memset(iep, 0, sizeof(*iep));
		iep->ir = ir;
		iep->ep = ep;
		iep->len = len;

#ifdef KERNEL_2_5
		if ( !(iep->buf = usb_buffer_alloc(dev, len, SLAB_ATOMIC, &iep->dma)) ) {
			mem_failure = 2;
		} else if ( !(iep->urb = usb_alloc_urb(0, GFP_KERNEL)) ) {
			mem_failure = 3;
		}
#else
		if ( !(iep->buf = kmalloc(len, GFP_KERNEL)) ) {
			mem_failure = 2;
		} else if ( !(iep->urb = usb_alloc_urb(0)) ) {
			mem_failure = 3;
		}
#endif
	}
	if (mem_failure) {
		free_in_endpt(iep, mem_failure);
		printk(DRIVER_NAME "[%d]: ep=0x%x out of memory (code=%d)\n", ir->devnum, addr, mem_failure);
		return NULL;
	}
	list_add_tail(&iep->iep_list_link, &ir->iep_listhead);
	dprintk(DRIVER_NAME "[%d]: adding ep=0x%0x to list\n", ir->devnum, iep->ep->bEndpointAddress);
	return iep;
}

static void free_out_endpt(struct out_endpt *oep, int mem_failure)
{
	struct irctl *ir;
	dprintk(DRIVER_NAME ": free_out_endpt(%p, %d)\n", oep, mem_failure);
	if (!oep) return;

	wake_up_all(&oep->wait);

	ir = oep->ir;
	if (!ir) {
		dprintk(DRIVER_NAME ": free_out_endpt: WARNING! null ir\n");
		return;
	}
	IRLOCK;
	switch (mem_failure) {
	case FREE_ALL:
	case 4:
		if (oep->urb) {
#ifdef KERNEL_2_5
			oep->urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
			usb_unlink_urb(oep->urb);
			usb_free_urb(oep->urb);
			oep->urb = 0;
		} else {
			dprintk(DRIVER_NAME "[%d]: free_out_endpt: null urb!\n", ir->devnum);
		}
	case 3:
#ifdef KERNEL_2_5
		usb_buffer_free(oep->ir->usbdev, USB_OUTLEN, oep->buf, oep->dma);
#else
		kfree(oep->buf);
#endif
		oep->buf = 0;
	case 2:
		kfree(oep);
	}
	IRUNLOCK;
}

static struct out_endpt *new_out_endpt(struct irctl *ir, struct usb_endpoint_descriptor *ep)
{
	struct usb_device *dev = ir->usbdev;
	struct out_endpt *oep;
	int mem_failure;

	dprintk(DRIVER_NAME "[%d]: acceptable outbound endpoint (0x%x) found\n", ir->devnum, ep->bEndpointAddress);

	mem_failure = 0;
	if ( !(oep = kmalloc(sizeof(*oep), GFP_KERNEL)) ) {
		mem_failure = 1;
	} else {
		memset(oep, 0, sizeof(*oep));
		oep->ir = ir;
		oep->ep = ep;
		init_waitqueue_head(&oep->wait);

#ifdef KERNEL_2_5
		if ( !(oep->buf = usb_buffer_alloc(dev, USB_OUTLEN, SLAB_ATOMIC, &oep->dma)) ) {
			mem_failure = 2;
		} else if ( !(oep->urb = usb_alloc_urb(0, GFP_KERNEL)) ) {
			mem_failure = 3;
		}
#else
		if ( !(oep->buf = kmalloc(USB_OUTLEN, GFP_KERNEL)) ) {
			mem_failure = 2;
		} else if ( !(oep->urb = usb_alloc_urb(0)) ) {
			mem_failure = 3;
		}
#endif
	}
	if (mem_failure) {
		free_out_endpt(oep, mem_failure);
		printk(DRIVER_NAME "[%d]: ep=0x%x out of memory (code=%d)\n", ir->devnum, ep->bEndpointAddress, mem_failure);
		return NULL;
	}
	return oep;
}

static void free_irctl(struct irctl *ir, int mem_failure)
{
	struct list_head *pos, *n;
	struct in_endpt *in;
	dprintk(DRIVER_NAME ": free_irctl(%p, %d)\n", ir, mem_failure);

	if (!ir) return;

	list_for_each_safe(pos, n, &ir->iep_listhead) {
		in = get_iep_from_link(pos);
		free_in_endpt(in, FREE_ALL);
	}
	if (ir->out_init) {
		free_out_endpt(ir->out_init, FREE_ALL);
		ir->out_init = NULL;
	}

	IRLOCK;
	switch (mem_failure) {
	case FREE_ALL:
	case 6:
	    	if (!--ir->dev_refcount) {
			list_del(&ir->remote_list_link);
			dprintk(DRIVER_NAME "[%d]: free_irctl: removing remote from list\n",
				ir->devnum);
		} else {
			dprintk(DRIVER_NAME "[%d]: free_irctl: refcount at %d,"
				"aborting free_irctl\n", ir->devnum, ir->dev_refcount);
			IRUNLOCK;
			return;
		}
	case 5:
	case 4:
	case 3:
		if (ir->p) {
			switch (mem_failure) {
			case 5: lirc_buffer_free(ir->p->rbuf);
			case 4: kfree(ir->p->rbuf);
			case 3: kfree(ir->p);
			}
		} else {
			printk(DRIVER_NAME "[%d]: ir->p is a null pointer!\n", ir->devnum);
		}
	case 2:
		IRUNLOCK;
		kfree(ir);
		return;
	}
	IRUNLOCK;
}

static struct irctl *new_irctl(struct usb_device *dev)
{
	struct irctl *ir;
	struct lirc_plugin *plugin;
	struct lirc_buffer *rbuf;
	int type, devnum;
	int mem_failure;

	devnum = dev->devnum;

	/* determine remote type */
	switch (dev->descriptor.idVendor) {
	case VENDOR_ATI1:
		type = ATI1_COMPATIBLE;
		break;
	case VENDOR_ATI2:
		type = ATI2_COMPATIBLE;
		break;
	default:
		dprintk(DRIVER_NAME "[%d]: unknown type\n", devnum);
		return NULL;
	}
	dprintk(DRIVER_NAME "[%d]: remote type = %d\n", devnum, type);

	/* allocate kernel memory */
	mem_failure = 0;
	if ( !(ir = kmalloc(sizeof(*ir), GFP_KERNEL)) ) {
		mem_failure = 1;
	} else {
		memset(ir, 0, sizeof(*ir));
		/* add this infrared remote struct to remote_list, keeping track of
		 * the number of drivers registered. */
		dprintk(DRIVER_NAME "[%d]: adding remote to list\n", devnum);
		list_add_tail(&ir->remote_list_link, &remote_list);
		ir->dev_refcount=1;

		if (!(plugin = kmalloc(sizeof(*plugin), GFP_KERNEL))) {
			mem_failure = 2;
		} else if (!(rbuf = kmalloc(sizeof(*rbuf), GFP_KERNEL))) {
			mem_failure = 3;
		} else if (lirc_buffer_init(rbuf, CODE_LENGTH, 1)) {
			mem_failure = 4;
		} else {
			memset(plugin, 0, sizeof(*plugin));
			strcpy(plugin->name, DRIVER_NAME " ");
			plugin->minor = -1;
			plugin->code_length = CODE_LENGTH*8;
			plugin->features = LIRC_CAN_REC_LIRCCODE;
			plugin->data = ir;
			plugin->rbuf = rbuf;
			plugin->set_use_inc = &set_use_inc;
			plugin->set_use_dec = &set_use_dec;
			plugin->owner = THIS_MODULE;
			ir->usbdev = dev;
			ir->p = plugin;
			ir->remote_type = type;
			ir->devnum = devnum;
			ir->mode = RW2_NULL_MODE;

			init_MUTEX(&ir->lock);
			INIT_LIST_HEAD(&ir->iep_listhead);
		}
	}
	if (mem_failure) {
		free_irctl(ir, mem_failure);
		printk(DRIVER_NAME "[%d]: out of memory (code=%d)\n", devnum, mem_failure);
		return NULL;
	}
	return ir;
}


/*
 * Scan the global list of remotes to see if the device listed is one of them.
 * If it is, the corresponding irctl is returned, with its dev_refcount
 * incremented.  Otherwise, returns null.
 */
static struct irctl *get_prior_reg_ir(struct usb_device *dev) {
	struct list_head *pos;
	struct irctl *ir = NULL;

	dprintk(DRIVER_NAME "[%d]: scanning remote_list...\n", dev->devnum);
	list_for_each(pos, &remote_list) {
		ir = get_irctl_from_link(pos);
		if (ir->usbdev != dev) {
		    dprintk(DRIVER_NAME "[%d]: device %d isn't it...", dev->devnum, ir->devnum);
		    ir = NULL;
		} else {
		    dprintk(DRIVER_NAME "[%d]: prior instance found.\n", dev->devnum);
		    ir->dev_refcount++;
		    break;
		}
	}
	return ir;
}

/* If the USB interface has an out endpoint for control (eg, the first Remote
 * Wonder) send the appropriate initialization packets. */
static void send_outbound_init(struct irctl *ir) {
	if (ir->out_init) {
		struct out_endpt *oep = ir->out_init;
		dprintk(DRIVER_NAME "[%d]: usb_remote_probe: initializing outbound ep\n", ir->devnum);
		usb_fill_int_urb(oep->urb, ir->usbdev,
			usb_sndintpipe(ir->usbdev, oep->ep->bEndpointAddress), oep->buf,
			USB_OUTLEN, usb_remote_send, oep, oep->ep->bInterval);

		send_packet(oep, 0x8004, init1);
		send_packet(oep, 0x8007, init2);
	}
}

/* Log driver and usb info */
static void log_usb_dev_info(struct usb_device *dev) {
	char buf[63], name[128]="";
	if (dev->descriptor.iManufacturer
		&& usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0)
		strncpy(name, buf, 128);
	if (dev->descriptor.iProduct
		&& usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0)
		snprintf(name, 128, "%s %s", name, buf);
	printk(DRIVER_NAME "[%d]: %s on usb%d:%d\n", dev->devnum, name,
	       dev->bus->busnum, dev->devnum);
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
	struct usb_endpoint_descriptor *ep;
	struct in_endpt *iep;
	struct irctl *ir;
	int i, type;

	dprintk(DRIVER_NAME "[%d]: usb_remote_probe: dev:%p, intf:%p, id:%p)\n",
		dev->devnum, dev, intf, id);

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,4)
	idesc = intf->cur_altsetting;
#else
	idesc = &intf->altsetting[intf->act_altsetting];
#endif

	/* Check if a usb remote has already been registered for this device */
	ir = get_prior_reg_ir(dev);

	if ( !ir && !(ir = new_irctl(dev)) ) {
#ifdef KERNEL_2_5
		return -ENOMEM;
#else
		return NULL;
#endif
	}
	type = ir->remote_type;

	// step through the endpoints to find first in and first out endpoint
	// of type interrupt transfer
#ifdef KERNEL_2_5
	for (i = 0; i < idesc->desc.bNumEndpoints; ++i) {
		ep = &idesc->endpoint[i].desc;
#else
	for (i = 0; i < idesc->bNumEndpoints; ++i) {
		ep = &idesc->endpoint[i];
#endif
		dprintk(DRIVER_NAME "[%d]: processing endpoint %d\n", dev->devnum, i);
		if ( ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
			&& ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)) {

			if ((iep = new_in_endpt(ir,ep))) {
				usb_fill_int_urb(iep->urb, dev,
					usb_rcvintpipe(dev,iep->ep->bEndpointAddress), iep->buf,
					iep->len, usb_remote_recv, iep, iep->ep->bInterval);
			}
		}

		if ( ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
			&& ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
			&& (ir->out_init == NULL)) {

			ir->out_init = new_out_endpt(ir,ep);
		}
	}
	if (list_empty(&ir->iep_listhead)) {
		printk(DRIVER_NAME "[%d]: inbound endpoint not found\n", ir->devnum);
		free_irctl(ir, FREE_ALL);
#ifdef KERNEL_2_5
		return -ENODEV;
#else
		return NULL;
#endif
	}
	if (ir->dev_refcount == 1) {
		if ((ir->p->minor = lirc_register_plugin(ir->p)) < 0) {
			free_irctl(ir, FREE_ALL);
#ifdef KERNEL_2_5
			return -ENODEV;
#else
			return NULL;
#endif
		}

		/* Note new driver registration in kernel logs */
		log_usb_dev_info(dev);

		/* outbound data (initialization) */
		send_outbound_init(ir);
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
//	struct usb_device *dev = interface_to_usbdev(intf);
	struct irctl *ir = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);
#else
static void usb_remote_disconnect(struct usb_device *dev, void *ptr)
{
	struct irctl *ir = ptr;
#endif

	dprintk(DRIVER_NAME ": disconnecting remote %d:\n", (ir? ir->devnum: -1));
	if (!ir || !ir->p)
		return;

	if (ir->usbdev) {
		/* Only unregister once */
		ir->usbdev = NULL;
		unregister_from_lirc(ir);
	}

	/* This also removes the current remote from remote_list */
	free_irctl(ir, FREE_ALL);
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

	INIT_LIST_HEAD(&remote_list);

	printk("\n" DRIVER_NAME ": " DRIVER_DESC " v" DRIVER_VERSION "\n");
	printk(DRIVER_NAME ": " DRIVER_AUTHOR "\n");
	dprintk(DRIVER_NAME ": debug mode enabled: $Id: lirc_atiusb.c,v 1.49 2005/03/12 11:32:14 lirc Exp $\n");

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
MODULE_PARM_DESC(debug, "Debug enabled or not (default: 0)");

module_param(mask, int, 0644);
MODULE_PARM_DESC(mask, "Set channel acceptance bit mask (default: 0xFFFF)");

module_param(unique, bool, 0644);
MODULE_PARM_DESC(unique, "Enable channel-specific codes (default: 0)");

module_param(repeat, int, 0644);
MODULE_PARM_DESC(repeat, "Repeat timeout (1/100 sec) (default: 10)");

module_param(mdeadzone, int, 0644);
MODULE_PARM_DESC(mdeadzone, "rw2 mouse sensitivity threshold (default: 0)");

/*
 * Enabling this will cause the built-in Remote Wonder II repeate coding to
 * not be squashed.  The second byte of the keys output will then be:
 *
 * 	1 initial press (button down)
 * 	2 holding (button remains pressed)
 * 	0 release (button up)
 *
 * By default, the driver emits 2 for both 1 and 2, and emits nothing for 0.
 * This is good for people having trouble getting their rw2 to send a good
 * consistent signal to the receiver.
 *
 * However, if you have no troubles with the driver outputting up-down pairs
 * at random points while you're still holding a button, then you can enable
 * this parameter to get finer grain repeat control out of your remote:
 *
 * 	1 Emit a single (per-channel) virtual code for all up/down events
 * 	2 Emit the actual rw2 output
 *
 * 1 is easier to write lircd configs for; 2 allows full control.
 */
module_param(emit_updown, int, 0644);
MODULE_PARM_DESC(emit_updown, "emit press/release codes (rw2): 0:don't (default), 1:emit 2 codes only, 2:code for each button");

module_param(emit_modekeys, int, 0644);
MODULE_PARM_DESC(emit_modekeys, "emit keycodes for aux1-aux4, pc, and mouse (rw2): 0:don't (default), 1:emit translated codes: one for mode switch, one for same mode, 2:raw codes");

module_param(mgradient, int, 0644);
MODULE_PARM_DESC(mgradient, "rw2 mouse: 1000*gradient from E to NE (default: 500 => .5 => ~27 degrees)");

EXPORT_NO_SYMBOLS;
