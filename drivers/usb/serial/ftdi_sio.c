/*
 * USB FTDI SIO driver
 *
 * 	Copyright (C) 1999 - 2001
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
 * See http://ftdi-usb-sio.sourceforge.net for upto date testing info
 *     and extra documentation
 * 
 * (04/Nov/2001) Bill Ryder
 *     Fixed bug in read_bulk_callback where incorrect urb buffer was used.
 *     cleaned up write offset calculation
 *     added write_room since default values can be incorrect for sio
 *     changed write_bulk_callback to use same queue_task as other drivers
 *       (the previous version caused panics)
 *     Removed port iteration code since the device only has one I/O port and it 
 *       was wrong anyway.
 * 
 * (31/May/2001) gkh
 *	switched from using spinlock to a semaphore, which fixes lots of problems.
 *
 * (23/May/2001)   Bill Ryder
 *     Added runtime debug patch (thanx Tyson D Sawyer).
 *     Cleaned up comments for 8U232
 *     Added parity, framing and overrun error handling
 *     Added receive break handling.
 * 
 * (04/08/2001) gb
 *	Identify version on module load.
 *       
 * (18/March/2001) Bill Ryder
 *     (Not released)
 *     Added send break handling. (requires kernel patch too)
 *     Fixed 8U232AM hardware RTS/CTS etc status reporting.
 *     Added flipbuf fix copied from generic device
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
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#ifdef CONFIG_USB_SERIAL_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif

#include "usb-serial.h"
#include "ftdi_sio.h"


/*
 * Version Information
 */
#define DRIVER_VERSION "v1.2.0"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>, Bill Ryder <bryder@sgi.com>"
#define DRIVER_DESC "USB FTDI RS232 Converters Driver"

static __devinitdata struct usb_device_id id_table_sio [] = {
	{ USB_DEVICE(FTDI_VID, FTDI_SIO_PID) },
	{ }						/* Terminating entry */
};

/* THe 8U232AM has the same API as the sio except for:
   - it can support MUCH higher baudrates (921600 at 48MHz/230400 
     at 12MHz so .. it's baudrate setting codes are different 
   - it has a two byte status code.
   - it returns characters very 16ms (the FTDI does it every 40ms)
  */

   
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
	__u16 last_set_data_urb_value ; /* the last data state set - needed for doing a break */
        int write_offset;
};
/* function prototypes for a FTDI serial converter */
static int  ftdi_sio_startup		(struct usb_serial *serial);
static int  ftdi_8U232AM_startup	(struct usb_serial *serial);
static void ftdi_sio_shutdown		(struct usb_serial *serial);
static int  ftdi_sio_open		(struct usb_serial_port *port, struct file *filp);
static void ftdi_sio_close		(struct usb_serial_port *port, struct file *filp);
static int  ftdi_sio_write		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
static int  ftdi_sio_write_room		(struct usb_serial_port *port);
static void ftdi_sio_write_bulk_callback (struct urb *urb);
static void ftdi_sio_read_bulk_callback	(struct urb *urb);
static void ftdi_sio_set_termios	(struct usb_serial_port *port, struct termios * old);
static int  ftdi_sio_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void ftdi_sio_break_ctl		(struct usb_serial_port *port, int break_state );

/* Should rename most ftdi_sio's to ftdi_ now since there are two devices 
   which share common code */ 

static struct usb_serial_device_type ftdi_sio_device = {
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
	write_room:		ftdi_sio_write_room,
	read_bulk_callback:	ftdi_sio_read_bulk_callback,
	write_bulk_callback:	ftdi_sio_write_bulk_callback,
	ioctl:			ftdi_sio_ioctl,
	set_termios:		ftdi_sio_set_termios,
	break_ctl:		ftdi_sio_break_ctl,
	startup:		ftdi_sio_startup,
        shutdown:               ftdi_sio_shutdown,
};

