/*
 * USB Cypress M8 driver
 *
 * 	Copyright (C) 2004
 * 	    Lonnie Mendez (dignome@gmail.com) 
 *	Copyright (C) 2003,2004
 *	    Neil Whelchel (koyama@firstlight.net)
 *
 * 	This program is free software; you can redistribute it and/or modify
 * 	it under the terms of the GNU General Public License as published by
 * 	the Free Software Foundation; either version 2 of the License, or
 * 	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * See http://geocities.com/i0xox0i for information on this driver and the
 * earthmate usb device.
 *
 *
 *  Lonnie Mendez <dignome@gmail.com>
 *  04-10-2004
 *	Driver modified to support dynamic line settings.  Various improvments
 *      and features.
 *
 *  Neil Whelchel
 *  10-2003
 *	Driver first released.
 *
 *
 * Long Term TODO:
 *	Improve transfer speeds - both read/write are somewhat slow
 *   at this point.
 */

/* Neil Whelchel wrote the cypress m8 implementation */
/* Thanks to cypress for providing references for the hid reports. */
/* Thanks to Jiang Zhang for providing links and for general help. */
/* Code originates and was built up from ftdi_sio, belkin, pl2303 and others. */


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/serial.h>

#ifdef CONFIG_USB_SERIAL_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif

static int stats;

#include "usb-serial.h"
#include "cypress_m8.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.06"
#define DRIVER_AUTHOR "Lonnie Mendez <dignome@gmail.com>, Neil Whelchel <koyama@firstlight.net>"
#define DRIVER_DESC "Cypress USB to Serial Driver"

