/*
 * USB FTDI SIO driver
 *
 * 	Copyright (C) 1999, 2000
 * 	    Greg Kroah-Hartman (greg@kroah.com)
 *          Bill Ryder (bryder@sgi.com)
 *
 * 	This program is free software; you can redistribute it and/or modify
 * 	it under the terms of the GNU General Public License as published by
 * 	the Free Software Foundation; either version 2 of the License, or
 * 	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * See http://reality.sgi.com/bryder_wellington/ftdi_sio for upto date testing info
 *     and extra documentation
 *       
 * (12/3/2000) Bill Ryder
 *     Added support for 8U232AM device.
 *     Moved PID and VIDs into header file only.
 *     Turned on low-latency for the tty (device will do high baudrates)
 *     Added shutdown routine to close files when device removed.
 *     More debug and error message cleanups.
 *     
 *
 * (11/13/2000) Bill Ryder
 *     Added spinlock protected open code and close code.
 *     Multiple opens work (sort of - see webpage mentioned above).
 *     Cleaned up comments. Removed multiple PID/VID definitions.
 *     Factorised cts/dtr code
 *     Made use of __FUNCTION__ in dbg's
 *      
 * (11/01/2000) Adam J. Richter
 *	usb_device_id table support
 * 
 * (10/05/2000) gkh
 *	Fixed bug with urb->dev not being set properly, now that the usb
 *	core needs it.
 * 
 * (09/11/2000) gkh
 *	Removed DEBUG #ifdefs with call to usb_serial_debug_data
 *
 * (07/19/2000) gkh
 *	Added module_init and module_exit functions to handle the fact that this
 *	driver is a loadable module now.
 *
 * (04/04/2000) Bill Ryder 
 *      Fixed bugs in TCGET/TCSET ioctls (by removing them - they are 
 *        handled elsewhere in the tty io driver chain).
 *
 * (03/30/2000) Bill Ryder 
 *      Implemented lots of ioctls
 * 	Fixed a race condition in write
 * 	Changed some dbg's to errs
 *
 * (03/26/2000) gkh
 * 	Split driver up into device specific pieces.
 *
 */

/* Bill Ryder - bryder@sgi.com - wrote the FTDI_SIO implementation */
/* Thanx to FTDI for so kindly providing details of the protocol required */
/*   to talk to the device */
/* Thanx to gkh and the rest of the usb dev group for all code I have assimilated :-) */


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

#include "ftdi_sio.h"


static __devinitdata struct usb_device_id id_table_sio [] = {
	{ USB_DEVICE(FTDI_VID, FTDI_SIO_PID) },
	{ }						/* Terminating entry */
};

/* THe 8U232AM has the same API as the sio - but it can support MUCH 
   higher baudrates (921600 at 48MHz/230400 at 12MHz 
   so .. it's baudrate setting codes are different */

   
static __devinitdata struct usb_device_id id_table_8U232AM [] = {
	{ USB_DEVICE(FTDI_VID, FTDI_8U232AM_PID) },
	{ }						/* Terminating entry */
};


static __devinitdata struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(FTDI_VID, FTDI_SIO_PID) },
	{ USB_DEVICE(FTDI_VID, FTDI_8U232AM_PID) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);


struct ftdi_private {
	ftdi_type_t ftdi_type;
	char last_status_byte; /* device sends this every 40ms when open */
	
	
};
/* function prototypes for a FTDI serial converter */
static int  ftdi_sio_startup		(struct usb_serial *serial);
static int  ftdi_8U232AM_startup	(struct usb_serial *serial);
static void ftdi_sio_shutdown		(struct usb_serial *serial);
static int  ftdi_sio_open		(struct usb_serial_port *port, struct file *filp);
static void ftdi_sio_close		(struct usb_serial_port *port, struct file *filp);
static int  ftdi_sio_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static void ftdi_sio_write_bulk_callback (struct urb *urb);
static void ftdi_sio_read_bulk_callback	(struct urb *urb);
static void ftdi_sio_set_termios	(struct usb_serial_port *port, struct termios * old);
static int  ftdi_sio_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);

