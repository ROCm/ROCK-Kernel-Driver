/*
 * g_serial.c -- USB gadget serial driver
 *
 * Copyright 2003 (C) Al Borchers (alborchers@steinerpoint.com)
 *
 * This code is based in part on the Gadget Zero driver, which
 * is Copyright (C) 2003 by David Brownell, all rights reserved.
 *
 * This code also borrows from usbserial.c, which is
 * Copyright (C) 1999 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2000 Peter Berger (pberger@brimson.com)
 * Copyright (C) 2000 Al Borchers (alborchers@steinerpoint.com)
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * either version 2 of that License or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/uts.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/uaccess.h>

#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>


/* Wait Cond */

#define __wait_cond_interruptible(wq, condition, lock, flags, ret)	\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			spin_unlock_irqrestore(lock, flags);		\
			schedule();					\
			spin_lock_irqsave(lock, flags);			\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)
	
#define wait_cond_interruptible(wq, condition, lock, flags)		\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__wait_cond_interruptible(wq, condition, lock, flags,	\
						__ret);			\
	__ret;								\
})

#define __wait_cond_interruptible_timeout(wq, condition, lock, flags, 	\
						timeout, ret)		\
do {									\
	signed long __timeout = timeout;				\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (__timeout == 0)					\
			break;						\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			spin_unlock_irqrestore(lock, flags);		\
			__timeout = schedule_timeout(__timeout);	\
			spin_lock_irqsave(lock, flags);			\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)
	
#define wait_cond_interruptible_timeout(wq, condition, lock, flags,	\
						timeout)		\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__wait_cond_interruptible_timeout(wq, condition, lock,	\
						flags, timeout, __ret);	\
	__ret;								\
})


/* Defines */

#define GS_VERSION_STR			"v0.1"
#define GS_VERSION_NUM			0x0001

#define GS_LONG_NAME			"Gadget Serial"
#define GS_SHORT_NAME			"g_serial"

#define GS_MAJOR			127
#define GS_MINOR_START			0

#define GS_NUM_PORTS			16

#define GS_NUM_CONFIGS			1
#define GS_NO_CONFIG_ID			0
#define GS_BULK_CONFIG_ID		2

#define GS_NUM_INTERFACES		1
#define GS_INTERFACE_ID			0
#define GS_ALT_INTERFACE_ID		0

#define GS_NUM_ENDPOINTS		2

#define GS_MAX_DESC_LEN			256

#define GS_DEFAULT_READ_Q_SIZE		32
#define GS_DEFAULT_WRITE_Q_SIZE		32

#define GS_DEFAULT_WRITE_BUF_SIZE	8192
#define GS_TMP_BUF_SIZE			8192

#define GS_CLOSE_TIMEOUT		15

/* debug settings */
#if G_SERIAL_DEBUG
static int debug = G_SERIAL_DEBUG;

