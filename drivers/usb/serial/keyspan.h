/*
  Keyspan USB to Serial Converter driver
 
  (C) Copyright (C) 2000-2001
      Hugh Blemings <hugh@misc.nu>
   
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  See http://misc.nu/hugh/keyspan.html for more information.
  
  Code in this driver inspired by and in a number of places taken
  from Brian Warner's original Keyspan-PDA driver.

  This driver has been put together with the support of Innosys, Inc.
  and Keyspan, Inc the manufacturers of the Keyspan USB-serial products.
  Thanks Guys :)
  
  Thanks to Paulus for miscellaneous tidy ups, some largish chunks
  of much nicer and/or completely new code and (perhaps most uniquely)
  having the patience to sit down and explain why and where he'd changed
  stuff.

  Tip 'o the hat to IBM (and previously Linuxcare :) for supporting 
  staff in their work on open source projects.
  
  See keyspan.c for update history.

*/

#ifndef __LINUX_USB_SERIAL_KEYSPAN_H
#define __LINUX_USB_SERIAL_KEYSPAN_H

#include <linux/config.h>

	/* Function prototypes for Keyspan serial converter */
static int  keyspan_open		(struct usb_serial_port *port,
					 struct file *filp);
static void keyspan_close		(struct usb_serial_port *port,
					 struct file *filp);
static int  keyspan_startup		(struct usb_serial *serial);
static void keyspan_shutdown		(struct usb_serial *serial);
static void keyspan_rx_throttle		(struct usb_serial_port *port);
static void keyspan_rx_unthrottle	(struct usb_serial_port *port);
static int  keyspan_write_room		(struct usb_serial_port *port);

static int  keyspan_write		(struct usb_serial_port *port,
					 int from_user,
					 const unsigned char *buf,
					 int count);

static void keyspan_send_setup		(struct usb_serial_port *port,
					 int reset_port);

#if 0
static void keyspan_write_bulk_callback (struct urb *urb);
#endif

//static void keyspan_usa26_read_int_callback  (struct urb *urb);
//static void keyspan_usa28_read_int_callback  (struct urb *urb);
static int  keyspan_chars_in_buffer 	(struct usb_serial_port *port);
static int  keyspan_ioctl		(struct usb_serial_port *port,
					 struct file *file,
					 unsigned int cmd,
					 unsigned long arg);
static void keyspan_set_termios		(struct usb_serial_port *port,
					 struct termios *old);
static void keyspan_break_ctl		(struct usb_serial_port *port,
					 int break_state);
static int  keyspan_fake_startup	(struct usb_serial *serial);

static int  keyspan_usa19_calc_baud	(u32 baud_rate, u32 baudclk, 
					 u8 *rate_hi, u8 *rate_low, u8 *prescaler);

static int  keyspan_usa19w_calc_baud	(u32 baud_rate, u32 baudclk,
					 u8 *rate_hi, u8 *rate_low, u8 *prescaler);

//static void keyspan_usa19_setup_urbs	(struct usb_serial *serial);

static int  keyspan_usa28_send_setup	(struct usb_serial *serial,
					 struct usb_serial_port *port,
					 int reset_port);
static int  keyspan_usa26_send_setup	(struct usb_serial *serial,
	       				 struct usb_serial_port *port,
					 int reset_port);
static int  keyspan_usa49_send_setup	(struct usb_serial *serial,
					 struct usb_serial_port *port,
					 int reset_port);

	/* Functions from usbserial.c for ezusb firmware handling */
extern int ezusb_set_reset (struct usb_serial *serial, unsigned char reset_bit);
extern int ezusb_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest);

	/* Struct used for firmware - increased size of data section
	   to allow Keyspan's 'C' firmware struct to be used unmodified */
struct ezusb_hex_record {
	__u16 address;
	__u8 data_size;
	__u8 data[64];
};
	/* Conditionally include firmware images, if they aren't
	   included create a null pointer instead.  Current 
	   firmware images aren't optimised to remove duplicate
	   addresses in the image itself. */
#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA28
        #include "keyspan_usa28_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa28_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA28X
        #include "keyspan_usa28x_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa28x_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA28XA
        #include "keyspan_usa28xa_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa28xa_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA28XB
        #include "keyspan_usa28xb_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa28xb_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA19
        #include "keyspan_usa19_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa19_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA18X
        #include "keyspan_usa18x_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa18x_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA19W
        #include "keyspan_usa19w_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa19w_firmware = NULL;
