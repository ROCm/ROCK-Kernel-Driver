/*
 * USB HandSpring Visor driver
 *
 *	Copyright (C) 1999, 2000
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 * 
 * (11/12/2000) gkh
 *	Fixed bug with data being dropped on the floor by forcing tty->low_latency
 *	to be on.  Hopefully this fixes the OHCI issue!
 *
 * (11/01/2000) Adam J. Richter
 *	usb_device_id table support
 * 
 * (10/05/2000) gkh
 *	Fixed bug with urb->dev not being set properly, now that the usb
 *	core needs it.
 * 
 * (09/11/2000) gkh
 *	Got rid of always calling kmalloc for every urb we wrote out to the
 *	device.
 *	Added visor_read_callback so we can keep track of bytes in and out for
 *	those people who like to know the speed of their device.
 *	Removed DEBUG #ifdefs with call to usb_serial_debug_data
 *
 * (09/06/2000) gkh
 *	Fixed oops in visor_exit.  Need to uncomment usb_unlink_urb call _after_
 *	the host controller drivers set urb->dev = NULL when the urb is finished.
 *
 * (08/28/2000) gkh
 *	Added locks for SMP safeness.
 *
 * (08/08/2000) gkh
 *	Fixed endian problem in visor_startup.
 *	Fixed MOD_INC and MOD_DEC logic and the ability to open a port more 
 *	than once.
 * 
 * (07/23/2000) gkh
 *	Added pool of write urbs to speed up transfers to the visor.
 * 
 * (07/19/2000) gkh
 *	Added module_init and module_exit functions to handle the fact that this
 *	driver is a loadable module now.
 *
 * (07/03/2000) gkh
 *	Added visor_set_ioctl and visor_set_termios functions (they don't do much
 *	of anything, but are good for debugging.)
 * 
 * (06/25/2000) gkh
 *	Fixed bug in visor_unthrottle that should help with the disconnect in PPP
 *	bug that people have been reporting.
 *
 * (06/23/2000) gkh
 *	Cleaned up debugging statements in a quest to find UHCI timeout bug.
 *
 * (04/27/2000) Ryan VanderBijl
 * 	Fixed memory leak in visor_close
 *
 * (03/26/2000) gkh
 *	Split driver up into device specific pieces.
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

#include "visor.h"

#define MIN(a,b)                (((a)<(b))?(a):(b))

/* function prototypes for a handspring visor */
static int  visor_open		(struct usb_serial_port *port, struct file *filp);
static void visor_close		(struct usb_serial_port *port, struct file *filp);
static int  visor_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static void visor_throttle	(struct usb_serial_port *port);
static void visor_unthrottle	(struct usb_serial_port *port);
static int  visor_startup	(struct usb_serial *serial);
static void visor_shutdown	(struct usb_serial *serial);
static int  visor_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void visor_set_termios	(struct usb_serial_port *port, struct termios *old_termios);
static void visor_write_bulk_callback	(struct urb *urb);
static void visor_read_bulk_callback	(struct urb *urb);


static __devinitdata struct usb_device_id id_table [] = {
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_VISOR_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table);



/* All of the device info needed for the Handspring Visor */
struct usb_serial_device_type handspring_device = {
	name:			"Handspring Visor",
	id_table:		id_table,
	needs_interrupt_in:	MUST_HAVE_NOT,		/* this device must not have an interrupt in endpoint */
	needs_bulk_in:		MUST_HAVE,		/* this device must have a bulk in endpoint */
	needs_bulk_out:		MUST_HAVE,		/* this device must have a bulk out endpoint */
	num_interrupt_in:	0,
	num_bulk_in:		2,
	num_bulk_out:		2,
	num_ports:		2,
	open:			visor_open,
	close:			visor_close,
	throttle:		visor_throttle,
	unthrottle:		visor_unthrottle,
	startup:		visor_startup,
	shutdown:		visor_shutdown,
	ioctl:			visor_ioctl,
	set_termios:		visor_set_termios,
	write:			visor_write,
	write_bulk_callback:	visor_write_bulk_callback,
	read_bulk_callback:	visor_read_bulk_callback,
};


#define NUM_URBS			24
#define URB_TRANSFER_BUFFER_SIZE	64
static struct urb	*write_urb_pool[NUM_URBS];
static spinlock_t	write_urb_pool_lock;
static int		bytes_in;
static int		bytes_out;


/******************************************************************************
 * Handspring Visor specific driver functions
 ******************************************************************************/
static int visor_open (struct usb_serial_port *port, struct file *filp)
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
	
	if (!port->active) {
		port->active = 1;
		bytes_in = 0;
		bytes_out = 0;

		/* force low_latency on so that our tty_push actually forces the data through, 
		   otherwise it is scheduled, and with high data rates (like with OHCI) data
		   can get lost. */
		port->tty->low_latency = 1;
		
		/* Start reading from the device */
		FILL_BULK_URB(port->read_urb, serial->dev, 
			      usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
			      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
			      visor_read_bulk_callback, port);
		result = usb_submit_urb(port->read_urb);
		if (result)
			err(__FUNCTION__ " - failed submitting read urb, error %d", result);
	}
	
	spin_unlock_irqrestore (&port->port_lock, flags);
	
	return 0;
}