static struct usb_device_id id_table_earthmate [] = {
	{ USB_DEVICE(VENDOR_ID_DELORME, PRODUCT_ID_EARTHMATEUSB) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_cyphidcomrs232 [] = {
	{ USB_DEVICE(VENDOR_ID_CYPRESS, PRODUCT_ID_CYPHIDCOM) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(VENDOR_ID_DELORME, PRODUCT_ID_EARTHMATEUSB) },
	{ USB_DEVICE(VENDOR_ID_CYPRESS, PRODUCT_ID_CYPHIDCOM) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

static struct usb_driver cypress_driver = {
	.name =		"cypress",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
};

struct cypress_private {
	spinlock_t lock;		   /* private lock */
	int chiptype;			   /* identifier of device, for quirks/etc */
	int bytes_in;			   /* used for statistics */
	int bytes_out;			   /* used for statistics */
	int cmd_count;			   /* used for statistics */
	int cmd_ctrl;			   /* always set this to 1 before issuing a command */
	int termios_initialized;
	__u8 line_control;	   	   /* holds dtr / rts value */
	__u8 current_status;	   	   /* received from last read - info on dsr,cts,cd,ri,etc */
	__u8 current_config;	   	   /* stores the current configuration byte */
	__u8 rx_flags;			   /* throttling - used from whiteheat/ftdi_sio */
	int baud_rate;			   /* stores current baud rate in integer form */
	int cbr_mask;			   /* stores current baud rate in masked form */
	int isthrottled;		   /* if throttled, discard reads */
	wait_queue_head_t delta_msr_wait;  /* used for TIOCMIWAIT */
	char prev_status, diff_status;	   /* used for TIOCMIWAIT */
	/* we pass a pointer to this as the arguement sent to cypress_set_termios old_termios */
	struct termios tmp_termios; 	   /* stores the old termios settings */
	int write_interval;		   /* interrupt out write interval, as obtained from interrupt_out_urb */
	int writepipe;			   /* used for clear halt, if necessary */
};

/* function prototypes for the Cypress USB to serial device */
static int  cypress_earthmate_startup	(struct usb_serial *serial);
static int  cypress_hidcom_startup	(struct usb_serial *serial);
static void cypress_shutdown		(struct usb_serial *serial);
static int  cypress_open		(struct usb_serial_port *port, struct file *filp);
static void cypress_close		(struct usb_serial_port *port, struct file *filp);
static int  cypress_write		(struct usb_serial_port *port, const unsigned char *buf, int count);
static int  cypress_write_room		(struct usb_serial_port *port);
static int  cypress_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void cypress_set_termios		(struct usb_serial_port *port, struct termios * old);
static int  cypress_tiocmget		(struct usb_serial_port *port, struct file *file);
static int  cypress_tiocmset		(struct usb_serial_port *port, struct file *file, unsigned int set, unsigned int clear);
static int  cypress_chars_in_buffer	(struct usb_serial_port *port);
static void cypress_throttle		(struct usb_serial_port *port);
static void cypress_unthrottle		(struct usb_serial_port *port);
static void cypress_read_int_callback	(struct urb *urb, struct pt_regs *regs);
static void cypress_write_int_callback	(struct urb *urb, struct pt_regs *regs);
static int  mask_to_rate		(unsigned mask);
static unsigned rate_to_mask		(int rate);

static struct usb_serial_device_type cypress_earthmate_device = {
	.owner =			THIS_MODULE,
	.name =				"DeLorme Earthmate USB",
	.short_name =			"earthmate",
	.id_table =			id_table_earthmate,
	.num_interrupt_in = 		1,
	.num_interrupt_out =		1,
	.num_bulk_in =			NUM_DONT_CARE,
	.num_bulk_out =			NUM_DONT_CARE,
	.num_ports =			1,
	.attach =			cypress_earthmate_startup,
	.shutdown =			cypress_shutdown,
	.open =				cypress_open,
	.close =			cypress_close,
	.write =			cypress_write,
	.write_room =			cypress_write_room,
	.ioctl =			cypress_ioctl,
	.set_termios =			cypress_set_termios,
	.tiocmget =			cypress_tiocmget,
	.tiocmset =			cypress_tiocmset,
	.chars_in_buffer =		cypress_chars_in_buffer,
	.throttle =		 	cypress_throttle,
	.unthrottle =			cypress_unthrottle,
	.read_int_callback =		cypress_read_int_callback,
	.write_int_callback =		cypress_write_int_callback,
};

static struct usb_serial_device_type cypress_hidcom_device = {
	.owner =			THIS_MODULE,
	.name =				"HID->COM RS232 Adapter",
	.short_name =			"cyphidcom",
	.id_table =			id_table_cyphidcomrs232,
	.num_interrupt_in =		1,
	.num_interrupt_out =		1,
	.num_bulk_in =			NUM_DONT_CARE,
	.num_bulk_out =			NUM_DONT_CARE,
	.num_ports =			1,
	.attach =			cypress_hidcom_startup,
	.shutdown =			cypress_shutdown,
	.open =				cypress_open,
	.close =			cypress_close,
	.write =			cypress_write,
	.write_room =			cypress_write_room,
	.ioctl =			cypress_ioctl,
	.set_termios =			cypress_set_termios,
	.tiocmget =			cypress_tiocmget,
	.tiocmset =			cypress_tiocmset,
	.chars_in_buffer =		cypress_chars_in_buffer,
	.throttle =			cypress_throttle,
	.unthrottle =			cypress_unthrottle,
	.read_int_callback =		cypress_read_int_callback,
	.write_int_callback =		cypress_write_int_callback,
};


/*****************************************************************************
 * Cypress serial helper functions
 *****************************************************************************/


/* This function can either set or retreive the current serial line settings */
static int cypress_serial_control (struct usb_serial_port *port, unsigned baud_mask, int data_bits, int stop_bits,
				   int parity_enable, int parity_type, int reset, int cypress_request_type)
{
	int i, n_baud_rate = 0, retval = 0;
	struct cypress_private *priv;
	__u8 feature_buffer[5];
	__u8 config;
	unsigned long flags;

	dbg("%s", __FUNCTION__);
	
	priv = usb_get_serial_port_data(port);

	switch(cypress_request_type) {
		case CYPRESS_SET_CONFIG:

			/*
			 * The general purpose firmware for the Cypress M8 allows for a maximum speed
 			 * of 57600bps (I have no idea whether DeLorme chose to use the general purpose
			 * firmware or not), if you need to modify this speed setting for your own
			 * project please add your own chiptype and modify the code likewise.  The
			 * Cypress HID->COM device will work successfully up to 115200bps.
			 */
			if (baud_mask != priv->cbr_mask) {
				dbg("%s - baud rate is changing", __FUNCTION__);
				if ( priv->chiptype == CT_EARTHMATE ) {
					/* 300 and 600 baud rates are supported under the generic firmware,
					 * but are not used with NMEA and SiRF protocols */
					
					if ( (baud_mask == B300) || (baud_mask == B600) ) {
						err("%s - failed setting baud rate, unsupported speed (default to 4800)",
						    __FUNCTION__);
						n_baud_rate = 4800;
					} else if ( (n_baud_rate = mask_to_rate(baud_mask)) == -1) {
						err("%s - failed setting baud rate, unsupported speed (default to 4800)",
						    __FUNCTION__);
						n_baud_rate = 4800;
					}
				} else if (priv->chiptype == CT_CYPHIDCOM) {
					if ( (n_baud_rate = mask_to_rate(baud_mask)) == -1) {
						err("%s - failed setting baud rate, unsupported speed (default to 4800)",
						    __FUNCTION__);
						n_baud_rate = 4800;
					}
				} else if (priv->chiptype == CT_GENERIC) {
					if ( (n_baud_rate = mask_to_rate(baud_mask)) == -1) {
						err("%s - failed setting baud rate, unsupported speed (default to 4800)",
						    __FUNCTION__);
						n_baud_rate = 4800;
					}
				} else {
					info("%s - please define your chiptype, using 4800bps default", __FUNCTION__);
					n_baud_rate = 4800;
				}
			} else {  /* baud rate not changing, keep the old */
				n_baud_rate = priv->baud_rate;
			}
			dbg("%s - baud rate is being sent as %d", __FUNCTION__, n_baud_rate);

			
			/*
			 * This algorithm accredited to Jiang Jay Zhang... thanks for all the help!
			 */
			for (i = 0; i < 4; ++i) {
				feature_buffer[i] = ( n_baud_rate >> (i*8) & 0xFF );
			}

			config = 0;	                 // reset config byte
			config |= data_bits;	         // assign data bits in 2 bit space ( max 3 )
			/* 1 bit gap */
			config |= (stop_bits << 3);      // assign stop bits in 1 bit space
			config |= (parity_enable << 4);  // assign parity flag in 1 bit space
			config |= (parity_type << 5); 	 // assign parity type in 1 bit space
			/* 1 bit gap */
			config |= (reset << 7);		 // assign reset at end of byte, 1 bit space

			feature_buffer[4] = config;
				
			dbg("%s - device is being sent this feature report:", __FUNCTION__);
			dbg("%s - %02X - %02X - %02X - %02X - %02X", __FUNCTION__, feature_buffer[0], feature_buffer[1],
		            feature_buffer[2], feature_buffer[3], feature_buffer[4]);
			
			retval = usb_control_msg (port->serial->dev, usb_sndctrlpipe(port->serial->dev, 0),
					  	  HID_REQ_SET_REPORT, USB_DIR_OUT | USB_RECIP_INTERFACE | USB_TYPE_CLASS,
					  	  0x0300, 0, feature_buffer, 5, 500);

			if (retval != 5)
				err("%s - failed sending serial line settings - %d", __FUNCTION__, retval);
			else {
				spin_lock_irqsave(&priv->lock, flags);
				priv->baud_rate = n_baud_rate;
				priv->cbr_mask = baud_mask;
				priv->current_config = config;
				++priv->cmd_count;
				spin_unlock_irqrestore(&priv->lock, flags);
			}
		break;
		case CYPRESS_GET_CONFIG:
			dbg("%s - retreiving serial line settings", __FUNCTION__);
			/* reset values in feature buffer */
			memset(feature_buffer, 0, 5);

			retval = usb_control_msg (port->serial->dev, usb_rcvctrlpipe(port->serial->dev, 0),
						  HID_REQ_GET_REPORT, USB_DIR_IN | USB_RECIP_INTERFACE | USB_TYPE_CLASS,
						  0x0300, 0, feature_buffer, 5, 500);
			if (retval != 5) {
				err("%s - failed to retreive serial line settings - %d", __FUNCTION__, retval);
				return retval;
			} else {
				spin_lock_irqsave(&priv->lock, flags);
				/* store the config in one byte, and later use bit masks to check values */
				priv->current_config = feature_buffer[4];
				/* reverse the process above to get the baud_mask value */
				n_baud_rate = 0; // reset bits
				for (i = 0; i < 4; ++i) {
					n_baud_rate |= ( feature_buffer[i] << (i*8) );
				}
				
				priv->baud_rate = n_baud_rate;
				if ( (priv->cbr_mask = rate_to_mask(n_baud_rate)) == 0x40)
					dbg("%s - failed setting the baud mask (not defined)", __FUNCTION__);
				++priv->cmd_count;
				spin_unlock_irqrestore(&priv->lock, flags);
			}
			break;
		default:
			err("%s - unsupported serial control command issued", __FUNCTION__);
	}
	return retval;
} /* cypress_serial_control */


/* given a baud mask, it will return speed on success */
static int mask_to_rate (unsigned mask)
{
	int rate;

	switch (mask) {
		case B0: rate = 0; break;
		case B300: rate = 300; break;
		case B600: rate = 600; break;
		case B1200: rate = 1200; break;
		case B2400: rate = 2400; break;
		case B4800: rate = 4800; break;
		case B9600: rate = 9600; break;
		case B19200: rate = 19200; break;
		case B38400: rate = 38400; break;
		case B57600: rate = 57600; break;
		case B115200: rate = 115200; break;
		default: rate = -1;
	}

	return rate;
}


static unsigned rate_to_mask (int rate)
{
	unsigned mask;

	switch (rate) {
		case 0: mask = B0; break;
		case 300: mask = B300; break;
		case 600: mask = B600; break;
		case 1200: mask = B1200; break;
		case 2400: mask = B2400; break;
		case 4800: mask = B4800; break;
		case 9600: mask = B9600; break;
		case 19200: mask = B19200; break;
		case 38400: mask = B38400; break;
		case 57600: mask = B57600; break;
		case 115200: mask = B115200; break;
		default: mask = 0x40;
	}

	return mask;
}
/*****************************************************************************
 * Cypress serial driver functions
 *****************************************************************************/


static int generic_startup (struct usb_serial *serial)
{
	struct cypress_private *priv;

	dbg("%s - port %d", __FUNCTION__, serial->port[0]->number);

	priv = kmalloc(sizeof (struct cypress_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	memset(priv, 0x00, sizeof (struct cypress_private));
	spin_lock_init(&priv->lock);
	init_waitqueue_head(&priv->delta_msr_wait);
	priv->writepipe = serial->port[0]->interrupt_out_urb->pipe;
	
	/* free up interrupt_out buffer / urb allocated by usbserial
 	 * for this port as we use our own urbs for writing */
	if (serial->port[0]->interrupt_out_buffer) {
		kfree(serial->port[0]->interrupt_out_buffer);
		serial->port[0]->interrupt_out_buffer = NULL;
	}
	if (serial->port[0]->interrupt_out_urb) {
		priv->write_interval = serial->port[0]->interrupt_out_urb->interval;
		usb_free_urb(serial->port[0]->interrupt_out_urb);
		serial->port[0]->interrupt_out_urb = NULL;
	} else /* still need a write interval */
		priv->write_interval = 10;

	priv->cmd_ctrl = 0;
	priv->line_control = 0;
	priv->termios_initialized = 0;
	priv->rx_flags = 0;
	usb_set_serial_port_data(serial->port[0], priv);
	
	return (0);	
}	


static int cypress_earthmate_startup (struct usb_serial *serial)
{
	struct cypress_private *priv;

	dbg("%s", __FUNCTION__);

	if (generic_startup(serial)) {
		dbg("%s - Failed setting up port %d", __FUNCTION__, serial->port[0]->number);
		return 1;
	}

	priv = usb_get_serial_port_data(serial->port[0]);
	priv->chiptype = CT_EARTHMATE;
	
	return (0);	
} /* cypress_earthmate_startup */


static int cypress_hidcom_startup (struct usb_serial *serial)
{
	struct cypress_private *priv;

	dbg("%s", __FUNCTION__);

	if (generic_startup(serial)) {
		dbg("%s - Failed setting up port %d", __FUNCTION__, serial->port[0]->number);
		return 1;
	}

	priv = usb_get_serial_port_data(serial->port[0]);
	priv->chiptype = CT_CYPHIDCOM;
	
	return (0);	
} /* cypress_hidcom_startup */


static void cypress_shutdown (struct usb_serial *serial)
{
	struct cypress_private *priv;

	dbg ("%s - port %d", __FUNCTION__, serial->port[0]->number);

	/* all open ports are closed at this point */

	priv = usb_get_serial_port_data(serial->port[0]);

	if (priv) {
		kfree(priv);
		usb_set_serial_port_data(serial->port[0], NULL);
	}
}


static int cypress_open (struct usb_serial_port *port, struct file *filp)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	unsigned long flags;
	int result = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	/* reset read/write statistics */
	priv->bytes_in = 0;
	priv->bytes_out = 0;
	priv->cmd_count = 0;

	/* turn on dtr / rts since we are not flow controlling by default */
	priv->line_control = CONTROL_DTR | CONTROL_RTS; /* sent in status byte */
	spin_unlock_irqrestore(&priv->lock, flags);
	priv->cmd_ctrl = 1;
	result = cypress_write(port, NULL, 0);
	
	port->tty->low_latency = 1;

	/* termios defaults are set by usb_serial_init */
	
	cypress_set_termios(port, &priv->tmp_termios);

	if (result) {
		dev_err(&port->dev, "%s - failed setting the control lines - error %d\n", __FUNCTION__, result);
		return result;
	} else
		dbg("%s - success setting the control lines", __FUNCTION__);

	/* throttling off */
	spin_lock_irqsave(&priv->lock, flags);
	priv->rx_flags = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* setup the port and
	 * start reading from the device */
	if(!port->interrupt_in_urb){
		err("%s - interrupt_in_urb is empty!", __FUNCTION__);
		return(-1);
	}

	usb_fill_int_urb(port->interrupt_in_urb, serial->dev,
		usb_rcvintpipe(serial->dev, port->interrupt_in_endpointAddress),
		port->interrupt_in_urb->transfer_buffer, port->interrupt_in_urb->transfer_buffer_length,
		cypress_read_int_callback, port, port->interrupt_in_urb->interval);
	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);

	if (result){
		dev_err(&port->dev, "%s - failed submitting read urb, error %d\n", __FUNCTION__, result);
	}

	return result;
} /* cypress_open */


static void cypress_close(struct usb_serial_port *port, struct file * filp)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned int c_cflag;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (port->tty) {
		c_cflag = port->tty->termios->c_cflag;
		if (c_cflag & HUPCL) {
			/* drop dtr and rts */
			priv = usb_get_serial_port_data(port);
			spin_lock_irqsave(&priv->lock, flags);
			priv->line_control = 0;
			priv->cmd_ctrl = 1;
			spin_unlock_irqrestore(&priv->lock, flags);
			cypress_write(port, NULL, 0);
		}
	}

	if (port->interrupt_in_urb) {
		dbg("%s - stopping read urb", __FUNCTION__);
		usb_kill_urb (port->interrupt_in_urb);
	}

	if (stats)
		dev_info (&port->dev, "Statistics: %d Bytes In | %d Bytes Out | %d Commands Issued\n",
		          priv->bytes_in, priv->bytes_out, priv->cmd_count);
} /* cypress_close */


static int cypress_write(struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	struct urb *urb;
	int status, s_pos = 0;
	__u8 transfer_size = 0;
	__u8 *buffer;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	if (count == 0 && !priv->cmd_ctrl) {
		spin_unlock_irqrestore(&priv->lock, flags);
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		return 0;
	}

	if (priv->cmd_ctrl)
		++priv->cmd_count;
	priv->cmd_ctrl = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	dbg("%s - interrupt out size is %d", __FUNCTION__, port->interrupt_out_size);
	dbg("%s - count is %d", __FUNCTION__, count);

	/* Allocate buffer and urb */
	buffer = kmalloc (port->interrupt_out_size, GFP_ATOMIC);
	if (!buffer) {
		dev_err(&port->dev, "ran out of memory for buffer\n");
		return -ENOMEM;
	}

	urb = usb_alloc_urb (0, GFP_ATOMIC);
	if (!urb) {
		dev_err(&port->dev, "failed allocating write urb\n");
		kfree (buffer);
		return -ENOMEM;
	}

	memset(buffer, 0, port->interrupt_out_size); // test if this is needed... probably not since loop removed

	spin_lock_irqsave(&priv->lock, flags);
	switch (port->interrupt_out_size) {
		case 32:
			// this is for the CY7C64013...
			transfer_size = min (count, 30);
			buffer[0] = priv->line_control;
			buffer[1] = transfer_size;
			s_pos = 2;
			break;
		case 8:
			// this is for the CY7C63743...
			transfer_size = min (count, 7);
			buffer[0] = priv->line_control | transfer_size;
			s_pos = 1;
			break;
		default:
			dbg("%s - wrong packet size", __FUNCTION__);
			spin_unlock_irqrestore(&priv->lock, flags);
			kfree (buffer);
			usb_free_urb (urb);
			return -1;
	}

	if (priv->line_control & CONTROL_RESET)
		priv->line_control &= ~CONTROL_RESET;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* copy data to offset position in urb transfer buffer */
	memcpy (&buffer[s_pos], buf, transfer_size);

	usb_serial_debug_data (debug, &port->dev, __FUNCTION__, port->interrupt_out_size, buffer);

	/* build up the urb */
	usb_fill_int_urb (urb, port->serial->dev,
			  usb_sndintpipe(port->serial->dev, port->interrupt_out_endpointAddress),
			  buffer, port->interrupt_out_size,
			  cypress_write_int_callback, port, priv->write_interval);

	status = usb_submit_urb(urb, GFP_ATOMIC);

	if (status) {
		dev_err(&port->dev, "%s - usb_submit_urb (write interrupt) failed with status %d\n",
			__FUNCTION__, status);
		transfer_size = status;
		kfree (buffer);
		goto exit;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->bytes_out += transfer_size;
	spin_unlock_irqrestore(&priv->lock, flags);

exit:
	/* buffer free'd in callback */
	usb_free_urb (urb);

	return transfer_size;

} /* cypress_write */


static int cypress_write_room(struct usb_serial_port *port)
{
	dbg("%s - port %d", __FUNCTION__, port->number);

	/*
	 * We really can take anything the user throw at us
	 * but let's pick a nice big number to tell the tty
	 * layer that we have lots of free space
	 */	

	return 2048;
}


static int cypress_tiocmget (struct usb_serial_port *port, struct file *file)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	__u8 status, control;
	unsigned int result = 0;
	unsigned long flags;
	
	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	control = priv->line_control;
	status = priv->current_status;
	spin_unlock_irqrestore(&priv->lock, flags);

	result = ((control & CONTROL_DTR)        ? TIOCM_DTR : 0)
		| ((control & CONTROL_RTS)       ? TIOCM_RTS : 0)
		| ((status & UART_CTS)        ? TIOCM_CTS : 0)
		| ((status & UART_DSR)        ? TIOCM_DSR : 0)
		| ((status & UART_RI)         ? TIOCM_RI  : 0)
		| ((status & UART_CD)         ? TIOCM_CD  : 0);

	dbg("%s - result = %x", __FUNCTION__, result);

	return result;
}


static int cypress_tiocmset (struct usb_serial_port *port, struct file *file,
			       unsigned int set, unsigned int clear)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	
	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	if (set & TIOCM_RTS)
		priv->line_control |= CONTROL_RTS;
	if (set & TIOCM_DTR)
		priv->line_control |= CONTROL_DTR;
	if (clear & TIOCM_RTS)
		priv->line_control &= ~CONTROL_RTS;
	if (clear & TIOCM_DTR)
		priv->line_control &= ~CONTROL_DTR;
	spin_unlock_irqrestore(&priv->lock, flags);

	priv->cmd_ctrl = 1;
	return cypress_write(port, NULL, 0);
}


static int cypress_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);