#define gs_debug(format, arg...) \
	do { if (debug) printk(KERN_DEBUG format, ## arg); } while(0)
#define gs_debug_level(level, format, arg...) \
	do { if (debug>=level) printk(KERN_DEBUG format, ## arg); } while(0)

#else

#define gs_debug(format, arg...) \
	do { } while(0)
#define gs_debug_level(level, format, arg...) \
	do { } while(0)

#endif /* G_SERIAL_DEBUG */


/* USB Controllers */

/*
 * NetChip 2280, PCI based.
 *
 * This has half a dozen configurable endpoints, four with dedicated
 * DMA channels to manage their FIFOs.  It supports high speed.
 * Those endpoints can be arranged in any desired configuration.
 */
#ifdef	CONFIG_USB_GADGET_NET2280
#define CHIP				"net2280"
#define EP0_MAXPACKET			64
static const char EP_OUT_NAME[] =	"ep-a";
#define EP_OUT_NUM			2
static const char EP_IN_NAME[] =	"ep-b";
#define EP_IN_NUM			2
#define HIGHSPEED
#define SELFPOWER			USB_CONFIG_ATT_SELFPOWER

extern int net2280_set_fifo_mode(struct usb_gadget *gadget, int mode);

static inline void hw_optimize(struct usb_gadget *gadget)
{
	/* we can have bigger ep-a/ep-b fifos (2KB each, 4 packets
	 * for highspeed bulk) because we're not using ep-c/ep-d.
	 */
	net2280_set_fifo_mode (gadget, 1);
}
#endif


/*
 * Dummy_hcd, software-based loopback controller.
 *
 * This imitates the abilities of the NetChip 2280, so we will use
 * the same configuration.
 */
#ifdef	CONFIG_USB_GADGET_DUMMY_HCD
#define CHIP				"dummy"
#define EP0_MAXPACKET			64
static const char EP_OUT_NAME[] =	"ep-a";
#define EP_OUT_NUM			2
static const char EP_IN_NAME[] =	"ep-b";
#define EP_IN_NUM			2
#define HIGHSPEED
#define SELFPOWER			USB_CONFIG_ATT_SELFPOWER

/* no hw optimizations to apply */
#define hw_optimize(g)			do {} while (0)
#endif


/*
 * PXA-2xx UDC:  widely used in second gen Linux-capable PDAs.
 *
 * This has fifteen fixed-function full speed endpoints, and it
 * can support all USB transfer types.
 *
 * These supports three or four configurations, with fixed numbers.
 * The hardware interprets SET_INTERFACE, net effect is that you
 * can't use altsettings or reset the interfaces independently.
 * So stick to a single interface.
 */
#ifdef	CONFIG_USB_GADGET_PXA2XX
#define CHIP				"pxa2xx"
#define EP0_MAXPACKET			16
static const char EP_OUT_NAME[] =	"ep2out-bulk";
#define EP_OUT_NUM			2
static const char EP_IN_NAME[] =	"ep1in-bulk";
#define EP_IN_NUM			1
#define SELFPOWER 			USB_CONFIG_ATT_SELFPOWER

/* no hw optimizations to apply */
#define hw_optimize(g)			do {} while (0)
#endif

#ifdef	CONFIG_USB_GADGET_OMAP
#define CHIP			"omap"
#define EP0_MAXPACKET			64
static const char EP_OUT_NAME [] = "ep2out-bulk";
#define EP_OUT_NUM	2
static const char EP_IN_NAME [] = "ep1in-bulk";
#define EP_IN_NUM	1
#define SELFPOWER 			USB_CONFIG_ATT_SELFPOWER
/* supports remote wakeup, but this driver doesn't */

/* no hw optimizations to apply */
#define hw_optimize(g) do {} while (0)
#endif


/*
 * SA-1100 UDC:  widely used in first gen Linux-capable PDAs.
 *
 * This has only two fixed function endpoints, which can only
 * be used for bulk (or interrupt) transfers.  (Plus control.)
 *
 * Since it can't flush its TX fifos without disabling the UDC,
 * the current configuration or altsettings can't change except
 * in special situations.  So this is a case of "choose it right
 * during enumeration" ...
 */
#ifdef	CONFIG_USB_GADGET_SA1100
#define CHIP				"sa1100"
#define EP0_MAXPACKET			8
static const char EP_OUT_NAME[] =	"ep1out-bulk";
#define EP_OUT_NUM			1
static const char EP_IN_NAME [] =	"ep2in-bulk";
#define EP_IN_NUM			2
#define SELFPOWER			USB_CONFIG_ATT_SELFPOWER

/* no hw optimizations to apply */
#define hw_optimize(g)			do {} while (0)
#endif


/*
 * Toshiba TC86C001 ("Goku-S") UDC
 *
 * This has three semi-configurable full speed bulk/interrupt endpoints.
 */
#ifdef	CONFIG_USB_GADGET_GOKU
#define CHIP				"goku"
#define DRIVER_VERSION_NUM		0x0116
#define EP0_MAXPACKET			8
static const char EP_OUT_NAME [] =	"ep1-bulk";
#define EP_OUT_NUM			1
static const char EP_IN_NAME [] =	"ep2-bulk";
#define EP_IN_NUM			2
#define SELFPOWER			USB_CONFIG_ATT_SELFPOWER

/* no hw optimizations to apply */
#define hw_optimize(g)			do {} while (0)
#endif

/*
 * USB Controller Defaults
 */
#ifndef EP0_MAXPACKET
#error Configure some USB peripheral controller for g_serial!
#endif

#ifndef SELFPOWER
/* default: say we rely on bus power */
#define SELFPOWER   			0
/* else value must be USB_CONFIG_ATT_SELFPOWER */
#endif

#ifndef	MAX_USB_POWER
/* any hub supports this steady state bus power consumption */
#define MAX_USB_POWER			100	/* mA */
#endif

#ifndef	WAKEUP
/* default: this driver won't do remote wakeup */
#define WAKEUP				0
/* else value must be USB_CONFIG_ATT_WAKEUP */
#endif

/* Thanks to NetChip Technologies for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define GS_VENDOR_ID	0x0525		/* NetChip */
#define GS_PRODUCT_ID	0xa4a6		/* Linux-USB Serial Gadget */


/* Structures */

struct gs_dev;

/* circular buffer */
struct gs_buf {
	unsigned int		buf_size;
	char			*buf_buf;
	char			*buf_get;
	char			*buf_put;
};

/* list of requests */
struct gs_req_entry {
	struct list_head	re_entry;
	struct usb_request	*re_req;
};

/* the port structure holds info for each port, one for each minor number */
struct gs_port {
	struct gs_dev 		*port_dev;	/* pointer to device struct */
	struct tty_struct	*port_tty;	/* pointer to tty struct */
	spinlock_t		port_lock;
	int 			port_num;
	int			port_open_count;
	int			port_in_use;	/* open/close in progress */
	wait_queue_head_t	port_write_wait;/* waiting to write */
	struct gs_buf		*port_write_buf;
};

/* the device structure holds info for the USB device */
struct gs_dev {
	struct usb_gadget	*dev_gadget;	/* gadget device pointer */
	spinlock_t		dev_lock;	/* lock for set/reset config */
	int			dev_config;	/* configuration number */
	struct usb_ep		*dev_in_ep;	/* address of in endpoint */
	struct usb_ep		*dev_out_ep;	/* address of out endpoint */
	struct usb_request	*dev_ctrl_req;	/* control request */
	struct list_head	dev_req_list;	/* list of write requests */
	int			dev_sched_port;	/* round robin port scheduled */
	struct gs_port		*dev_port[GS_NUM_PORTS]; /* the ports */
};


/* Functions */

/* module */
static int __init gs_module_init(void);
static void __exit gs_module_exit(void);

/* tty driver */
static int gs_open(struct tty_struct *tty, struct file *file);
static void gs_close(struct tty_struct *tty, struct file *file);
static int gs_write(struct tty_struct *tty, int from_user,
	const unsigned char *buf, int count);
static void gs_put_char(struct tty_struct *tty, unsigned char ch);
static void gs_flush_chars(struct tty_struct *tty);
static int gs_write_room(struct tty_struct *tty);
static int gs_chars_in_buffer(struct tty_struct *tty);
static void gs_throttle(struct tty_struct * tty);
static void gs_unthrottle(struct tty_struct * tty);
static void gs_break(struct tty_struct *tty, int break_state);
static int  gs_ioctl(struct tty_struct *tty, struct file *file,
	unsigned int cmd, unsigned long arg);
static void gs_set_termios(struct tty_struct *tty, struct termios *old);

static int gs_send(struct gs_dev *dev);
static int gs_send_packet(struct gs_dev *dev, char *packet,
	unsigned int size);
static int gs_recv_packet(struct gs_dev *dev, char *packet,
	unsigned int size);
static void gs_read_complete(struct usb_ep *ep, struct usb_request *req);
static void gs_write_complete(struct usb_ep *ep, struct usb_request *req);

/* gadget driver */
static int gs_bind(struct usb_gadget *gadget);
static void gs_unbind(struct usb_gadget *gadget);
static int gs_setup(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl);
static void gs_setup_complete(struct usb_ep *ep, struct usb_request *req);
static void gs_disconnect(struct usb_gadget *gadget);
static int gs_set_config(struct gs_dev *dev, unsigned config);
static void gs_reset_config(struct gs_dev *dev);
static int gs_build_config_desc(u8 *buf, enum usb_device_speed speed,
		u8 type, unsigned int index);

static struct usb_request *gs_alloc_req(struct usb_ep *ep, unsigned int len,
	int kmalloc_flags);
static void gs_free_req(struct usb_ep *ep, struct usb_request *req);

static struct gs_req_entry *gs_alloc_req_entry(struct usb_ep *ep, unsigned len,
	int kmalloc_flags);
static void gs_free_req_entry(struct usb_ep *ep, struct gs_req_entry *req);

static int gs_alloc_ports(struct gs_dev *dev, int kmalloc_flags);
static void gs_free_ports(struct gs_dev *dev);

/* circular buffer */
static struct gs_buf *gs_buf_alloc(unsigned int size, int kmalloc_flags);
static void gs_buf_free(struct gs_buf *gb);
static void gs_buf_clear(struct gs_buf *gb);
static unsigned int gs_buf_data_avail(struct gs_buf *gb);
static unsigned int gs_buf_space_avail(struct gs_buf *gb);
static unsigned int gs_buf_put(struct gs_buf *gb, const char *buf,
	unsigned int count);
static unsigned int gs_buf_get(struct gs_buf *gb, char *buf,
	unsigned int count);


/* Globals */

static struct gs_dev *gs_device;

static struct semaphore	gs_open_close_sem[GS_NUM_PORTS];

static unsigned int read_q_size = GS_DEFAULT_READ_Q_SIZE;
static unsigned int write_q_size = GS_DEFAULT_WRITE_Q_SIZE;

static unsigned int write_buf_size = GS_DEFAULT_WRITE_BUF_SIZE;

static unsigned char gs_tmp_buf[GS_TMP_BUF_SIZE];
static struct semaphore	gs_tmp_buf_sem;

/* tty driver struct */
static struct tty_operations gs_tty_ops = {
	.open =			gs_open,
	.close =		gs_close,
	.write =		gs_write,
	.put_char =		gs_put_char,
	.flush_chars =		gs_flush_chars,
	.write_room =		gs_write_room,
	.ioctl =		gs_ioctl,
	.set_termios =		gs_set_termios,
	.throttle =		gs_throttle,
	.unthrottle =		gs_unthrottle,
	.break_ctl =		gs_break,
	.chars_in_buffer =	gs_chars_in_buffer,
};
static struct tty_driver *gs_tty_driver;

/* gadget driver struct */
static struct usb_gadget_driver gs_gadget_driver = {
#ifdef HIGHSPEED
	.speed =		USB_SPEED_HIGH,
#else
	.speed =		USB_SPEED_FULL,
#endif
	.function =		GS_LONG_NAME,
	.bind =			gs_bind,
	.unbind =		gs_unbind,
	.setup =		gs_setup,
	.disconnect =		gs_disconnect,
	.driver = {
		.name =		GS_SHORT_NAME,
		/* .shutdown = ... */
		/* .suspend = ...  */
		/* .resume = ...   */
	},
};


/* USB descriptors */

#define GS_MANUFACTURER_STR_ID	1
#define GS_PRODUCT_STR_ID	2
#define GS_SERIAL_STR_ID	3
#define GS_CONFIG_STR_ID	4

/* static strings, in iso 8859/1 */
static struct usb_string gs_strings[] = {
	{ GS_MANUFACTURER_STR_ID, UTS_SYSNAME " " UTS_RELEASE " with " CHIP },
	{ GS_PRODUCT_STR_ID, GS_LONG_NAME },
	{ GS_SERIAL_STR_ID, "0" },
	{ GS_CONFIG_STR_ID, "Bulk" },
	{  } /* end of list */
};

static struct usb_gadget_strings gs_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		gs_strings,
};

static const struct usb_device_descriptor gs_device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		__constant_cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_VENDOR_SPEC,
	.bMaxPacketSize0 =	EP0_MAXPACKET,
	.idVendor =		__constant_cpu_to_le16(GS_VENDOR_ID),
	.idProduct =		__constant_cpu_to_le16(GS_PRODUCT_ID),
	.bcdDevice =		__constant_cpu_to_le16(GS_VERSION_NUM),
	.iManufacturer =	GS_MANUFACTURER_STR_ID,
	.iProduct =		GS_PRODUCT_STR_ID,
	.iSerialNumber =	GS_SERIAL_STR_ID,
	.bNumConfigurations =	GS_NUM_CONFIGS,
};

static const struct usb_config_descriptor gs_config_desc = {
	.bLength =		USB_DT_CONFIG_SIZE,
	.bDescriptorType =	USB_DT_CONFIG,
	/* .wTotalLength set by gs_build_config_desc */
	.bNumInterfaces =	GS_NUM_INTERFACES,
	.bConfigurationValue =	GS_BULK_CONFIG_ID,
	.iConfiguration =	GS_CONFIG_STR_ID,
	.bmAttributes =		USB_CONFIG_ATT_ONE | SELFPOWER | WAKEUP,
	.bMaxPower =		(MAX_USB_POWER + 1) / 2,
};

static const struct usb_interface_descriptor gs_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	GS_NUM_ENDPOINTS,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	.iInterface =		GS_CONFIG_STR_ID,
};

static const struct usb_endpoint_descriptor gs_fullspeed_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	EP_IN_NUM | USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(64),
};