static struct usb_serial_device_type ftdi_8U232AM_device = {
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
	write_room:		ftdi_sio_write_room,
	read_bulk_callback:	ftdi_sio_read_bulk_callback,
	write_bulk_callback:	ftdi_sio_write_bulk_callback,
	ioctl:			ftdi_sio_ioctl,
	set_termios:		ftdi_sio_set_termios,
	break_ctl:		ftdi_sio_break_ctl,
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
	
	
	priv = serial->port->private = kmalloc(sizeof(struct ftdi_private), GFP_KERNEL);
	if (!priv){
		err(__FUNCTION__"- kmalloc(%Zd) failed.", sizeof(struct ftdi_private));
		return -ENOMEM;
	}

	priv->ftdi_type = sio;
	priv->write_offset = 1;
	
	return (0);
}


static int ftdi_8U232AM_startup (struct usb_serial *serial)
{
	struct ftdi_private *priv;
 

	priv = serial->port->private = kmalloc(sizeof(struct ftdi_private), GFP_KERNEL);
	if (!priv){
		err(__FUNCTION__"- kmalloc(%Zd) failed.", sizeof(struct ftdi_private));
		return -ENOMEM;
	}

	priv->ftdi_type = F8U232AM;
	priv->write_offset = 0;
	
	return (0);
}

static void ftdi_sio_shutdown (struct usb_serial *serial)
{
	
	dbg (__FUNCTION__);


	/* stop reads and writes on all ports */
	while (serial->port[0].open_count > 0) {
	        ftdi_sio_close (&serial->port[0], NULL);
	}
	if (serial->port[0].private){
		kfree(serial->port[0].private);
		serial->port[0].private = NULL;
	}
}



static int  ftdi_sio_open (struct usb_serial_port *port, struct file *filp)
{ /* ftdi_sio_open */
	struct termios tmp_termios;
	struct usb_serial *serial = port->serial;
	int result = 0;
	char buf[1]; /* Needed for the usb_control_msg I think */

	dbg(__FUNCTION__);

	down (&port->sem);
	
	MOD_INC_USE_COUNT;
	++port->open_count;

	if (!port->active){
		port->active = 1;

		/* This will push the characters through immediately rather 
		   than queue a task to deliver them */
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
	}

	up (&port->sem);
	return result;
} /* ftdi_sio_open */


