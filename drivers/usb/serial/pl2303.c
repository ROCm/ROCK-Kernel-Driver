/*
 * Prolific PL2303 USB to serial adaptor driver
 *
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 * Original driver for 2.2.x by anonymous
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * 2001_Oct_06 gkh
 *	Added RTS and DTR line control.  Thanks to joe@bndlg.de for parts of it.
 *
 * 2001_Sep_19 gkh
 *	Added break support.
 *
 * 2001_Aug_30 gkh
 *	fixed oops in write_bulk_callback.
 *
 * 2001_Aug_28 gkh
 *	reworked buffer logic to be like other usb-serial drivers.  Hopefully
 *	removing some reported problems.
 *
 * 2001_Jun_06 gkh
 *	finished porting to 2.4 format.
 * 
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
#include <linux/serial.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/usb.h>

#ifdef CONFIG_USB_SERIAL_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif

#include "usb-serial.h"
#include "pl2303.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.9"
#define DRIVER_DESC "Prolific PL2303 USB to serial adaptor driver"



static __devinitdata struct usb_device_id id_table [] = {
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_RSAQ2) },
	{ USB_DEVICE(IODATA_VENDOR_ID, IODATA_PRODUCT_ID) },
	{ USB_DEVICE(ATEN_VENDOR_ID, ATEN_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table);


#define SET_LINE_REQUEST_TYPE		0x21
#define SET_LINE_REQUEST		0x20

#define SET_CONTROL_REQUEST_TYPE	0x21
#define SET_CONTROL_REQUEST		0x22
#define CONTROL_DTR			0x01
#define CONTROL_RTS			0x02

#define BREAK_REQUEST_TYPE		0x21
#define BREAK_REQUEST			0x23	
#define BREAK_ON			0xffff
#define BREAK_OFF			0x0000

#define GET_LINE_REQUEST_TYPE		0xa1
#define GET_LINE_REQUEST		0x21

#define VENDOR_WRITE_REQUEST_TYPE	0x40
#define VENDOR_WRITE_REQUEST		0x01

#define VENDOR_READ_REQUEST_TYPE	0xc0
#define VENDOR_READ_REQUEST		0x01

/* function prototypes for a PL2303 serial converter */
static int pl2303_open (struct usb_serial_port *port, struct file *filp);
static void pl2303_close (struct usb_serial_port *port, struct file *filp);
static void pl2303_set_termios (struct usb_serial_port *port,
				struct termios *old);
static int pl2303_ioctl (struct usb_serial_port *port, struct file *file,
			 unsigned int cmd, unsigned long arg);
static void pl2303_read_int_callback (struct urb *urb);
static void pl2303_read_bulk_callback (struct urb *urb);
static void pl2303_write_bulk_callback (struct urb *urb);
static int pl2303_write (struct usb_serial_port *port, int from_user,
			 const unsigned char *buf, int count);
static void pl2303_break_ctl(struct usb_serial_port *port,int break_state);
static int pl2303_startup (struct usb_serial *serial);
static void pl2303_shutdown (struct usb_serial *serial);


