/*
 * USB ConnectTech WhiteHEAT driver
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
 * (11/01/2000) Adam J. Richter
 *	usb_device_id table support
 * 
 * (10/05/2000) gkh
 *	Fixed bug with urb->dev not being set properly, now that the usb
 *	core needs it.
 * 
 * (10/03/2000) smd
 *	firmware is improved to guard against crap sent to device
 *	firmware now replies CMD_FAILURE on bad things
 *	read_callback fix you provided for private info struct
 *	command_finished now indicates success or fail
 *	setup_port struct now packed to avoid gcc padding
 *	firmware uses 1 based port numbering, driver now handles that
 *
 * (09/11/2000) gkh
 *	Removed DEBUG #ifdefs with call to usb_serial_debug_data
 *
 * (07/19/2000) gkh
 *	Added module_init and module_exit functions to handle the fact that this
 *	driver is a loadable module now.
 *	Fixed bug with port->minor that was found by Al Borchers
 *
 * (07/04/2000) gkh
 *	Added support for port settings. Baud rate can now be changed. Line signals
 *	are not transferred to and from the tty layer yet, but things seem to be 
 *	working well now.
 *
 * (05/04/2000) gkh
 *	First cut at open and close commands. Data can flow through the ports at
 *	default speeds now.
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

#include "whiteheat_fw.h"		/* firmware for the ConnectTech WhiteHEAT device */

#include "whiteheat.h"			/* WhiteHEAT specific commands */

#define CONNECT_TECH_VENDOR_ID		0x0710
#define CONNECT_TECH_FAKE_WHITE_HEAT_ID	0x0001
#define CONNECT_TECH_WHITE_HEAT_ID	0x8001

/*
   ID tables for whiteheat are unusual, because we want to different
   things for different versions of the device.  Eventually, this
   will be doable from a single table.  But, for now, we define two
   separate ID tables, and then a third table that combines them
   just for the purpose of exporting the autoloading information.
*/
static __devinitdata struct usb_device_id id_table_std [] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

static __devinitdata struct usb_device_id id_table_prerenumeration [] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

static __devinitdata struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_WHITE_HEAT_ID) },
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_FAKE_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

/* function prototypes for the Connect Tech WhiteHEAT serial converter */
static int  whiteheat_open		(struct usb_serial_port *port, struct file *filp);
static void whiteheat_close		(struct usb_serial_port *port, struct file *filp);
static int  whiteheat_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void whiteheat_set_termios	(struct usb_serial_port *port, struct termios * old);
static void whiteheat_throttle		(struct usb_serial_port *port);
static void whiteheat_unthrottle	(struct usb_serial_port *port);
static int  whiteheat_startup		(struct usb_serial *serial);
static void whiteheat_shutdown		(struct usb_serial *serial);

struct usb_serial_device_type whiteheat_fake_device = {
	name:			"Connect Tech - WhiteHEAT - (prerenumeration)",
	id_table:		id_table_prerenumeration,
	needs_interrupt_in:	DONT_CARE,				/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,				/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,				/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		whiteheat_startup	
};

struct usb_serial_device_type whiteheat_device = {
	name:			"Connect Tech - WhiteHEAT",
	id_table:		id_table_std,
	needs_interrupt_in:	DONT_CARE,				/* don't have to have an interrupt in endpoint */
	needs_bulk_in:		DONT_CARE,				/* don't have to have a bulk in endpoint */
	needs_bulk_out:		DONT_CARE,				/* don't have to have a bulk out endpoint */
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		4,
	open:			whiteheat_open,
	close:			whiteheat_close,
	throttle:		whiteheat_throttle,
	unthrottle:		whiteheat_unthrottle,
	ioctl:			whiteheat_ioctl,
	set_termios:		whiteheat_set_termios,
	shutdown:		whiteheat_shutdown,
};

struct whiteheat_private {
	__u8			command_finished;
	wait_queue_head_t	wait_command;	/* for handling sleeping while waiting for a command to finish */
};


/* local function prototypes */
static inline void set_rts	(struct usb_serial_port *port, unsigned char rts);
static inline void set_dtr	(struct usb_serial_port *port, unsigned char dtr);
static inline void set_break	(struct usb_serial_port *port, unsigned char brk);



#define COMMAND_PORT		4
#define COMMAND_TIMEOUT		(2*HZ)	/* 2 second timeout for a command */

/*****************************************************************************
 * Connect Tech's White Heat specific driver functions
 *****************************************************************************/
