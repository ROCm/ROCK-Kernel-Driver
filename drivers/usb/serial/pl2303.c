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
#define DRIVER_VERSION "v0.5"
#define DRIVER_DESC "Prolific PL2303 USB to serial adaptor driver"


#ifndef MIN
#define MIN(a,b)        ((a) < (b) ? (a) : (b))
#endif


#define	PL2303_LOCK(port,flags)					\
		do {						\
		spin_lock_irqsave(&((struct pl2303_private *)(port->private))->lock, flags);	\
		} while (0)

#define	PL2303_UNLOCK(port,flags)				\
		do {						\
		spin_unlock_irqrestore(&((struct pl2303_private *)(port->private))->lock, flags);	\
		} while (0)



static __devinitdata struct usb_device_id id_table [] = {
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID) },
	{ USB_DEVICE(ATEN_VENDOR_ID, ATEN_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table);

struct pl2303_private {
	spinlock_t	lock;
	unsigned char	*xmit_buf;
	int		xmit_head;
	int		xmit_tail;
	int		xmit_cnt;
};

/* function prototypes for a PL2303 serial converter */
static int pl2303_startup (struct usb_serial *serial);
static int pl2303_open (struct usb_serial_port *port, struct file *filp);
static void pl2303_close (struct usb_serial_port *port, struct file *filp);
static void pl2303_set_termios (struct usb_serial_port *port,
				struct termios *old);
static int pl2303_ioctl (struct usb_serial_port *port, struct file *file,
			 unsigned int cmd, unsigned long arg);
static void pl2303_throttle (struct usb_serial_port *port);
static void pl2303_unthrottle (struct usb_serial_port *port);
static void pl2303_read_int_callback (struct urb *urb);
static void pl2303_read_bulk_callback (struct urb *urb);
static void pl2303_write_bulk_callback (struct urb *urb);
static int pl2303_write (struct usb_serial_port *port, int from_user,
			 const unsigned char *buf, int count);
static int pl2303_write_room(struct usb_serial_port *port);
static int pl2303_chars_in_buffer(struct usb_serial_port *port);
static void pl2303_break_ctl(struct usb_serial_port *port,int break_state);
static void start_xmit (struct usb_serial_port *port);


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
	throttle:		pl2303_throttle,
	unthrottle:		pl2303_unthrottle,
	write:			pl2303_write,
	ioctl:			pl2303_ioctl,
	write_room:		pl2303_write_room,
	chars_in_buffer:	pl2303_chars_in_buffer,
	break_ctl:		pl2303_break_ctl,
	set_termios:		pl2303_set_termios,
	read_bulk_callback:	pl2303_read_bulk_callback,
	read_int_callback:	pl2303_read_int_callback,
	write_bulk_callback:	pl2303_write_bulk_callback,
	startup:		pl2303_startup,
};


#define WDR_TIMEOUT (HZ * 5 )   /* default urb timeout */

static unsigned char *tmp_buf;
static DECLARE_MUTEX (tmp_buf_sem);



static int
pl2303_write (struct usb_serial_port *port, int from_user,
	      const unsigned char *buf, int count)
{				/* pl2303_write */
	struct pl2303_private *info = (struct pl2303_private *)port->private;
	unsigned long flags;
	int c,ret=0;
	struct tty_struct *tty=port->tty;

	dbg ("pl2303_write port %d, %d bytes", port->number, count);

	if (!info) {
		return -ENODEV;
	}

	if (!tty || !info->xmit_buf || !tmp_buf) {
		return 0;
	}


	PL2303_LOCK(port,flags);


	if (from_user) {
		down(&tmp_buf_sem);
		while (1) {
			c = MIN(count, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
					   SERIAL_XMIT_SIZE - info->xmit_head));
			if (c <= 0)
				break;

			c -= copy_from_user(tmp_buf, buf, c);
			if (!c) {
				if (!ret) {
					ret = -EFAULT;
				}
				break;
			}
			c = MIN(c, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				       SERIAL_XMIT_SIZE - info->xmit_head));
			memcpy(info->xmit_buf + info->xmit_head, tmp_buf, c);
			info->xmit_head = ((info->xmit_head + c) & (SERIAL_XMIT_SIZE-1));
			info->xmit_cnt += c;
			buf += c;
			count -= c;
			ret += c;
		}
		up(&tmp_buf_sem);
	} else {
		while (1) {
			c = MIN(count, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1, 
					   SERIAL_XMIT_SIZE - info->xmit_head));
			if (c <= 0) {
				break;
			}
			memcpy(info->xmit_buf + info->xmit_head, buf, c);
			info->xmit_head = (info->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
			info->xmit_cnt += c;
			buf += c;
			count -= c;
			ret += c;
		}
	}
	PL2303_UNLOCK(port, flags);

	if (info->xmit_cnt && !tty->stopped && !tty->hw_stopped) {
		start_xmit(port);
	}
	return ret;
}