static void ftdi_sio_close (struct usb_serial_port *port, struct file *filp)
{ /* ftdi_sio_close */
	struct usb_serial *serial = port->serial; /* Checked in usbserial.c */
	unsigned int c_cflag = port->tty->termios->c_cflag;
	char buf[1];

	dbg( __FUNCTION__);

	down (&port->sem);
	--port->open_count;

	if (port->open_count <= 0) {
		if (serial->dev) {
			if (c_cflag & HUPCL){
				/* Disable flow control */
				if (usb_control_msg(serial->dev, 
						    usb_sndctrlpipe(serial->dev, 0),
						    FTDI_SIO_SET_FLOW_CTRL_REQUEST,
						    FTDI_SIO_SET_FLOW_CTRL_REQUEST_TYPE,
						    0, 0, buf, 0, WDR_TIMEOUT) < 0) {
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
			/* ***CHECK*** behaviour when there is nothing queued */
			usb_unlink_urb (port->write_urb);
			usb_unlink_urb (port->read_urb);
		}
		port->active = 0;
		port->open_count = 0;
	} else {  
		/* Send a HUP if necessary */
		if (!(port->tty->termios->c_cflag & CLOCAL)){
			tty_hangup(port->tty);
		}
	}

	up (&port->sem);
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
	unsigned char *first_byte = port->write_urb->transfer_buffer;
	int data_offset ;
	int result;
	
	dbg(__FUNCTION__ " port %d, %d bytes", port->number, count);

	if (count == 0) {
		err("write request of 0 bytes");
		return 0;
	}
	
	data_offset = priv->write_offset;
        dbg("data_offset set to %d",data_offset);

	if (port->write_urb->status == -EINPROGRESS) {
		dbg (__FUNCTION__ " - already writing");
		return (0);
	}		

	down(&port->sem);

	count += data_offset;
	count = (count > port->bulk_out_size) ? port->bulk_out_size : count;

		/* Copy in the data to send */
	if (from_user) {
		if (copy_from_user(port->write_urb->transfer_buffer + data_offset,
				   buf, count - data_offset )){
			up (&port->sem);
			return -EFAULT;
		}
	} else {
		memcpy(port->write_urb->transfer_buffer + data_offset,
		       buf, count - data_offset );
	}  

	first_byte = port->write_urb->transfer_buffer;
	if (data_offset > 0){
		/* Write the control byte at the front of the packet*/
		*first_byte = 1 | ((count-data_offset) << 2) ; 
	}

	dbg(__FUNCTION__ " Bytes: %d, First Byte: 0x%02x",count, first_byte[0]);
	usb_serial_debug_data (__FILE__, __FUNCTION__, count, first_byte);
		
	/* send the data out the bulk port */
	FILL_BULK_URB(port->write_urb, serial->dev, 
		      usb_sndbulkpipe(serial->dev, port->bulk_out_endpointAddress),
		      port->write_urb->transfer_buffer, count,
		      ftdi_sio_write_bulk_callback, port);
		
	result = usb_submit_urb(port->write_urb);
	if (result) {
		err(__FUNCTION__ " - failed submitting write urb, error %d", result);
		up (&port->sem);
		return 0;
	}
	up (&port->sem);

	dbg(__FUNCTION__ " write returning: %d", count - data_offset);
	return (count - data_offset);

} /* ftdi_sio_write */

static void ftdi_sio_write_bulk_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;

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
	queue_task(&port->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

	return;
} /* ftdi_sio_write_bulk_callback */


static int ftdi_sio_write_room( struct usb_serial_port *port )
{
	struct ftdi_private *priv = (struct ftdi_private *)port->private;
	int room;
	if ( port->write_urb->status == -EINPROGRESS) {
		/* There is a race here with the _write routines but it won't hurt */
		room = 0;
	} else { 
		room = port->bulk_out_size - priv->write_offset;
	}
	return(room);


} /* ftdi_sio_write_room */


static void ftdi_sio_read_bulk_callback (struct urb *urb)
{ /* ftdi_sio_serial_buld_callback */
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
       	struct tty_struct *tty = port->tty ;
	char error_flag;
       	unsigned char *data = urb->transfer_buffer;

	const int data_offset = 2;
	int i;
	int result;

	dbg(__FUNCTION__ " - port %d", port->number);

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
                dbg("Just status 0o%03o0o%03o",data[0],data[1]);
        }


	/* TO DO -- check for hung up line and handle appropriately: */
	/*   send hangup  */
	/* See acm.c - you do a tty_hangup  - eg tty_hangup(tty) */
	/* if CD is dropped and the line is not CLOCAL then we should hangup */

	/* Handle errors and break */
	error_flag = TTY_NORMAL;
        /* Although the device uses a bitmask and hence can have multiple */
        /* errors on a packet - the order here sets the priority the */
        /* error is returned to the tty layer  */
	
	if ( data[1] & FTDI_RS_OE ) { 
		error_flag = TTY_OVERRUN;
                dbg("OVERRRUN error");
	}
	if ( data[1] & FTDI_RS_BI ) { 
		error_flag = TTY_BREAK;
                dbg("BREAK received");
	}
	if ( data[1] & FTDI_RS_PE ) { 
		error_flag = TTY_PARITY;
                dbg("PARITY error");
	}
	if ( data[1] & FTDI_RS_FE ) { 
		error_flag = TTY_FRAME;
                dbg("FRAMING error");
	}
	if (urb->actual_length > data_offset) {

		for (i = data_offset ; i < urb->actual_length ; ++i) {
			/* have to make sure we don't overflow the buffer
			  with tty_insert_flip_char's */
			if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(tty);
			}
			/* Note that the error flag is duplicated for 
			   every character received since we don't know
			   which character it applied to */
			tty_insert_flip_char(tty, data[i], error_flag);
		}
	  	tty_flip_buffer_push(tty);


	} 

#ifdef NOT_CORRECT_BUT_KEEPING_IT_FOR_NOW
	/* if a parity error is detected you get status packets forever
	   until a character is sent without a parity error.
	   This doesn't work well since the application receives a never
	   ending stream of bad data - even though new data hasn't been sent.
	   Therefore I (bill) have taken this out.
	   However - this might make sense for framing errors and so on 
	   so I am leaving the code in for now.
	*/
      else {
		if (error_flag != TTY_NORMAL){
			dbg("error_flag is not normal");
				/* In this case it is just status - if that is an error send a bad character */
				if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
					tty_flip_buffer_push(tty);
				}
				tty_insert_flip_char(tty, 0xff, error_flag);
				tty_flip_buffer_push(tty);
		}
	}