#endif

#ifdef CONFIG_USB_SERIAL_KEYSPAN_USA49W
        #include "keyspan_usa49w_fw.h"
#else
	static const struct ezusb_hex_record *keyspan_usa49w_firmware = NULL;
#endif
	
	/* Values used for baud rate calculation - device specific */
#define	KEYSPAN_INVALID_BAUD_RATE		(-1)
#define	KEYSPAN_BAUD_RATE_OK			(0)
#define	KEYSPAN_USA18X_BAUDCLK			(12000000L)	/* a guess */
#define	KEYSPAN_USA19_BAUDCLK			(12000000L)
#define	KEYSPAN_USA19W_BAUDCLK			(24000000L)
#define	KEYSPAN_USA28X_BAUDCLK			(12000000L)
#define	KEYSPAN_USA49W_BAUDCLK			(48000000L)

	/* Some constants used to characterise each device. 
	   There is a four port device due later in the year,
	   we allow for it now in the following */
#define		KEYSPAN_MAX_NUM_PORTS		(4)
#define		KEYSPAN_MAX_FLIPS		(2)
	
	/* Device info for the Keyspan serial converter, used
	   by the overall usb-serial probe function */
#define KEYSPAN_VENDOR_ID			(0x06cd)

	/* Product IDs for the eight products supported, pre-renumeration */
#define	keyspan_usa18x_pre_product_id		0x0105
#define	keyspan_usa19_pre_product_id		0x0103
#define	keyspan_usa19w_pre_product_id		0x0106
#define	keyspan_usa28_pre_product_id		0x0101
#define	keyspan_usa28x_pre_product_id		0x0102
#define	keyspan_usa28xa_pre_product_id		0x0114
#define	keyspan_usa28xb_pre_product_id		0x0113
#define	keyspan_usa49w_pre_product_id		0x0109

	/* Product IDs post-renumeration.  Note that the 28x and 28xb
	   have the same id's post-renumeration but behave identically
	   so it's not an issue. */
#define	keyspan_usa18x_product_id		0x0112
#define	keyspan_usa19_product_id		0x0107
#define	keyspan_usa19w_product_id		0x0108
#define	keyspan_usa28_product_id		0x010f
#define	keyspan_usa28x_product_id		0x0110
#define	keyspan_usa28xa_product_id		0x0115
#define	keyspan_usa28xb_product_id		0x0110
#define	keyspan_usa49w_product_id		0x010a


typedef struct {
	/* product ID value */
	int	product_id;
	
	enum	{msg_usa26, msg_usa28, msg_usa49} msg_format;

		/* Number of physical ports */
	int	num_ports;

		/* 1 if endpoint flipping used on input, 0 if not */
	int	indat_endp_flip;

		/* 1 if endpoint flipping used on output, 0 if not */
	int 	outdat_endp_flip;

		/* Table mapping input data endpoint IDs to physical
		   port number and flip if used */
	int	indat_endpoints[KEYSPAN_MAX_NUM_PORTS];

		/* Same for output endpoints */
	int	outdat_endpoints[KEYSPAN_MAX_NUM_PORTS];

		/* Input acknowledge endpoints */
	int	inack_endpoints[KEYSPAN_MAX_NUM_PORTS];

		/* Output control endpoints */	
	int	outcont_endpoints[KEYSPAN_MAX_NUM_PORTS];

		/* Endpoint used for input status */
	int	instat_endpoint;

		/* Endpoint used for global control functions */
	int	glocont_endpoint;	
	
	int	(*calculate_baud_rate) (u32 baud_rate, u32 baudclk,
			u8 *rate_hi, u8 *rate_low, u8 *prescaler);
	u32	baudclk;

} keyspan_device_details; 

	/* Now for each device type we setup the device detail
	   structure with the appropriate information (provided
	   in Keyspan's documentation) */

