/*
  Keyspan USB to Serial Converter driver
 
  (C) Copyright (C) 2000
      Hugh Blemings <hugh@linuxcare.com>
   
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  See http://www.linuxcare.com.au/hugh/keyspan.html for more
  information on this driver.
  
  Code in this driver inspired by and in a number of places taken
  from Brian Warner's original Keyspan-PDA driver.

  This driver has been put together with the support of Innosys, Inc.
  and Keyspan, Inc the manufacturers of the Keyspan USB-serial products.
  Thanks Guys :)
  
  Thanks to Paulus for miscellaneous tidy ups, some largish chunks
  of much nicer and/or completely new code and (perhaps most uniquely)
  having the patience to sit down and explain why and where he'd changed
  stuff. 
  
  Tip 'o the hat to Linuxcare for supporting staff in their work on
  open source projects.

  Change History
   (11/01/2000) Adam J. Richter
	usb_device_id table support.
   
    Tue Oct 10 23:15:33 EST 2000 Hugh
      Merged Paul's changes with my USA-49W mods.  Work in progress
      still...
  
    Wed Jul 19 14:00:42 EST 2000 gkh
      Added module_init and module_exit functions to handle the fact that
      this driver is a loadable module now.
 
    Tue Jul 18 16:14:52 EST 2000 Hugh
      Basic character input/output for USA-19 now mostly works,
      fixed at 9600 baud for the moment.

    Sat Jul  8 11:11:48 EST 2000 Hugh
      First public release - nothing works except the firmware upload.
      Tested on PPC and x86 architectures, seems to behave...
*/


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

#define DEBUG
/*  #ifdef CONFIG_USB_SERIAL_DEBUG */
	#define DEBUG
/*  #endif */
#include <linux/usb.h>

#include "usb-serial.h"
#include "keyspan.h"

#define INSTAT_BUFLEN	32
#define GLOCONT_BUFLEN	64

	/* Per device and per port private data */
struct keyspan_serial_private {
	/* number of active ports */
	atomic_t	active_count;

	const keyspan_device_details	*device_details;

	urb_t		*instat_urb;
	char		instat_buf[INSTAT_BUFLEN];

	/* XXX this one probably will need a lock */
	urb_t		*glocont_urb;
	char		glocont_buf[GLOCONT_BUFLEN];
};

struct keyspan_port_private {
	/* Keep track of which input & output endpoints to use */
	int		in_flip;
	int		out_flip;

	/* Keep duplicate of device details in each port
	   structure as well - simplifies some of the
	   callback functions etc. */
	const keyspan_device_details	*device_details;

	/* Input endpoints and buffer for this port */
	urb_t		*in_urbs[2];
	char		in_buffer[2][64];
	/* Output endpoints and buffer for this port */
	urb_t		*out_urbs[2];
	char		out_buffer[2][64];

	/* Input ack endpoint */
	urb_t		*inack_urb;
	char		inack_buffer[1];

	/* Output control endpoint */
	urb_t		*outcont_urb;
	char		outcont_buffer[64];

	/* Settings for the port */
	int		baud;
	int		old_baud;
	unsigned int	cflag;
	enum		{flow_none, flow_cts, flow_xon} flow_control;
	int		rts_state;	/* Handshaking pins (outputs) */
	int		dtr_state;
	int		cts_state;	/* Handshaking pins (inputs) */
	int		dsr_state;
	int		dcd_state;
	int		ri_state;

	unsigned long	tx_start_time[2];
	int		resend_cont;	/* need to resend control packet */
};

	
/* Include Keyspan message headers.  All current Keyspan Adapters
   make use of one of three message formats which are referred
   to as USA-26, USA-28 and USA-49 by Keyspan and within this driver. */
#include "keyspan_usa26msg.h"
#include "keyspan_usa28msg.h"
#include "keyspan_usa49msg.h"
	
/* If you don't get debugging output, uncomment the following
   two lines to enable cheat. */
#if 0
  #undef 	dbg 
  #define	dbg	printk 
#endif

static void keyspan_send_setup(struct usb_serial_port *port);

/* Functions used by new usb-serial code. */
int keyspan_init (void)
{
	usb_serial_register (&keyspan_usa18x_pre_device);
	usb_serial_register (&keyspan_usa19_pre_device);
	usb_serial_register (&keyspan_usa19w_pre_device);
	usb_serial_register (&keyspan_usa28_pre_device);
	usb_serial_register (&keyspan_usa28x_pre_device);
	usb_serial_register (&keyspan_usa49w_pre_device);

	usb_serial_register (&keyspan_usa18x_device);
	usb_serial_register (&keyspan_usa19_device);
	usb_serial_register (&keyspan_usa19w_device);
	usb_serial_register (&keyspan_usa28_device);
	usb_serial_register (&keyspan_usa28x_device);
	usb_serial_register (&keyspan_usa49w_device);
	return 0;
}

void keyspan_exit (void)
{
	usb_serial_deregister (&keyspan_usa18x_pre_device);
	usb_serial_deregister (&keyspan_usa19_pre_device);
	usb_serial_deregister (&keyspan_usa19w_pre_device);
	usb_serial_deregister (&keyspan_usa28_pre_device);
	usb_serial_deregister (&keyspan_usa28x_pre_device);
	usb_serial_deregister (&keyspan_usa49w_pre_device);

	usb_serial_deregister (&keyspan_usa18x_device);
	usb_serial_deregister (&keyspan_usa19_device);
	usb_serial_deregister (&keyspan_usa19w_device);
	usb_serial_deregister (&keyspan_usa28_device);
	usb_serial_deregister (&keyspan_usa28x_device);
	usb_serial_deregister (&keyspan_usa49w_device);
}

module_init(keyspan_init);
module_exit(keyspan_exit);

static void keyspan_rx_throttle (struct usb_serial_port *port)
{
	dbg("keyspan_rx_throttle port %d", port->number);
}


static void keyspan_rx_unthrottle (struct usb_serial_port *port)
{
	dbg("keyspan_rx_unthrottle port %d", port->number);
}


static void keyspan_break_ctl (struct usb_serial_port *port, int break_state)
{
	dbg("keyspan_break_ctl");
}


static void keyspan_set_termios (struct usb_serial_port *port, 
				     struct termios *old_termios)
{
	int				baud_rate;
	struct keyspan_port_private 	*p_priv;
	const keyspan_device_details	*d_details;
	unsigned int 			cflag;