/* All of the device info needed for the PL2303 SIO serial converter */
static struct usb_serial_device_type pl2303_device = {
	name:			"PL-2303",
	id_table:		id_table,
	needs_interrupt_in:	DONT_CARE,		/* this device must have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			pl2303_open,
	close:			pl2303_close,
	write:			pl2303_write,
	ioctl:			pl2303_ioctl,
	break_ctl:		pl2303_break_ctl,
	set_termios:		pl2303_set_termios,
	read_bulk_callback:	pl2303_read_bulk_callback,
	read_int_callback:	pl2303_read_int_callback,
	write_bulk_callback:	pl2303_write_bulk_callback,
	startup:		pl2303_startup,
	shutdown:		pl2303_shutdown,
};

struct pl2303_private { 
	u8 line_control;
};


static int pl2303_startup (struct usb_serial *serial)
{
	struct pl2303_private *priv;
	int i;

	for (i = 0; i < serial->num_ports; ++i) {
		priv = kmalloc (sizeof (struct pl2303_private), GFP_KERNEL);
		if (!priv)
			return -ENOMEM;
		memset (priv, 0x00, sizeof (struct pl2303_private));
		serial->port[i].private = priv;
	}
	return 0;
}

static int set_control_lines (struct usb_device *dev, u8 value)
{
	int retval;
	
	retval = usb_control_msg (dev, usb_sndctrlpipe (dev, 0),
				  SET_CONTROL_REQUEST, SET_CONTROL_REQUEST_TYPE,
				  value, 0, NULL, 0, 100);
	dbg (__FUNCTION__" - value = %d, retval = %d", value, retval);
	return retval;
}

static int pl2303_write (struct usb_serial_port *port, int from_user,  const unsigned char *buf, int count)
{
	int result;

	dbg (__FUNCTION__ " - port %d, %d bytes", port->number, count);

	if (!port->tty) {
		err (__FUNCTION__ " - no tty???");
		return 0;
	}

	if (port->write_urb->status == -EINPROGRESS) {
		dbg (__FUNCTION__ " - already writing");
		return 0;
	}

	count = (count > port->bulk_out_size) ? port->bulk_out_size : count;
	if (from_user) {
		if (copy_from_user (port->write_urb->transfer_buffer, buf, count))
			return -EFAULT;
	} else {
		memcpy (port->write_urb->transfer_buffer, buf, count);
	}
	
	usb_serial_debug_data (__FILE__, __FUNCTION__, count, port->write_urb->transfer_buffer);

	port->write_urb->transfer_buffer_length = count;
	port->write_urb->dev = port->serial->dev;
	result = usb_submit_urb (port->write_urb);
	if (result)
		err(__FUNCTION__ " - failed submitting write urb, error %d", result);
	else
		result = count;

	return result;
}



static void pl2303_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct pl2303_private *priv;
	unsigned int cflag;
	unsigned char *buf;
	int baud;
	int i;

	dbg (__FUNCTION__ " -  port %d", port->number);

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

	buf = kmalloc (7, GFP_KERNEL);
	if (!buf) {
		err(__FUNCTION__ " - out of memory.");
		return;
	}
	memset (buf, 0x00, 0x07);
	
	i = usb_control_msg (serial->dev, usb_rcvctrlpipe (serial->dev, 0),
			     GET_LINE_REQUEST, GET_LINE_REQUEST_TYPE,
			     0, 0, buf, 7, 100);
	dbg ("0xa1:0x21:0:0  %d - %x %x %x %x %x %x %x", i,
	     buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);


	i = usb_control_msg (serial->dev, usb_sndctrlpipe (serial->dev, 0),
			     VENDOR_WRITE_REQUEST, VENDOR_WRITE_REQUEST_TYPE,
			     0, 1, NULL, 0, 100);

	dbg ("0x40:1:0:1  %d", i);

	if (cflag & CSIZE) {
		switch (cflag & CSIZE) {
			case CS5:	buf[6] = 5;	break;
			case CS6:	buf[6] = 6;	break;
			case CS7:	buf[6] = 7;	break;
			default:
			case CS8:	buf[6] = 8;	break;
		}
		dbg (__FUNCTION__ " - data bits = %d", buf[6]);
	}

	baud = 0;
	switch (cflag & CBAUD) {
		case B0:	baud = 0;	break;
		case B75:	baud = 75;	break;
		case B150:	baud = 150;	break;
		case B300:	baud = 300;	break;
		case B600:	baud = 600;	break;
		case B1200:	baud = 1200;	break;
		case B1800:	baud = 1800;	break;
		case B2400:	baud = 2400;	break;
		case B4800:	baud = 4800;	break;
		case B9600:	baud = 9600;	break;
		case B19200:	baud = 19200;	break;
		case B38400:	baud = 38400;	break;
		case B57600:	baud = 57600;	break;
		case B115200:	baud = 115200;	break;
		case B230400:	baud = 230400;	break;
		case B460800:	baud = 460800;	break;
		default:
			err ("pl2303 driver does not support the baudrate requested (fix it)");
			break;
	}
	dbg (__FUNCTION__ " - baud = %d", baud);
	if (baud) {
		buf[0] = baud & 0xff;
		buf[1] = (baud >> 8) & 0xff;
		buf[2] = (baud >> 16) & 0xff;
		buf[3] = (baud >> 24) & 0xff;
	}

	/* For reference buf[4]=0 is 1 stop bits */
	/* For reference buf[4]=1 is 1.5 stop bits */
	/* For reference buf[4]=2 is 2 stop bits */
	if (cflag & CSTOPB) {
		buf[4] = 2;
		dbg(__FUNCTION__ " - stop bits = 2");
	} else {
		buf[4] = 0;
		dbg(__FUNCTION__ " - stop bits = 1");
	}