/* Should rename most ftdi_sio's to ftdi_ now since there are two devices 
   which share common code */ 

struct usb_serial_device_type ftdi_sio_device = {
	name:			"FTDI SIO",
	id_table:		id_table_sio,
	needs_interrupt_in:	MUST_HAVE_NOT,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	0,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			ftdi_sio_open,
	close:			ftdi_sio_close,
	write:			ftdi_sio_write,
	read_bulk_callback:	ftdi_sio_read_bulk_callback,
	write_bulk_callback:	ftdi_sio_write_bulk_callback,
	ioctl:			ftdi_sio_ioctl,
	set_termios:		ftdi_sio_set_termios,
	startup:		ftdi_sio_startup,
        shutdown:               ftdi_sio_shutdown,
};

struct usb_serial_device_type ftdi_8U232AM_device = {
	name:			"FTDI 8U232AM",
	id_table:		id_table_8U232AM,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	0,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			ftdi_sio_open,
	close:			ftdi_sio_close,
	write:			ftdi_sio_write,
	read_bulk_callback:	ftdi_sio_read_bulk_callback,
	write_bulk_callback:	ftdi_sio_write_bulk_callback,
	ioctl:			ftdi_sio_ioctl,
	set_termios:		ftdi_sio_set_termios,
	startup:		ftdi_8U232AM_startup,
        shutdown:               ftdi_sio_shutdown,
};


/*
 * ***************************************************************************
 * FTDI SIO Serial Converter specific driver functions
 * ***************************************************************************
 */

#define WDR_TIMEOUT (HZ * 5 ) /* default urb timeout */

/* utility functions to set and unset dtr and rts */
#define HIGH 1
#define LOW 0
static int set_rts(struct usb_device *dev, 
		   unsigned int pipe,
		   int high_or_low)
{
	static char buf[1];
	unsigned ftdi_high_or_low = (high_or_low? FTDI_SIO_SET_RTS_HIGH : 
				FTDI_SIO_SET_RTS_LOW);
	return(usb_control_msg(dev, pipe,
			       FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
			       FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			       ftdi_high_or_low, 0, 
			       buf, 0, WDR_TIMEOUT));
}
static int set_dtr(struct usb_device *dev, 
		   unsigned int pipe,
		   int high_or_low)
{
	static char buf[1];
	unsigned ftdi_high_or_low = (high_or_low? FTDI_SIO_SET_DTR_HIGH : 
				FTDI_SIO_SET_DTR_LOW);
	return(usb_control_msg(dev, pipe,
			       FTDI_SIO_SET_MODEM_CTRL_REQUEST, 
			       FTDI_SIO_SET_MODEM_CTRL_REQUEST_TYPE,
			       ftdi_high_or_low, 0, 
			       buf, 0, WDR_TIMEOUT));
}



static int ftdi_sio_startup (struct usb_serial *serial)
{
	struct ftdi_private *priv;
	
	init_waitqueue_head(&serial->port[0].write_wait);
	
	priv = serial->port->private = kmalloc(sizeof(struct ftdi_private), GFP_KERNEL);
	if (!priv){
		err(__FUNCTION__"- kmalloc(%d) failed.", sizeof(struct ftdi_private));
		return -ENOMEM;
	}

	priv->ftdi_type = sio;
	
	return (0);
}


static int ftdi_8U232AM_startup (struct usb_serial *serial)
{
	struct ftdi_private *priv;
 
	init_waitqueue_head(&serial->port[0].write_wait);

	priv = serial->port->private = kmalloc(sizeof(struct ftdi_private), GFP_KERNEL);
	if (!priv){
		err(__FUNCTION__"- kmalloc(%d) failed.", sizeof(struct ftdi_private));
		return -ENOMEM;
	}

	priv->ftdi_type = F8U232AM;
	
	return (0);
}