	dbg("%s - port %d, cmd 0x%.4x", __FUNCTION__, port->number, cmd);

	switch (cmd) {
		case TIOCGSERIAL:
			if (copy_to_user((void __user *)arg, port->tty->termios, sizeof(struct termios))) {
				return -EFAULT;
			}
			return (0);
			break;
		case TIOCSSERIAL:
			if (copy_from_user(port->tty->termios, (void __user *)arg, sizeof(struct termios))) {
				return -EFAULT;
			}
			/* here we need to call cypress_set_termios to invoke the new settings */
			cypress_set_termios(port, &priv->tmp_termios);
			return (0);
			break;
		/* these are called when setting baud rate from gpsd */
		case TCGETS:
			if (copy_to_user((void __user *)arg, port->tty->termios, sizeof(struct termios))) {
				return -EFAULT;
			}
			return (0);
			break;
		case TCSETS:
			if (copy_from_user(port->tty->termios, (void __user *)arg, sizeof(struct termios))) {
				return -EFAULT;
			}
			/* here we need to call cypress_set_termios to invoke the new settings */
			cypress_set_termios(port, &priv->tmp_termios);
			return (0);
			break;
		/* This code comes from drivers/char/serial.c and ftdi_sio.c */
		case TIOCMIWAIT:
			while (priv != NULL) {
				interruptible_sleep_on(&priv->delta_msr_wait);
				/* see if a signal did it */
				if (signal_pending(current))
					return -ERESTARTSYS;
				else {
					char diff = priv->diff_status;

					if (diff == 0) {
						return -EIO; /* no change => error */
					}
					
					/* consume all events */
					priv->diff_status = 0;

					/* return 0 if caller wanted to know about these bits */
					if ( ((arg & TIOCM_RNG) && (diff & UART_RI)) ||
					     ((arg & TIOCM_DSR) && (diff & UART_DSR)) ||
					     ((arg & TIOCM_CD) && (diff & UART_CD)) ||
					     ((arg & TIOCM_CTS) && (diff & UART_CTS)) ) {
						return 0;
					}
					/* otherwise caller can't care less about what happened,
					 * and so we continue to wait for more events.
					 */
				}
			}
			return 0;
			break;
		default:
			break;
	}