	if (cflag & PARENB) {
		/* For reference buf[5]=0 is none parity */
		/* For reference buf[5]=1 is odd parity */
		/* For reference buf[5]=2 is even parity */
		/* For reference buf[5]=3 is mark parity */
		/* For reference buf[5]=4 is space parity */
		if (cflag & PARODD) {
			buf[5] = 1;
			dbg(__FUNCTION__ " - parity = odd");
		} else {
			buf[5] = 2;
			dbg(__FUNCTION__ " - parity = even");
		}
	} else {
		buf[5] = 0;
		dbg(__FUNCTION__ " - parity = none");
	}

	i = usb_control_msg (serial->dev, usb_sndctrlpipe (serial->dev, 0),
			     SET_LINE_REQUEST, SET_LINE_REQUEST_TYPE, 
			     0, 0, buf, 7, 100);
	dbg ("0x21:0x20:0:0  %d", i);

	if (cflag && CBAUD) {
		priv = port->private;
		if ((cflag && CBAUD) == B0)
			priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
		else
			priv->line_control |= (CONTROL_DTR | CONTROL_RTS);
		set_control_lines (serial->dev, priv->line_control);
	}
	
	buf[0] = buf[1] = buf[2] = buf[3] = buf[4] = buf[5] = buf[6] = 0;

	i = usb_control_msg (serial->dev, usb_rcvctrlpipe (serial->dev, 0),
			     GET_LINE_REQUEST, GET_LINE_REQUEST_TYPE,
			     0, 0, buf, 7, 100);
	dbg ("0xa1:0x21:0:0  %d - %x %x %x %x %x %x %x", i,
	     buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

	if (cflag & CRTSCTS) {
		i = usb_control_msg (serial->dev, usb_sndctrlpipe (serial->dev, 0),
				     VENDOR_WRITE_REQUEST_TYPE, VENDOR_WRITE_REQUEST_TYPE,
				     0x0, 0x41, NULL, 0, 100);
		dbg ("0x40:0x1:0x0:0x41  %d", i);
	}

	kfree (buf);
}       


static int pl2303_open (struct usb_serial_port *port, struct file *filp)
{
	struct termios tmp_termios;
	struct usb_serial *serial = port->serial;
	unsigned char buf[10];
	int result;

	if (port_paranoia_check (port, __FUNCTION__))
		return -ENODEV;
		
	dbg (__FUNCTION__ " -  port %d", port->number);

	down (&port->sem);

	++port->open_count;
	MOD_INC_USE_COUNT;

	if (!port->active) {
		port->active = 1;

#define FISH(a,b,c,d)									\
		result=usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev,0),	\
				       b, a, c, d, buf, 1, 100);			\
		dbg("0x%x:0x%x:0x%x:0x%x  %d - %x",a,b,c,d,result,buf[0]);

#define SOUP(a,b,c,d)									\
		result=usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev,0),	\
				       b, a, c, d, NULL, 0, 100);			\
		dbg("0x%x:0x%x:0x%x:0x%x  %d",a,b,c,d,result);

		FISH (VENDOR_READ_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8484, 0);
		SOUP (VENDOR_WRITE_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0404, 0);
		FISH (VENDOR_READ_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8484, 0);
		FISH (VENDOR_READ_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8383, 0);
		FISH (VENDOR_READ_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8484, 0);
		SOUP (VENDOR_WRITE_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0404, 1);
		FISH (VENDOR_READ_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8484, 0);
		FISH (VENDOR_READ_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8383, 0);
		SOUP (VENDOR_WRITE_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0, 1);
		SOUP (VENDOR_WRITE_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 1, 0xc0);
		SOUP (VENDOR_WRITE_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 2, 4);

		/* Setup termios */
		*(port->tty->termios) = tty_std_termios;
		port->tty->termios->c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;

		pl2303_set_termios (port, &tmp_termios);

		//FIXME: need to assert RTS and DTR if CRTSCTS off

		dbg (__FUNCTION__ " - submitting read urb");
		port->read_urb->dev = serial->dev;
		result = usb_submit_urb (port->read_urb);
		if (result) {
			err(__FUNCTION__ " - failed submitting read urb, error %d", result);
			up (&port->sem);
			pl2303_close (port, NULL);
			return -EPROTO;
		}

		dbg (__FUNCTION__ " - submitting interrupt urb");
		port->interrupt_in_urb->dev = serial->dev;
		result = usb_submit_urb (port->interrupt_in_urb);
		if (result) {
			err(__FUNCTION__ " - failed submitting interrupt urb, error %d", result);
			up (&port->sem);
			pl2303_close (port, NULL);
			return -EPROTO;
		}
	}
	up (&port->sem);
	return 0;
}