	/*  dbg(__FUNCTION__ "."); */

	p_priv = (struct keyspan_port_private *)(port->private);
	d_details = p_priv->device_details;
	cflag = port->tty->termios->c_cflag;

	/* Baud rate calculation takes baud rate as an integer
	   so other rates can be generated if desired. */
	baud_rate = tty_get_baud_rate(port->tty);
	/* If no match or invalid, don't change */		
	if (baud_rate >= 0
	    && d_details->calculate_baud_rate(baud_rate, d_details->baudclk,
				NULL, NULL, NULL) == KEYSPAN_BAUD_RATE_OK) {
		/* FIXME - more to do here to ensure rate changes cleanly */
		p_priv->baud = baud_rate;
	}

	/* set CTS/RTS handshake etc. */
	p_priv->cflag = cflag;
	p_priv->flow_control = (cflag & CRTSCTS)? flow_cts: flow_none;

	keyspan_send_setup(port);
}

static int keyspan_ioctl(struct usb_serial_port *port, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	unsigned int			value, set;
	struct keyspan_port_private 	*p_priv;

	p_priv = (struct keyspan_port_private *)(port->private);
	
	switch (cmd) {
	case TIOCMGET:
		value = ((p_priv->rts_state) ? TIOCM_RTS : 0) |
			((p_priv->dtr_state) ? TIOCM_DTR : 0) |
			((p_priv->cts_state) ? TIOCM_CTS : 0) |
			((p_priv->dsr_state) ? TIOCM_DSR : 0) |
			((p_priv->dcd_state) ? TIOCM_CAR : 0) |
			((p_priv->ri_state) ? TIOCM_RNG : 0); 

		if (put_user(value, (unsigned int *) arg))
			return -EFAULT;
		return 0;
	
	case TIOCMSET:
		if (get_user(value, (unsigned int *) arg))
			return -EFAULT;
		p_priv->rts_state = ((value & TIOCM_RTS) ? 1 : 0);
		p_priv->dtr_state = ((value & TIOCM_DTR) ? 1 : 0);
		keyspan_send_setup(port);
		return 0;

	case TIOCMBIS:
	case TIOCMBIC:
		if (get_user(value, (unsigned int *) arg))
			return -EFAULT;
		set = (cmd == TIOCMBIS);
		if (value & TIOCM_RTS)
			p_priv->rts_state = set;
		if (value & TIOCM_DTR)
			p_priv->dtr_state = set;
		keyspan_send_setup(port);
		return 0;
	}

	return -ENOIOCTLCMD;
}

	/* Write function is generic for the three protocols used
	   with only a minor change for usa49 required */
static int keyspan_write(struct usb_serial_port *port, int from_user, 
			 const unsigned char *buf, int count)
{
	struct keyspan_port_private 	*p_priv;
	const keyspan_device_details	*d_details;
	int				flip;
	int left, todo;
	urb_t *this_urb;
	int err;

	p_priv = (struct keyspan_port_private *)(port->private);
	d_details = p_priv->device_details;

#if 0
	dbg(__FUNCTION__ " for port %d (%d chars [%x]), flip=%d",
	    port->number, count, buf[0], p_priv->out_flip);
#endif

	for (left = count; left > 0; left -= todo) {
		todo = left;
		if (todo > 63)
			todo = 63;

		flip = p_priv->out_flip;
	
		/* Check we have a valid urb/endpoint before we use it... */
		if ((this_urb = p_priv->out_urbs[flip]) == 0) {
			/* no bulk out, so return 0 bytes written */
			dbg(__FUNCTION__ " no output urb :(");
			return count;
		}

		if (this_urb->status == -EINPROGRESS) {
			if (this_urb->transfer_flags & USB_ASYNC_UNLINK)
				break;
			if (jiffies - p_priv->tx_start_time[flip] < 10 * HZ)
				break;
			this_urb->transfer_flags |= USB_ASYNC_UNLINK;
			usb_unlink_urb(this_urb);
			break;
		}

		/* First byte in buffer is "last flag" - unused so
		   for now so set to zero */
		((char *)this_urb->transfer_buffer)[0] = 0;

		if (from_user) {
			copy_from_user(this_urb->transfer_buffer + 1, buf, todo);
		} else {
			memcpy (this_urb->transfer_buffer + 1, buf, todo);
		}
		buf += todo;

		/* send the data out the bulk port */
		this_urb->transfer_buffer_length = todo + 1;

		this_urb->transfer_flags &= ~USB_ASYNC_UNLINK;
		this_urb->dev = port->serial->dev;
		if ((err = usb_submit_urb(this_urb)) != 0) {
			dbg("usb_submit_urb(write bulk) failed (%d)", err);
		}
		p_priv->tx_start_time[flip] = jiffies;

		/* Flip for next time if usa26 or usa28 interface
		   (not used on usa49) */
		p_priv->out_flip = (flip + 1) & d_details->outdat_endp_flip;
	}

	return count - left;
}

static void	usa26_indat_callback(struct urb *urb)
{
	int			i, err;
	int			endpoint;
	struct usb_serial_port	*port;
	struct tty_struct	*tty;
	unsigned char 		*data = urb->transfer_buffer;

	/*  dbg (__FUNCTION__); */

	endpoint = usb_pipeendpoint(urb->pipe);

	if (urb->status) {
		dbg(__FUNCTION__ "nonzero status: %x on endpoint %d.",
			      		urb->status, endpoint);
		return;
	}

	port = (struct usb_serial_port *) urb->context;
	tty = port->tty;
	if (urb->actual_length) {
		if (data[0] == 0) {
			/* no error on any byte */
			for (i = 1; i < urb->actual_length ; ++i) {
				tty_insert_flip_char(tty, data[i], 0);
			}
		} else {
			/* some bytes had errors, every byte has status */
			for (i = 0; i + 1 < urb->actual_length; i += 2) {
				int stat = data[i], flag = 0;
				if (stat & RXERROR_OVERRUN)
					flag |= TTY_OVERRUN;
				if (stat & RXERROR_FRAMING)
					flag |= TTY_FRAME;
				if (stat & RXERROR_PARITY)
					flag |= TTY_PARITY;
				/* XXX should handle break (0x10) */
				tty_insert_flip_char(tty, data[i+1], flag);
			}
		}
		tty_flip_buffer_push(tty);
	}
				
		/* Resubmit urb so we continue receiving */
	urb->dev = port->serial->dev;
	if ((err = usb_submit_urb(urb)) != 0) {
		dbg(__FUNCTION__ "resubmit read urb failed. (%d)", err);
	}
	return;
}

	/* Outdat handling is common for usa26, usa28 and usa49 messages */