static void ftdi_sio_shutdown (struct usb_serial *serial)
{
	
	dbg (__FUNCTION__);

	/* Close ports if they are open */
	while (serial->port[0].open_count > 0) {
		ftdi_sio_close (&serial->port[0], NULL);
	}
	if (serial->port->private){
		kfree(serial->port->private);
		serial->port->private = NULL;
	}
}



static int  ftdi_sio_open (struct usb_serial_port *port, struct file *filp)
{ /* ftdi_sio_open */
	struct termios tmp_termios;
	struct usb_serial *serial = port->serial;
	unsigned long flags;	/* Used for spinlock */
	int result;
	char buf[1]; /* Needed for the usb_control_msg I think */

	dbg(__FUNCTION__);

	spin_lock_irqsave (&port->port_lock, flags);
	
	MOD_INC_USE_COUNT;
	++port->open_count;

	if (!port->active){
		port->active = 1;

		spin_unlock_irqrestore (&port->port_lock, flags);

		/* do not allow a task to be queued to deliver received data */
		port->tty->low_latency = 1;

		/* No error checking for this (will get errors later anyway) */
		/* See ftdi_sio.h for description of what is reset */
		usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				FTDI_SIO_RESET_REQUEST, FTDI_SIO_RESET_REQUEST_TYPE, 
				FTDI_SIO_RESET_SIO, 
				0, buf, 0, WDR_TIMEOUT);

		/* Setup termios defaults. According to tty_io.c the 
		   settings are driver specific */
		port->tty->termios->c_cflag =
			B9600 | CS8 | CREAD | HUPCL | CLOCAL;

		/* ftdi_sio_set_termios  will send usb control messages */
		ftdi_sio_set_termios(port, &tmp_termios);	

		/* Turn on RTS and DTR since we are not flow controlling by default */
		if (set_dtr(serial->dev, usb_sndctrlpipe(serial->dev, 0),HIGH) < 0) {
			err(__FUNCTION__ " Error from DTR HIGH urb");
		}
		if (set_rts(serial->dev, usb_sndctrlpipe(serial->dev, 0),HIGH) < 0){
			err(__FUNCTION__ " Error from RTS HIGH urb");
		}
	
		/* Start reading from the device */
		FILL_BULK_URB(port->read_urb, serial->dev, 
			      usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
			      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
			      ftdi_sio_read_bulk_callback, port);
		result = usb_submit_urb(port->read_urb);
		if (result)
			err(__FUNCTION__ " - failed submitting read urb, error %d", result);
	} else { /* the port was already active - so no initialisation is done */
		spin_unlock_irqrestore (&port->port_lock, flags);
	}

	return (0);
} /* ftdi_sio_open */


static void ftdi_sio_close (struct usb_serial_port *port, struct file *filp)
{ /* ftdi_sio_close */
	struct usb_serial *serial = port->serial;
	unsigned int c_cflag = port->tty->termios->c_cflag;
	char buf[1];
	unsigned long flags;

	dbg( __FUNCTION__);

	spin_lock_irqsave (&port->port_lock, flags);
	--port->open_count;

	if (port->open_count <= 0) {
		spin_unlock_irqrestore (&port->port_lock, flags);
		if (c_cflag & HUPCL){
			/* Disable flow control */
			if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
					    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
					    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
					    0, 0, 
					    buf, 0, WDR_TIMEOUT) < 0) {
				err("error from flowcontrol urb");
			}	    

			/* drop DTR */
			if (set_dtr(serial->dev, usb_sndctrlpipe(serial->dev, 0), LOW) < 0){
				err("Error from DTR LOW urb");
			}
			/* drop RTS */
			if (set_rts(serial->dev, usb_sndctrlpipe(serial->dev, 0),LOW) < 0) {
				err("Error from RTS LOW urb");
			}	
		} /* Note change no line is hupcl is off */

		/* shutdown our bulk reads and writes */
		usb_unlink_urb (port->write_urb);
		usb_unlink_urb (port->read_urb);
		port->active = 0;
		port->open_count = 0;
	} else {  
		spin_unlock_irqrestore (&port->port_lock, flags);

		/* Send a HUP if necessary */
		if (!(port->tty->termios->c_cflag & CLOCAL)){
			tty_hangup(port->tty);
		}
		
	}

	MOD_DEC_USE_COUNT;

} /* ftdi_sio_close */


  
/* The ftdi_sio requires the first byte to have:
 *  B0 1
 *  B1 0
 *  B2..7 length of message excluding byte 0
 */