static void pl2303_close (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial;
	struct pl2303_private *priv;
	unsigned int c_cflag;
	int result;

	if (port_paranoia_check (port, __FUNCTION__))
		return;
	serial = get_usb_serial (port, __FUNCTION__);
	if (!serial)
		return;
	
	dbg (__FUNCTION__ " - port %d", port->number);

	down (&port->sem);

	--port->open_count;
	if (port->open_count <= 0) {
		if (serial->dev) {
			c_cflag = port->tty->termios->c_cflag;
			if (c_cflag & HUPCL) {
				/* drop DTR and RTS */
				priv = port->private;
				priv->line_control = 0;
				set_control_lines (port->serial->dev,
						   priv->line_control);
			}

			/* shutdown our urbs */
			dbg (__FUNCTION__ " - shutting down urbs");
			result = usb_unlink_urb (port->write_urb);
			if (result)
				dbg (__FUNCTION__ " - usb_unlink_urb "
				     "(write_urb) failed with reason: %d",
				     result);

			result = usb_unlink_urb (port->read_urb);
			if (result)
				dbg (__FUNCTION__ " - usb_unlink_urb "
				     "(read_urb) failed with reason: %d",
				     result);

			result = usb_unlink_urb (port->interrupt_in_urb);
			if (result)
				dbg (__FUNCTION__ " - usb_unlink_urb "
				     "(interrupt_in_urb) failed with reason: %d",
				     result);
		}

		port->active = 0;
		port->open_count = 0;
	}

	up (&port->sem);
	MOD_DEC_USE_COUNT;
}

static int set_modem_info (struct usb_serial_port *port, unsigned int cmd, unsigned int *value)
{
	struct pl2303_private *priv = port->private;
	unsigned int arg;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
		case TIOCMBIS:
			if (arg & TIOCM_RTS)
				priv->line_control |= CONTROL_RTS;
			if (arg & TIOCM_DTR)
				priv->line_control |= CONTROL_DTR;
			break;

		case TIOCMBIC:
			if (arg & TIOCM_RTS)
				priv->line_control &= ~CONTROL_RTS;
			if (arg & TIOCM_DTR)
				priv->line_control &= ~CONTROL_DTR;
			break;

		case TIOCMSET:
			/* turn off RTS and DTR and then only turn
			   on what was asked to */
			priv->line_control &= ~(CONTROL_RTS | CONTROL_DTR);
			priv->line_control |= ((arg & TIOCM_RTS) ? CONTROL_RTS : 0);
			priv->line_control |= ((arg & TIOCM_DTR) ? CONTROL_DTR : 0);
			break;
	}

	return set_control_lines (port->serial->dev, priv->line_control);
}

