/*
 * USB IR Dongle driver
 *
 *	Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * This driver allows a USB IrDA device to be used as a "dumb" serial device.
 * This can be useful if you do not have access to a full IrDA stack on the
 * other side of the connection.  If you do have an IrDA stack on both devices,
 * please use the usb-irda driver, as it contains the proper error checking and
 * other goodness of a full IrDA stack.
 *
 * Portions of this driver were taken from drivers/net/irda/irda-usb.c, which
 * was written by Roman Weissgaerber <weissg@vienna.at>, Dag Brattli
 * <dag@brattli.net>, and Jean Tourrilhes <jt@hpl.hp.com>
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * 2001_Nov_08  greg kh
 *	Changed the irda_usb_find_class_desc() function based on comments and
 *	code from Martin Diehl.
 *
 * 2001_Nov_01	greg kh
 *	Added support for more IrDA USB devices.
 *	Added support for zero packet.  Added buffer override paramater, so
 *	users can transfer larger packets at once if they wish.  Both patches
 *	came from Dag Brattli <dag@obexcode.com>.
 *
 * 2001_Oct_07	greg kh
 *	initial version released.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <net/irda/irda-usb.h>

#ifdef CONFIG_USB_SERIAL_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif

#include "usb-serial.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.3"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>"
#define DRIVER_DESC "USB IR Dongle driver"

/* if overridden by the user, then use their value for the size of the read and
 * write urbs */
static int buffer_size = 0;

static int  ir_startup (struct usb_serial *serial);
static int  ir_open (struct usb_serial_port *port, struct file *filep);
static void ir_close (struct usb_serial_port *port, struct file *filep);
static int  ir_write (struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static void ir_write_bulk_callback (struct urb *urb);
static void ir_read_bulk_callback (struct urb *urb);
static void ir_set_termios (struct usb_serial_port *port, struct termios *old_termios);


static __devinitdata struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x050f, 0x0180) },		/* KC Technology, KC-180 */
	{ USB_DEVICE(0x08e9, 0x0100) },		/* XTNDAccess */
	{ USB_DEVICE(0x09c4, 0x0011) },		/* ACTiSys ACT-IR2000U */
	{ USB_INTERFACE_INFO (USB_CLASS_APP_SPEC, USB_CLASS_IRDA, 0) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table);


struct usb_serial_device_type ir_device = {
	name:			"IR Dongle",
	id_table:		id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	set_termios:		ir_set_termios,
	startup:		ir_startup,
	open:			ir_open,
	close:			ir_close,
	write:			ir_write,
	write_bulk_callback:	ir_write_bulk_callback,
	read_bulk_callback:	ir_read_bulk_callback,
};

static inline void irda_usb_dump_class_desc(struct irda_class_desc *desc)
{
	dbg("bLength=%x", desc->bLength);
	dbg("bDescriptorType=%x", desc->bDescriptorType);
	dbg("bcdSpecRevision=%x", desc->bcdSpecRevision); 
	dbg("bmDataSize=%x", desc->bmDataSize);
	dbg("bmWindowSize=%x", desc->bmWindowSize);
	dbg("bmMinTurnaroundTime=%d", desc->bmMinTurnaroundTime);
	dbg("wBaudRate=%x", desc->wBaudRate);
	dbg("bmAdditionalBOFs=%x", desc->bmAdditionalBOFs);
	dbg("bIrdaRateSniff=%x", desc->bIrdaRateSniff);
	dbg("bMaxUnicastList=%x", desc->bMaxUnicastList);
}

/*------------------------------------------------------------------*/
/*
 * Function irda_usb_find_class_desc(dev, ifnum)
 *
 *    Returns instance of IrDA class descriptor, or NULL if not found
 *
 * The class descriptor is some extra info that IrDA USB devices will
 * offer to us, describing their IrDA characteristics. We will use that in
 * irda_usb_init_qos()
 *
 * Based on the same function in drivers/net/irda/irda-usb.c
 */
static struct irda_class_desc *irda_usb_find_class_desc(struct usb_device *dev, unsigned int ifnum)
{
	struct irda_class_desc *desc;
	int ret;
		
	desc = kmalloc(sizeof (struct irda_class_desc), GFP_KERNEL);
	if (desc == NULL) 
		return NULL;
	memset(desc, 0, sizeof(struct irda_class_desc));
	
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev,0),
			IU_REQ_GET_CLASS_DESC,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, ifnum, desc, sizeof(*desc), MSECS_TO_JIFFIES(500));
	
	dbg(__FUNCTION__ " -  ret=%d", ret);
	if (ret < sizeof(*desc)) {
		dbg(__FUNCTION__ " - class descriptor read %s (%d)",
				(ret<0) ? "failed" : "too short", ret);
		goto error;
	}
	if (desc->bDescriptorType != USB_DT_IRDA) {
		dbg(__FUNCTION__ " - bad class descriptor type");
		goto error;
	}
	
	irda_usb_dump_class_desc(desc);
	return desc;