static void	usa2x_outdat_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port = (struct usb_serial_port *) urb->context;
	p_priv = (struct keyspan_port_private *)(port->private);
	/*  dbg (__FUNCTION__ " urb %d", urb == p_priv->out_urbs[1]); */

	if (port->active) {
		queue_task(&port->tqueue, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
}

static void	usa26_inack_callback(struct urb *urb)
{
	dbg (__FUNCTION__);
	
}

static void	usa26_outcont_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port = (struct usb_serial_port *) urb->context;
	p_priv = (struct keyspan_port_private *)(port->private);

	if (p_priv->resend_cont) {
		/*  dbg (__FUNCTION__ " sending setup"); */
		keyspan_usa26_send_setup(port->serial, port);
	}
}

static void	usa26_instat_callback(struct urb *urb)
{
	unsigned char 				*data = urb->transfer_buffer;
	keyspan_usa26_portStatusMessage		*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state, err;

	serial = (struct usb_serial *) urb->context;

	if (urb->status) {
		dbg(__FUNCTION__ " nonzero status: %x", urb->status);
		return;
	}
	if (urb->actual_length != 9) {
		dbg(__FUNCTION__ " %d byte report??", urb->actual_length);
		goto exit;
	}

	msg = (keyspan_usa26_portStatusMessage *)data;

#if 0
	dbg(__FUNCTION__ " port status: port %d cts %d dcd %d dsr %d ri %d toff %d txoff %d rxen %d cr %d",
	    msg->port, msg->hskia_cts, msg->gpia_dcd, msg->dsr, msg->ri, msg->_txOff,
	    msg->_txXoff, msg->rxEnabled, msg->controlResponse);
#endif

	/* Now do something useful with the data */


	/* Check port number from message and retrieve private data */	
	if (msg->port >= serial->num_ports) {
		dbg ("Unexpected port number %d", msg->port);
		goto exit;
	}
	port = &serial->port[msg->port];
	p_priv = (struct keyspan_port_private *)(port->private);
	
	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->hskia_cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->gpia_dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (port->tty && !C_CLOCAL(port->tty)
	    && old_dcd_state != p_priv->dcd_state) {
		if (old_dcd_state)
			tty_hangup(port->tty);
		/*  else */
		/*	wake_up_interruptible(&p_priv->open_wait); */
	}
	
exit:
	/* Resubmit urb so we continue receiving */
	urb->dev = serial->dev;
	if ((err = usb_submit_urb(urb)) != 0) {
		dbg(__FUNCTION__ "resubmit read urb failed. (%d)", err);
	}
}

static void	usa26_glocont_callback(struct urb *urb)
{
	dbg (__FUNCTION__);
	
}


static void     usa28_indat_callback(struct urb *urb)
{
	int                     i, err;
	struct usb_serial_port  *port;
	struct tty_struct       *tty;
	unsigned char           *data;
	struct keyspan_port_private             *p_priv;

	/*  dbg (__FUNCTION__); */

	port = (struct usb_serial_port *) urb->context;
	p_priv = (struct keyspan_port_private *)(port->private);
	data = urb->transfer_buffer;

	if (urb != p_priv->in_urbs[p_priv->in_flip])
		return;

	do {
		if (urb->status) {
			dbg(__FUNCTION__ "nonzero status: %x on endpoint
%d.",
			    urb->status, usb_pipeendpoint(urb->pipe));
			return;
		}

		port = (struct usb_serial_port *) urb->context;
		p_priv = (struct keyspan_port_private *)(port->private);
		data = urb->transfer_buffer;

		tty = port->tty;
		if (urb->actual_length) {
			for (i = 0; i < urb->actual_length ; ++i) {
				tty_insert_flip_char(tty, data[i], 0);
			}
			tty_flip_buffer_push(tty);
		}

		/* Resubmit urb so we continue receiving */
		urb->dev = port->serial->dev;
		if ((err = usb_submit_urb(urb)) != 0) {
			dbg(__FUNCTION__ "resubmit read urb failed. (%d)",
err);
		}
		p_priv->in_flip ^= 1;

		urb = p_priv->in_urbs[p_priv->in_flip];
	} while (urb->status != -EINPROGRESS);
}

static void	usa28_inack_callback(struct urb *urb)
{
	dbg (__FUNCTION__);
}

static void	usa28_outcont_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port = (struct usb_serial_port *) urb->context;
	p_priv = (struct keyspan_port_private *)(port->private);

	if (p_priv->resend_cont) {
		dbg (__FUNCTION__ " sending setup");
		keyspan_usa28_send_setup(port->serial, port);
	}
}

static void	usa28_instat_callback(struct urb *urb)
{
	int					err;
	unsigned char 				*data = urb->transfer_buffer;
	keyspan_usa28_portStatusMessage		*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state;

	serial = (struct usb_serial *) urb->context;

	if (urb->status) {
		dbg(__FUNCTION__ " nonzero status: %x", urb->status);
		return;
	}

	if (urb->actual_length != sizeof(struct keyspan_usa28_portStatusMessage)) {
		dbg(__FUNCTION__ " bad length %d", urb->actual_length);
		goto exit;
	}

	/*dbg(__FUNCTION__ " %x %x %x %x %x %x %x %x %x %x %x %x",
	    data[0], data[1], data[2], data[3], data[4], data[5],
	    data[6], data[7], data[8], data[9], data[10], data[11]);*/
	
		/* Now do something useful with the data */
	msg = (keyspan_usa28_portStatusMessage *)data;


		/* Check port number from message and retrieve private data */	
	if (msg->port >= serial->num_ports) {
		dbg ("Unexpected port number %d", msg->port);
		goto exit;
	}
	port = &serial->port[msg->port];
	p_priv = (struct keyspan_port_private *)(port->private);
	
	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (port->tty && !C_CLOCAL(port->tty)
	    && old_dcd_state != p_priv->dcd_state) {
		if (old_dcd_state)
			tty_hangup(port->tty);
		/*  else */
		/*	wake_up_interruptible(&p_priv->open_wait); */
	}

exit:	
		/* Resubmit urb so we continue receiving */
	urb->dev = serial->dev;
	if ((err = usb_submit_urb(urb)) != 0) {
		dbg(__FUNCTION__ "resubmit read urb failed. (%d)", err);
	}
}