static void visor_close (struct usb_serial_port *port, struct file * filp)
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
			/* send a shutdown message to the device */
			usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0), VISOR_CLOSE_NOTIFICATION,
					0xc2, 0x0000, 0x0000, transfer_buffer, 0x12, 300);
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


static int visor_write (struct usb_serial_port *port, int from_user, const unsigned char *buf, int count)
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
		if (from_user)
			copy_from_user (urb->transfer_buffer, current_position, transfer_size);
		else
			memcpy (urb->transfer_buffer, current_position, transfer_size);
		
		count = (count > port->bulk_out_size) ? port->bulk_out_size : count;

		/* build up our urb */
		FILL_BULK_URB (urb, serial->dev, usb_sndbulkpipe(serial->dev, port->bulk_out_endpointAddress), 
				urb->transfer_buffer, transfer_size, visor_write_bulk_callback, port);
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


static void visor_write_bulk_callback (struct urb *urb)
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


static void visor_read_bulk_callback (struct urb *urb)
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
			/* if we insert more than TTY_FLIPBUF_SIZE characters, we drop them. */
			if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(tty);
			}
			/* this doesn't actually push the data through unless tty->low_latency is set */
			tty_insert_flip_char(tty, data[i], 0);
		}
		tty_flip_buffer_push(tty);
		bytes_in += urb->actual_length;
	}

	/* Continue trying to always read  */
	FILL_BULK_URB(port->read_urb, serial->dev, 
		      usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
		      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
		      visor_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb);
	if (result)
		err(__FUNCTION__ " - failed resubmitting read urb, error %d", result);
	return;
}


static void visor_throttle (struct usb_serial_port *port)
{
	unsigned long flags;

	dbg(__FUNCTION__ " - port %d", port->number);

	spin_lock_irqsave (&port->port_lock, flags);

	usb_unlink_urb (port->read_urb);

	spin_unlock_irqrestore (&port->port_lock, flags);

	return;
}


static void visor_unthrottle (struct usb_serial_port *port)
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


static int  visor_startup (struct usb_serial *serial)
{
	int response;
	int i;
	unsigned char *transfer_buffer =  kmalloc (256, GFP_KERNEL);

	if (!transfer_buffer) {
		err(__FUNCTION__ " - kmalloc(%d) failed.", 256);
		return -ENOMEM;
	}

	dbg(__FUNCTION__);

	dbg(__FUNCTION__ " - Set config to 1");
	usb_set_configuration (serial->dev, 1);

	/* send a get connection info request */
	response = usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0), VISOR_GET_CONNECTION_INFORMATION,
					0xc2, 0x0000, 0x0000, transfer_buffer, 0x12, 300);
	if (response < 0) {
		err(__FUNCTION__ " - error getting connection information");
	} else {
		struct visor_connection_info *connection_info = (struct visor_connection_info *)transfer_buffer;
		char *string;

		le16_to_cpus(&connection_info->num_ports);
		info("%s: Number of ports: %d", serial->type->name, connection_info->num_ports);
		for (i = 0; i < connection_info->num_ports; ++i) {
			switch (connection_info->connections[i].port_function_id) {
				case VISOR_FUNCTION_GENERIC:
					string = "Generic";
					break;
				case VISOR_FUNCTION_DEBUGGER:
					string = "Debugger";
					break;
				case VISOR_FUNCTION_HOTSYNC:
					string = "HotSync";
					break;
				case VISOR_FUNCTION_CONSOLE:
					string = "Console";
					break;
				case VISOR_FUNCTION_REMOTE_FILE_SYS:
					string = "Remote File System";
					break;
				default:
					string = "unknown";
					break;	
			}
			info("%s: port %d, is for %s use and is bound to ttyUSB%d", serial->type->name, connection_info->connections[i].port, string, serial->minor + i);
		}
	}

	/* ask for the number of bytes available, but ignore the response as it is broken */
	response = usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0), VISOR_REQUEST_BYTES_AVAILABLE,
					0xc2, 0x0000, 0x0005, transfer_buffer, 0x02, 300);
	if (response < 0) {
		err(__FUNCTION__ " - error getting bytes available request");
	}

	kfree (transfer_buffer);

	/* continue on with initialization */
	return 0;
}


static void visor_shutdown (struct usb_serial *serial)
{
	int i;

	dbg (__FUNCTION__);

	/* stop reads and writes on all ports */
	for (i=0; i < serial->num_ports; ++i) {
		while (serial->port[i].open_count > 0) {
			visor_close (&serial->port[i], NULL);
		}
	}
}


static int visor_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	dbg(__FUNCTION__ " - port %d, cmd 0x%.4x", port->number, cmd);

	return -ENOIOCTLCMD;
}


/* This function is all nice and good, but we don't change anything based on it :) */
static void visor_set_termios (struct usb_serial_port *port, struct termios *old_termios)
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


static int __init visor_init (void)
{
	struct urb *urb;
	int i;

	usb_serial_register (&handspring_device);
	
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


static void __exit visor_exit (void)
{
	int i;
	unsigned long flags;

	usb_serial_deregister (&handspring_device);

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


module_init(visor_init);
module_exit(visor_exit);

MODULE_AUTHOR("Greg Kroah-Hartman <greg@kroah.com>");
MODULE_DESCRIPTION("USB HandSpring Visor driver");