static const keyspan_device_details usa18x_device_details = {
	keyspan_usa18x_product_id,	/* product ID */
       	msg_usa26, 			/* msg type*/
	1,				/* num ports */
	0,				/* indat endpoint flip */
	1,				/* outdat endpoint flip */
  	{0x81},				/* per port indat */
  	{0x01},				/* per port outdat */
	{0x85},				/* per port inack */
	{0x05},				/* per port outcont */
	0x87,				/* instat endpoint */
	0x07,				/* glocont endpoint */
	keyspan_usa19w_calc_baud,	/* calc baud rate */
	KEYSPAN_USA18X_BAUDCLK		/* base baud clock */
};

static const keyspan_device_details usa19_device_details = {
	keyspan_usa19_product_id,	/* product ID */
       	msg_usa28, 			/* msg type*/
	1,				/* num ports */
	1,				/* indat endpoint flip */
	1,				/* outdat endpoint flip */
  	{0x81},				/* per port indat */
  	{0x01},				/* per port outdat */
	{0x83},				/* per port inack */
	{0x03},				/* per port outcont */
	0x84,				/* instat endpoint */
	-1,				/* glocont endpoint */
	keyspan_usa19_calc_baud,	/* calc baud rate */
	KEYSPAN_USA19_BAUDCLK		/* base baud clock */
};

static const keyspan_device_details usa19w_device_details = {
	keyspan_usa19w_product_id,	/* product ID */
       	msg_usa26, 			/* msg type*/
	1,				/* num ports */
	0,				/* indat endpoint flip */
	1,				/* outdat endpoint flip */
  	{0x81},				/* per port indat */
  	{0x01},				/* per port outdat */
	{0x85},				/* per port inack */
	{0x05},				/* per port outcont */
	0x87,				/* instat endpoint */
	0x07,				/* glocont endpoint */
	keyspan_usa19w_calc_baud,	/* calc baud rate */
	KEYSPAN_USA19W_BAUDCLK		/* base baud clock */
};

static const keyspan_device_details usa28x_device_details = {
	keyspan_usa28x_product_id,	/* product ID */
       	msg_usa26, 			/* msg type*/
	2,				/* num ports */
	0,				/* indat endpoint flip */
	1,				/* outdat endpoint flip */
  	{0x81, 0x83},			/* per port indat */
  	{0x01, 0x03},			/* per port outdat */
	{0x85, 0x86},			/* per port inack */
	{0x05, 0x06},			/* per port outcont */
	0x87,				/* instat endpoint */
	0x07,				/* glocont endpoint */
	keyspan_usa19w_calc_baud,	/* calc baud rate */
	KEYSPAN_USA28X_BAUDCLK
};

static const keyspan_device_details usa28xa_device_details = {
	keyspan_usa28xa_product_id,	/* product ID */
       	msg_usa26, 			/* msg type*/
	2,				/* num ports */
	0,				/* indat endpoint flip */
	1,				/* outdat endpoint flip */
  	{0x81, 0x83},			/* per port indat */
  	{0x01, 0x03},			/* per port outdat */
	{0x85, 0x86},			/* per port inack */
	{0x05, 0x06},			/* per port outcont */
	0x87,				/* instat endpoint */
	0x07,				/* glocont endpoint */
	keyspan_usa19w_calc_baud,	/* calc baud rate */
	KEYSPAN_USA28X_BAUDCLK
};

	/* We don't need a separate entry for the usa28xb as it appears as a 28x anyway */

static const keyspan_device_details usa49w_device_details = {
	keyspan_usa49w_product_id,	/* product ID */
       	msg_usa49, 			/* msg type*/
	4,				/* num ports */
	0,				/* indat endpoint flip */
	0,				/* outdat endpoint flip */
  	{ 0x81, 0x82, 0x83, 0x84},	/* per port indat */
  	{ 0x01, 0x02, 0x03, 0x04},	/* per port outdat */
	{-1, -1, -1, -1},		/* per port inack */
	{-1, -1, -1, -1},		/* per port outcont */
	0x87,				/* instat endpoint */
	0x07,				/* glocont endpoint */
	keyspan_usa19w_calc_baud,	/* calc baud rate */
	KEYSPAN_USA49W_BAUDCLK
};

static const keyspan_device_details *keyspan_devices[] = {
	&usa18x_device_details,
	&usa19_device_details,
	&usa19w_device_details,
	&usa28x_device_details,
	&usa28xa_device_details,
	&usa49w_device_details,
	NULL
};