static void	usa28_glocont_callback(struct urb *urb)
{
	dbg (__FUNCTION__);
}


static void	usa49_glocont_callback(struct urb *urb)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;
	int i;

	/*  dbg (__FUNCTION__); */

	serial = (struct usb_serial *) urb->context;
	for (i = 0; i < serial->num_ports; ++i) {
		port = &serial->port[i];
		p_priv = (struct keyspan_port_private *)(port->private);

		if (p_priv->resend_cont) {
			/*  dbg (__FUNCTION__ " sending setup"); */
			keyspan_usa49_send_setup(serial, port);
			break;
		}
	}
}

	/* This is actually called glostat in the Keyspan
	   doco */
static void	usa49_instat_callback(struct urb *urb)
{
	int					err;
	unsigned char 				*data = urb->transfer_buffer;
	keyspan_usa49_portStatusMessage		*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state;

	/*  dbg (__FUNCTION__); */

	serial = (struct usb_serial *) urb->context;

	if (urb->status) {
		dbg(__FUNCTION__ " nonzero status: %x", urb->status);
		return;
	}

	if (urb->actual_length != sizeof(struct keyspan_usa49_portStatusMessage)) {
		dbg(__FUNCTION__ " bad length %d", urb->actual_length);
		goto exit;
	}

	/*dbg(__FUNCTION__ " %x %x %x %x %x %x %x %x %x %x %x",
	    data[0], data[1], data[2], data[3], data[4], data[5],
	    data[6], data[7], data[8], data[9], data[10]);*/
	
		/* Now do something useful with the data */
	msg = (keyspan_usa49_portStatusMessage *)data;

		/* Check port number from message and retrieve private data */	
	if (msg->portNumber >= serial->num_ports) {
		dbg ("Unexpected port number %d", msg->portNumber);
		goto exit;
	}
	port = &serial->port[msg->portNumber];
	p_priv = (struct keyspan_port_private *)(port->private);
	
	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (port->tty && !C_CLOCAL(port->tty)
	    && old_dcd_state != p_priv->dcd_state) {
		if (old_dcd_state)
			tty_hangup(port->tty);
		/*  else */
		/*	wake_up_interruptible(&p_priv->open_wait); */
	}

exit:	
		/* Resubmit urb so we continue receiving */
	urb->dev = serial->dev;

	if ((err = usb_submit_urb(urb)) != 0) {
		dbg(__FUNCTION__ "resubmit read urb failed. (%d)", err);
	}
}

static void	usa49_inack_callback(struct urb *urb)
{
	dbg (__FUNCTION__);
}

static void	usa49_indat_callback(struct urb *urb)
{
	int			i, err;
	int			endpoint;
	struct usb_serial_port	*port;
	struct tty_struct	*tty;
	unsigned char 		*data = urb->transfer_buffer;

	/*  dbg (__FUNCTION__); */

	endpoint = usb_pipeendpoint(urb->pipe);

	if (urb->status) {
		dbg(__FUNCTION__ "nonzero status: %x on endpoint %d.",
			      		urb->status, endpoint);
		return;
	}

	port = (struct usb_serial_port *) urb->context;
	tty = port->tty;
	if (urb->actual_length) {
		if (data[0] == 0) {
			/* no error on any byte */
			for (i = 1; i < urb->actual_length ; ++i) {
				tty_insert_flip_char(tty, data[i], 0);
			}
		} else {
			/* some bytes had errors, every byte has status */
			for (i = 0; i + 1 < urb->actual_length; i += 2) {
				int stat = data[i], flag = 0;
				if (stat & RXERROR_OVERRUN)
					flag |= TTY_OVERRUN;
				if (stat & RXERROR_FRAMING)
					flag |= TTY_FRAME;
				if (stat & RXERROR_PARITY)
					flag |= TTY_PARITY;
				/* XXX should handle break (0x10) */
				tty_insert_flip_char(tty, data[i+1], flag);
			}
		}
		tty_flip_buffer_push(tty);
	}
				
		/* Resubmit urb so we continue receiving */
	urb->dev = port->serial->dev;
	if ((err = usb_submit_urb(urb)) != 0) {
		dbg(__FUNCTION__ "resubmit read urb failed. (%d)", err);
	}
}

/* not used, usa-49 doesn't have per-port control endpoints */
static void	usa49_outcont_callback(struct urb *urb)
{
	dbg (__FUNCTION__);
}



static int keyspan_write_room (struct usb_serial_port *port)
{
//	dbg("keyspan_write_room called");
	return (32);

}


static int keyspan_chars_in_buffer (struct usb_serial_port *port)
{
	return (0);
}


static int keyspan_open (struct usb_serial_port *port, struct file *filp)
{
	struct keyspan_port_private 	*p_priv;
	struct keyspan_serial_private 	*s_priv;
	struct usb_serial 		*serial = port->serial;
	const keyspan_device_details	*d_details;
	int				i, already_active, err;
	unsigned long flags;
	urb_t *urb;

	s_priv = (struct keyspan_serial_private *)(serial->private);
	p_priv = (struct keyspan_port_private *)(port->private);
	d_details = s_priv->device_details;
	
	/*  dbg("keyspan_open called."); */
	MOD_INC_USE_COUNT;

	spin_lock_irqsave (&port->port_lock, flags);
	++port->open_count;
	already_active = port->active;
	port->active = 1;
	spin_unlock_irqrestore (&port->port_lock, flags);

	if (already_active)
		return 0;

	p_priv = (struct keyspan_port_private *)(port->private);

	/* Set some sane defaults */
	p_priv->baud = 9600;
	p_priv->cflag = CREAD | CLOCAL;
	p_priv->flow_control = flow_none;
	p_priv->rts_state = 1;
	p_priv->dtr_state = 1;

	/* Start reading from endpoints */
	for (i = 0; i < 2; i++) {
		if ((urb = p_priv->in_urbs[i]) == NULL)
			continue;
		urb->dev = serial->dev;
		if ((err = usb_submit_urb(urb)) != 0) {
			dbg(__FUNCTION__ " submit urb %d failed (%d)", i, err);
		}
	}
/*    Now done in startup routine
	if (atomic_inc_return(&s_priv->active_count) == 1) {
		s_priv->instat_urb->dev = serial->dev;
		if ((err = usb_submit_urb(s_priv->instat_urb)) != 0) {
			dbg(__FUNCTION__ " submit instat urb failed %d", err);
		}
	}
*/

	keyspan_send_setup(port);

	return (0);
}