static int ftdi_sio_write (struct usb_serial_port *port, int from_user, 
			   const unsigned char *buf, int count)
{ /* ftdi_sio_write */
	struct usb_serial *serial = port->serial;
	struct ftdi_private *priv = (struct ftdi_private *)port->private;
	int data_offset ;
	int rc; 
	int result;
	DECLARE_WAITQUEUE(wait, current);
	
	dbg(__FUNCTION__ " port %d, %d bytes", port->number, count);

	if (count == 0) {
		err("write request of 0 bytes");
		return 0;
	}
	
	if (priv->ftdi_type == sio){
		data_offset = 1;
	} else {
		data_offset = 0;
	}
        dbg("data_offset set to %d",data_offset);

	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		unsigned char *first_byte = port->write_urb->transfer_buffer;

		/* Was seeing a race here, got a read callback, then write callback before
		   hitting interuptible_sleep_on  - so wrapping in a wait_queue */

		add_wait_queue(&port->write_wait, &wait);
		set_current_state (TASK_INTERRUPTIBLE);
		while (port->write_urb->status == -EINPROGRESS) {
			dbg(__FUNCTION__ " write in progress - retrying");
			if (signal_pending(current)) {
				current->state = TASK_RUNNING;
				remove_wait_queue(&port->write_wait, &wait);
				rc = -ERESTARTSYS;
				goto err;
			}
			schedule();
		}		
		remove_wait_queue(&port->write_wait, &wait);
		set_current_state(TASK_RUNNING);

		count += data_offset;
		count = (count > port->bulk_out_size) ? port->bulk_out_size : count;
		if (count == 0) {
			return 0;
		}

		/* Copy in the data to send */
		if (from_user) {
			copy_from_user(port->write_urb->transfer_buffer + data_offset , 
				       buf, count - data_offset );
		}
		else {
			memcpy(port->write_urb->transfer_buffer + data_offset,
			       buf, count - data_offset );
		}  

		first_byte = port->write_urb->transfer_buffer;
		if (data_offset > 0){
			/* Write the control byte at the front of the packet*/
			*first_byte = 1 | ((count-data_offset) << 2) ; 
		}

		dbg(__FUNCTION__ " Bytes: %d, First Byte: 0o%03o",count, first_byte[0]);
		usb_serial_debug_data (__FILE__, __FUNCTION__, count, first_byte);
		
		/* send the data out the bulk port */
		FILL_BULK_URB(port->write_urb, serial->dev, 
			      usb_sndbulkpipe(serial->dev, port->bulk_out_endpointAddress),
			      port->write_urb->transfer_buffer, count,
			      ftdi_sio_write_bulk_callback, port);
		
		result = usb_submit_urb(port->write_urb);
		if (result) {
			err(__FUNCTION__ " - failed submitting write urb, error %d", result);
			return 0;
		}

		dbg(__FUNCTION__ " write returning: %d", count - data_offset);
		return (count - data_offset);
	}
	
	/* no bulk out, so return 0 bytes written */
	return 0;
 err: /* error exit */
	return(rc);
} /* ftdi_sio_write */