static const struct usb_endpoint_descriptor gs_fullspeed_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	EP_OUT_NUM | USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(64),
};

static const struct usb_endpoint_descriptor gs_highspeed_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	EP_IN_NUM | USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static const struct usb_endpoint_descriptor gs_highspeed_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	EP_OUT_NUM | USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

#ifdef HIGHSPEED
static const struct usb_qualifier_descriptor gs_qualifier_desc = {
	.bLength =		sizeof(struct usb_qualifier_descriptor),
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,
	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	.bDeviceClass =		USB_CLASS_VENDOR_SPEC,
	/* assumes ep0 uses the same value for both speeds ... */
	.bMaxPacketSize0 =	EP0_MAXPACKET,
	.bNumConfigurations =	GS_NUM_CONFIGS,
};
#endif


/* Module */
MODULE_DESCRIPTION(GS_LONG_NAME);
MODULE_AUTHOR("Al Borchers");
MODULE_LICENSE("GPL");

#if G_SERIAL_DEBUG
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Enable debugging, 0=off, 1=on");
#endif

MODULE_PARM(read_q_size, "i");
MODULE_PARM_DESC(read_q_size, "Read request queue size, default=32");

MODULE_PARM(write_q_size, "i");
MODULE_PARM_DESC(write_q_size, "Write request queue size, default=32");

MODULE_PARM(write_buf_size, "i");
MODULE_PARM_DESC(write_buf_size, "Write buffer size, default=8192");

module_init(gs_module_init);
module_exit(gs_module_exit);

/*
*  gs_module_init
*
*  Register as a USB gadget driver and a tty driver.
*/
static int __init gs_module_init(void)
{
	int i;
	int retval;

	retval = usb_gadget_register_driver(&gs_gadget_driver);
	if (retval) {
		printk(KERN_ERR "gs_module_init: cannot register gadget driver, ret=%d\n", retval);
		return retval;
	}

	gs_tty_driver = alloc_tty_driver(GS_NUM_PORTS);
	if (!gs_tty_driver)
		return -ENOMEM;
	gs_tty_driver->owner = THIS_MODULE;
	gs_tty_driver->driver_name = GS_SHORT_NAME;
	gs_tty_driver->name = "ttygs";
	gs_tty_driver->devfs_name = "usb/ttygs/";
	gs_tty_driver->major = GS_MAJOR;
	gs_tty_driver->minor_start = GS_MINOR_START;
	gs_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	gs_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	gs_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	gs_tty_driver->init_termios = tty_std_termios;
	gs_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_set_operations(gs_tty_driver, &gs_tty_ops);

	for (i=0; i < GS_NUM_PORTS; i++)
		sema_init(&gs_open_close_sem[i], 1);

	sema_init(&gs_tmp_buf_sem, 1);

	retval = tty_register_driver(gs_tty_driver);
	if (retval) {
		usb_gadget_unregister_driver(&gs_gadget_driver);
		put_tty_driver(gs_tty_driver);
		printk(KERN_ERR "gs_module_init: cannot register tty driver, ret=%d\n", retval);
		return retval;
	}

	printk(KERN_INFO "gs_module_init: %s %s loaded\n", GS_LONG_NAME, GS_VERSION_STR);
	return 0;
}

/*
* gs_module_exit
*
* Unregister as a tty driver and a USB gadget driver.
*/
static void __exit gs_module_exit(void)
{
	tty_unregister_driver(gs_tty_driver);
	put_tty_driver(gs_tty_driver);
	usb_gadget_unregister_driver(&gs_gadget_driver);

	printk(KERN_INFO "gs_module_exit: %s %s unloaded\n", GS_LONG_NAME, GS_VERSION_STR);
}

/* TTY Driver */

/*
 * gs_open
 */
