/*
 * USB Empeg empeg-car player driver
 *
 *	Copyright (C) 2000
 *	    Gary Brubaker (xavyer@ix.netcom.com)
 *
 *	Copyright (C) 1999, 2000
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License, as published by
 *	the Free Software Foundation, version 2.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 * (12/03/2000) gb
 *	Added port->tty->ldisc.set_termios(port->tty, NULL) to empeg_open()
 *      This notifies the tty driver that the termios have changed.
 * 
 * (11/13/2000) gb
 *	Moved tty->low_latency = 1 from empeg_read_bulk_callback() to empeg_open()
 *	(It only needs to be set once - Doh!)
 * 
 * (11/11/2000) gb
 *	Updated to work with id_table structure.
 * 
 * (11/04/2000) gb
 *	Forked this from visor.c, and hacked it up to work with an
 *	Empeg ltd. empeg-car player.  Constructive criticism welcomed.
 *	I would like to say, 'Thank You' to Greg Kroah-Hartman for the
 *	use of his code, and for his guidance, advice and patience. :)
 *	A 'Thank You' is in order for John Ripley of Empeg ltd for his
 *	advice, and patience too.
 * 
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#ifdef CONFIG_USB_SERIAL_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

#include "usb-serial.h"

#define EMPEG_VENDOR_ID                 0x084f
#define EMPEG_PRODUCT_ID                0x0001

#define MIN(a,b)		(((a)<(b))?(a):(b))

/* function prototypes for an empeg-car player */
static int  empeg_open		(struct usb_serial_port *port, struct file *filp);
static void empeg_close		(struct usb_serial_port *port, struct file *filp);
static int  empeg_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static void empeg_throttle	(struct usb_serial_port *port);
static void empeg_unthrottle	(struct usb_serial_port *port);
static int  empeg_startup	(struct usb_serial *serial);
static void empeg_shutdown	(struct usb_serial *serial);
static int  empeg_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void empeg_set_termios	(struct usb_serial_port *port, struct termios *old_termios);
static void empeg_write_bulk_callback	(struct urb *urb);
static void empeg_read_bulk_callback	(struct urb *urb);

static __devinitdata struct usb_device_id id_table [] = {
	{ USB_DEVICE(EMPEG_VENDOR_ID, EMPEG_PRODUCT_ID) },
        { }                                     /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table);

struct usb_serial_device_type empeg_device = {
	name:			"Empeg",
	id_table:		id_table,
	needs_interrupt_in:	MUST_HAVE_NOT,	/* must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,	/* must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,	/* must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			empeg_open,
	close:			empeg_close,
	throttle:		empeg_throttle,
	unthrottle:		empeg_unthrottle,
	startup:		empeg_startup,
	shutdown:		empeg_shutdown,
	ioctl:			empeg_ioctl,
	set_termios:		empeg_set_termios,
	write:			empeg_write,
	write_bulk_callback:	empeg_write_bulk_callback,
	read_bulk_callback:	empeg_read_bulk_callback,
};

#define NUM_URBS			16
#define URB_TRANSFER_BUFFER_SIZE	4096

static struct urb	*write_urb_pool[NUM_URBS];
static spinlock_t	write_urb_pool_lock;
static int		bytes_in;
static int		bytes_out;

/******************************************************************************
 * Empeg specific driver functions
 ******************************************************************************/
static int empeg_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	unsigned long flags;
	int result;

	if (port_paranoia_check (port, __FUNCTION__))
		return -ENODEV;

	dbg(__FUNCTION__ " - port %d", port->number);

	spin_lock_irqsave (&port->port_lock, flags);

	++port->open_count;
	MOD_INC_USE_COUNT;

	/* gb - 2000/11/05
	 *
	 * personally, I think these termios should be set in
	 * empeg_startup(), but it appears doing so leads to one
	 * of those chicken/egg problems. :)
	 *
	 */
	port->tty->termios->c_iflag
		&= ~(IGNBRK
		| BRKINT
		| PARMRK
		| ISTRIP
		| INLCR
		| IGNCR
		| ICRNL
		| IXON);