static void ftdi_sio_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
       	struct tty_struct *tty = port->tty;

	dbg("ftdi_sio_write_bulk_callback");

	if (port_paranoia_check (port, "ftdi_sio_write_bulk_callback")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "ftdi_sio_write_bulk_callback")) {
		return;
	}
	
	if (urb->status) {
		dbg("nonzero write bulk status received: %d", urb->status);
		return;
	}

	wake_up_interruptible(&port->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);

	wake_up_interruptible(&tty->write_wait);
	
	return;
} /* ftdi_sio_write_bulk_callback */

static void ftdi_sio_read_bulk_callback (struct urb *urb)
{ /* ftdi_sio_serial_buld_callback */
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct ftdi_private *priv = (struct ftdi_private *)port->private;
	struct usb_serial *serial;
       	struct tty_struct *tty = port->tty ;
       	unsigned char *data = urb->transfer_buffer;

	const int data_offset = 2;
	int i;
	int result;

	dbg(__FUNCTION__);

	if (port_paranoia_check (port, "ftdi_sio_read_bulk_callback")) {
		return;
	}

	serial = port->serial;
	if (serial_paranoia_check (serial, "ftdi_sio_read_bulk_callback")) {
		return;
	}

	if (urb->status) {
		/* This will happen at close every time so it is a dbg not an err */
		dbg("nonzero read bulk status received: %d", urb->status);
		return;
	}

	if (urb->actual_length > 2) {
		usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, data);
	} else {
                dbg("Just status");
        }

	priv->last_status_byte = data[0]; /* this has modem control lines */

	/* TO DO -- check for hung up line and handle appropriately: */
	/*   send hangup  */
	/* See acm.c - you do a tty_hangup  - eg tty_hangup(tty) */
	/* if CD is dropped and the line is not CLOCAL then we should hangup */


	if (urb->actual_length > data_offset) {
		for (i = data_offset ; i < urb->actual_length ; ++i) {
			tty_insert_flip_char(tty, data[i], 0);
	  	}
	  	tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	FILL_BULK_URB(urb, serial->dev, 
		      usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
		      urb->transfer_buffer, urb->transfer_buffer_length,
		      ftdi_sio_read_bulk_callback, port);

	result = usb_submit_urb(urb);
	if (result)
		err(__FUNCTION__ " - failed resubmitting read urb, error %d", result);

	return;
} /* ftdi_sio_serial_read_bulk_callback */