static int gs_open(struct tty_struct *tty, struct file *file)
{
	int port_num;
	unsigned long flags;
	struct gs_port *port;
	struct gs_dev *dev;
	struct gs_buf *buf;
	struct semaphore *sem;

	port_num = tty->index;

	gs_debug("gs_open: (%d,%p,%p)\n", port_num, tty, file);

	tty->driver_data = NULL;

	if (port_num < 0 || port_num >= GS_NUM_PORTS) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) invalid port number\n",
			port_num, tty, file);
		return -ENODEV;
	}

	dev = gs_device;

	if (dev == NULL) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) NULL device pointer\n",
			port_num, tty, file);
		return -ENODEV;
	}

	sem = &gs_open_close_sem[port_num];
	if (down_interruptible(sem)) {
		printk(KERN_ERR
		"gs_open: (%d,%p,%p) interrupted waiting for semaphore\n",
			port_num, tty, file);
		return -ERESTARTSYS;
	}

	spin_lock_irqsave(&dev->dev_lock, flags);

	if (dev->dev_config == GS_NO_CONFIG_ID) {
		printk(KERN_ERR
			"gs_open: (%d,%p,%p) device is not connected\n",
			port_num, tty, file);
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		up(sem);
		return -ENODEV;
	}

	port = dev->dev_port[port_num];

	if (port == NULL) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) NULL port pointer\n",
			port_num, tty, file);
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		up(sem);
		return -ENODEV;
	}

	spin_lock(&port->port_lock);
	spin_unlock(&dev->dev_lock);

	if (port->port_dev == NULL) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) port disconnected (1)\n",
			port_num, tty, file);
		spin_unlock_irqrestore(&port->port_lock, flags);
		up(sem);
		return -EIO;
	}

	if (port->port_open_count > 0) {
		++port->port_open_count;
		spin_unlock_irqrestore(&port->port_lock, flags);
		gs_debug("gs_open: (%d,%p,%p) already open\n",
			port_num, tty, file);
		up(sem);
		return 0;
	}

	/* mark port as in use, we can drop port lock and sleep if necessary */
	port->port_in_use = 1;

	/* allocate write buffer on first open */
	if (port->port_write_buf == NULL) {
		spin_unlock_irqrestore(&port->port_lock, flags);
		buf = gs_buf_alloc(write_buf_size, GFP_KERNEL);
		spin_lock_irqsave(&port->port_lock, flags);

		/* might have been disconnected while asleep, check */
		if (port->port_dev == NULL) {
			printk(KERN_ERR
				"gs_open: (%d,%p,%p) port disconnected (2)\n",
				port_num, tty, file);
			port->port_in_use = 0;
			spin_unlock_irqrestore(&port->port_lock, flags);
			up(sem);
			return -EIO;
		}

		if ((port->port_write_buf=buf) == NULL) {
			printk(KERN_ERR "gs_open: (%d,%p,%p) cannot allocate port write buffer\n",
				port_num, tty, file);
			port->port_in_use = 0;
			spin_unlock_irqrestore(&port->port_lock, flags);
			up(sem);
			return -ENOMEM;
		}

	}

	/* wait for carrier detect (not implemented) */

	/* might have been disconnected while asleep, check */
	if (port->port_dev == NULL) {
		printk(KERN_ERR "gs_open: (%d,%p,%p) port disconnected (3)\n",
			port_num, tty, file);
		port->port_in_use = 0;
		spin_unlock_irqrestore(&port->port_lock, flags);
		up(sem);
		return -EIO;
	}

	tty->driver_data = port;
	port->port_tty = tty;
	port->port_open_count = 1;
	port->port_in_use = 0;

	spin_unlock_irqrestore(&port->port_lock, flags);
	up(sem);

	gs_debug("gs_open: (%d,%p,%p) completed\n", port_num, tty, file);

	return 0;
}

/*
 * gs_close
 */
static void gs_close(struct tty_struct *tty, struct file *file)
{
	unsigned long flags;
	struct gs_port *port = tty->driver_data;
	struct semaphore *sem;

	if (port == NULL) {
		printk(KERN_ERR "gs_close: NULL port pointer\n");
		return;
	}

	gs_debug("gs_close: (%d,%p,%p)\n", port->port_num, tty, file);

	sem = &gs_open_close_sem[port->port_num];
	down(sem);

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_open_count == 0) {
		printk(KERN_ERR
			"gs_close: (%d,%p,%p) port is already closed\n",
			port->port_num, tty, file);
		spin_unlock_irqrestore(&port->port_lock, flags);
		up(sem);
		return;
	}

	if (port->port_open_count > 0) {
		--port->port_open_count;
		spin_unlock_irqrestore(&port->port_lock, flags);
		up(sem);
		return;
	}

	/* free disconnected port on final close */
	if (port->port_dev == NULL) {
		kfree(port);
		spin_unlock_irqrestore(&port->port_lock, flags);
		up(sem);
		return;
	}

	/* mark port as closed but in use, we can drop port lock */
	/* and sleep if necessary */
	port->port_in_use = 1;
	port->port_open_count = 0;

	/* wait for write buffer to drain, or */
	/* at most GS_CLOSE_TIMEOUT seconds */
	if (gs_buf_data_avail(port->port_write_buf) > 0) {
		wait_cond_interruptible_timeout(port->port_write_wait,
		port->port_dev == NULL
		|| gs_buf_data_avail(port->port_write_buf) == 0,
		&port->port_lock, flags, GS_CLOSE_TIMEOUT * HZ);
	}

	/* free disconnected port on final close */
	/* (might have happened during the above sleep) */
	if (port->port_dev == NULL) {
		kfree(port);
		spin_unlock_irqrestore(&port->port_lock, flags);
		up(sem);
		return;
	}

	gs_buf_clear(port->port_write_buf);

	tty->driver_data = NULL;
	port->port_tty = NULL;
	port->port_in_use = 0;

	spin_unlock_irqrestore(&port->port_lock, flags);
	up(sem);

	gs_debug("gs_close: (%d,%p,%p) completed\n",
		port->port_num, tty, file);
}

/*
 * gs_write
 */
static int gs_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count)
{
	unsigned long flags;
	struct gs_port *port = tty->driver_data;

	if (port == NULL) {
		printk(KERN_ERR "gs_write: NULL port pointer\n");
		return -EIO;
	}

	gs_debug("gs_write: (%d,%p) writing %d bytes\n", port->port_num, tty,
		count);

	if (count == 0)
		return 0;

	/* copy from user into tmp buffer, get tmp_buf semaphore */
	if (from_user) {
		if (count > GS_TMP_BUF_SIZE)
			count = GS_TMP_BUF_SIZE;
		down(&gs_tmp_buf_sem);
		if (copy_from_user(gs_tmp_buf, buf, count) != 0) {
			up(&gs_tmp_buf_sem);
			printk(KERN_ERR
			"gs_write: (%d,%p) cannot copy from user space\n",
				port->port_num, tty);
			return -EFAULT;
		}
		buf = gs_tmp_buf;
	}

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev == NULL) {
		printk(KERN_ERR "gs_write: (%d,%p) port is not connected\n",
			port->port_num, tty);
		spin_unlock_irqrestore(&port->port_lock, flags);
		if (from_user)
			up(&gs_tmp_buf_sem);
		return -EIO;
	}

	if (port->port_open_count == 0) {
		printk(KERN_ERR "gs_write: (%d,%p) port is closed\n",
			port->port_num, tty);
		spin_unlock_irqrestore(&port->port_lock, flags);
		if (from_user)
			up(&gs_tmp_buf_sem);
		return -EBADF;
	}

	count = gs_buf_put(port->port_write_buf, buf, count);

	spin_unlock_irqrestore(&port->port_lock, flags);

	if (from_user)
		up(&gs_tmp_buf_sem);

	gs_send(gs_device);

	gs_debug("gs_write: (%d,%p) wrote %d bytes\n", port->port_num, tty,
		count);

	return count;
}

/*
 * gs_put_char
 */
static void gs_put_char(struct tty_struct *tty, unsigned char ch)
{
	unsigned long flags;
	struct gs_port *port = tty->driver_data;

	if (port == NULL) {
		printk(KERN_ERR "gs_put_char: NULL port pointer\n");
		return;
	}

	gs_debug("gs_put_char: (%d,%p) char=0x%x, called from %p, %p, %p\n", port->port_num, tty, ch, __builtin_return_address(0), __builtin_return_address(1), __builtin_return_address(2));

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev == NULL) {
		printk(KERN_ERR "gs_put_char: (%d,%p) port is not connected\n",
			port->port_num, tty);
		spin_unlock_irqrestore(&port->port_lock, flags);
		return;
	}

	if (port->port_open_count == 0) {
		printk(KERN_ERR "gs_put_char: (%d,%p) port is closed\n",
			port->port_num, tty);
		spin_unlock_irqrestore(&port->port_lock, flags);
		return;
	}

	gs_buf_put(port->port_write_buf, &ch, 1);

	spin_unlock_irqrestore(&port->port_lock, flags);
}

/*
 * gs_flush_chars
 */
