/*
 * Driver for USB Scanners (linux-2.4.0test1-ac7)
 *
 * Copyright (C) 1999, 2000 David E. Nelson
 *
 * David E. Nelson (dnelson@jump.net)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */ 

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>

// #define DEBUG

#include <linux/usb.h>

static __s32 vendor=-1, product=-1;

MODULE_AUTHOR("David E. Nelson, dnelson@jump.net, http://www.jump.net/~dnelson");
MODULE_DESCRIPTION("USB Scanner Driver");

MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");


/* Enable to activate the ioctl interface.  This is mainly meant for */
/* development purposes until an ioctl number is officially registered */
// #define SCN_IOCTL

/* WARNING: These DATA_DUMP's can produce a lot of data. Caveat Emptor. */
// #define RD_DATA_DUMP /* Enable to dump data - limited to 24 bytes */
// #define WR_DATA_DUMP /* DEBUG does not have to be defined. */

#define IS_EP_BULK(ep)  ((ep).bmAttributes == USB_ENDPOINT_XFER_BULK ? 1 : 0)
#define IS_EP_BULK_IN(ep) (IS_EP_BULK(ep) && ((ep).bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
#define IS_EP_BULK_OUT(ep) (IS_EP_BULK(ep) && ((ep).bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
#define IS_EP_INTR(ep) ((ep).bmAttributes == USB_ENDPOINT_XFER_INT ? 1 : 0)

#define USB_SCN_MINOR(X) MINOR((X)->i_rdev) - SCN_BASE_MNR

#ifdef DEBUG
#define SCN_DEBUG(X) X
#else
#define SCN_DEBUG(X)
#endif

#define IBUF_SIZE 32768
#define OBUF_SIZE 4096

/* read_scanner timeouts -- RD_NAK_TIMEOUT * RD_EXPIRE = Number of seconds */
#define RD_NAK_TIMEOUT (10*HZ)	/* Number of X seconds to wait */
#define RD_EXPIRE 12		/* Number of attempts to wait X seconds */


/* FIXME: These are NOT registered ioctls()'s */
#define PV8630_IOCTL_INREQUEST 69
#define PV8630_IOCTL_OUTREQUEST 70

#define SCN_MAX_MNR 16		/* We're allocated 16 minors */
#define SCN_BASE_MNR 48		/* USB Scanners start at minor 48 */

struct scn_usb_data {
	struct usb_device *scn_dev;
	struct urb scn_irq;
	unsigned int ifnum;	/* Interface number of the USB device */
	kdev_t scn_minor;	/* Scanner minor - used in disconnect() */
	unsigned char button;	/* Front panel buffer */
        char isopen;		/* Not zero if the device is open */
	char present;		/* Not zero if device is present */
	char *obuf, *ibuf;	/* transfer buffers */
	char bulk_in_ep, bulk_out_ep, intr_ep; /* Endpoint assignments */
	wait_queue_head_t rd_wait_q; /* read timeouts */
	struct semaphore gen_lock; /* lock to prevent concurrent reads or writes */
};

static struct scn_usb_data *p_scn_table[SCN_MAX_MNR] = { NULL, /* ... */};

static struct usb_driver scanner_driver;