error:
	kfree(desc);
	return NULL;
}

static int ir_startup (struct usb_serial *serial)
{
	struct irda_class_desc *irda_desc;

	irda_desc = irda_usb_find_class_desc (serial->dev, 0);
	if (irda_desc == NULL) {
		err ("IRDA class descriptor not found, device not bound");
		return -ENODEV;
	}
	dbg (__FUNCTION__" - Baud rates supported: %s%s%s%s%s%s%s%s%s",
	     (irda_desc->wBaudRate & 0x0001) ? "2400 " : "",
	     irda_desc->wBaudRate & 0x0002 ? "9600 " : "",
	     irda_desc->wBaudRate & 0x0004 ? "19200 " : "",
	     irda_desc->wBaudRate & 0x0008 ? "38400 " : "",
	     irda_desc->wBaudRate & 0x0010 ? "57600 " : "",
	     irda_desc->wBaudRate & 0x0020 ? "115200 " : "",
	     irda_desc->wBaudRate & 0x0040 ? "576000 " : "",
	     irda_desc->wBaudRate & 0x0080 ? "1152000 " : "",
	     irda_desc->wBaudRate & 0x0100 ? "4000000 " : "");

	kfree (irda_desc);

	return 0;		
}

static int ir_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	char *buffer;
	int result = 0;

	if (port_paranoia_check (port, __FUNCTION__))
		return -ENODEV;
	
	dbg(__FUNCTION__ " - port %d", port->number);

	down (&port->sem);
	
	++port->open_count;
	MOD_INC_USE_COUNT;
	
	if (!port->active) {
		port->active = 1;

		if (buffer_size) {
			/* override the default buffer sizes */
			buffer = kmalloc (buffer_size, GFP_KERNEL);
			if (!buffer) {
				err (__FUNCTION__ " - out of memory.");
				return -ENOMEM;
			}
			kfree (port->read_urb->transfer_buffer);
			port->read_urb->transfer_buffer = buffer;
			port->read_urb->transfer_buffer_length = buffer_size;

			buffer = kmalloc (buffer_size, GFP_KERNEL);
			if (!buffer) {
				err (__FUNCTION__ " - out of memory.");
				return -ENOMEM;
			}
			kfree (port->write_urb->transfer_buffer);
			port->write_urb->transfer_buffer = buffer;
			port->write_urb->transfer_buffer_length = buffer_size;
			port->bulk_out_size = buffer_size;
		}

		/* Start reading from the device */
		FILL_BULK_URB(port->read_urb, serial->dev, 
			      usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
			      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
			      ir_read_bulk_callback, port);
		result = usb_submit_urb(port->read_urb);
		if (result)
			err(__FUNCTION__ " - failed submitting read urb, error %d", result);
	}
	
	up (&port->sem);
	
	return result;
}

static void ir_close (struct usb_serial_port *port, struct file * filp)
{
	struct usb_serial *serial;

	if (port_paranoia_check (port, __FUNCTION__))
		return;
	
	dbg(__FUNCTION__ " - port %d", port->number);
			 
	serial = get_usb_serial (port, __FUNCTION__);
	if (!serial)
		return;
	
	down (&port->sem);

	--port->open_count;

	if (port->open_count <= 0) {
		if (serial->dev) {
			/* shutdown our bulk read */
			usb_unlink_urb (port->read_urb);
		}
		port->active = 0;
		port->open_count = 0;

	}
	up (&port->sem);
	MOD_DEC_USE_COUNT;
}