static void gs_flush_chars(struct tty_struct *tty)
{
	unsigned long flags;
	struct gs_port *port = tty->driver_data;

	if (port == NULL) {
		printk(KERN_ERR "gs_flush_chars: NULL port pointer\n");
		return;
	}

	gs_debug("gs_flush_chars: (%d,%p)\n", port->port_num, tty);

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev == NULL) {
		printk(KERN_ERR
			"gs_flush_chars: (%d,%p) port is not connected\n",
			port->port_num, tty);
		spin_unlock_irqrestore(&port->port_lock, flags);
		return;
	}

	if (port->port_open_count == 0) {
		printk(KERN_ERR "gs_flush_chars: (%d,%p) port is closed\n",
			port->port_num, tty);
		spin_unlock_irqrestore(&port->port_lock, flags);
		return;
	}

	spin_unlock_irqrestore(&port->port_lock, flags);

	gs_send(gs_device);
}

/*
 * gs_write_room
 */
static int gs_write_room(struct tty_struct *tty)
{

	int room = 0;
	unsigned long flags;
	struct gs_port *port = tty->driver_data;


	if (port == NULL)
		return 0;

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev != NULL && port->port_open_count > 0
	&& port->port_write_buf != NULL)
		room = gs_buf_space_avail(port->port_write_buf);

	spin_unlock_irqrestore(&port->port_lock, flags);

	gs_debug("gs_write_room: (%d,%p) room=%d\n",
		port->port_num, tty, room);

	return room;
}

/*
 * gs_chars_in_buffer
 */
static int gs_chars_in_buffer(struct tty_struct *tty)
{
	int chars = 0;
	unsigned long flags;
	struct gs_port *port = tty->driver_data;

	if (port == NULL)
		return 0;

	spin_lock_irqsave(&port->port_lock, flags);

	if (port->port_dev != NULL && port->port_open_count > 0
	&& port->port_write_buf != NULL)
		chars = gs_buf_data_avail(port->port_write_buf);

	spin_unlock_irqrestore(&port->port_lock, flags);

	gs_debug("gs_chars_in_buffer: (%d,%p) chars=%d\n",
		port->port_num, tty, chars);

	return chars;
}

/*
 * gs_throttle
 */
static void gs_throttle(struct tty_struct *tty)
{
}

/*
 * gs_unthrottle
 */
static void gs_unthrottle(struct tty_struct *tty)
{
}

/*
 * gs_break
 */
static void gs_break(struct tty_struct *tty, int break_state)
{
}

/*
 * gs_ioctl
 */
static int gs_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct gs_port *port = tty->driver_data;

	if (port == NULL) {
		printk(KERN_ERR "gs_ioctl: NULL port pointer\n");
		return -EIO;
	}

	gs_debug("gs_ioctl: (%d,%p,%p) cmd=0x%4.4x, arg=%lu\n",
		port->port_num, tty, file, cmd, arg);

	/* handle ioctls */

	/* could not handle ioctl */
	return -ENOIOCTLCMD;
}

/*
 * gs_set_termios
 */
static void gs_set_termios(struct tty_struct *tty, struct termios *old)
{
}