	dbg("%s - arg not supported - it was 0x%04x - check include/asm/ioctls.h", __FUNCTION__, cmd);

	return -ENOIOCTLCMD;
} /* cypress_ioctl */


static void cypress_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	int data_bits, stop_bits, parity_type, parity_enable;
	unsigned cflag, iflag, baud_mask;
	unsigned long flags;
	
	dbg("%s - port %d", __FUNCTION__, port->number);

	tty = port->tty;
	if ((!tty) || (!tty->termios)) {
		dbg("%s - no tty structures", __FUNCTION__);
		return;
	}

	spin_lock_irqsave(&priv->lock, flags);
	if (!priv->termios_initialized) {
		if (priv->chiptype == CT_EARTHMATE) {
			*(tty->termios) = tty_std_termios;
			tty->termios->c_cflag = B4800 | CS8 | CREAD | HUPCL | CLOCAL;
		} else if (priv->chiptype == CT_CYPHIDCOM) {
			*(tty->termios) = tty_std_termios;
			tty->termios->c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
		}
		priv->termios_initialized = 1;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	cflag = tty->termios->c_cflag;
	iflag = tty->termios->c_iflag;

	/* check if there are new settings */
	if (old_termios) {
		if ((cflag != old_termios->c_cflag) ||
		    (RELEVANT_IFLAG(iflag) != RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg("%s - attempting to set new termios settings", __FUNCTION__);
			/* should make a copy of this in case something goes wrong in the function, we can restore it */
			spin_lock_irqsave(&priv->lock, flags);
			priv->tmp_termios = *(tty->termios);
			spin_unlock_irqrestore(&priv->lock, flags); 
		} else {
			dbg("%s - nothing to do, exiting", __FUNCTION__);
			return;
		}
	} else
		return;

	/* set number of data bits, parity, stop bits */
	/* when parity is disabled the parity type bit is ignored */

	stop_bits = cflag & CSTOPB ? 1 : 0; /* 1 means 2 stop bits, 0 means 1 stop bit */
	
	if (cflag & PARENB) {
		parity_enable = 1;
		parity_type = cflag & PARODD ? 1 : 0; /* 1 means odd parity, 0 means even parity */
	} else
		parity_enable = parity_type = 0;

	if (cflag & CSIZE) {
		switch (cflag & CSIZE) {
			case CS5: data_bits = 0; break;
			case CS6: data_bits = 1; break;
			case CS7: data_bits = 2; break;
			case CS8: data_bits = 3; break;
			default: err("%s - CSIZE was set, but not CS5-CS8", __FUNCTION__); data_bits = 3;
		}
	} else
		data_bits = 3;

	spin_lock_irqsave(&priv->lock, flags);
	if ((cflag & CBAUD) == B0) {
		/* drop dtr and rts */
		dbg("%s - dropping the lines, baud rate 0bps", __FUNCTION__);
		baud_mask = B0;
		priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
	} else {
		baud_mask = (cflag & CBAUD);
		switch(baud_mask) {
			case B300: dbg("%s - setting baud 300bps", __FUNCTION__); break;
			case B600: dbg("%s - setting baud 600bps", __FUNCTION__); break;
			case B1200: dbg("%s - setting baud 1200bps", __FUNCTION__); break;
			case B2400: dbg("%s - setting baud 2400bps", __FUNCTION__); break;
			case B4800: dbg("%s - setting baud 4800bps", __FUNCTION__); break;
			case B9600: dbg("%s - setting baud 9600bps", __FUNCTION__); break;
			case B19200: dbg("%s - setting baud 19200bps", __FUNCTION__); break;
			case B38400: dbg("%s - setting baud 38400bps", __FUNCTION__); break;
			case B57600: dbg("%s - setting baud 57600bps", __FUNCTION__); break;
			case B115200: dbg("%s - setting baud 115200bps", __FUNCTION__); break;
			default: dbg("%s - unknown masked baud rate", __FUNCTION__);
		}
		priv->line_control |= CONTROL_DTR;
		
		/* this is probably not what I think it is... check into it */
		if (cflag & CRTSCTS)
			priv->line_control |= CONTROL_RTS;
		else
			priv->line_control &= ~CONTROL_RTS;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	
	dbg("%s - sending %d stop_bits, %d parity_enable, %d parity_type, %d data_bits (+5)", __FUNCTION__,
	    stop_bits, parity_enable, parity_type, data_bits);

	cypress_serial_control(port, baud_mask, data_bits, stop_bits, parity_enable,
			       parity_type, 0, CYPRESS_SET_CONFIG);

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(50*HZ/1000); /* give some time between change and read (50ms) */ 

	/* we perform a CYPRESS_GET_CONFIG so that the current settings are filled into the private structure
         * this should confirm that all is working if it returns what we just set */
	cypress_serial_control(port, 0, 0, 0, 0, 0, 0, CYPRESS_GET_CONFIG);

	/* Here we can define custom tty settings for devices
         *
         * the main tty base comes from empeg.c
         */

	spin_lock_irqsave(&priv->lock, flags);	
	if ( (priv->chiptype == CT_EARTHMATE) && (priv->baud_rate == 4800) ) {

		dbg("Using custom termios settings for a baud rate of 4800bps.");
		/* define custom termios settings for NMEA protocol */

		
		tty->termios->c_iflag /* input modes - */
			&= ~(IGNBRK		/* disable ignore break */
			| BRKINT		/* disable break causes interrupt */
			| PARMRK		/* disable mark parity errors */
			| ISTRIP		/* disable clear high bit of input characters */
			| INLCR			/* disable translate NL to CR */
			| IGNCR			/* disable ignore CR */
			| ICRNL			/* disable translate CR to NL */
			| IXON);		/* disable enable XON/XOFF flow control */
		
		tty->termios->c_oflag /* output modes */
			&= ~OPOST;		/* disable postprocess output characters */
		
		tty->termios->c_lflag /* line discipline modes */
			&= ~(ECHO 		/* disable echo input characters */
			| ECHONL		/* disable echo new line */
			| ICANON		/* disable erase, kill, werase, and rprnt special characters */
			| ISIG			/* disable interrupt, quit, and suspend special characters */
			| IEXTEN);		/* disable non-POSIX special characters */

	} else if (priv->chiptype == CT_CYPHIDCOM) {

		// Software app handling it for device...	

	} else {
		
		/* do something here */

	}
	spin_unlock_irqrestore(&priv->lock, flags);

	/* set lines */
	priv->cmd_ctrl = 1;
	cypress_write(port, NULL, 0);
	
	return;
} /* cypress_set_termios */


static int cypress_chars_in_buffer(struct usb_serial_port *port)
{
	dbg("%s - port %d", __FUNCTION__, port->number);

	/*
	 * We can't really account for how much data we
	 * have sent out, but hasn't made it through to the
	 * device, so just tell the tty layer that everything
	 * is flushed.
	 */

	return 0;
}


static void cypress_throttle (struct usb_serial_port *port)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	priv->rx_flags = THROTTLED;
	spin_unlock_irqrestore(&priv->lock, flags);	   
}


static void cypress_unthrottle (struct usb_serial_port *port)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	int actually_throttled, result;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	actually_throttled = priv->rx_flags & ACTUALLY_THROTTLED;
	priv->rx_flags = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (actually_throttled) {
		port->interrupt_in_urb->dev = port->serial->dev;

		result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
		if (result)
			dev_err(&port->dev, "%s - failed submitting read urb, error %d\n", __FUNCTION__, result);
	}
}