static void command_port_write_callback (struct urb *urb)
{
	dbg (__FUNCTION__);

	if (urb->status) {
		dbg ("nonzero urb status: %d", urb->status);
		return;
	}

	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, urb->transfer_buffer);

	return;
}


static void command_port_read_callback (struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = get_usb_serial (port, __FUNCTION__);
	struct whiteheat_private *info;
	unsigned char *data = urb->transfer_buffer;
	int result;

	dbg (__FUNCTION__);

	if (urb->status) {
		dbg (__FUNCTION__ " - nonzero urb status: %d", urb->status);
		return;
	}

	if (!serial) {
		dbg(__FUNCTION__ " - bad serial pointer, exiting");
		return;
	}
	
	usb_serial_debug_data (__FILE__, __FUNCTION__, urb->actual_length, data);

	info = (struct whiteheat_private *)port->private;
	if (!info) {
		dbg (__FUNCTION__ " - info is NULL, exiting.");
		return;
	}

	/* right now, if the command is COMMAND_COMPLETE, just flip the bit saying the command finished */
	/* in the future we're going to have to pay attention to the actual command that completed */
	if (data[0] == WHITEHEAT_CMD_COMPLETE) {
		info->command_finished = WHITEHEAT_CMD_COMPLETE;
		wake_up_interruptible(&info->wait_command);
	}
	
	if (data[0] == WHITEHEAT_CMD_FAILURE) {
		info->command_finished = WHITEHEAT_CMD_FAILURE;
		wake_up_interruptible(&info->wait_command);
	}
	
	/* Continue trying to always read */
	FILL_BULK_URB(port->read_urb, serial->dev, 
		      usb_rcvbulkpipe(serial->dev, port->bulk_in_endpointAddress),
		      port->read_urb->transfer_buffer, port->read_urb->transfer_buffer_length,
		      command_port_read_callback, port);
	result = usb_submit_urb(port->read_urb);
	if (result)
		dbg(__FUNCTION__ " - failed resubmitting read urb, error %d", result);
}


static int whiteheat_send_cmd (struct usb_serial *serial, __u8 command, __u8 *data, __u8 datasize)
{
	struct whiteheat_private *info;
	struct usb_serial_port *port;
	int timeout;
	__u8 *transfer_buffer;

	dbg(__FUNCTION__" - command %d", command);

	port = &serial->port[COMMAND_PORT];
	info = (struct whiteheat_private *)port->private;
	info->command_finished = FALSE;
	
	transfer_buffer = (__u8 *)port->write_urb->transfer_buffer;
	transfer_buffer[0] = command;
	memcpy (&transfer_buffer[1], data, datasize);
	port->write_urb->transfer_buffer_length = datasize + 1;
	port->write_urb->dev = serial->dev;
	if (usb_submit_urb (port->write_urb)) {
		dbg (__FUNCTION__" - submit urb failed");
		return -1;
	}

	/* wait for the command to complete */
	timeout = COMMAND_TIMEOUT;
	while (timeout && (info->command_finished == FALSE)) {
		timeout = interruptible_sleep_on_timeout (&info->wait_command, timeout);
	}

	if (info->command_finished == FALSE) {
		dbg (__FUNCTION__ " - command timed out.");
		return -1;
	}

	if (info->command_finished == WHITEHEAT_CMD_FAILURE) {
		dbg (__FUNCTION__ " - command failed.");
		return -1;
	}

	if (info->command_finished == WHITEHEAT_CMD_COMPLETE) {
		dbg (__FUNCTION__ " - command completed.");
		return 0;
	}

	return 0;
}


static int whiteheat_open (struct usb_serial_port *port, struct file *filp)
{
	struct whiteheat_min_set	open_command;
	struct usb_serial_port 		*command_port;
	struct whiteheat_private	*info;
	int				result;

	dbg(__FUNCTION__" - port %d", port->number);

	if (port->active) {
		dbg (__FUNCTION__ " - device already open");
		return -EINVAL;
	}
	port->active = 1;

	/* set up some stuff for our command port */
	command_port = &port->serial->port[COMMAND_PORT];
	if (command_port->private == NULL) {
		info = (struct whiteheat_private *)kmalloc (sizeof(struct whiteheat_private), GFP_KERNEL);
		if (info == NULL) {
			err(__FUNCTION__ " - out of memory");
			return -ENOMEM;
		}
		
		init_waitqueue_head(&info->wait_command);
		command_port->private = info;
		command_port->write_urb->complete = command_port_write_callback;
		command_port->read_urb->complete = command_port_read_callback;
		command_port->read_urb->dev = port->serial->dev;
		command_port->tty = port->tty;		/* need this to "fake" our our sanity check macros */
		usb_submit_urb (command_port->read_urb);
	}
	
	/* Start reading from the device */
	port->read_urb->dev = port->serial->dev;
	result = usb_submit_urb(port->read_urb);
	if (result)
		err(__FUNCTION__ " - failed submitting read urb, error %d", result);

	/* send an open port command */
	/* firmware uses 1 based port numbering */
	open_command.port = port->number - port->serial->minor + 1;
	whiteheat_send_cmd (port->serial, WHITEHEAT_OPEN, (__u8 *)&open_command, sizeof(open_command));

	/* Need to do device specific setup here (control lines, baud rate, etc.) */
	/* FIXME!!! */

	dbg(__FUNCTION__ " - exit");
	
	return 0;
}