static inline void stop_urb(urb_t *urb)
{
	if (urb && urb->status == -EINPROGRESS) {
		urb->transfer_flags &= ~USB_ASYNC_UNLINK;
		usb_unlink_urb(urb);
	}
}

static void keyspan_close(struct usb_serial_port *port, struct file *filp)
{
	int			i;
	struct usb_serial	*serial = port->serial; /* FIXME should so sanity check */
	struct keyspan_serial_private 	*s_priv;
	struct keyspan_port_private 	*p_priv;
	unsigned long flags;

	/*  dbg("keyspan_close called"); */
	s_priv = (struct keyspan_serial_private *)(serial->private);
	p_priv = (struct keyspan_port_private *)(port->private);

	spin_lock_irqsave (&port->port_lock, flags);

	if (--port->open_count <= 0) {
		if (port->active) {
			/* Stop reading/writing urbs */
			stop_urb(p_priv->inack_urb);
			stop_urb(p_priv->outcont_urb);
			for (i = 0; i < 2; i++) {
				stop_urb(p_priv->in_urbs[i]);
				stop_urb(p_priv->out_urbs[i]);
			}
			/* Now done in shutdown 
			if (atomic_dec_return(&s_priv->active_count) <= 0) {
				stop_urb(s_priv->instat_urb);
				stop_urb(s_priv->glocont_urb);
			} */
		}
		port->active = 0;
		port->open_count = 0;
		port->tty = 0;
	}
	spin_unlock_irqrestore (&port->port_lock, flags);

	MOD_DEC_USE_COUNT;
}


	/* download the firmware to a pre-renumeration device */
static int keyspan_fake_startup (struct usb_serial *serial)
{
	int 				response;
	const struct ezusb_hex_record 	*record;
	char				*fw_name;

	dbg("Keyspan startup version %04x product %04x",
	    serial->dev->descriptor.bcdDevice,
	    serial->dev->descriptor.idProduct); 
	
	if ((serial->dev->descriptor.bcdDevice & 0x8000) != 0x8000) {
		dbg("Firmware already loaded.  Quitting.");
		return(1);
	}

		/* Select firmware image on the basis of idProduct */
	switch (serial->dev->descriptor.idProduct) {
	case 0x0101:
		record = &keyspan_usa28_firmware[0];
		fw_name = "USA28";
		break;

	case 0x0102:
		record = &keyspan_usa28x_firmware[0];
		fw_name = "USA28X";
		break;

	case 0x0103:
		record = &keyspan_usa19_firmware[0];
		fw_name = "USA19";
		break;
			     
	case 0x0105:
		record = &keyspan_usa18x_firmware[0];
		fw_name = "USA18X";
		break;
			     
	case 0x0106:
		record = &keyspan_usa19w_firmware[0];
		fw_name = "USA19W";
		break;
		
	case 0x0109:
		record = &keyspan_usa49w_firmware[0];
		fw_name = "USA49W";
		break;

	default:
		record = NULL;
		fw_name = "Unknown";
		break;
	}

	if (record == NULL) {
		err("Required keyspan firmware image (%s) unavailable.", fw_name);
		return(1);
	}

	dbg("Uploading Keyspan %s firmware.", fw_name);

		/* download the firmware image */
	response = ezusb_set_reset(serial, 1);

	while(record->address != 0xffff) {
		response = ezusb_writememory(serial, record->address,
					     (unsigned char *)record->data,
					     record->data_size, 0xa0);
		if (response < 0) {
			err("ezusb_writememory failed for Keyspan"
			    "firmware (%d %04X %p %d)",
			    response, 
			    record->address, record->data, record->data_size);
			break;
		}
		record++;
	}
		/* bring device out of reset. Renumeration will occur in a
		   moment and the new device will bind to the real driver */
	response = ezusb_set_reset(serial, 0);

	/* we don't want this device to have a driver assigned to it. */
	return (1);
}

/* Helper functions used by keyspan_setup_urbs */
static urb_t *keyspan_setup_urb(struct usb_serial *serial, int endpoint,
				int dir, void *ctx, char *buf, int len,
				void (*callback)(urb_t *))
{
	urb_t *urb;

	if (endpoint == -1)
		return NULL;		/* endpoint not needed */

	/*  dbg (__FUNCTION__ " alloc for endpoint %d.", endpoint); */
	urb = usb_alloc_urb(0);		/* No ISO */
	if (urb == NULL) {
		dbg (__FUNCTION__ " alloc for endpoint %d failed.", endpoint);
		return NULL;
	}

		/* Fill URB using supplied data. */
	FILL_BULK_URB(urb, serial->dev,
		      usb_sndbulkpipe(serial->dev, endpoint) | dir,
		      buf, len, callback, ctx);

	return urb;
}

struct callbacks {
	void	(*instat_callback)(urb_t *);
	void	(*glocont_callback)(urb_t *);
	void	(*indat_callback)(urb_t *);
	void	(*outdat_callback)(urb_t *);
	void	(*inack_callback)(urb_t *);
	void	(*outcont_callback)(urb_t *);
} keyspan_callbacks[] = {
	{
		/* msg_usa26 callbacks */
		instat_callback: usa26_instat_callback,
		glocont_callback: usa26_glocont_callback,
		indat_callback: usa26_indat_callback,
		outdat_callback: usa2x_outdat_callback,
		inack_callback: usa26_inack_callback,
		outcont_callback: usa26_outcont_callback,
	}, {
		/* msg_usa28 callbacks */
		instat_callback: usa28_instat_callback,
		glocont_callback: usa28_glocont_callback,
		indat_callback: usa28_indat_callback,
		outdat_callback: usa2x_outdat_callback,
		inack_callback: usa28_inack_callback,
		outcont_callback: usa28_outcont_callback,
	}, {
		/* msg_usa49 callbacks */
		instat_callback: usa49_instat_callback,
		glocont_callback: usa49_glocont_callback,
		indat_callback: usa49_indat_callback,
		outdat_callback: usa2x_outdat_callback,
		inack_callback: usa49_inack_callback,
		outcont_callback: usa49_outcont_callback,
	}
};

	/* Generic setup urbs function that uses
	   data in device_details */
