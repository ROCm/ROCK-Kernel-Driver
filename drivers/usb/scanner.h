/*
 * Driver for USB Scanners (linux-2.4.12)
 *
 * Copyright (C) 1999, 2000, 2001 David E. Nelson
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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>

// #define DEBUG

/* Enable this to support the older ioctl interfaces scanners that
 * a PV8630 Scanner-On-Chip.  The prefered method is the
 * SCANNER_IOCTL_CTRLMSG ioctl.
 */
// #define PV8630 

#define DRIVER_VERSION "0.4.6"
#define DRIVER_DESC "USB Scanner Driver"

#include <linux/usb.h>

static __s32 vendor=-1, product=-1, read_timeout=0;

MODULE_AUTHOR("David E. Nelson, dnelson@jump.net, http://www.jump.net/~dnelson");
MODULE_DESCRIPTION(DRIVER_DESC" "DRIVER_VERSION);
MODULE_LICENSE("GPL");

MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");

MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");

MODULE_PARM(read_timeout, "i");
MODULE_PARM_DESC(read_timeout, "User specified read timeout in seconds");


/* WARNING: These DATA_DUMP's can produce a lot of data. Caveat Emptor. */
// #define RD_DATA_DUMP /* Enable to dump data - limited to 24 bytes */
// #define WR_DATA_DUMP /* DEBUG does not have to be defined. */