static __devinitdata struct usb_device_id keyspan_ids_combined[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa18x_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19w_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28x_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xa_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xb_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49w_pre_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa18x_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19w_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28x_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xa_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xb_product_id) },
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49w_product_id)},
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, keyspan_ids_combined);

/* Eventually, we will not need separate id tables for each USB
   ID pattern.  But, for now, it looks like we need slightly different
   behavior for each match. */

static __devinitdata struct usb_device_id keyspan_usa18x_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa18x_pre_product_id) },
	{ }	/* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa19_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19_pre_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa19w_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19w_pre_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa28_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28_pre_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa28x_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28x_pre_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa28xa_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xa_pre_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa28xb_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xb_pre_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa49w_pre_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49w_pre_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa18x_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa18x_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa19_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa19w_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa19w_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa28_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa28x_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28x_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa28xa_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa28xa_product_id) },
	{ } /* Terminating entry */
};

static __devinitdata struct usb_device_id keyspan_usa49w_ids[] = {
	{ USB_DEVICE(KEYSPAN_VENDOR_ID, keyspan_usa49w_product_id) },
	{ } /* Terminating entry */
};

    /* Structs for the devices, pre and post renumeration. */
static struct usb_serial_device_type keyspan_usa18x_pre_device = {
	name:			"Keyspan USA18X - (without firmware)",
	id_table:		keyspan_usa18x_pre_ids,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		keyspan_fake_startup	
};

static struct usb_serial_device_type keyspan_usa19_pre_device = {
	name:			"Keyspan USA19 - (without firmware)",
	id_table:		keyspan_usa19_pre_ids,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		keyspan_fake_startup	
};


static struct usb_serial_device_type keyspan_usa19w_pre_device = {
	name:			"Keyspan USA19W - (without firmware)",
	id_table:		keyspan_usa19w_pre_ids,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		1,
	startup:		keyspan_fake_startup	
};


static struct usb_serial_device_type keyspan_usa28_pre_device = {
	name:			"Keyspan USA28 - (without firmware)",
	id_table:		keyspan_usa28_pre_ids,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	startup:		keyspan_fake_startup	
};

static struct usb_serial_device_type keyspan_usa28x_pre_device = {
	name:			"Keyspan USA28X - (without firmware)",
	id_table:		keyspan_usa28x_pre_ids,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	startup:		keyspan_fake_startup	
};

static struct usb_serial_device_type keyspan_usa28xa_pre_device = {
	name:			"Keyspan USA28XA - (without firmware)",
	id_table:		keyspan_usa28xa_pre_ids,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	startup:		keyspan_fake_startup	
};

static struct usb_serial_device_type keyspan_usa28xb_pre_device = {
	name:			"Keyspan USA28XB - (without firmware)",
	id_table:		keyspan_usa28xb_pre_ids,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	startup:		keyspan_fake_startup	
};

static struct usb_serial_device_type keyspan_usa49w_pre_device = {
	name:			"Keyspan USA49W - (without firmware)",
	id_table:		keyspan_usa49w_pre_ids,
	needs_interrupt_in:	DONT_CARE,
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		4,
	startup:		keyspan_fake_startup	
};

static struct usb_serial_device_type keyspan_usa18x_device = {
	name:			"Keyspan USA18X",
	id_table:		keyspan_usa18x_ids,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		3,
	num_bulk_out:		4,
	num_ports:		1,
	open:			keyspan_open,
	close:			keyspan_close,
	write:			keyspan_write,
	write_room:		keyspan_write_room,
	//write_bulk_callback: 	Not used - we define our own herbs
	//read_int_callback:	keyspan_usa26_read_int_callback,
	chars_in_buffer:	keyspan_chars_in_buffer,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	ioctl:			keyspan_ioctl,
	set_termios:		keyspan_set_termios,
	break_ctl:		keyspan_break_ctl,
	startup:		keyspan_startup,
	shutdown:		keyspan_shutdown,
};