static int pl2303_write_room(struct usb_serial_port *port)
{
	struct pl2303_private *info = (struct pl2303_private *)port->private;
	int     ret;

	if (!info)
		return 0;

	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int pl2303_chars_in_buffer(struct usb_serial_port *port)
{
	struct pl2303_private *info = (struct pl2303_private *)port->private;

	if (!info)
		return 0;

	return info->xmit_cnt;
}

static void pl2303_throttle(struct usb_serial_port *port)
{
#if 0
	//struct usb_serial *serial = port->serial;
	struct tty_struct *tty=port->tty;
	unsigned long flags;


	char    buf[64];

	dbg("throttle %s: %d....", tty_name(tty, buf),
	    tty->ldisc.chars_in_buffer(tty));

//FIXME FIXME FIXME	
	if (I_IXOFF(tty))
		rs_send_xchar(tty, STOP_CHAR(tty));

	PL2303_LOCK(port,flags);
	//Should remove read request if one is present
	PL2303_UNLOCK(port,flags);
#endif
}

static void pl2303_unthrottle(struct usb_serial_port *port)
{
#if 0
	//struct usb_serial *serial = port->serial;
	struct tty_struct *tty=port->tty;
	unsigned long flags;


	char    buf[64];

	dbg("unthrottle %s: %d....", tty_name(tty, buf),
	    tty->ldisc.chars_in_buffer(tty));

	//FIXME FIXME FIXME FIXME FIXME
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			rs_send_xchar(tty, START_CHAR(tty));
	}

	PL2303_LOCK(port,flags);
	//Should add read request if one is not present
	PL2303_UNLOCK(fport,flags);
#endif
}