static int get_modem_info (struct usb_serial_port *port, unsigned int *value)
{
	struct pl2303_private *priv = port->private;
	unsigned int mcr = priv->line_control;
	unsigned int result;

	result = ((mcr & CONTROL_DTR)		? TIOCM_DTR : 0)
		  | ((mcr & CONTROL_RTS)	? TIOCM_RTS : 0);

	dbg (__FUNCTION__ " - result = %x", result);

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

static int pl2303_ioctl (struct usb_serial_port *port, struct file *file, unsigned int cmd, unsigned long arg)
{
	dbg (__FUNCTION__" (%d) cmd = 0x%04x", port->number, cmd);

	switch (cmd) {
		
		case TIOCMGET:
			dbg (__FUNCTION__" (%d) TIOCMGET", port->number);
			return get_modem_info (port, (unsigned int *)arg);

		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			dbg(__FUNCTION__" (%d) TIOCMSET/TIOCMBIC/TIOCMSET",  port->number);
			return set_modem_info(port, cmd, (unsigned int *) arg);

		default:
			dbg (__FUNCTION__" not supported = 0x%04x", cmd);
			break;
	}

	return -ENOIOCTLCMD;
}


static void pl2303_break_ctl (struct usb_serial_port *port, int break_state)
{
	struct usb_serial *serial = port->serial;
	u16 state;
	int result;

	dbg (__FUNCTION__ " - port %d", port->number);

	if (break_state == 0)
		state = BREAK_OFF;
	else
		state = BREAK_ON;
	dbg (__FUNCTION__" - turning break %s", state==BREAK_OFF ? "off" : "on");

	result = usb_control_msg (serial->dev, usb_rcvctrlpipe (serial->dev, 0),
				  BREAK_REQUEST, BREAK_REQUEST_TYPE, state, 
				  0, NULL, 0, 100);
	if (result)
		dbg (__FUNCTION__" - error sending break = %d", result);
}


static void pl2303_shutdown (struct usb_serial *serial)
{
	int i;

	dbg (__FUNCTION__);

	/* stop everything on all ports */
	for (i = 0; i < serial->num_ports; ++i)
		while (serial->port[i].open_count > 0) {
			pl2303_close (&serial->port[i], NULL);
			kfree (serial->port[i].private);
		}
}


static void pl2303_read_int_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *) urb->context;
	struct usb_serial *serial = get_usb_serial (port, __FUNCTION__);
	//unsigned char *data = urb->transfer_buffer;
	//int i;

//ints auto restart...

	if (!serial) {
		return;
	}

	if (urb->status) {
		urb->status = 0;
		return;
	}

	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, urb->transfer_buffer);
#if 0
//FIXME need to update state of terminal lines variable
#endif

	return;
}


static void pl2303_read_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *) urb->context;
	struct usb_serial *serial = get_usb_serial (port, __FUNCTION__);
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int i;
	int result;

	if (port_paranoia_check (port, __FUNCTION__))
		return;

	dbg(__FUNCTION__ " - port %d", port->number);

	if (!serial) {
		dbg(__FUNCTION__ " - bad serial pointer, exiting");
		return;
	}

	if (urb->status) {
		dbg (__FUNCTION__ " - urb->status = %d", urb->status);
		if (!port->active) {
			dbg (__FUNCTION__ " - port is closed, exiting.");
			return;
		}
		if (urb->status == -EPROTO) {
			/* PL2303 mysteriously fails with -EPROTO reschedule the read */
			dbg (__FUNCTION__ " - caught -EPROTO, resubmitting the urb");
			urb->status = 0;
			urb->dev = serial->dev;
			result = usb_submit_urb(urb);
			if (result)
				err(__FUNCTION__ " - failed resubmitting read urb, error %d", result);
			return;
		}
		dbg (__FUNCTION__ " - unable to handle the error, exiting.");
		return;
	}

	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, data);

	tty = port->tty;
	if (urb->actual_length) {
		for (i = 0; i < urb->actual_length; ++i) {
			if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(tty);
			}
			tty_insert_flip_char (tty, data[i], 0);
		}
		tty_flip_buffer_push (tty);
	}

	/* Schedule the next read _if_ we are still open */
	if (port->active) {
		urb->dev = serial->dev;
		result = usb_submit_urb(urb);
		if (result)
			err(__FUNCTION__ " - failed resubmitting read urb, error %d", result);
	}

	return;
}



static void pl2303_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *) urb->context;
	int result;

	if (port_paranoia_check (port, __FUNCTION__))
		return;
	
	dbg(__FUNCTION__ " - port %d", port->number);
	
	if (urb->status) {
		/* error in the urb, so we have to resubmit it */
		if (serial_paranoia_check (port->serial, __FUNCTION__)) {
			return;
		}
		dbg (__FUNCTION__ " - Overflow in write");
		dbg (__FUNCTION__ " - nonzero write bulk status received: %d", urb->status);
		port->write_urb->transfer_buffer_length = 1;
		port->write_urb->dev = port->serial->dev;
		result = usb_submit_urb (port->write_urb);
		if (result)
			err(__FUNCTION__ " - failed resubmitting write urb, error %d", result);

		return;
	}

	queue_task(&port->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

	return;
}


static int __init pl2303_init (void)
{
	usb_serial_register (&pl2303_device);
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}


static void __exit pl2303_exit (void)
{
	usb_serial_deregister (&pl2303_device);
}


module_init(pl2303_init);
module_exit(pl2303_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");