	port->tty->termios->c_oflag
		&= ~OPOST;

	port->tty->termios->c_lflag
		&= ~(ECHO
		| ECHONL
		| ICANON
		| ISIG
		| IEXTEN);

	port->tty->termios->c_cflag
		&= ~(CSIZE
		| PARENB);

	port->tty->termios->c_cflag
		|= CS8;

	/* gb - 2000/12/03
	 *
	 * Contributed by Borislav Deianov
	 *
	 * Notify the tty driver that the termios have changed!!
	 *
	 */
        port->tty->ldisc.set_termios(port->tty, NULL);

	/* gb - 2000/11/05
	 *
	 * force low_latency on
	 *
	 * The tty_flip_buffer_push()'s in empeg_read_bulk_callback() will actually
	 * force the data through if low_latency is set.  Otherwise the pushes are
	 * scheduled; this is bad as it opens up the possibility of dropping bytes
	 * on the floor.  We are trying to sustain high data transfer rates; and
	 * don't want to drop bytes on the floor.
	 * Moral: use low_latency - drop no bytes - life is good. :)
	 *
	 */
	port->tty->low_latency = 1;

	if (!port->active) {
		port->active = 1;
		bytes_in = 0;
		bytes_out = 0;

		/* Start reading from the device */
		FILL_BULK_URB(
			port->read_urb,
			serial->dev, 
			usb_rcvbulkpipe(serial->dev,
				port->bulk_in_endpointAddress),
			port->read_urb->transfer_buffer,
			port->read_urb->transfer_buffer_length,
			empeg_read_bulk_callback,
			port);

		port->read_urb->transfer_flags |= USB_QUEUE_BULK;

		result = usb_submit_urb(port->read_urb);

		if (result)
			err(__FUNCTION__ " - failed submitting read urb, error %d", result);

	}

	spin_unlock_irqrestore (&port->port_lock, flags);

	return 0;
}


static void empeg_close (struct usb_serial_port *port, struct file * filp)
{
	struct usb_serial *serial;
	unsigned char *transfer_buffer;
	unsigned long flags;

	if (port_paranoia_check (port, __FUNCTION__))
		return;

	dbg(__FUNCTION__ " - port %d", port->number);

	serial = get_usb_serial (port, __FUNCTION__);
	if (!serial)
		return;

	spin_lock_irqsave (&port->port_lock, flags);

	--port->open_count;
	MOD_DEC_USE_COUNT;

	if (port->open_count <= 0) {
		transfer_buffer =  kmalloc (0x12, GFP_KERNEL);

		if (!transfer_buffer) {
			err(__FUNCTION__ " - kmalloc(%d) failed.", 0x12);
		} else {
			kfree (transfer_buffer);
		}

		/* shutdown our bulk read */
		usb_unlink_urb (port->read_urb);
		port->active = 0;
		port->open_count = 0;
	}

	spin_unlock_irqrestore (&port->port_lock, flags);

	/* Uncomment the following line if you want to see some statistics in your syslog */
	/* info ("Bytes In = %d  Bytes Out = %d", bytes_in, bytes_out); */

}