static void
pl2303_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{				/* pl2303_set_termios */
	struct usb_serial *serial = port->serial;
	unsigned int cflag = port->tty->termios->c_cflag;
	unsigned char buf[7] = { 0, 0, 0, 0, 0, 0, 0};
	int baud;
	int i;


	dbg ("pl2303_set_termios port %d", port->number);


	i = usb_control_msg (serial->dev, usb_rcvctrlpipe (serial->dev, 0),
			     0x21, 0xa1, 0, 0, buf, 7, 100);

	dbg ("0xa1:0x21:0:0  %d - %x %x %x %x %x %x %x", i,
	     buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);


	i = usb_control_msg (serial->dev, usb_sndctrlpipe (serial->dev, 0),
			     1, 0x40, 0, 1, NULL, 0, 100);

	dbg ("0x40:1:0:1  %d", i);



	if (cflag & CSIZE) {
		switch (cflag & CSIZE) {
			case CS6:
				buf[6] = 6;
				dbg ("Setting CS6");
				break;
			case CS7:
				buf[6] = 7;
				dbg ("Setting CS7");
				break;
			case CS8:
				buf[6] = 8;
				dbg ("Setting CS8");
				break;
			default:
				err ("CSIZE was set but not CS6-CS8");
		}
	}

	baud = 0;
	switch (cflag & CBAUD) {
		case B0:
			err ("Can't do B0 yet");  //FIXME
			break;
		case B300:
			baud = 300;
			break;
		case B600:
			baud = 600;
			break;
		case B1200:
			baud = 1200;
			break;
		case B2400:
			baud = 2400;
			break;
		case B4800:
			baud = 4800;
			break;
		case B9600:
			baud = 9600;
			break;
		case B19200:
			baud = 19200;
			break;
		case B38400:
			baud = 38400;
			break;
		case B57600:
			baud = 57600;
			break;
		case B115200:
			baud = 115200;
			break;
		default:
			dbg ("pl2303 driver does not support the baudrate requested (fix it)");
			break;
	}

	if (baud) {
		buf[0] = baud & 0xff;
		buf[1] = (baud >> 8) & 0xff;
		buf[2] = (baud >> 16) & 0xff;
		buf[3] = (baud >> 24) & 0xff;
	}


	/* For reference buf[4]=1 is 1.5 stop bits */

	if (cflag & CSTOPB) {
		buf[4] = 2;
	}


	if (cflag & PARENB) {
		/* For reference buf[5]=3 is mark parity */
		/* For reference buf[5]=4 is space parity */
		if (cflag & PARODD) {
			buf[5] = 1;
		} else {
			buf[5] = 2;
		}
	}

	i = usb_control_msg (serial->dev, usb_sndctrlpipe (serial->dev, 0),
			     0x20, 0x21, 0, 0, buf, 7, 100);

	dbg ("0x21:0x20:0:0  %d", i);

	i = usb_control_msg (serial->dev, usb_sndctrlpipe (serial->dev, 0),
			     0x22, 0x21, 1, 0, NULL, 0, 100);

	dbg ("0x21:0x22:1:0  %d", i);

	i = usb_control_msg (serial->dev, usb_sndctrlpipe (serial->dev, 0),
			     0x22, 0x21, 3, 0, NULL, 0, 100);

	dbg ("0x21:0x22:3:0  %d", i);

	buf[0] = buf[1] = buf[2] = buf[3] = buf[4] = buf[5] = buf[6] = 0;

	i = usb_control_msg (serial->dev, usb_rcvctrlpipe (serial->dev, 0),
			     0x21, 0xa1, 0, 0, buf, 7, 100);

	dbg ("0xa1:0x21:0:0  %d - %x %x %x %x %x %x %x", i,
	     buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

	if (cflag & CRTSCTS) {

		i = usb_control_msg (serial->dev, usb_sndctrlpipe (serial->dev, 0),
				     0x01, 0x40, 0x0, 0x41, NULL, 0, 100);

		dbg ("0x40:0x1:0x0:0x41  %d", i);

	}


	return;
}       


static int
pl2303_open (struct usb_serial_port *port, struct file *filp)
{				/* pl2303_open */
	struct termios tmp_termios;
	struct usb_serial *serial = port->serial;
	unsigned char buf[10];
	int i;

	dbg ("pl2303_open port %d", port->number);

	port->active++;

#define FISH(a,b,c,d) \
	i=usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev,0), \
		    b, a,c  , d, buf, 1, 100); \
	dbg("0x%x:0x%x:0x%x:0x%x  %d - %x",a,b,c,d,i,buf[0]);