static void cypress_read_int_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	unsigned long flags;
	char tty_flag = TTY_NORMAL;
	int bytes=0;
	int result;
	int i=0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (urb->status) {
		dbg("%s - nonzero read status received: %d", __FUNCTION__, urb->status);
		return;
	}

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->rx_flags & THROTTLED) {
		priv->rx_flags |= ACTUALLY_THROTTLED;
		spin_unlock_irqrestore(&priv->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	tty = port->tty;
	if (!tty) {
		dbg("%s - bad tty pointer - exiting", __FUNCTION__);
		return;
	}

	usb_serial_debug_data (debug, &port->dev, __FUNCTION__, urb->actual_length, data);
	
	spin_lock_irqsave(&priv->lock, flags);
	switch(urb->actual_length) {
		case 32:
			// This is for the CY7C64013...
			priv->current_status = data[0] & 0xF8;
			bytes = data[1]+2;
			i=2;
			break;
		case 8:
			// This is for the CY7C63743...
			priv->current_status = data[0] & 0xF8;
			bytes = (data[0] & 0x07)+1;
			i=1;
			break;
		default:
			dbg("%s - wrong packet size - received %d bytes", __FUNCTION__, urb->actual_length);
			spin_unlock_irqrestore(&priv->lock, flags);
			goto continue_read;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	spin_lock_irqsave(&priv->lock, flags);
	/* check to see if status has changed */
	if (priv != NULL) {
		if (priv->current_status != priv->prev_status) {
			priv->diff_status |= priv->current_status ^ priv->prev_status;
			wake_up_interruptible(&priv->delta_msr_wait);
			priv->prev_status = priv->current_status;
		}
	}
	spin_unlock_irqrestore(&priv->lock, flags);	

	/* hangup, as defined in acm.c... this might be a bad place for it though */
	if (tty && !(tty->termios->c_cflag & CLOCAL) && !(priv->current_status & UART_CD)) {
		dbg("%s - calling hangup", __FUNCTION__);
		tty_hangup(tty);
		goto continue_read;
	}

	/* There is one error bit... I'm assuming it is a parity error indicator
	 * as the generic firmware will set this bit to 1 if a parity error occurs.
	 * I can not find reference to any other error events.
	 *
	 */
	spin_lock_irqsave(&priv->lock, flags);
	if (priv->current_status & CYP_ERROR) {
		spin_unlock_irqrestore(&priv->lock, flags);
		tty_flag = TTY_PARITY;
		dbg("%s - Parity Error detected", __FUNCTION__);
	} else
		spin_unlock_irqrestore(&priv->lock, flags);

	/* process read if there is data other than line status */
	if (tty && (bytes > i)) {
		for (; i < bytes ; ++i) {
			dbg("pushing byte number %d - %d",i,data[i]);
			if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(tty);
			}
			tty_insert_flip_char(tty, data[i], tty_flag);
		}
		tty_flip_buffer_push(port->tty);
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->bytes_in += bytes;  /* control and status byte(s) are also counted */
	spin_unlock_irqrestore(&priv->lock, flags);

continue_read:
	
	/* Continue trying to always read... unless the port has closed.  */

	if (port->open_count > 0) {
	usb_fill_int_urb(port->interrupt_in_urb, port->serial->dev,
		usb_rcvintpipe(port->serial->dev, port->interrupt_in_endpointAddress),
		port->interrupt_in_urb->transfer_buffer,
		port->interrupt_in_urb->transfer_buffer_length,
		cypress_read_int_callback, port,
		port->interrupt_in_urb->interval);
	result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev, "%s - failed resubmitting read urb, error %d\n", __FUNCTION__, result);
	}
	
	return;
} /* cypress_read_int_callback */


static void cypress_write_int_callback(struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree (urb->transfer_buffer);

	dbg("%s - port %d", __FUNCTION__, port->number);
	
	if (urb->status) {
		dbg("%s - nonzero write status received: %d", __FUNCTION__, urb->status);
		return;
	}

	schedule_work(&port->work);
}


/*****************************************************************************
 * Module functions
 *****************************************************************************/

static int __init cypress_init(void)
{
	int retval;
	
	dbg("%s", __FUNCTION__);
	
	retval = usb_serial_register(&cypress_earthmate_device);
	if (retval)
		goto failed_em_register;
	retval = usb_serial_register(&cypress_hidcom_device);
	if (retval)
		goto failed_hidcom_register;
	retval = usb_register(&cypress_driver);
	if (retval)
		goto failed_usb_register;

	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
failed_usb_register:
	usb_deregister(&cypress_driver);
failed_hidcom_register:
	usb_serial_deregister(&cypress_hidcom_device);
failed_em_register:
	usb_serial_deregister(&cypress_earthmate_device);

	return retval;
}


static void __exit cypress_exit (void)
{
	dbg("%s", __FUNCTION__);

	usb_deregister (&cypress_driver);
	usb_serial_deregister (&cypress_earthmate_device);
	usb_serial_deregister (&cypress_hidcom_device);
}


module_init(cypress_init);
module_exit(cypress_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
module_param(stats, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(stats, "Enable statistics or not");