__u16 translate_baudrate_to_ftdi(unsigned int cflag, ftdi_type_t ftdi_type) 
{ /* translate_baudrate_to_ftdi */
	
	__u16 urb_value = ftdi_sio_b9600;

	if (ftdi_type == sio){
		switch(cflag & CBAUD){
		case B0: break; /* ignored by this */
		case B300: urb_value = ftdi_sio_b300; dbg("Set to 300"); break;
		case B600: urb_value = ftdi_sio_b600; dbg("Set to 600") ; break;
		case B1200: urb_value = ftdi_sio_b1200; dbg("Set to 1200") ; break;
		case B2400: urb_value = ftdi_sio_b2400; dbg("Set to 2400") ; break;
		case B4800: urb_value = ftdi_sio_b4800; dbg("Set to 4800") ; break;
		case B9600: urb_value = ftdi_sio_b9600; dbg("Set to 9600") ; break;
		case B19200: urb_value = ftdi_sio_b19200; dbg("Set to 19200") ; break;
		case B38400: urb_value = ftdi_sio_b38400; dbg("Set to 38400") ; break;
		case B57600: urb_value = ftdi_sio_b57600; dbg("Set to 57600") ; break;
		case B115200: urb_value = ftdi_sio_b115200; dbg("Set to 115200") ; break;
		default: dbg(__FUNCTION__ " FTDI_SIO does not support the baudrate (%d) requested",
			     (cflag & CBAUD)); 
		   break;
		}
	} else { /* it is 8U232AM */
		switch(cflag & CBAUD){
		case B0: break; /* ignored by this */
		case B300: urb_value = ftdi_8U232AM_48MHz_b300; dbg("Set to 300"); break;
		case B600: urb_value = ftdi_8U232AM_48MHz_b600; dbg("Set to 600") ; break;
		case B1200: urb_value = ftdi_8U232AM_48MHz_b1200; dbg("Set to 1200") ; break;
		case B2400: urb_value = ftdi_8U232AM_48MHz_b2400; dbg("Set to 2400") ; break;
		case B4800: urb_value = ftdi_8U232AM_48MHz_b4800; dbg("Set to 4800") ; break;
		case B9600: urb_value = ftdi_8U232AM_48MHz_b9600; dbg("Set to 9600") ; break;
		case B19200: urb_value = ftdi_8U232AM_48MHz_b19200; dbg("Set to 19200") ; break;
		case B38400: urb_value = ftdi_8U232AM_48MHz_b38400; dbg("Set to 38400") ; break;
		case B57600: urb_value = ftdi_8U232AM_48MHz_b57600; dbg("Set to 57600") ; break;
		case B115200: urb_value = ftdi_8U232AM_48MHz_b115200; dbg("Set to 115200") ; break;
		case B230400: urb_value = ftdi_8U232AM_48MHz_b230400; dbg("Set to 230400") ; break;
		case B460800: urb_value = ftdi_8U232AM_48MHz_b460800; dbg("Set to 460800") ; break;
		case B921600: urb_value = ftdi_8U232AM_48MHz_b921600; dbg("Set to 921600") ; break;
		default: dbg(__FUNCTION__ " The baudrate (%d) requested is not implemented",
			     (cflag & CBAUD)); 
		   break;
		}
	}
	return(urb_value);
}

/* As I understand this - old_termios contains the original termios settings */
/*  and tty->termios contains the new setting to be used */
/* */
/*   WARNING: set_termios calls this with old_termios in kernel space */