#define SOUP(a,b,c,d) \
	i=usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev,0), \
		    b, a,c  , d, NULL, 0, 100); \
	dbg("0x%x:0x%x:0x%x:0x%x  %d",a,b,c,d,i);


	FISH (0xc0, 1, 0x8484, 0);
	SOUP (0x40, 1, 0x0404, 0);
	FISH (0xc0, 1, 0x8484, 0);
	FISH (0xc0, 1, 0x8383, 0);
	FISH (0xc0, 1, 0x8484, 0);
	SOUP (0x40, 1, 0x0404, 1);
	FISH (0xc0, 1, 0x8484, 0);
	FISH (0xc0, 1, 0x8383, 0);
	SOUP (0x40, 1, 0, 1);
	SOUP (0x40, 1, 1, 0xc0);
	SOUP (0x40, 1, 2, 4);

	/* Setup termios */

	if (port->active == 1) {
		*(port->tty->termios) = tty_std_termios;
		port->tty->termios->c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	}


	pl2303_set_termios (port, &tmp_termios);

	//FIXME: need to assert RTS and DTR if CRTSCTS off


	if (port->active == 1) {
		struct pl2303_private *info;
		unsigned long flags,page;
		int i;

		info = (struct pl2303_private *)kmalloc (sizeof(struct pl2303_private), GFP_KERNEL);
		if (info == NULL) {
			err(__FUNCTION__ " - out of memory");
			pl2303_close (port, NULL);
			return -ENOMEM;
		}
		spin_lock_init(&info->lock);
		port->private = info;


		page = get_free_page(GFP_KERNEL);
		if (!page) {
			pl2303_close (port, NULL);
			return -ENOMEM;
		}

		PL2303_LOCK(port,flags);

		info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

		if (tmp_buf)
			free_page(page);
		else
			tmp_buf	= (unsigned char *) page;

		PL2303_UNLOCK(port,flags);

		page = get_free_page(GFP_KERNEL);
		if (!page) {
			pl2303_close (port, NULL);
			return -ENOMEM;
		}

		PL2303_LOCK(port,flags);

		if (info->xmit_buf)
			free_page(page);
		else
			info->xmit_buf=(unsigned char *) page;

		PL2303_UNLOCK(port,flags);


		if ((i = usb_submit_urb (port->read_urb))) {
			err ("usb_submit_urb(read bulk 1) failed");
			dbg ("i=%d", i);
			pl2303_close (port, NULL);
			return -EPROTO;

		}

		if ((i = usb_submit_urb (port->interrupt_in_urb))) {
			err ("usb_submit_urb(interrupt ink) failed");
			dbg ("i=%d", i);
			pl2303_close (port, NULL);

			return -EPROTO;
		}
	}

	return(0);
}				/* pl2303_open */


static void
pl2303_close (struct usb_serial_port *port, struct file *filp)
{				/* pl2303_close */
	struct pl2303_private *info;
	unsigned int c_cflag = port->tty->termios->c_cflag;
	unsigned long flags;

	dbg ("pl2303_close port %d", port->number);

	/* shutdown our bulk reads and writes */
	if (port->active == 1) {

		if (c_cflag & HUPCL) {
			//FIXME: Do drop DTR
			//FIXME: Do drop RTS
		}

		usb_unlink_urb (port->write_urb);
		usb_unlink_urb (port->read_urb);
		usb_unlink_urb (port->interrupt_in_urb);

		info = (struct pl2303_private *)port->private;
		if (info) {
			PL2303_LOCK(port,flags);
			if (info->xmit_buf) {
				unsigned char * temp;
				temp = info->xmit_buf;
				info->xmit_buf = 0;
				free_page((unsigned long) temp);
			}
			PL2303_UNLOCK(port,flags);
		}

		//FIXME: tmp_buf memory leak


	}
	port->active--;
}				/* pl2303_close */


/* do some startup allocations not currently performed by usb_serial_probe() */
static int
pl2303_startup (struct usb_serial *serial)
{
	return(0);
}


static int
pl2303_ioctl (struct usb_serial_port *port, struct file *file,
	      unsigned int cmd, unsigned long arg)
{
// struct usb_serial *serial = port->serial;
// __u16 urb_value=0; /* Will hold the new flags */
// char buf[1];
// int  ret, mask;


	dbg ("pl2303_sio ioctl 0x%04x", cmd);

	/* Based on code from acm.c and others */
	switch (cmd) {
		
		case TIOCMGET:
			dbg ("TIOCMGET");


			return put_user (0, (unsigned long *) arg);
			break;
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return 0;

		default:
			/* This is not an error - turns out the higher layers will do 
			 *  some ioctls itself (see comment above)
			 */
			dbg ("pl2303_sio ioctl arg not supported - it was 0x%04x", cmd);
			return(-ENOIOCTLCMD);
			break;
	}
	dbg ("pl2303_ioctl returning 0");

	return 0;
}				/* pl2303_ioctl */