static void whiteheat_close(struct usb_serial_port *port, struct file * filp)
{
	struct whiteheat_min_set	close_command;
	
	dbg(__FUNCTION__ " - port %d", port->number);
	
	/* send a close command to the port */
	/* firmware uses 1 based port numbering */
	close_command.port = port->number - port->serial->minor + 1;
	whiteheat_send_cmd (port->serial, WHITEHEAT_CLOSE, (__u8 *)&close_command, sizeof(close_command));

	/* Need to change the control lines here */
	/* FIXME */
	
	/* shutdown our bulk reads and writes */
	usb_unlink_urb (port->write_urb);
	usb_unlink_urb (port->read_urb);
	port->active = 0;
}


static int whiteheat_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	dbg(__FUNCTION__ " - port %d, cmd 0x%.4x", port->number, cmd);

	return -ENOIOCTLCMD;
}


static void whiteheat_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	unsigned int cflag = port->tty->termios->c_cflag;
	struct whiteheat_port_settings port_settings;

	dbg(__FUNCTION__ " -port %d", port->number);

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
	
	/* set the port number */
	/* firmware uses 1 based port numbering */
	port_settings.port = port->number + 1;

	/* get the byte size */
	switch (cflag & CSIZE) {
		case CS5:	port_settings.bits = 5;   break;
		case CS6:	port_settings.bits = 6;   break;
		case CS7:	port_settings.bits = 7;   break;
		default:
		case CS8:	port_settings.bits = 8;   break;
	}
	dbg(__FUNCTION__ " - data bits = %d", port_settings.bits);
	
	/* determine the parity */
	if (cflag & PARENB)
		if (cflag & PARODD)
			port_settings.parity = 'o';
		else
			port_settings.parity = 'e';
	else
		port_settings.parity = 'n';
	dbg(__FUNCTION__ " - parity = %c", port_settings.parity);

	/* figure out the stop bits requested */
	if (cflag & CSTOPB)
		port_settings.stop = 2;
	else
		port_settings.stop = 1;
	dbg(__FUNCTION__ " - stop bits = %d", port_settings.stop);

	
	/* figure out the flow control settings */
	if (cflag & CRTSCTS)
		port_settings.hflow = (WHITEHEAT_CTS_FLOW | WHITEHEAT_RTS_FLOW);
	else
		port_settings.hflow = 0;
	dbg(__FUNCTION__ " - hardware flow control = %s %s %s %s",
	    (port_settings.hflow & WHITEHEAT_CTS_FLOW) ? "CTS" : "",
	    (port_settings.hflow & WHITEHEAT_RTS_FLOW) ? "RTS" : "",
	    (port_settings.hflow & WHITEHEAT_DSR_FLOW) ? "DSR" : "",
	    (port_settings.hflow & WHITEHEAT_DTR_FLOW) ? "DTR" : "");
	
	/* determine software flow control */
	if (I_IXOFF(port->tty))
		port_settings.sflow = 'b';
	else
		port_settings.sflow = 'n';
	dbg(__FUNCTION__ " - software flow control = %c", port_settings.sflow);
	
	port_settings.xon = START_CHAR(port->tty);
	port_settings.xoff = STOP_CHAR(port->tty);
	dbg(__FUNCTION__ " - XON = %2x, XOFF = %2x", port_settings.xon, port_settings.xoff);

	/* get the baud rate wanted */
	port_settings.baud = tty_get_baud_rate(port->tty);
	dbg(__FUNCTION__ " - baud rate = %d", port_settings.baud);

	/* handle any settings that aren't specified in the tty structure */
	port_settings.lloop = 0;
	
	/* now send the message to the device */
	whiteheat_send_cmd (port->serial, WHITEHEAT_SETUP_PORT, (__u8 *)&port_settings, sizeof(port_settings));
	
	return;
}