static struct usb_device_id scanner_device_ids [] = {
	/* Acer */
	{ USB_DEVICE(0x04a5, 0x2060) },	/* Prisa Acerscan 620U & 640U (!)*/
	{ USB_DEVICE(0x04a5, 0x2040) },	/* Prisa AcerScan 620U (!) */
	{ USB_DEVICE(0x04a5, 0x20c0) },  /* Prisa AcerScan 1240UT */
	{ USB_DEVICE(0x04a5, 0x2022) },	/* Vuego Scan Brisa 340U */
	{ USB_DEVICE(0x04a5, 0x1a20) },	/* Unknown - Oliver Schwartz */
	{ USB_DEVICE(0x04a5, 0x1a2a) },	/* Unknown - Oliver Schwartz */
	{ USB_DEVICE(0x04a5, 0x207e) },	/* Prisa 640BU */
	{ USB_DEVICE(0x04a5, 0x20be) },	/* Unknown - Oliver Schwartz */
	{ USB_DEVICE(0x04a5, 0x20c0) },	/* Unknown - Oliver Schwartz */
	{ USB_DEVICE(0x04a5, 0x20de) },	/* S2W 3300U */
	{ USB_DEVICE(0x04a5, 0x20b0) },	/* Unknown - Oliver Schwartz */
	{ USB_DEVICE(0x04a5, 0x20fe) },	/* Unknown - Oliver Schwartz */
	/* Agfa */
	{ USB_DEVICE(0x06bd, 0x0001) },	/* SnapScan 1212U */
	{ USB_DEVICE(0x06bd, 0x0002) },	/* SnapScan 1236U */
	{ USB_DEVICE(0x06bd, 0x2061) },	/* Another SnapScan 1212U (?)*/
	{ USB_DEVICE(0x06bd, 0x0100) },	/* SnapScan Touch */
	{ USB_DEVICE(0x06bd, 0x2091) }, /* SnapScan e20 */
	{ USB_DEVICE(0x06bd, 0x2095) }, /* SnapScan e25 */
	{ USB_DEVICE(0x06bd, 0x2097) }, /* SnapScan e26 */
	{ USB_DEVICE(0x06bd, 0x208d) }, /* Snapscan e40 */
	/* Canon */
	{ USB_DEVICE(0x04a9, 0x2202) }, /* FB620U */
	{ USB_DEVICE(0x04a9, 0x220b) }, /* D646U */
	{ USB_DEVICE(0x04a9, 0x2207) }, /* 1220U */
	/* Colorado -- See Primax/Colorado below */
	/* Epson -- See Seiko/Epson below */
	/* Genius */
	{ USB_DEVICE(0x0458, 0x2001) },	/* ColorPage-Vivid Pro */
	{ USB_DEVICE(0x0458, 0x2007) },	/* ColorPage HR6 V2 */
	{ USB_DEVICE(0x0458, 0x2008) },	/* Unknown */
	{ USB_DEVICE(0x0458, 0x2009) },	/* Unknown */
	{ USB_DEVICE(0x0458, 0x2013) },	/* Unknown */
	{ USB_DEVICE(0x0458, 0x2015) },	/* Unknown  */
	{ USB_DEVICE(0x0458, 0x2016) },	/* Unknown  */
	/* Hewlett Packard */
	{ USB_DEVICE(0x03f0, 0x0205) },	/* 3300C */
	{ USB_DEVICE(0x03f0, 0x0405) }, /* 3400C */
	{ USB_DEVICE(0x03f0, 0x0101) },	/* 4100C */
	{ USB_DEVICE(0x03f0, 0x0105) },	/* 4200C */
	{ USB_DEVICE(0x03f0, 0x0305) }, /* 4300C */
	{ USB_DEVICE(0x03f0, 0x0102) },	/* PhotoSmart S20 */
	{ USB_DEVICE(0x03f0, 0x0401) },	/* 5200C */
	//	{ USB_DEVICE(0x03f0, 0x0701) },	/* 5300C - NOT SUPPORTED - see http://www.neatech.nl/oss/HP5300C/ */
	{ USB_DEVICE(0x03f0, 0x0201) },	/* 6200C */
	{ USB_DEVICE(0x03f0, 0x0601) },	/* 6300C */
	{ USB_DEVICE(0x03f0, 0x605) },	/* 2200C */
	/* iVina */
	{ USB_DEVICE(0x0638, 0x0268) }, /* 1200U */
	/* Lifetec */
	{ USB_DEVICE(0x05d8, 0x4002) }, /* Lifetec LT9385 */
	/* Memorex */
	{ USB_DEVICE(0x0461, 0x0346) }, /* 6136u - repackaged Primax ? */
	/* Microtek -- No longer supported - Enable SCSI and USB Microtek in kernel config */
	//	{ USB_DEVICE(0x05da, 0x0099) },	/* ScanMaker X6 - X6U */
	//	{ USB_DEVICE(0x05da, 0x0094) },	/* Phantom 336CX - C3 */
	//	{ USB_DEVICE(0x05da, 0x00a0) },	/* Phantom 336CX - C3 #2 */
	//	{ USB_DEVICE(0x05da, 0x009a) },	/* Phantom C6 */
	//	{ USB_DEVICE(0x05da, 0x00a3) },	/* ScanMaker V6USL */
	//	{ USB_DEVICE(0x05da, 0x80a3) },	/* ScanMaker V6USL #2 */
	//	{ USB_DEVICE(0x05da, 0x80ac) },	/* ScanMaker V6UL - SpicyU */
	/* Minolta */
	//	{ USB_DEVICE(0x0638,0x026a) }, /* Minolta Dimage Scan Dual II */
	/* Mustek */
	{ USB_DEVICE(0x055f, 0x0001) },	/* 1200 CU */
	{ USB_DEVICE(0x0400, 0x1000) },	/* BearPaw 1200 */
	{ USB_DEVICE(0x055f, 0x0002) },	/* 600 CU */
	{ USB_DEVICE(0x055f, 0x0873) }, /* 600 USB */
	{ USB_DEVICE(0x055f, 0x0003) },	/* 1200 USB */
	{ USB_DEVICE(0x055f, 0x0006) },	/* 1200 UB */
	{ USB_DEVICE(0x0400, 0x1001) }, /* BearPaw 2400 */
	{ USB_DEVICE(0x055f, 0x0008) }, /* 1200 CU Plus */
	{ USB_DEVICE(0x0ff5, 0x0010) }, /* BearPaw 1200F */
	/* Plustek */
	{ USB_DEVICE(0x07b3, 0x0017) }, /* OpticPro UT12 */
	{ USB_DEVICE(0x07b3, 0x0011) }, /* OpticPro UT24 */
	{ USB_DEVICE(0x07b3, 0x0005) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x0007) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x000F) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x0010) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x0012) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x0013) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x0014) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x0015) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x0016) }, /* Unknown */
	{ USB_DEVICE(0x07b3, 0x0012) }, /* Unknown */
	/* Primax/Colorado */
	{ USB_DEVICE(0x0461, 0x0300) },	/* G2-300 #1 */
	{ USB_DEVICE(0x0461, 0x0380) },	/* G2-600 #1 */
	{ USB_DEVICE(0x0461, 0x0301) },	/* G2E-300 #1 */
	{ USB_DEVICE(0x0461, 0x0381) },	/* ReadyScan 636i */
	{ USB_DEVICE(0x0461, 0x0302) },	/* G2-300 #2 */
	{ USB_DEVICE(0x0461, 0x0382) },	/* G2-600 #2 */
	{ USB_DEVICE(0x0461, 0x0303) },	/* G2E-300 #2 */
	{ USB_DEVICE(0x0461, 0x0383) },	/* G2E-600 */
	{ USB_DEVICE(0x0461, 0x0340) },	/* Colorado USB 9600 */
	// { USB_DEVICE(0x0461, 0x0360) },	/* Colorado USB 19200 - undetected endpoint */
	{ USB_DEVICE(0x0461, 0x0341) },	/* Colorado 600u */
	{ USB_DEVICE(0x0461, 0x0361) },	/* Colorado 1200u */
	/* Relisis */
	// { USB_DEVICE(0x0475, 0x0103) },	/* Episode - undetected endpoint */
	/* Seiko/Epson Corp. */
	{ USB_DEVICE(0x04b8, 0x0101) },	/* Perfection 636U and 636Photo */
	{ USB_DEVICE(0x04b8, 0x0103) },	/* Perfection 610 */
	{ USB_DEVICE(0x04b8, 0x0104) },	/* Perfection 1200U and 1200Photo*/
	{ USB_DEVICE(0x04b8, 0x0106) },	/* Stylus Scan 2500 */
	{ USB_DEVICE(0x04b8, 0x0107) },	/* Expression 1600 */
	{ USB_DEVICE(0x04b8, 0x010a) }, /* Perfection 1640SU and 1640SU Photo */
	{ USB_DEVICE(0x04b8, 0x010b) }, /* Perfection 1240U */
	{ USB_DEVICE(0x04b8, 0x010c) }, /* Perfection 640U */
	{ USB_DEVICE(0x04b8, 0x010e) }, /* Expression 1680 */
	{ USB_DEVICE(0x04b8, 0x0110) }, /* Perfection 1650 */
	{ USB_DEVICE(0x04b8, 0x0112) }, /* Perfection 2450 - GT-9700 for the Japanese mkt */
	/* Umax */
	{ USB_DEVICE(0x1606, 0x0010) },	/* Astra 1220U */
	{ USB_DEVICE(0x1606, 0x0030) },	/* Astra 2000U */
	{ USB_DEVICE(0x1606, 0x0130) }, /* Astra 2100U */
	{ USB_DEVICE(0x1606, 0x0230) },	/* Astra 2200U */
	/* Visioneer */
	{ USB_DEVICE(0x04a7, 0x0221) },	/* OneTouch 5300 USB */
	{ USB_DEVICE(0x04a7, 0x0211) },	/* OneTouch 7600 USB */
	{ USB_DEVICE(0x04a7, 0x0231) },	/* 6100 USB */
	{ USB_DEVICE(0x04a7, 0x0311) },	/* 6200 EPP/USB */
	{ USB_DEVICE(0x04a7, 0x0321) },	/* OneTouch 8100 EPP/USB */
	{ USB_DEVICE(0x04a7, 0x0331) }, /* OneTouch 8600 EPP/USB */
	{ }				/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, scanner_device_ids);

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
#define RD_NAK_TIMEOUT (10*HZ)	/* Default number of X seconds to wait */
#define RD_EXPIRE 12		/* Number of attempts to wait X seconds */