static void keyspan_setup_urbs(struct usb_serial *serial)
{
	int				i, j;
	struct keyspan_serial_private 	*s_priv;
	const keyspan_device_details	*d_details;
	struct usb_serial_port		*port;
	struct keyspan_port_private	*p_priv;
	struct callbacks		*cback;
	int				endp;

	/*  dbg (__FUNCTION__); */

	s_priv = (struct keyspan_serial_private *)(serial->private);
	d_details = s_priv->device_details;

		/* Setup values for the various callback routines */
	cback = &keyspan_callbacks[d_details->msg_format];

		/* Allocate and set up urbs for each one that is in use, 
		   starting with instat endpoints */
	s_priv->instat_urb = keyspan_setup_urb
		(serial, d_details->instat_endpoint, USB_DIR_IN,
		 serial, s_priv->instat_buf, INSTAT_BUFLEN,
		 cback->instat_callback);

	s_priv->glocont_urb = keyspan_setup_urb
		(serial, d_details->glocont_endpoint, USB_DIR_OUT,
		 serial, s_priv->glocont_buf, GLOCONT_BUFLEN,
		 cback->glocont_callback);

		/* Setup endpoints for each port specific thing */
	for (i = 0; i < d_details->num_ports; i ++) {
		port = &serial->port[i];
		p_priv = (struct keyspan_port_private *)(port->private);

		/* Do indat endpoints first, once for each flip */
		endp = d_details->indat_endpoints[i];
		for (j = 0; j <= d_details->indat_endp_flip; ++j, ++endp) {
			p_priv->in_urbs[j] = keyspan_setup_urb
				(serial, endp, USB_DIR_IN, port,
				 p_priv->in_buffer[j], 64,
				 cback->indat_callback);
		}
		for (; j < 2; ++j)
			p_priv->in_urbs[j] = NULL;

		/* outdat endpoints also have flip */
		endp = d_details->outdat_endpoints[i];
		for (j = 0; j <= d_details->outdat_endp_flip; ++j, ++endp) {
			p_priv->out_urbs[j] = keyspan_setup_urb
				(serial, endp, USB_DIR_OUT, port,
				 p_priv->out_buffer[j], 64,
				 cback->outdat_callback);
		}
		for (; j < 2; ++j)
			p_priv->out_urbs[j] = NULL;

		/* inack endpoint */
		p_priv->inack_urb = keyspan_setup_urb
			(serial, d_details->inack_endpoints[i], USB_DIR_IN,
			 port, p_priv->inack_buffer, 1, cback->inack_callback);

		/* outcont endpoint */
		p_priv->outcont_urb = keyspan_setup_urb
			(serial, d_details->outcont_endpoints[i], USB_DIR_OUT,
			 port, p_priv->outcont_buffer, 64,
			 cback->outcont_callback);
	}	

}

/* usa19 function doesn't require prescaler */
static int keyspan_usa19_calc_baud(u32 baud_rate, u32 baudclk,
				   u8 *rate_hi, u8 *rate_low, u8 *prescaler)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
		div,	/* divisor */	
		cnt;	/* inverse of divisor (programmed into 8051) */
		

		/* prevent divide by zero...  */
	if( (b16 = (baud_rate * 16L)) == 0) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}

		/* Any "standard" rate over 57k6 is marginal on the USA-19
		   as we run out of divisor resolution. */
	if (baud_rate > 57600) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}

		/* calculate the divisor and the counter (its inverse) */
	if( (div = (baudclk / b16)) == 0) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}
	else {
		cnt = 0 - div;
	}

	if(div > 0xffff) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}

		/* return the counter values if non-null */
	if (rate_low) {
		*rate_low = (u8) (cnt & 0xff);
	}
	if (rate_hi) {
		*rate_hi = (u8) ((cnt >> 8) & 0xff);
	}
	if (rate_low && rate_hi) {
		dbg (__FUNCTION__ " %d %02x %02x.", baud_rate, *rate_hi, *rate_low);
	}
	
	return (KEYSPAN_BAUD_RATE_OK);
}

static int keyspan_usa19w_calc_baud(u32 baud_rate, u32 baudclk,
				    u8 *rate_hi, u8 *rate_low, u8 *prescaler)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
		clk,	/* clock with 13/8 prescaler */
		div,	/* divisor using 13/8 prescaler */	
		res,	/* resulting baud rate using 13/8 prescaler */
		diff,	/* error using 13/8 prescaler */
		smallest_diff;
	u8	best_prescaler;
	int	i;

	/*  dbg (__FUNCTION__ " %d.", baud_rate); */

		/* prevent divide by zero */
	if( (b16 = baud_rate * 16L) == 0) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}

		/* Calculate prescaler by trying them all and looking
		   for best fit */
		
		/* start with largest possible difference */
	smallest_diff = 0xffffffff;

		/* 0 is an invalid prescaler, used as a flag */
	best_prescaler = 0;

	for(i = 8; i <= 0xff; ++i)
	{
		clk = (baudclk * 8) / (u32) i;
		
		if( (div = clk / b16) == 0) {
			continue;
		}

		res = clk / div;
		diff= (res > b16) ? (res-b16) : (b16-res);

		if(diff < smallest_diff)
		{
			best_prescaler = i;
			smallest_diff = diff;
		}
	}

	if(best_prescaler == 0) {
		return (KEYSPAN_INVALID_BAUD_RATE);
	}

	clk = (baudclk * 8) / (u32) best_prescaler;
	div = clk / b16;

		/* return the divisor and prescaler if non-null */
	if (rate_low) {
		*rate_low = (u8) (div & 0xff);
	}
	if (rate_hi) {
		*rate_hi = (u8) ((div >> 8) & 0xff);
	}
	if (prescaler) {
		*prescaler = best_prescaler;
		/*  dbg(__FUNCTION__ " %d %d", *prescaler, div); */
	}
	return (KEYSPAN_BAUD_RATE_OK);
}