static void ftdi_sio_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{ /* ftdi_sio_set_termios */
	struct usb_serial *serial = port->serial;
	unsigned int cflag = port->tty->termios->c_cflag;
	struct ftdi_private *priv = (struct ftdi_private *)port->private;	
	__u16 urb_value; /* will hold the new flags */
	char buf[1]; /* Perhaps I should dynamically alloc this? */
	
	
	dbg(__FUNCTION__);


	/* FIXME -For this cut I don't care if the line is really changing or 
	   not  - so just do the change regardless  - should be able to 
	   compare old_termios and tty->termios */
	/* NOTE These routines can get interrupted by 
	   ftdi_sio_read_bulk_callback  - need to examine what this 
           means - don't see any problems yet */
	
	/* Set number of data bits, parity, stop bits */
	
	urb_value = 0;
	urb_value |= (cflag & CSTOPB ? FTDI_SIO_SET_DATA_STOP_BITS_2 :
		      FTDI_SIO_SET_DATA_STOP_BITS_1);
	urb_value |= (cflag & PARENB ? 
		      (cflag & PARODD ? FTDI_SIO_SET_DATA_PARITY_ODD : 
		       FTDI_SIO_SET_DATA_PARITY_EVEN) :
		      FTDI_SIO_SET_DATA_PARITY_NONE);
	if (cflag & CSIZE) {
		switch (cflag & CSIZE) {
		case CS5: urb_value |= 5; dbg("Setting CS5"); break;
		case CS6: urb_value |= 6; dbg("Setting CS6"); break;
		case CS7: urb_value |= 7; dbg("Setting CS7"); break;
		case CS8: urb_value |= 8; dbg("Setting CS8"); break;
		default:
			err("CSIZE was set but not CS5-CS8");
		}
	}
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_DATA_REQUEST, 
			    FTDI_SIO_SET_DATA_REQUEST_TYPE,
			    urb_value , 0,
			    buf, 0, 100) < 0) {
		err(__FUNCTION__ " FAILED to set databits/stopbits/parity");
	}	   

	/* Now do the baudrate */
	urb_value = translate_baudrate_to_ftdi((cflag & CBAUD), priv->ftdi_type);
	
	if ((cflag & CBAUD) == B0 ) {
		/* Disable flow control */
		if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err(__FUNCTION__ " error from disable flowcontrol urb");
		}	    
		/* Drop RTS and DTR */
		if (set_dtr(serial->dev, usb_sndctrlpipe(serial->dev, 0),LOW) < 0){
			err(__FUNCTION__ " Error from DTR LOW urb");
		}
		if (set_rts(serial->dev, usb_sndctrlpipe(serial->dev, 0),LOW) < 0){
			err(__FUNCTION__ " Error from RTS LOW urb");
		}	
		
	} else {
		/* set the baudrate determined before */
		if (usb_control_msg(serial->dev, 
				    usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_BAUDRATE_REQUEST, 
				    FTDI_SIO_SET_BAUDRATE_REQUEST_TYPE,
				    urb_value, 0, 
				    buf, 0, 100) < 0) {
			err(__FUNCTION__ " urb failed to set baurdrate");
		}
	}
	/* Set flow control */
	/* Note device also supports DTR/CD (ugh) and Xon/Xoff in hardware */
	if (cflag & CRTSCTS) {
		dbg(__FUNCTION__ " Setting to CRTSCTS flow control");
		if (usb_control_msg(serial->dev, 
				    usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0 , FTDI_SIO_RTS_CTS_HS,
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("urb failed to set to rts/cts flow control");
		}		
		
	} else { 
		/* CHECKME Assuming XON/XOFF handled by tty stack - not by device */
		dbg(__FUNCTION__ " Turning off hardware flow control");
		if (usb_control_msg(serial->dev, 
				    usb_sndctrlpipe(serial->dev, 0),
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST, 
				    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
				    0, 0, 
				    buf, 0, WDR_TIMEOUT) < 0) {
			err("urb failed to clear flow control");
		}				
		
	}
	return;
} /* ftdi_sio_set_termios */