/* FIXME: These are NOT registered ioctls()'s */
#ifdef PV8630
#define PV8630_IOCTL_INREQUEST 69
#define PV8630_IOCTL_OUTREQUEST 70
#endif /* PV8630 */


/* read vendor and product IDs from the scanner */
#define SCANNER_IOCTL_VENDOR _IOR('U', 0x20, int)
#define SCANNER_IOCTL_PRODUCT _IOR('U', 0x21, int)
/* send/recv a control message to the scanner */
#define SCANNER_IOCTL_CTRLMSG _IOWR('U', 0x22, devrequest )


#define SCN_MAX_MNR 16		/* We're allocated 16 minors */
#define SCN_BASE_MNR 48		/* USB Scanners start at minor 48 */

static DECLARE_MUTEX (scn_mutex); /* Initializes to unlocked */

struct scn_usb_data {
	struct usb_device *scn_dev;
	devfs_handle_t devfs;	/* devfs device */
	struct urb scn_irq;
	unsigned int ifnum;	/* Interface number of the USB device */
	kdev_t scn_minor;	/* Scanner minor - used in disconnect() */
	unsigned char button;	/* Front panel buffer */
	char isopen;		/* Not zero if the device is open */
	char present;		/* Not zero if device is present */
	char *obuf, *ibuf;	/* transfer buffers */
	char bulk_in_ep, bulk_out_ep, intr_ep; /* Endpoint assignments */
	wait_queue_head_t rd_wait_q; /* read timeouts */
	struct semaphore sem; /* lock to prevent concurrent reads or writes */
	unsigned int rd_nak_timeout; /* Seconds to wait before read() timeout. */
};

extern devfs_handle_t usb_devfs_handle;

static struct scn_usb_data *p_scn_table[SCN_MAX_MNR] = { NULL, /* ... */};

static struct usb_driver scanner_driver;