static void whiteheat_throttle (struct usb_serial_port *port)
{
	dbg(__FUNCTION__" - port %d", port->number);

	/* Change the control signals */
	/* FIXME!!! */

	return;
}


static void whiteheat_unthrottle (struct usb_serial_port *port)
{
	dbg(__FUNCTION__" - port %d", port->number);

	/* Change the control signals */
	/* FIXME!!! */

	return;
}


/* steps to download the firmware to the WhiteHEAT device:
 - hold the reset (by writing to the reset bit of the CPUCS register)
 - download the VEND_AX.HEX file to the chip using VENDOR_REQUEST-ANCHOR_LOAD
 - release the reset (by writing to the CPUCS register)
 - download the WH.HEX file for all addresses greater than 0x1b3f using
   VENDOR_REQUEST-ANCHOR_EXTERNAL_RAM_LOAD
 - hold the reset
 - download the WH.HEX file for all addresses less than 0x1b40 using
   VENDOR_REQUEST_ANCHOR_LOAD
 - release the reset
 - device renumerated itself and comes up as new device id with all
   firmware download completed.
*/
static int  whiteheat_startup (struct usb_serial *serial)
{
	int response;
	const struct whiteheat_hex_record *record;
	
	dbg(__FUNCTION__);
	
	response = ezusb_set_reset (serial, 1);

	record = &whiteheat_loader[0];
	while (record->address != 0xffff) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err(__FUNCTION__ " - ezusb_writememory failed for loader (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}

	response = ezusb_set_reset (serial, 0);

	record = &whiteheat_firmware[0];
	while (record->address < 0x1b40) {
		++record;
	}
	while (record->address != 0xffff) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa3);
		if (response < 0) {
			err(__FUNCTION__ " - ezusb_writememory failed for first firmware step (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}
	
	response = ezusb_set_reset (serial, 1);

	record = &whiteheat_firmware[0];
	while (record->address < 0x1b40) {
		response = ezusb_writememory (serial, record->address, 
				(unsigned char *)record->data, record->data_size, 0xa0);
		if (response < 0) {
			err(__FUNCTION__" - ezusb_writememory failed for second firmware step (%d %04X %p %d)", 
				response, record->address, record->data, record->data_size);
			break;
		}
		++record;
	}

	response = ezusb_set_reset (serial, 0);

	/* we want this device to fail to have a driver assigned to it. */
	return 1;
}


static void whiteheat_shutdown (struct usb_serial *serial)
{
	struct usb_serial_port 		*command_port;

	dbg(__FUNCTION__);

	/* free up our private data for our command port */
	command_port = &serial->port[COMMAND_PORT];
	if (command_port->private != NULL) {
		kfree (command_port->private);
		command_port->private = NULL;
	}

	return;
}




static void set_command (struct usb_serial_port *port, unsigned char state, unsigned char command)
{
	struct whiteheat_rdb_set rdb_command;
	
	/* send a set rts command to the port */
	/* firmware uses 1 based port numbering */
	rdb_command.port = port->number - port->serial->minor + 1;
	rdb_command.state = state;

	whiteheat_send_cmd (port->serial, command, (__u8 *)&rdb_command, sizeof(rdb_command));
}


static inline void set_rts (struct usb_serial_port *port, unsigned char rts)
{
	set_command (port, rts, WHITEHEAT_SET_RTS);
}


static inline void set_dtr (struct usb_serial_port *port, unsigned char dtr)
{
	set_command (port, dtr, WHITEHEAT_SET_DTR);
}


static inline void set_break (struct usb_serial_port *port, unsigned char brk)
{
	set_command (port, brk, WHITEHEAT_SET_BREAK);
}


static int __init whiteheat_init (void)
{
	usb_serial_register (&whiteheat_fake_device);
	usb_serial_register (&whiteheat_device);
	return 0;
}


static void __exit whiteheat_exit (void)
{
	usb_serial_deregister (&whiteheat_fake_device);
	usb_serial_deregister (&whiteheat_device);
}


module_init(whiteheat_init);
module_exit(whiteheat_exit);

MODULE_AUTHOR("Greg Kroah-Hartman <greg@kroah.com>");
MODULE_DESCRIPTION("USB ConnectTech WhiteHEAT driver");