/*
* gs_send
*
* This function finds available write requests, calls
* gs_send_packet to fill these packets with data, and
* continues until either there are no more write requests
* available or no more data to send.  This function is
* run whenever data arrives or write requests are available.
*/
static int gs_send(struct gs_dev *dev)
{
	int ret,len;
	unsigned long flags;
	struct usb_ep *ep;
	struct usb_request *req;
	struct gs_req_entry *req_entry;

	if (dev == NULL) {
		printk(KERN_ERR "gs_send: NULL device pointer\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&dev->dev_lock, flags);

	ep = dev->dev_in_ep;

	while(!list_empty(&dev->dev_req_list)) {

		req_entry = list_entry(dev->dev_req_list.next,
			struct gs_req_entry, re_entry);

		req = req_entry->re_req;

		len = gs_send_packet(dev, req->buf, ep->maxpacket);

		if (len > 0) {
gs_debug_level(3, "gs_send: len=%d, 0x%2.2x 0x%2.2x 0x%2.2x ...\n", len, *((unsigned char *)req->buf), *((unsigned char *)req->buf+1), *((unsigned char *)req->buf+2));
			list_del(&req_entry->re_entry);
			req->length = len;
			if ((ret=usb_ep_queue(ep, req, GFP_ATOMIC))) {
				printk(KERN_ERR
				"gs_send: cannot queue read request, ret=%d\n",
					ret);
				break;
			}
		} else {
			break;
		}

	}

	spin_unlock_irqrestore(&dev->dev_lock, flags);

	return 0;
}

/*
 * gs_send_packet
 *
 * If there is data to send, a packet is built in the given
 * buffer and the size is returned.  If there is no data to
 * send, 0 is returned.  If there is any error a negative
 * error number is returned.
 *
 * Called during USB completion routine, on interrupt time.
 *
 * We assume that disconnect will not happen until all completion
 * routines have completed, so we can assume that the dev_port
 * array does not change during the lifetime of this function.
 */
static int gs_send_packet(struct gs_dev *dev, char *packet, unsigned int size)
{
	unsigned int len;
	struct gs_port *port;

	/* TEMPORARY -- only port 0 is supported right now */
	port = dev->dev_port[0];

	if (port == NULL) {
		printk(KERN_ERR
			"gs_send_packet: port=%d, NULL port pointer\n",
			0);
		return -EIO;
	}

	spin_lock(&port->port_lock);

	len = gs_buf_data_avail(port->port_write_buf);
	if (len < size)
		size = len;

	if (size == 0) {
		spin_unlock(&port->port_lock);
		return 0;
	}

	size = gs_buf_get(port->port_write_buf, packet, size);

	wake_up_interruptible(&port->port_tty->write_wait);

	spin_unlock(&port->port_lock);

	return size;
}

/*
 * gs_recv_packet
 *
 * Called for each USB packet received.  Reads the packet
 * header and stuffs the data in the appropriate tty buffer.
 * Returns 0 if successful, or a negative error number.
 *
 * Called during USB completion routine, on interrupt time.
 *
 * We assume that disconnect will not happen until all completion
 * routines have completed, so we can assume that the dev_port
 * array does not change during the lifetime of this function.
 */
static int gs_recv_packet(struct gs_dev *dev, char *packet, unsigned int size)
{
	unsigned int len;
	struct gs_port *port;

	/* TEMPORARY -- only port 0 is supported right now */
	port = dev->dev_port[0];

	if (port == NULL) {
		printk(KERN_ERR "gs_recv_packet: port=%d, NULL port pointer\n",
			port->port_num);
		return -EIO;
	}

	spin_lock(&port->port_lock);

	if (port->port_tty == NULL) {
		printk(KERN_ERR "gs_recv_packet: port=%d, NULL tty pointer\n",
			port->port_num);
		spin_unlock(&port->port_lock);
		return -EIO;
	}

	if (port->port_tty->magic != TTY_MAGIC) {
		printk(KERN_ERR "gs_recv_packet: port=%d, bad tty magic\n",
			port->port_num);
		spin_unlock(&port->port_lock);
		return -EIO;
	}

	len = (unsigned int)(TTY_FLIPBUF_SIZE - port->port_tty->flip.count);
	if (len < size)
		size = len;

	if (size > 0) {
		memcpy(port->port_tty->flip.char_buf_ptr, packet, size);
		port->port_tty->flip.char_buf_ptr += size;
		port->port_tty->flip.count += size;
		tty_flip_buffer_push(port->port_tty);
		wake_up_interruptible(&port->port_tty->read_wait);
	}

	spin_unlock(&port->port_lock);

	return 0;
}

/*
* gs_read_complete
*/
static void gs_read_complete(struct usb_ep *ep, struct usb_request *req)
{
	int ret;
	struct gs_dev *dev = ep->driver_data;

	if (dev == NULL) {
		printk(KERN_ERR "gs_read_complete: NULL device pointer\n");
		return;
	}

	switch(req->status) {
	case 0:
 		/* normal completion */
		gs_recv_packet(dev, req->buf, req->actual);
requeue:
		req->length = ep->maxpacket;
		if ((ret=usb_ep_queue(ep, req, GFP_ATOMIC))) {
			printk(KERN_ERR
			"gs_read_complete: cannot queue read request, ret=%d\n",
				ret);
		}
		break;

	case -ESHUTDOWN:
		/* disconnect */
		gs_debug("gs_read_complete: shutdown\n");
		gs_free_req(ep, req);
		break;

	default:
		/* unexpected */
		printk(KERN_ERR
		"gs_read_complete: unexpected status error, status=%d\n",
			req->status);
		goto requeue;
		break;
	}
}

/*
* gs_write_complete
*/
static void gs_write_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gs_dev *dev = ep->driver_data;
	struct gs_req_entry *gs_req = req->context;

	if (dev == NULL) {
		printk(KERN_ERR "gs_write_complete: NULL device pointer\n");
		return;
	}

	switch(req->status) {
	case 0:
		/* normal completion */
requeue:
		if (gs_req == NULL) {
			printk(KERN_ERR
				"gs_write_complete: NULL request pointer\n");
			return;
		}

		spin_lock(&dev->dev_lock);
		list_add(&gs_req->re_entry, &dev->dev_req_list);
		spin_unlock(&dev->dev_lock);

		gs_send(dev);

		break;

	case -ESHUTDOWN:
		/* disconnect */
		gs_debug("gs_write_complete: shutdown\n");
		gs_free_req(ep, req);
		break;

	default:
		printk(KERN_ERR
		"gs_write_complete: unexpected status error, status=%d\n",
			req->status);
		goto requeue;
		break;
	}
}

/* Gadget Driver */

/*
 * gs_bind
 *
 * Called on module load.  Allocates and initializes the device
 * structure and a control request.
 */
static int gs_bind(struct usb_gadget *gadget)
{
	int ret;
	struct gs_dev *dev;

	gs_device = dev = kmalloc(sizeof(struct gs_dev), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	set_gadget_data(gadget, dev);

	memset(dev, 0, sizeof(struct gs_dev));
	dev->dev_gadget = gadget;
	spin_lock_init(&dev->dev_lock);
	INIT_LIST_HEAD(&dev->dev_req_list);

	if ((ret=gs_alloc_ports(dev, GFP_KERNEL)) != 0) {
		printk(KERN_ERR "gs_bind: cannot allocate ports\n");
		gs_unbind(gadget);
		return ret;
	}

	/* preallocate control response and buffer */
	dev->dev_ctrl_req = gs_alloc_req(gadget->ep0, GS_MAX_DESC_LEN,
		GFP_KERNEL);
	if (dev->dev_ctrl_req == NULL) {
		gs_unbind(gadget);
		return -ENOMEM;
	}
	dev->dev_ctrl_req->complete = gs_setup_complete;

	gadget->ep0->driver_data = dev;

	printk(KERN_INFO "gs_bind: %s %s bound\n",
		GS_LONG_NAME, GS_VERSION_STR);

	return 0;
}

/*
 * gs_unbind
 *
 * Called on module unload.  Frees the control request and device
 * structure.
 */
static void gs_unbind(struct usb_gadget *gadget)
{
	struct gs_dev *dev = get_gadget_data(gadget);

	gs_device = NULL;

	/* read/write requests already freed, only control request remains */
	if (dev != NULL) {
		if (dev->dev_ctrl_req != NULL)
			gs_free_req(gadget->ep0, dev->dev_ctrl_req);
		gs_free_ports(dev);
		kfree(dev);
		set_gadget_data(gadget, NULL);
	}

	printk(KERN_INFO "gs_unbind: %s %s unbound\n", GS_LONG_NAME,
		GS_VERSION_STR);
}

/*
 * gs_setup
 *
 * Implements all the control endpoint functionality that's not
 * handled in hardware or the hardware driver.
 *
 * Returns the size of the data sent to the host, or a negative
 * error number.
 */
static int gs_setup(struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	int ret = -EOPNOTSUPP;
	unsigned int sv_config;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->dev_ctrl_req;

	switch (ctrl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != USB_DIR_IN)
			break;

		switch (ctrl->wValue >> 8) {
		case USB_DT_DEVICE:
			ret = min(ctrl->wLength,
				(u16)sizeof(struct usb_device_descriptor));
			memcpy(req->buf, &gs_device_desc, ret);
			break;

#ifdef HIGHSPEED
		case USB_DT_DEVICE_QUALIFIER:
			ret = min(ctrl->wLength,
				(u16)sizeof(struct usb_qualifier_descriptor));
			memcpy(req->buf, &gs_qualifier_desc, ret);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
#endif /* HIGHSPEED */
		case USB_DT_CONFIG:
			ret = gs_build_config_desc(req->buf, gadget->speed,
				ctrl->wValue >> 8, ctrl->wValue & 0xff);
			if (ret >= 0)
				ret = min(ctrl->wLength, (u16)ret);
			break;

		case USB_DT_STRING:
			/* wIndex == language code. */
			ret = usb_gadget_get_string(&gs_string_table,
				ctrl->wValue & 0xff, req->buf);
			if (ret >= 0)
				ret = min(ctrl->wLength, (u16)ret);
			break;
		}
		break;

	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0)
			break;
		spin_lock(&dev->dev_lock);
		ret = gs_set_config(dev, ctrl->wValue);
		spin_unlock(&dev->dev_lock);
		break;

	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != USB_DIR_IN)
			break;
		*(u8 *)req->buf = dev->dev_config;
		ret = min(ctrl->wLength, (u16)1);
		break;

	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType != USB_RECIP_INTERFACE)
			break;
		spin_lock(&dev->dev_lock);
		if (dev->dev_config == GS_BULK_CONFIG_ID
		&& ctrl->wIndex == GS_INTERFACE_ID
		&& ctrl->wValue == GS_ALT_INTERFACE_ID) {
			sv_config = dev->dev_config;
			/* since there is only one interface, setting the */
			/* interface is equivalent to setting the config */
			gs_reset_config(dev);
			gs_set_config(dev, sv_config);
			ret = 0;
		}
		spin_unlock(&dev->dev_lock);
		break;

	case USB_REQ_GET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_IN|USB_RECIP_INTERFACE))
			break;
		if (dev->dev_config == GS_NO_CONFIG_ID)
			break;
		if (ctrl->wIndex != GS_INTERFACE_ID) {
			ret = -EDOM;
			break;
		}
		*(u8 *)req->buf = GS_ALT_INTERFACE_ID;
		ret = min(ctrl->wLength, (u16)1);
		break;

	default:
		printk(KERN_ERR "gs_setup: unknown request, type=%02x, request=%02x, value=%04x, index=%04x, length=%d\n",
			ctrl->bRequestType, ctrl->bRequest, ctrl->wValue,
			ctrl->wIndex, ctrl->wLength);
		break;

	}

	/* respond with data transfer before status phase? */
	if (ret >= 0) {
		req->length = ret;
		req->zero = ret < ctrl->wLength
				&& (ret % gadget->ep0->maxpacket) == 0;
		ret = usb_ep_queue(gadget->ep0, req, GFP_ATOMIC);
		if (ret < 0) {
			printk(KERN_ERR
				"gs_setup: cannot queue response, ret=%d\n",
				ret);
			req->status = 0;
			gs_setup_complete(gadget->ep0, req);
		}
	}

	/* device either stalls (ret < 0) or reports success */
	return ret;
}

/*
 * gs_setup_complete
 */
static void gs_setup_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (req->status || req->actual != req->length) {
		printk(KERN_ERR "gs_setup_complete: status error, status=%d, actual=%d, length=%d\n",
			req->status, req->actual, req->length);
	}
}