static int keyspan_usa26_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port)
{
	struct keyspan_usa26_portControlMessage	msg;		
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const  keyspan_device_details		*d_details;
	int 					outcont_urb;
	urb_t *this_urb;
	int err;

	/*  dbg (__FUNCTION__); */

	s_priv = (struct keyspan_serial_private *)(serial->private);
	p_priv = (struct keyspan_port_private *)(port->private);
	d_details = s_priv->device_details;

	outcont_urb = d_details->outcont_endpoints[port->number];
	this_urb = p_priv->outcont_urb;

		/* Make sure we have an urb then send the message */
	if (this_urb == NULL) {
		dbg(__FUNCTION__ " oops no urb.");
		return -1;
	}

	p_priv->resend_cont = 1;
	if (this_urb->status == -EINPROGRESS) {
		/*  dbg (__FUNCTION__ " already writing"); */
		return(-1);
	}

	memset(&msg, 0, sizeof (struct keyspan_usa26_portControlMessage));
	
		/* Only set baud rate if it's changed */	
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0xff;
		if (d_details->calculate_baud_rate
		    (p_priv->baud, d_details->baudclk, &msg.baudHi,
		     &msg.baudLo, &msg.prescaler) == KEYSPAN_INVALID_BAUD_RATE ) {
			dbg(__FUNCTION__ "Invalid baud rate %d requested, using 9600.",
			    p_priv->baud);
			msg.baudLo = 0;
			msg.baudHi = 125;	/* Values for 9600 baud */
			msg.prescaler = 10;
		}
		msg.setPrescaler = 0xff;
	}

	msg.lcr = USA_DATABITS_8 | STOPBITS_5678_1;
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD)?
			USA_PARITY_ODD: USA_PARITY_EVEN;
	}
	msg.setLcr = 0xff;

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;
	msg.setFlowControl = 0xff;
	
	msg.forwardingLength = 1;
	msg.xonChar = 17;
	msg.xoffChar = 19;
	
	msg._txOn = 1;
	msg._txOff = 0;
	msg.txFlush = 0;
	msg.txBreak = 0;
	msg.rxOn = 1;
	msg.rxOff = 0;
	msg.rxFlush = 0;
	msg.rxForward = 0;
	/*msg.returnStatus = 1;
	msg.resetDataToggle = 0xff;*/

		/* Do handshaking outputs */	
	msg.setTxTriState_setRts = 0xff;
	msg.txTriState_rts = p_priv->rts_state;

	msg.setHskoa_setDtr = 0xff;
	msg.hskoa_dtr = p_priv->dtr_state;
		
	p_priv->resend_cont = 0;
	memcpy (this_urb->transfer_buffer, &msg, sizeof(msg));
	
	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	this_urb->dev = serial->dev;
	if ((err = usb_submit_urb(this_urb)) != 0) {
		dbg(__FUNCTION__ " usb_submit_urb(setup) failed (%d)", err);
	}
#if 0
	else {
		dbg(__FUNCTION__ " usb_submit_urb(%d) OK %d bytes (end %d)",
		    outcont_urb, this_urb->transfer_buffer_length,
		    usb_pipeendpoint(this_urb->pipe));
	}
#endif

	return (0);
}

static int keyspan_usa28_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port)
{
	struct keyspan_usa28_portControlMessage	msg;		
	struct keyspan_serial_private	 	*s_priv;
	struct keyspan_port_private 		*p_priv;
	const  keyspan_device_details		*d_details;
	urb_t *this_urb;
	int err;

	s_priv = (struct keyspan_serial_private *)(serial->private);
	p_priv = (struct keyspan_port_private *)(port->private);
	d_details = s_priv->device_details;

	/* only do something if we have a bulk out endpoint */
	if ((this_urb = p_priv->outcont_urb) == NULL) {
		dbg(__FUNCTION__ " oops no urb.");
		return -1;
	}

	p_priv->resend_cont = 1;
	if (this_urb->status == -EINPROGRESS) {
		dbg (__FUNCTION__ " already writing");
		return(-1);
	}

	memset(&msg, 0, sizeof (struct keyspan_usa28_portControlMessage));

	msg.setBaudRate = 1;
	if (keyspan_usa19_calc_baud(p_priv->baud, d_details->baudclk,
		&msg.baudHi, &msg.baudLo, NULL) == KEYSPAN_INVALID_BAUD_RATE ) {
		dbg(__FUNCTION__ "Invalid baud rate requested %d.", 9600);
		msg.baudLo = 0xff;
		msg.baudHi = 0xb2;	/* Values for 9600 baud */
	}

	/* If parity is enabled, we must calculate it ourselves. */
	msg.parity = 0;		/* XXX for now */

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;

	/* Do handshaking outputs, DTR is inverted relative to RTS */	
	msg.rts = p_priv->rts_state;
	msg.dtr = p_priv->dtr_state;

	msg.forwardingLength = 1;
	msg.forwardMs = 10;
	msg.breakThreshold = 45;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	msg._txOn = 1;
	msg._txOff = 0;
	msg.txFlush = 0;
	msg.txForceXoff = 0;
	msg.txBreak = 0;
	msg.rxOn = 1;
	msg.rxOff = 0;
	msg.rxFlush = 0;
	msg.rxForward = 0;
	/*msg.returnStatus = 1;
	msg.resetDataToggle = 0xff;*/

	p_priv->resend_cont = 0;
	memcpy (this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	this_urb->dev = serial->dev;
	if ((err = usb_submit_urb(this_urb)) != 0) {
		dbg(__FUNCTION__ " usb_submit_urb(setup) failed");
	}
#if 0
	else {
		dbg(__FUNCTION__ " usb_submit_urb(setup) OK %d bytes",
		    this_urb->transfer_buffer_length);
	}
#endif

	return (0);
}

static int keyspan_usa49_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port)
{
	struct keyspan_usa49_portControlMessage	msg;		
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const  keyspan_device_details		*d_details;
	int 					glocont_urb;
	urb_t *this_urb;
	int err;

	/*  dbg (__FUNCTION__); */

	s_priv = (struct keyspan_serial_private *)(serial->private);
	p_priv = (struct keyspan_port_private *)(port->private);
	d_details = s_priv->device_details;

	glocont_urb = d_details->glocont_endpoint;
	this_urb = s_priv->glocont_urb;

	/*  dbg(__FUNCTION__ " port %d\n", port->number); */

		/* Make sure we have an urb then send the message */
	if (this_urb == NULL) {
		dbg(__FUNCTION__ " oops no urb for port %d.", port->number);
		return -1;
	}

	p_priv->resend_cont = 1;
	if (this_urb->status == -EINPROGRESS) {
		/*  dbg (__FUNCTION__ " already writing"); */
		return(-1);
	}

	memset(&msg, 0, sizeof (struct keyspan_usa49_portControlMessage));

	msg.portNumber = port->number;
	
		/* Only set baud rate if it's changed */	
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0xff;
		if (d_details->calculate_baud_rate
		    (p_priv->baud, d_details->baudclk, &msg.baudHi,
		     &msg.baudLo, &msg.prescaler) == KEYSPAN_INVALID_BAUD_RATE ) {
			dbg(__FUNCTION__ "Invalid baud rate %d requested, using 9600.",
			    p_priv->baud);
			msg.baudLo = 0;
			msg.baudHi = 125;	/* Values for 9600 baud */
			msg.prescaler = 10;
		}
		//msg.setPrescaler = 0xff;
	}

	msg.lcr = USA_DATABITS_8 | STOPBITS_5678_1;
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD)?
			USA_PARITY_ODD: USA_PARITY_EVEN;
	}
	msg.setLcr = 0xff;

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;
	msg.setFlowControl = 0xff;
	
	msg.forwardingLength = 1;
	msg.xonChar = 17;
	msg.xoffChar = 19;
	
	msg._txOn = 1;
	msg._txOff = 0;
	msg.txFlush = 0;
	msg.txBreak = 0;
	msg.rxOn = 1;
	msg.rxOff = 0;
	msg.rxFlush = 0;
	msg.rxForward = 0;
	msg.enablePort = 0xff;
	msg.disablePort = 0;

		/* Do handshaking outputs */	
	msg.setRts = 0xff;
	msg.rts = p_priv->rts_state;

	msg.setDtr = 0xff;
	msg.dtr = p_priv->dtr_state;
		
	p_priv->resend_cont = 0;
	memcpy (this_urb->transfer_buffer, &msg, sizeof(msg));
	
	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	this_urb->dev = serial->dev;
	if ((err = usb_submit_urb(this_urb)) != 0) {
		dbg(__FUNCTION__ " usb_submit_urb(setup) failed (%d)", err);
	}