static struct usb_serial_device_type keyspan_usa19_device = {
	name:			"Keyspan USA19",
	id_table:		keyspan_usa19_ids,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		3,
	num_bulk_out:		4,
	num_ports:		1,
	open:			keyspan_open,
	close:			keyspan_close,
	write:			keyspan_write,
	write_room:		keyspan_write_room,
//	write_bulk_callback: 	keyspan_write_bulk_callback,
//	read_int_callback:	keyspan_usa28_read_int_callback,
	chars_in_buffer:	keyspan_chars_in_buffer,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	ioctl:			keyspan_ioctl,
	set_termios:		keyspan_set_termios,
	break_ctl:		keyspan_break_ctl,
	startup:		keyspan_startup,
	shutdown:		keyspan_shutdown,
};


static struct usb_serial_device_type keyspan_usa19w_device = {
	name:			"Keyspan USA19W",
	id_table:		keyspan_usa19w_ids,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		3,
	num_bulk_out:		4,
	num_ports:		1,
	open:			keyspan_open,
	close:			keyspan_close,
	write:			keyspan_write,
	write_room:		keyspan_write_room,
	//write_bulk_callback: 	Not used - we define our own herbs
	//read_int_callback:	keyspan_usa26_read_int_callback,
	chars_in_buffer:	keyspan_chars_in_buffer,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	ioctl:			keyspan_ioctl,
	set_termios:		keyspan_set_termios,
	break_ctl:		keyspan_break_ctl,
	startup:		keyspan_startup,
	shutdown:		keyspan_shutdown,
};


static struct usb_serial_device_type keyspan_usa28_device = {
	name:			"Keyspan USA28",
	id_table:		keyspan_usa28_ids,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	open:			keyspan_open,
	close:			keyspan_close,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	set_termios:		keyspan_set_termios,
};


static struct usb_serial_device_type keyspan_usa28x_device = {
	name:			"Keyspan USA28X/XB",
	id_table:		keyspan_usa28x_ids,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	open:			keyspan_open,
	close:			keyspan_close,
	write:			keyspan_write,
	write_room:		keyspan_write_room,
//	write_bulk_callback: 	keyspan_write_bulk_callback,
//	read_int_callback:	keyspan_usa26_read_int_callback,
	chars_in_buffer:	keyspan_chars_in_buffer,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	ioctl:			keyspan_ioctl,
	set_termios:		keyspan_set_termios,
	break_ctl:		keyspan_break_ctl,
	startup:		keyspan_startup,
	shutdown:		keyspan_shutdown,

};

static struct usb_serial_device_type keyspan_usa28xa_device = {
	name:			"Keyspan USA28XA",
	id_table:		keyspan_usa28xa_ids,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		DONT_CARE,
	needs_bulk_out:		DONT_CARE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		NUM_DONT_CARE,
	num_bulk_out:		NUM_DONT_CARE,
	num_ports:		2,
	open:			keyspan_open,
	close:			keyspan_close,
	write:			keyspan_write,
	write_room:		keyspan_write_room,
//	write_bulk_callback: 	keyspan_write_bulk_callback,
//	read_int_callback:	keyspan_usa26_read_int_callback,
	chars_in_buffer:	keyspan_chars_in_buffer,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	ioctl:			keyspan_ioctl,
	set_termios:		keyspan_set_termios,
	break_ctl:		keyspan_break_ctl,
	startup:		keyspan_startup,
	shutdown:		keyspan_shutdown,

};

static struct usb_serial_device_type keyspan_usa49w_device = {
	name:			"Keyspan USA49W",
	id_table:		keyspan_usa49w_ids,
	needs_interrupt_in:	DONT_CARE,	
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	NUM_DONT_CARE,
	num_bulk_in:		5,
	num_bulk_out:		5,
	num_ports:		4,
	open:			keyspan_open,
	close:			keyspan_close,
	write:			keyspan_write,
	write_room:		keyspan_write_room,
	//write_bulk_callback: 	Not used - we define our own herbs
	//read_int_callback:	keyspan_usa26_read_int_callback,
	chars_in_buffer:	keyspan_chars_in_buffer,
	throttle:		keyspan_rx_throttle,
	unthrottle:		keyspan_rx_unthrottle,
	ioctl:			keyspan_ioctl,
	set_termios:		keyspan_set_termios,
	break_ctl:		keyspan_break_ctl,
	startup:		keyspan_startup,
	shutdown:		keyspan_shutdown,
};


#endif