/*
 * gs_disconnect
 *
 * Called when the device is disconnected.  Frees the closed
 * ports and disconnects open ports.  Open ports will be freed
 * on close.  Then reallocates the ports for the next connection.
 */
static void gs_disconnect(struct usb_gadget *gadget)
{
	unsigned long flags;
	struct gs_dev *dev = get_gadget_data(gadget);

	spin_lock_irqsave(&dev->dev_lock, flags);

	gs_reset_config(dev);

	/* free closed ports and disconnect open ports */
	/* (open ports will be freed when closed) */
	gs_free_ports(dev);

	/* re-allocate ports for the next connection */
	if (gs_alloc_ports(dev, GFP_ATOMIC) != 0)
		printk(KERN_ERR "gs_disconnect: cannot re-allocate ports\n");

	spin_unlock_irqrestore(&dev->dev_lock, flags);

	printk(KERN_INFO "gs_disconnect: %s disconnected\n", GS_LONG_NAME);
}

/*
 * gs_set_config
 *
 * Configures the device by enabling device specific
 * optimizations, setting up the endpoints, allocating
 * read and write requests and queuing read requests.
 *
 * The device lock must be held when calling this function.
 */
static int gs_set_config(struct gs_dev *dev, unsigned config)
{
	int i;
	int ret = 0;
	struct usb_gadget *gadget = dev->dev_gadget;
	struct usb_ep *ep;
	struct usb_request *req;
	struct gs_req_entry *req_entry;

	if (dev == NULL) {
		printk(KERN_ERR "gs_set_config: NULL device pointer\n");
		return 0;
	}

	if (config == dev->dev_config)
		return 0;

	gs_reset_config(dev);

	if (config == GS_NO_CONFIG_ID)
		return 0;

	if (config != GS_BULK_CONFIG_ID)
		return -EINVAL;

	hw_optimize(gadget);

	gadget_for_each_ep(ep, gadget) {

		if (strcmp(ep->name, EP_IN_NAME) == 0) {
			ret = usb_ep_enable(ep,
				gadget->speed == USB_SPEED_HIGH ?
 				&gs_highspeed_in_desc : &gs_fullspeed_in_desc);
			if (ret == 0) {
				ep->driver_data = dev;
				dev->dev_in_ep = ep;
			} else {
				printk(KERN_ERR "gs_set_config: cannot enable in endpoint %s, ret=%d\n",
					ep->name, ret);
				gs_reset_config(dev);
				return ret;
			}
		}

		else if (strcmp(ep->name, EP_OUT_NAME) == 0) {
			ret = usb_ep_enable(ep,
				gadget->speed == USB_SPEED_HIGH ?
				&gs_highspeed_out_desc :
				&gs_fullspeed_out_desc);
			if (ret == 0) {
				ep->driver_data = dev;
				dev->dev_out_ep = ep;
			} else {
				printk(KERN_ERR "gs_set_config: cannot enable out endpoint %s, ret=%d\n",
					ep->name, ret);
				gs_reset_config(dev);
				return ret;
			}
		}

	}

	if (dev->dev_in_ep == NULL || dev->dev_out_ep == NULL) {
		gs_reset_config(dev);
		printk(KERN_ERR "gs_set_config: cannot find endpoints\n");
		return -ENODEV;
	}

	/* allocate and queue read requests */
	ep = dev->dev_out_ep;
	for (i=0; i<read_q_size && ret == 0; i++) {
		if ((req=gs_alloc_req(ep, ep->maxpacket, GFP_ATOMIC))) {
			req->complete = gs_read_complete;
			if ((ret=usb_ep_queue(ep, req, GFP_ATOMIC))) {
				printk(KERN_ERR "gs_set_config: cannot queue read request, ret=%d\n",
					ret);
			}
		} else {
			gs_reset_config(dev);
			printk(KERN_ERR
			"gs_set_config: cannot allocate read requests\n");
			return -ENOMEM;
		}
	}

	/* allocate write requests, and put on free list */
	ep = dev->dev_in_ep;
	for (i=0; i<write_q_size; i++) {
		if ((req_entry=gs_alloc_req_entry(ep, ep->maxpacket, GFP_ATOMIC))) {
			req_entry->re_req->complete = gs_write_complete;
			list_add(&req_entry->re_entry, &dev->dev_req_list);
		} else {
			gs_reset_config(dev);
			printk(KERN_ERR
			"gs_set_config: cannot allocate write requests\n");
			return -ENOMEM;
		}
	}

	dev->dev_config = config;

	printk(KERN_INFO "gs_set_config: %s configured for %s speed\n",
		GS_LONG_NAME,
		gadget->speed == USB_SPEED_HIGH ? "high" : "full");

	return 0;
}

/*
 * gs_reset_config
 *
 * Mark the device as not configured, disable all endpoints,
 * which forces completion of pending I/O and frees queued
 * requests, and free the remaining write requests on the
 * free list.
 *
 * The device lock must be held when calling this function.
 */
static void gs_reset_config(struct gs_dev *dev)
{
	struct gs_req_entry *req_entry;

	if (dev == NULL) {
		printk(KERN_ERR "gs_reset_config: NULL device pointer\n");
		return;
	}

	if (dev->dev_config == GS_NO_CONFIG_ID)
		return;

	dev->dev_config = GS_NO_CONFIG_ID;

	/* free write requests on the free list */
	while(!list_empty(&dev->dev_req_list)) {
		req_entry = list_entry(dev->dev_req_list.next,
			struct gs_req_entry, re_entry);
		list_del(&req_entry->re_entry);
		gs_free_req_entry(dev->dev_in_ep, req_entry);
	}

	/* disable endpoints, forcing completion of pending i/o; */
	/* completion handlers free their requests in this case */
	if (dev->dev_in_ep) {
		usb_ep_disable(dev->dev_in_ep);
		dev->dev_in_ep = NULL;
	}
	if (dev->dev_out_ep) {
		usb_ep_disable(dev->dev_out_ep);
		dev->dev_out_ep = NULL;
	}
}

/*
 * gs_build_config_desc
 *
 * Builds a config descriptor in the given buffer and returns the
 * length, or a negative error number.
 */
static int gs_build_config_desc(u8 *buf, enum usb_device_speed speed, u8 type, unsigned int index)
{
	int high_speed;
	int len = USB_DT_CONFIG_SIZE + USB_DT_INTERFACE_SIZE
				+ GS_NUM_ENDPOINTS * USB_DT_ENDPOINT_SIZE;

	/* only one config */
	if (index != 0)
		return -EINVAL;

	memcpy(buf, &gs_config_desc, USB_DT_CONFIG_SIZE);
	((struct usb_config_descriptor *)buf)->bDescriptorType = type;
	((struct usb_config_descriptor *)buf)->wTotalLength = __constant_cpu_to_le16(len);
	buf += USB_DT_CONFIG_SIZE;

	memcpy(buf, &gs_interface_desc, USB_DT_INTERFACE_SIZE);
	buf += USB_DT_INTERFACE_SIZE;

	/* other speed switches high and full speed */
	high_speed = (speed == USB_SPEED_HIGH);
	if (type == USB_DT_OTHER_SPEED_CONFIG)
		high_speed = !high_speed;

	memcpy(buf,
		high_speed ? &gs_highspeed_in_desc : &gs_fullspeed_in_desc,
		USB_DT_ENDPOINT_SIZE);
	buf += USB_DT_ENDPOINT_SIZE;
	memcpy(buf,
		high_speed ? &gs_highspeed_out_desc : &gs_fullspeed_out_desc,
		USB_DT_ENDPOINT_SIZE);

	return len;
}