#if 0
	else {
		dbg(__FUNCTION__ " usb_submit_urb(%d) OK %d bytes (end %d)",
		    outcont_urb, this_urb->transfer_buffer_length,
		    usb_pipeendpoint(this_urb->pipe));
	}
#endif

	return (0);
}

static void keyspan_send_setup(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct keyspan_serial_private 	*s_priv;
	const keyspan_device_details	*d_details;

	s_priv = (struct keyspan_serial_private *)(serial->private);
	d_details = s_priv->device_details;

	switch (d_details->msg_format) {
	case msg_usa26:
		keyspan_usa26_send_setup(serial, port);
		break;
	case msg_usa28:
		keyspan_usa28_send_setup(serial, port);
		break;
	case msg_usa49:
		keyspan_usa49_send_setup(serial, port);
		break;
	}
}

/* Gets called by the "real" driver (ie once firmware is loaded
   and renumeration has taken place. */
static int keyspan_startup (struct usb_serial *serial)
{
	int				i, err;
	struct usb_serial_port		*port;
	struct keyspan_serial_private 	*s_priv;
	struct keyspan_port_private	*p_priv;
	const keyspan_device_details	*d_details;

	/*  dbg("keyspan_startup called."); */

	for (i = 0; (d_details = keyspan_devices[i]) != NULL; ++i)
		if (d_details->product_id == serial->dev->descriptor.idProduct)
			break;
	if (d_details == NULL) {
		printk(KERN_ERR __FUNCTION__ ": unknown product id %x\n",
		       serial->dev->descriptor.idProduct);
		return 1;
	}

	/* Setup private data for serial driver */
	serial->private = kmalloc(sizeof(struct keyspan_serial_private),
				  GFP_KERNEL);
	if (!serial->private) {
		dbg(__FUNCTION__ "kmalloc for keyspan_serial_private failed.");
		return (1);
	}
	memset(serial->private, 0, sizeof(struct keyspan_serial_private));

	s_priv = (struct keyspan_serial_private *)(serial->private);
	s_priv->device_details = d_details;
		
	/* Now setup per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = &serial->port[i];
		port->private = kmalloc(sizeof(struct keyspan_port_private),
					GFP_KERNEL);
		if (!port->private) {
			dbg(__FUNCTION__ "kmalloc for keyspan_port_private (%d) failed!.", i);
			return (1);
		}
		memset(port->private, 0, sizeof(struct keyspan_port_private));
		p_priv = (struct keyspan_port_private *)(port->private);
		p_priv->device_details = d_details;
	}

	keyspan_setup_urbs(serial);

	s_priv->instat_urb->dev = serial->dev;
	if ((err = usb_submit_urb(s_priv->instat_urb)) != 0) {
		dbg(__FUNCTION__ " submit instat urb failed %d", err);
	}
			
	return (0);
}

static void keyspan_shutdown (struct usb_serial *serial)
{
	int				i, j;
	struct usb_serial_port		*port;
	struct keyspan_serial_private 	*s_priv;
	struct keyspan_port_private	*p_priv;

	/*  dbg("keyspan_shutdown called"); */

	s_priv = (struct keyspan_serial_private *)(serial->private);

	/* Stop reading/writing urbs */
	stop_urb(s_priv->instat_urb);
	stop_urb(s_priv->glocont_urb);
	for (i = 0; i < serial->num_ports; ++i) {
		port = &serial->port[i];
		p_priv = (struct keyspan_port_private *)(port->private);
		stop_urb(p_priv->inack_urb);
		stop_urb(p_priv->outcont_urb);
		for (j = 0; j < 2; j++) {
			stop_urb(p_priv->in_urbs[j]);
			stop_urb(p_priv->out_urbs[j]);
		}
	}

	/* Now free them */
	if (s_priv->instat_urb)
		usb_free_urb(s_priv->instat_urb);
	if (s_priv->glocont_urb)
		usb_free_urb(s_priv->glocont_urb);
	for (i = 0; i < serial->num_ports; ++i) {
		port = &serial->port[i];
		p_priv = (struct keyspan_port_private *)(port->private);
		if (p_priv->inack_urb)
			usb_free_urb(p_priv->inack_urb);
		if (p_priv->outcont_urb)
			usb_free_urb(p_priv->outcont_urb);
		for (j = 0; j < 2; j++) {
			if (p_priv->in_urbs[j])
				usb_free_urb(p_priv->in_urbs[j]);
			if (p_priv->out_urbs[j])
				usb_free_urb(p_priv->out_urbs[j]);
		}
	}

	/*  dbg("Freeing serial->private."); */
	kfree(serial->private);

	/*  dbg("Freeing port->private."); */
	/* Now free per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = &serial->port[i];
		while (port->open_count > 0) {
			--port->open_count;
			MOD_DEC_USE_COUNT;
		}
		kfree(port->private);
	}
}