#endif

	/* Continue trying to always read  */
	FILL_BULK_URB(port->read_urb, serial->dev, 
		      usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
		      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
		      ftdi_sio_read_bulk_callback, port);

	result = usb_submit_urb(port->read_urb);
	if (result)
		err(__FUNCTION__ " - failed resubmitting read urb, error %d", result);

	return;
} /* ftdi_sio_serial_read_bulk_callback */


static __u16 translate_baudrate_to_ftdi(unsigned int cflag, ftdi_type_t ftdi_type) 
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

static void ftdi_sio_break_ctl( struct usb_serial_port *port, int break_state )
{
	struct usb_serial *serial = port->serial;
	struct ftdi_private *priv = (struct ftdi_private *)port->private;
	__u16 urb_value = 0; 
	char buf[1];
	
	/* break_state = -1 to turn on break, and 0 to turn off break */
	/* see drivers/char/tty_io.c to see it used */
	/* last_set_data_urb_value NEVER has the break bit set in it */

	if (break_state) {
		urb_value = priv->last_set_data_urb_value | FTDI_SIO_SET_BREAK;
	} else {
		urb_value = priv->last_set_data_urb_value; 
	}

	
	if (usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    FTDI_SIO_SET_DATA_REQUEST, 
			    FTDI_SIO_SET_DATA_REQUEST_TYPE,
			    urb_value , 0,
			    buf, 0, WDR_TIMEOUT) < 0) {
		err(__FUNCTION__ " FAILED to enable/disable break state (state was %d)",break_state);
	}	   

	dbg(__FUNCTION__ " break state is %d - urb is %d",break_state, urb_value);
	
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

	/* This is needed by the break command since it uses the same command - but is
	 *  or'ed with this value  */
	priv->last_set_data_urb_value = urb_value;
	
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
	char buf[2];
	int  ret, mask;
	
	dbg(__FUNCTION__ " cmd 0x%04x", cmd);

	/* Based on code from acm.c and others */
	switch (cmd) {

	case TIOCMGET:
		dbg(__FUNCTION__ " TIOCMGET");
		if (priv->ftdi_type == sio){
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
			/* the 8U232AM returns a two byte value (the sio is a 1 byte value) - in the same 
			   format as the data returned from the in point */
			if ((ret = usb_control_msg(serial->dev, 
						   usb_rcvctrlpipe(serial->dev, 0),
						   FTDI_SIO_GET_MODEM_STATUS_REQUEST, 
						   FTDI_SIO_GET_MODEM_STATUS_REQUEST_TYPE,
						   0, 0, 
						   buf, 2, WDR_TIMEOUT)) < 0 ) {
				err(__FUNCTION__ " Could not get modem status of device - err: %d",
				    ret);
				return(ret);
			}
		}

		return put_user((buf[0] & FTDI_SIO_DSR_MASK ? TIOCM_DSR : 0) |
				(buf[0] & FTDI_SIO_CTS_MASK ? TIOCM_CTS : 0) |
				(buf[0]  & FTDI_SIO_RI_MASK  ? TIOCM_RI  : 0) |
				(buf[0]  & FTDI_SIO_RLSD_MASK ? TIOCM_CD  : 0),
				(unsigned long *) arg);
		break;

	case TIOCMSET: /* Turns on and off the lines as specified by the mask */
		dbg(__FUNCTION__ " TIOCMSET");
		if (get_user(mask, (unsigned long *) arg))
			return -EFAULT;
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
 	        if (get_user(mask, (unsigned long *) arg))
			return -EFAULT;
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
 	        if (get_user(mask, (unsigned long *) arg))
			return -EFAULT;
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
	info(DRIVER_VERSION ":" DRIVER_DESC);
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

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");