static int ir_write (struct usb_serial_port *port, int from_user, const unsigned char *buf, int count)
{
	unsigned char *transfer_buffer;
	int result;

	dbg(__FUNCTION__ " - port = %d, count = %d", port->number, count);

	if (!port->tty) {
		err (__FUNCTION__ " - no tty???");
		return 0;
	}

	if (count == 0)
		return 0;

	if (port->write_urb->status == -EINPROGRESS) {
		dbg (__FUNCTION__ " - already writing");
		return 0;
	}

	/*
	 * The first byte of the packet we send to the device contains a BOD
	 * and baud rate change.  So we set it to 0.
	 * See section 5.4.2.2 of the USB IrDA spec.
	 */
	transfer_buffer = port->write_urb->transfer_buffer;
	count = min (port->bulk_out_size-1, count);
	if (from_user) {
		if (copy_from_user (&transfer_buffer[1], buf, count))
			return -EFAULT;
	} else {
		memcpy (&transfer_buffer[1], buf, count);
	}

	/* use 12 XBOF's as default */
	transfer_buffer[0] = 0x30;

	usb_serial_debug_data (__FILE__, __FUNCTION__, count+1, transfer_buffer);

	port->write_urb->transfer_buffer_length = count + 1;
	port->write_urb->dev = port->serial->dev;
	port->write_urb->transfer_flags |= USB_ZERO_PACKET;
	result = usb_submit_urb (port->write_urb);
	if (result)
		err(__FUNCTION__ " - failed submitting write urb, error %d", result);
	else
		result = count;

	return result;
}

static void ir_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;

	if (port_paranoia_check (port, __FUNCTION__))
		return;
	
	dbg(__FUNCTION__ " - port %d", port->number);
	
	if (urb->status) {
		dbg(__FUNCTION__ " - nonzero write bulk status received: %d", urb->status);
		return;
	}

	queue_task(&port->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	
	return;
}

static void ir_read_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = get_usb_serial (port, __FUNCTION__);
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int result;

	if (port_paranoia_check (port, __FUNCTION__))
		return;

	dbg(__FUNCTION__ " - port %d", port->number);

	if (!serial) {
		dbg(__FUNCTION__ " - bad serial pointer, exiting");
		return;
	}

	if (urb->status) {
		dbg(__FUNCTION__ " - nonzero read bulk status received: %d", urb->status);
		return;
	}

	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, data);

	/* Bypass flip-buffers, and feed the ldisc directly due to our 
	 * potentally large buffer size.  Since we used to set low_latency,
	 * this is exactly what the tty layer did anyway :) */
	tty = port->tty;
	tty->ldisc.receive_buf(tty, data+1, NULL, urb->actual_length-1);

	/* Continue trying to always read  */
	FILL_BULK_URB(port->read_urb, serial->dev, 
		      usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
		      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
		      ir_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb);
	if (result)
		err(__FUNCTION__ " - failed resubmitting read urb, error %d", result);
	return;
}

static void ir_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	unsigned char *transfer_buffer;
	unsigned int cflag;
	int result;
	u8 baud;

	dbg(__FUNCTION__ " - port %d", port->number);

	if ((!port->tty) || (!port->tty->termios)) {
		dbg(__FUNCTION__" - no tty structures");
		return;
	}

	cflag = port->tty->termios->c_cflag;
	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(port->tty->termios->c_iflag) == RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg(__FUNCTION__ " - nothing to change...");
			return;
		}
	}

	/* All we can change is the baud rate */
	if (cflag & CBAUD) {
		dbg (__FUNCTION__ " - asking for baud %d", tty_get_baud_rate(port->tty));
		/* 
		 * FIXME, we should compare the baud request against the
		 * capability stated in the IR header that we got in the
		 * startup funtion.
		 */
		switch (cflag & CBAUD) {
			case B2400:	baud = SPEED_2400;	break;
			case B9600:	baud = SPEED_9600;	break;
			case B19200:	baud = SPEED_19200;	break;
			case B38400:	baud = SPEED_38400;	break;
			case B57600:	baud = SPEED_57600;	break;
			case B115200:	baud = SPEED_115200;	break;
			case B576000:	baud = SPEED_576000;	break;
			case B1152000:	baud = SPEED_1152000;	break;
			case B4000000:	baud = SPEED_4000000;	break;
			default:
				err ("ir-usb driver does not support the baudrate (%d) requested", tty_get_baud_rate(port->tty));
				return;
		}
		
		/* FIXME need to check to see if our write urb is busy right
		 * now, or use a urb pool. */
		/* send the baud change out on an "empty" data packet */
		transfer_buffer = port->write_urb->transfer_buffer;
		transfer_buffer[0] = baud;
		port->write_urb->transfer_buffer_length = 1;
		port->write_urb->dev = port->serial->dev;
		result = usb_submit_urb (port->write_urb);
		if (result)
			err(__FUNCTION__ " - failed submitting write urb, error %d", result);
	}
	return;
}


static int __init ir_init (void)
{
	usb_serial_register (&ir_device);
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}


static void __exit ir_exit (void)
{
	usb_serial_deregister (&ir_device);
}


module_init(ir_init);
module_exit(ir_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");
MODULE_PARM(buffer_size, "i");
MODULE_PARM_DESC(buffer_size, "Size of the transfer buffers");