static int empeg_write (struct usb_serial_port *port, int from_user, const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	struct urb *urb;
	const unsigned char *current_position = buf;
	unsigned long flags;
	int status;
	int i;
	int bytes_sent = 0;
	int transfer_size;

	dbg(__FUNCTION__ " - port %d", port->number);

	usb_serial_debug_data (__FILE__, __FUNCTION__, count, buf);

	while (count > 0) {

		/* try to find a free urb in our list of them */
		urb = NULL;

		spin_lock_irqsave (&write_urb_pool_lock, flags);

		for (i = 0; i < NUM_URBS; ++i) {
			if (write_urb_pool[i]->status != -EINPROGRESS) {
				urb = write_urb_pool[i];
				break;
			}
		}

		spin_unlock_irqrestore (&write_urb_pool_lock, flags);

		if (urb == NULL) {
			dbg (__FUNCTION__ " - no more free urbs");
			goto exit;
		}

		if (urb->transfer_buffer == NULL) {
			urb->transfer_buffer = kmalloc (URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);
			if (urb->transfer_buffer == NULL) {
				err(__FUNCTION__" no more kernel memory...");
				goto exit;
			}
		}

		transfer_size = MIN (count, URB_TRANSFER_BUFFER_SIZE);

		if (from_user) {
			copy_from_user (urb->transfer_buffer, current_position, transfer_size);
		} else {
			memcpy (urb->transfer_buffer, current_position, transfer_size);
		}

		count = (count > port->bulk_out_size) ? port->bulk_out_size : count;

		/* build up our urb */
		FILL_BULK_URB (
			urb,
			serial->dev,
			usb_sndbulkpipe(serial->dev,
				port->bulk_out_endpointAddress), 
			urb->transfer_buffer,
			transfer_size,
			empeg_write_bulk_callback,
			port);

		urb->transfer_flags |= USB_QUEUE_BULK;

		/* send it down the pipe */
		status = usb_submit_urb(urb);
		if (status)
			dbg(__FUNCTION__ " - usb_submit_urb(write bulk) failed with status = %d", status);

		current_position += transfer_size;
		bytes_sent += transfer_size;
		count -= transfer_size;
		bytes_out += transfer_size;

	}

exit:
	return bytes_sent;

} 


static void empeg_write_bulk_callback (struct urb *urb)
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


static void empeg_read_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
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
		dbg(__FUNCTION__ " - nonzero read bulk status received: %d", urb->status);
		return;
	}

	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, data);

	tty = port->tty;

	if (urb->actual_length) {
		for (i = 0; i < urb->actual_length ; ++i) {
			/* gb - 2000/11/13
			 * If we insert too many characters we'll overflow the buffer.
			 * This means we'll lose bytes - Decidedly bad.
			 */
			if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(tty);
				}
			/* gb - 2000/11/13
			 * This doesn't push the data through unless tty->low_latency is set.
			 */
			tty_insert_flip_char(tty, data[i], 0);
		}
		/* gb - 2000/11/13
		 * Goes straight through instead of scheduling - if tty->low_latency is set.
		 */
		tty_flip_buffer_push(tty);
		bytes_in += urb->actual_length;
	}

	/* Continue trying to always read  */
	FILL_BULK_URB(
		port->read_urb,
		serial->dev, 
		usb_rcvbulkpipe(serial->dev,
			port->bulk_in_endpointAddress),
		port->read_urb->transfer_buffer,
		port->read_urb->transfer_buffer_length,
		empeg_read_bulk_callback,
		port);

	port->read_urb->transfer_flags |= USB_QUEUE_BULK;

	result = usb_submit_urb(port->read_urb);

	if (result)
		err(__FUNCTION__ " - failed resubmitting read urb, error %d", result);

	return;

}


static void empeg_throttle (struct usb_serial_port *port)
{
	unsigned long flags;

	dbg(__FUNCTION__ " - port %d", port->number);

	spin_lock_irqsave (&port->port_lock, flags);

	usb_unlink_urb (port->read_urb);

	spin_unlock_irqrestore (&port->port_lock, flags);

	return;

}


static void empeg_unthrottle (struct usb_serial_port *port)
{
	unsigned long flags;
	int result;

	dbg(__FUNCTION__ " - port %d", port->number);

	spin_lock_irqsave (&port->port_lock, flags);

	port->read_urb->dev = port->serial->dev;

	result = usb_submit_urb(port->read_urb);

	if (result)
		err(__FUNCTION__ " - failed submitting read urb, error %d", result);

	spin_unlock_irqrestore (&port->port_lock, flags);

	return;

}


static int  empeg_startup (struct usb_serial *serial)
{

	dbg(__FUNCTION__);

	dbg(__FUNCTION__ " - Set config to 1");
	usb_set_configuration (serial->dev, 1);

	/* continue on with initialization */
	return 0;

}


static void empeg_shutdown (struct usb_serial *serial)
{
	int i;

	dbg (__FUNCTION__);

	/* stop reads and writes on all ports */
	for (i=0; i < serial->num_ports; ++i) {
		while (serial->port[i].open_count > 0) {
			empeg_close (&serial->port[i], NULL);
		}
	}

}