/*
 * gs_alloc_req
 *
 * Allocate a usb_request and its buffer.  Returns a pointer to the
 * usb_request or NULL if there is an error.
 */
static struct usb_request *gs_alloc_req(struct usb_ep *ep, unsigned int len, int kmalloc_flags)
{
	struct usb_request *req;

	if (ep == NULL)
		return NULL;

	req = usb_ep_alloc_request(ep, kmalloc_flags);

	if (req != NULL) {
		req->length = len;
		req->buf = usb_ep_alloc_buffer(ep, len, &req->dma,
			kmalloc_flags);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			return NULL;
		}
	}

	return req;
}

/*
 * gs_free_req
 *
 * Free a usb_request and its buffer.
 */
static void gs_free_req(struct usb_ep *ep, struct usb_request *req)
{
	if (ep != NULL && req != NULL) {
		if (req->buf != NULL)
			usb_ep_free_buffer(ep, req->buf, req->dma,
				req->length);
		usb_ep_free_request(ep, req);
	}
}

/*
 * gs_alloc_req_entry
 *
 * Allocates a request and its buffer, using the given
 * endpoint, buffer len, and kmalloc flags.
 */
static struct gs_req_entry *gs_alloc_req_entry(struct usb_ep *ep, unsigned len, int kmalloc_flags)
{
	struct gs_req_entry	*req;

	req = kmalloc(sizeof(struct gs_req_entry), kmalloc_flags);
	if (req == NULL)
		return NULL;

	req->re_req = gs_alloc_req(ep, len, kmalloc_flags);
	if (req->re_req == NULL) {
		kfree(req);
		return NULL;
	}

	req->re_req->context = req;

	return req;
}

/*
 * gs_free_req_entry
 *
 * Frees a request and its buffer.
 */
static void gs_free_req_entry(struct usb_ep *ep, struct gs_req_entry *req)
{
	if (ep != NULL && req != NULL) {
		if (req->re_req != NULL)
			gs_free_req(ep, req->re_req);
		kfree(req);
	}
}

/*
 * gs_alloc_ports
 *
 * Allocate all ports and set the gs_dev struct to point to them.
 * Return 0 if successful, or a negative error number.
 *
 * The device lock is normally held when calling this function.
 */
static int gs_alloc_ports(struct gs_dev *dev, int kmalloc_flags)
{
	int i;
	struct gs_port *port;

	if (dev == NULL)
		return -EIO;

	for (i=0; i<GS_NUM_PORTS; i++) {
		if ((port=(struct gs_port *)kmalloc(sizeof(struct gs_port), kmalloc_flags)) == NULL)
			return -ENOMEM;

		memset(port, 0, sizeof(struct gs_port));
		port->port_dev = dev;
		port->port_num = i;
		spin_lock_init(&port->port_lock);
		init_waitqueue_head(&port->port_write_wait);

		dev->dev_port[i] = port;
	}

	return 0;
}

/*
 * gs_free_ports
 *
 * Free all closed ports.  Open ports are disconnected by
 * freeing their write buffers, setting their device pointers
 * and the pointers to them in the device to NULL.  These
 * ports will be freed when closed.
 *
 * The device lock is normally held when calling this function.
 */
static void gs_free_ports(struct gs_dev *dev)
{
	int i;
	unsigned long flags;
	struct gs_port *port;

	if (dev == NULL)
		return;

	for (i=0; i<GS_NUM_PORTS; i++) {
		if ((port=dev->dev_port[i]) != NULL) {
			dev->dev_port[i] = NULL;

			spin_lock_irqsave(&port->port_lock, flags);

			if (port->port_write_buf != NULL) {
				gs_buf_free(port->port_write_buf);
				port->port_write_buf = NULL;
			}

			if (port->port_open_count > 0 || port->port_in_use) {
				port->port_dev = NULL;
				wake_up_interruptible(&port->port_write_wait);
				wake_up_interruptible(&port->port_tty->read_wait);
				wake_up_interruptible(&port->port_tty->write_wait);
			} else {
				kfree(port);
			}

			spin_unlock_irqrestore(&port->port_lock, flags);
		}
	}
}

/* Circular Buffer */

/*
 * gs_buf_alloc
 *
 * Allocate a circular buffer and all associated memory.
 */
static struct gs_buf *gs_buf_alloc(unsigned int size, int kmalloc_flags)
{
	struct gs_buf *gb;

	if (size == 0)
		return NULL;

	gb = (struct gs_buf *)kmalloc(sizeof(struct gs_buf), kmalloc_flags);
	if (gb == NULL)
		return NULL;

	gb->buf_buf = kmalloc(size, kmalloc_flags);
	if (gb->buf_buf == NULL) {
		kfree(gb);
		return NULL;
	}

	gb->buf_size = size;
	gb->buf_get = gb->buf_put = gb->buf_buf;

	return gb;
}

/*
 * gs_buf_free
 *
 * Free the buffer and all associated memory.
 */
void gs_buf_free(struct gs_buf *gb)
{
	if (gb != NULL) {
		if (gb->buf_buf != NULL)
			kfree(gb->buf_buf);
		kfree(gb);
	}
}

/*
 * gs_buf_clear
 *
 * Clear out all data in the circular buffer.
 */
void gs_buf_clear(struct gs_buf *gb)
{
	if (gb != NULL)
		gb->buf_get = gb->buf_put;
		/* equivalent to a get of all data available */
}

/*
 * gs_buf_data_avail
 *
 * Return the number of bytes of data available in the circular
 * buffer.
 */
unsigned int gs_buf_data_avail(struct gs_buf *gb)
{
	if (gb != NULL)
		return (gb->buf_size + gb->buf_put - gb->buf_get) % gb->buf_size;
	else
		return 0;
}

/*
 * gs_buf_space_avail
 *
 * Return the number of bytes of space available in the circular
 * buffer.
 */
unsigned int gs_buf_space_avail(struct gs_buf *gb)
{
	if (gb != NULL)
		return (gb->buf_size + gb->buf_get - gb->buf_put - 1) % gb->buf_size;
	else
		return 0;
}

/*
 * gs_buf_put
 *
 * Copy data data from a user buffer and put it into the circular buffer.
 * Restrict to the amount of space available.
 *
 * Return the number of bytes copied.
 */
unsigned int gs_buf_put(struct gs_buf *gb, const char *buf, unsigned int count)
{
	unsigned int len;

	if (gb == NULL)
		return 0;

	len  = gs_buf_space_avail(gb);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = gb->buf_buf + gb->buf_size - gb->buf_put;
	if (count > len) {
		memcpy(gb->buf_put, buf, len);
		memcpy(gb->buf_buf, buf+len, count - len);
		gb->buf_put = gb->buf_buf + count - len;
	} else {
		memcpy(gb->buf_put, buf, count);
		if (count < len)
			gb->buf_put += count;
		else /* count == len */
			gb->buf_put = gb->buf_buf;
	}

	return count;
}

/*
 * gs_buf_get
 *
 * Get data from the circular buffer and copy to the given buffer.
 * Restrict to the amount of data available.
 *
 * Return the number of bytes copied.
 */
unsigned int gs_buf_get(struct gs_buf *gb, char *buf, unsigned int count)
{
	unsigned int len;

	if (gb == NULL)
		return 0;

	len = gs_buf_data_avail(gb);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = gb->buf_buf + gb->buf_size - gb->buf_get;
	if (count > len) {
		memcpy(buf, gb->buf_get, len);
		memcpy(buf+len, gb->buf_buf, count - len);
		gb->buf_get = gb->buf_buf + count - len;
	} else {
		memcpy(buf, gb->buf_get, count);
		if (count < len)
			gb->buf_get += count;
		else /* count == len */
			gb->buf_get = gb->buf_buf;
	}

	return count;
}