static int ftdi_sio_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct usb_serial *serial = port->serial;
	struct ftdi_private *priv = (struct ftdi_private *)port->private;
	__u16 urb_value=0; /* Will hold the new flags */
	char buf[1];
	int  ret, mask;
	
	dbg(__FUNCTION__ " cmd 0x%04x", cmd);

	/* Based on code from acm.c and others */
	switch (cmd) {

	case TIOCMGET:
		dbg(__FUNCTION__ " TIOCMGET");
		/* The MODEM_STATUS_REQUEST works for the sio but not the 232 */
		if (priv->ftdi_type == sio){
			/* TO DECIDE - use the 40ms status packets or not? */
			/*   PRO: No need to send urb */
			/*   CON: Could be 40ms out of date */

			/* Request the status from the device */
			if ((ret = usb_control_msg(serial->dev, 
						   usb_rcvctrlpipe(serial->dev, 0),
						   FTDI_SIO_GET_MODEM_STATUS_REQUEST, 
						   FTDI_SIO_GET_MODEM_STATUS_REQUEST_TYPE,
						   0, 0, 
						   buf, 1, WDR_TIMEOUT)) < 0 ) {
				err(__FUNCTION__ " Could not get modem status of device - err: %d",
				    ret);
				return(ret);
			}
		} else {
			/* This gets updated every 40ms - so just copy it in */
			buf[0] = priv->last_status_byte;
		}

		return put_user((buf[0] & FTDI_SIO_DSR_MASK ? TIOCM_DSR : 0) |
				(buf[0] & FTDI_SIO_CTS_MASK ? TIOCM_CTS : 0) |
				(buf[0]  & FTDI_SIO_RI_MASK  ? TIOCM_RI  : 0) |
				(buf[0]  & FTDI_SIO_RLSD_MASK ? TIOCM_CD  : 0),
				(unsigned long *) arg);
		break;

	case TIOCMSET: /* Turns on and off the lines as specified by the mask */
		dbg(__FUNCTION__ " TIOCMSET");
		if ((ret = get_user(mask, (unsigned long *) arg))) return ret;
		urb_value = ((mask & TIOCM_DTR) ? HIGH : LOW);
		if (set_dtr(serial->dev, usb_sndctrlpipe(serial->dev, 0),urb_value) < 0){
			err("Error from DTR set urb (TIOCMSET)");
		}
		urb_value = ((mask & TIOCM_RTS) ? HIGH : LOW);
		if (set_rts(serial->dev, usb_sndctrlpipe(serial->dev, 0),urb_value) < 0){
			err("Error from RTS set urb (TIOCMSET)");
		}	
		break;
					
	case TIOCMBIS: /* turns on (Sets) the lines as specified by the mask */
		dbg(__FUNCTION__ " TIOCMBIS");
 	        if ((ret = get_user(mask, (unsigned long *) arg))) return ret;
  	        if (mask & TIOCM_DTR){
			if ((ret = set_dtr(serial->dev, 
					   usb_sndctrlpipe(serial->dev, 0),
					   HIGH)) < 0) {
				err("Urb to set DTR failed");
				return(ret);
			}
		}
		if (mask & TIOCM_RTS) {
			if ((ret = set_rts(serial->dev, 
					   usb_sndctrlpipe(serial->dev, 0),
					   HIGH)) < 0){
				err("Urb to set RTS failed");
				return(ret);
			}
		}
					break;

	case TIOCMBIC: /* turns off (Clears) the lines as specified by the mask */
		dbg(__FUNCTION__ " TIOCMBIC");
 	        if ((ret = get_user(mask, (unsigned long *) arg))) return ret;
  	        if (mask & TIOCM_DTR){
			if ((ret = set_dtr(serial->dev, 
					   usb_sndctrlpipe(serial->dev, 0),
					   LOW)) < 0){
				err("Urb to unset DTR failed");
				return(ret);
			}
		}	
		if (mask & TIOCM_RTS) {
			if ((ret = set_rts(serial->dev, 
					   usb_sndctrlpipe(serial->dev, 0),
					   LOW)) < 0){
				err("Urb to unset RTS failed");
				return(ret);
			}
		}
		break;

		/*
		 * I had originally implemented TCSET{A,S}{,F,W} and
		 * TCGET{A,S} here separately, however when testing I
		 * found that the higher layers actually do the termios
		 * conversions themselves and pass the call onto
		 * ftdi_sio_set_termios. 
		 *
		 */

	default:
	  /* This is not an error - turns out the higher layers will do 
	   *  some ioctls itself (see comment above)
 	   */
		dbg(__FUNCTION__ " arg not supported - it was 0x%04x",cmd);
		return(-ENOIOCTLCMD);
		break;
	}
	return 0;
} /* ftdi_sio_ioctl */


static int __init ftdi_sio_init (void)
{
	dbg(__FUNCTION__);
	usb_serial_register (&ftdi_sio_device);
	usb_serial_register (&ftdi_8U232AM_device);
	return 0;
}


static void __exit ftdi_sio_exit (void)
{
	dbg(__FUNCTION__);
	usb_serial_deregister (&ftdi_sio_device);
	usb_serial_deregister (&ftdi_8U232AM_device);
}


module_init(ftdi_sio_init);
module_exit(ftdi_sio_exit);

MODULE_AUTHOR("Greg Kroah-Hartman <greg@kroah.com>, Bill Ryder <bryder@sgi.com>");
MODULE_DESCRIPTION("USB FTDI RS232 converters driver");