static void pl2303_break_ctl(struct usb_serial_port *port,int break_state)
{
//FIXME
}


static void
pl2303_read_int_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *) urb->context;
	struct usb_serial *serial = get_usb_serial (port, "pl2303_read_int_callback");
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


#if 0
//FIXME need to update state of terminal lines variable
	if (urb->actual_length) {
		printk (KERN_DEBUG __FILE__ ": INT data read - length = %d, data = ",
			urb->actual_length);
		for (i = 0; i < urb->actual_length; ++i) {
			printk ("%.2x ", data[i]);
		}
		printk ("\n");
	}
#endif

	return;
}

static void
pl2303_read_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *) urb->context;
	struct usb_serial *serial = get_usb_serial (port, "pl2303_read_bulk_callback");
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int i;

	if (!serial) {
		return;
	}

// PL2303 mysteriously fails with -EPROTO reschedule the read
	if (urb->status) {
		urb->status = 0;
		if (usb_submit_urb (urb))
			dbg ("failed resubmitting read bulk urb");
		return;
	}

	if (debug) {
		if (urb->actual_length) {
			printk (KERN_DEBUG __FILE__ ": BULK data read - length = %d, data = ",
				urb->actual_length);
			for (i = 0; i < urb->actual_length; ++i) {
				printk ("%.2x ", data[i]);
			}
			printk ("\n");
		}
	}

	tty = port->tty;
	if (urb->actual_length) {
		for (i = 0; i < urb->actual_length; ++i) {
			if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
				dbg ("ARGH ------------ Flip buffer overrun...");

				break;
			}
			tty_insert_flip_char (tty, data[i], 0);
		}
		tty_flip_buffer_push (tty);
	}


	/* Schedule the next read*/
	if (usb_submit_urb (urb))
		dbg ("failed submitting read bulk urb");

	return;
}



static void
pl2303_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *) urb->context;
	struct usb_serial *serial;
	struct tty_struct *tty = port->tty;

	dbg ("pl2303_write_bulk_callback");


	if (port_paranoia_check (port, "pl2303_write_bulk_callback")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "pl2303_write_bulk_callback")) {
		return;
	}


	if (urb->status) {
		dbg ("Overflow in write");
		dbg ("nonzero write bulk status received: %d", urb->status);
		//need to resubmit frame;

		port->write_urb->transfer_buffer_length = 1;

		//Resubmit ourselves

		if (usb_submit_urb (port->write_urb))
			err ("usb_submit_urb(write bulk) failed");

		return;
	}

	wake_up_interruptible (&port->write_wait);

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup) (tty);

	wake_up_interruptible (&tty->write_wait);

	start_xmit(port);

	return;
}



static void
start_xmit (struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct pl2303_private *info;
	unsigned long flags;

	serial = port->serial;
	info = (struct pl2303_private *)port->private;

	if (info) {
		PL2303_LOCK(port,flags);

		if (port->write_urb->status != -EINPROGRESS) {
			if (info->xmit_tail != info->xmit_head) {

				memcpy (port->write_urb->transfer_buffer, &info->xmit_buf[info->xmit_tail],1);
				info->xmit_cnt--;
				info->xmit_tail = (info->xmit_tail + 1) & (SERIAL_XMIT_SIZE - 1);


				port->write_urb->transfer_buffer_length = 1;


				if (usb_submit_urb (port->write_urb))
					err ("usb_submit_urb(write bulk) failed");


			}
		}

		PL2303_UNLOCK(port,flags);
	}
}


static int __init pl2303_init (void)
{
	usb_serial_register (&pl2303_device);
	info(DRIVER_VERSION " : " DRIVER_DESC);
	return 0;
}


static void __exit pl2303_exit (void)
{
	usb_serial_deregister (&pl2303_device);
}


module_init(pl2303_init);
module_exit(pl2303_exit);

MODULE_DESCRIPTION(DRIVER_DESC);

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");