static int empeg_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	dbg(__FUNCTION__ " - port %d, cmd 0x%.4x", port->number, cmd);

	return -ENOIOCTLCMD;
}


/* This function is all nice and good, but we don't change anything based on it :) */
static void empeg_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	unsigned int cflag = port->tty->termios->c_cflag;

	dbg(__FUNCTION__ " - port %d", port->number);

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(port->tty->termios->c_iflag) == RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg(__FUNCTION__ " - nothing to change...");
			return;
		}
	}

	if ((!port->tty) || (!port->tty->termios)) {
		dbg(__FUNCTION__" - no tty structures");
		return;
	}

	/* get the byte size */
	switch (cflag & CSIZE) {
		case CS5:	dbg(__FUNCTION__ " - data bits = 5");   break;
		case CS6:	dbg(__FUNCTION__ " - data bits = 6");   break;
		case CS7:	dbg(__FUNCTION__ " - data bits = 7");   break;
		default:
		case CS8:	dbg(__FUNCTION__ " - data bits = 8");   break;
	}

	/* determine the parity */
	if (cflag & PARENB)
		if (cflag & PARODD)
			dbg(__FUNCTION__ " - parity = odd");
		else
			dbg(__FUNCTION__ " - parity = even");
	else
		dbg(__FUNCTION__ " - parity = none");

	/* figure out the stop bits requested */
	if (cflag & CSTOPB)
		dbg(__FUNCTION__ " - stop bits = 2");
	else
		dbg(__FUNCTION__ " - stop bits = 1");

	/* figure out the flow control settings */
	if (cflag & CRTSCTS)
		dbg(__FUNCTION__ " - RTS/CTS is enabled");
	else
		dbg(__FUNCTION__ " - RTS/CTS is disabled");

	/* determine software flow control */
	if (I_IXOFF(port->tty))
		dbg(__FUNCTION__ " - XON/XOFF is enabled, XON = %2x, XOFF = %2x", START_CHAR(port->tty), STOP_CHAR(port->tty));
	else
		dbg(__FUNCTION__ " - XON/XOFF is disabled");

	/* get the baud rate wanted */
	dbg(__FUNCTION__ " - baud rate = %d", tty_get_baud_rate(port->tty));

	return;

}


static int __init empeg_init (void)
{
	struct urb *urb;
	int i;

	usb_serial_register (&empeg_device);

	/* create our write urb pool and transfer buffers */ 
	spin_lock_init (&write_urb_pool_lock);
	for (i = 0; i < NUM_URBS; ++i) {
		urb = usb_alloc_urb(0);
		write_urb_pool[i] = urb;
		if (urb == NULL) {
			err("No more urbs???");
			continue;
		}

		urb->transfer_buffer = NULL;
		urb->transfer_buffer = kmalloc (URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);
		if (!urb->transfer_buffer) {
			err (__FUNCTION__ " - out of memory for urb buffers.");
			continue;
		}
	}

	return 0;

}


static void __exit empeg_exit (void)
{
	int i;
	unsigned long flags;

	usb_serial_deregister (&empeg_device);

	spin_lock_irqsave (&write_urb_pool_lock, flags);

	for (i = 0; i < NUM_URBS; ++i) {
		if (write_urb_pool[i]) {
			/* FIXME - uncomment the following usb_unlink_urb call when
			 * the host controllers get fixed to set urb->dev = NULL after
			 * the urb is finished.  Otherwise this call oopses. */
			/* usb_unlink_urb(write_urb_pool[i]); */
			if (write_urb_pool[i]->transfer_buffer)
				kfree(write_urb_pool[i]->transfer_buffer);
			usb_free_urb (write_urb_pool[i]);
		}
	}

	spin_unlock_irqrestore (&write_urb_pool_lock, flags);

}


module_init(empeg_init);
module_exit(empeg_exit);

MODULE_AUTHOR("Gary Brubaker <xavyer@ix.netcom.com>");
MODULE_DESCRIPTION("USB Empeg Mark I/II Driver");
