/*
 * USB Serial Converter driver
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
 * (10/05/2000) gkh
 *	Added interrupt_in_endpointAddress and bulk_in_endpointAddress to help
 *	fix bug with urb->dev not being set properly, now that the usb core
 *	needs it.
 * 
 * (09/11/2000) gkh
 *	Added usb_serial_debug_data function to help get rid of #DEBUG in the
 *	drivers.
 *
 * (08/28/2000) gkh
 *	Added port_lock to port structure.
 *
 * (08/08/2000) gkh
 *	Added open_count to port structure.
 *
 * (07/23/2000) gkh
 *	Added bulk_out_endpointAddress to port structure.
 *
 * (07/19/2000) gkh, pberger, and borchers
 *	Modifications to allow usb-serial drivers to be modules.
 *
 * 
 */


#ifndef __LINUX_USB_SERIAL_H
#define __LINUX_USB_SERIAL_H

#include <linux/config.h>

#define SERIAL_TTY_MAJOR	188	/* Nice legal number now */
#define SERIAL_TTY_MINORS	255	/* loads of devices :) */

#define MAX_NUM_PORTS		8	/* The maximum number of ports one device can grab at once */

#define USB_SERIAL_MAGIC	0x6702	/* magic number for usb_serial struct */
#define USB_SERIAL_PORT_MAGIC	0x7301	/* magic number for usb_serial_port struct */

/* parity check flag */
#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))


struct usb_serial_port {
	int			magic;
	struct usb_serial	*serial;	/* pointer back to the owner of this port */
	struct tty_struct *	tty;		/* the coresponding tty for this port */
	unsigned char		number;
	char			active;		/* someone has this device open */

	unsigned char *		interrupt_in_buffer;
	struct urb *		interrupt_in_urb;
	__u8			interrupt_in_endpointAddress;

	unsigned char *		bulk_in_buffer;
	struct urb *		read_urb;
	__u8			bulk_in_endpointAddress;

	unsigned char *		bulk_out_buffer;
	int			bulk_out_size;
	struct urb *		write_urb;
	__u8			bulk_out_endpointAddress;

	wait_queue_head_t	write_wait;

	struct tq_struct	tqueue;		/* task queue for line discipline waking up */
	int			open_count;	/* number of times this port has been opened */
	spinlock_t		port_lock;
	
	void *			private;	/* data private to the specific port */
};

struct usb_serial {
	int				magic;
	struct usb_device *		dev;
	struct usb_serial_device_type *	type;			/* the type of usb serial device this is */
	struct usb_interface *		interface;		/* the interface for this device */
	struct tty_driver *		tty_driver;		/* the tty_driver for this device */
	unsigned char			minor;			/* the starting minor number for this device */
	unsigned char			num_ports;		/* the number of ports this device has */
	char				num_interrupt_in;	/* number of interrupt in endpoints we have */
	char				num_bulk_in;		/* number of bulk in endpoints we have */
	char				num_bulk_out;		/* number of bulk out endpoints we have */
	struct usb_serial_port		port[MAX_NUM_PORTS];

	void *			private;		/* data private to the specific driver */
};


#define MUST_HAVE_NOT	0x01
#define MUST_HAVE	0x02
#define DONT_CARE	0x03

#define	HAS		0x02
#define HAS_NOT		0x01

#define NUM_DONT_CARE	(-1)


/* This structure defines the individual serial converter. */
struct usb_serial_device_type {
	char	*name;
	const struct usb_device_id *id_table;
	char	needs_interrupt_in;
	char	needs_bulk_in;
	char	needs_bulk_out;
	char	num_interrupt_in;
	char	num_bulk_in;
	char	num_bulk_out;
	char	num_ports;		/* number of serial ports this device has */

	struct list_head	driver_list;
	
	/* function call to make before accepting driver */
	/* return 0 to continue initialization, anything else to abort */
	int (*startup) (struct usb_serial *serial);
	
	void (*shutdown) (struct usb_serial *serial);

	/* serial function calls */
	int  (*open)		(struct usb_serial_port *port, struct file * filp);
	void (*close)		(struct usb_serial_port *port, struct file * filp);
	int  (*write)		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
	int  (*write_room)	(struct usb_serial_port *port);
	int  (*ioctl)		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
	void (*set_termios)	(struct usb_serial_port *port, struct termios * old);
	void (*break_ctl)	(struct usb_serial_port *port, int break_state);
	int  (*chars_in_buffer)	(struct usb_serial_port *port);
	void (*throttle)	(struct usb_serial_port *port);
	void (*unthrottle)	(struct usb_serial_port *port);

	void (*read_int_callback)(struct urb *urb);
	void (*read_bulk_callback)(struct urb *urb);
	void (*write_bulk_callback)(struct urb *urb);
};

extern int  usb_serial_register(struct usb_serial_device_type *new_device);
extern void usb_serial_deregister(struct usb_serial_device_type *device);

/* determine if we should include the EzUSB loader functions */
#if defined(CONFIG_USB_SERIAL_KEYSPAN_PDA) || defined(CONFIG_USB_SERIAL_WHITEHEAT) || defined(CONFIG_USB_SERIAL_KEYSPAN) || defined(CONFIG_USB_SERIAL_KEYSPAN_PDA_MODULE) || defined(CONFIG_USB_SERIAL_WHITEHEAT_MODULE) || defined(CONFIG_USB_SERIAL_KEYSPAN_MODULE)
	#define	USES_EZUSB_FUNCTIONS
	extern int ezusb_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest);
	extern int ezusb_set_reset (struct usb_serial *serial, unsigned char reset_bit);
#else
	#undef 	USES_EZUSB_FUNCTIONS
#endif


/* Inline functions to check the sanity of a pointer that is passed to us */
static inline int serial_paranoia_check (struct usb_serial *serial, const char *function)
{
	if (!serial) {
		dbg("%s - serial == NULL", function);
		return -1;
	}
	if (serial->magic != USB_SERIAL_MAGIC) {
		dbg("%s - bad magic number for serial", function);
		return -1;
	}
	if (!serial->type) {
		dbg("%s - serial->type == NULL!", function);
		return -1;
	}

	return 0;
}


static inline int port_paranoia_check (struct usb_serial_port *port, const char *function)
{
	if (!port) {
		dbg("%s - port == NULL", function);
		return -1;
	}
	if (port->magic != USB_SERIAL_PORT_MAGIC) {
		dbg("%s - bad magic number for port", function);
		return -1;
	}
	if (!port->serial) {
		dbg("%s - port->serial == NULL", function);
		return -1;
	}
	if (!port->tty) {
		dbg("%s - port->tty == NULL", function);
		return -1;
	}

	return 0;
}


static inline struct usb_serial* get_usb_serial (struct usb_serial_port *port, const char *function) 
{ 
	/* if no port was specified, or it fails a paranoia check */
	if (!port || 
		port_paranoia_check (port, function) ||
		serial_paranoia_check (port->serial, function)) {
		/* then say that we dont have a valid usb_serial thing, which will
		 * end up genrating -ENODEV return values */ 
		return NULL;
	}

	return port->serial;
}


static inline void usb_serial_debug_data (const char *file, const char *function, int size, const unsigned char *data)
{
#ifdef CONFIG_USB_SERIAL_DEBUG
	int i;
	printk (KERN_DEBUG "%s: %s - length = %d, data = ", file, function, size);
	for (i = 0; i < size; ++i) {
		printk ("%.2x ", data[i]);
	}
	printk ("\n");
#endif
}

#endif	/* ifdef __LINUX_USB_SERIAL_H */

