/*
 * IO Edgeport Driver tables
 *
 *	Copyright (C) 2001
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 * 
 */

static __devinitdata struct usb_device_id edgeport_4_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_4)}, {} };
static __devinitdata struct usb_device_id rapidport_4_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_RAPIDPORT_4) }, {} };
static __devinitdata struct usb_device_id edgeport_4t_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_4T) }, {} };
static __devinitdata struct usb_device_id edgeport_2_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2) }, {} };
static __devinitdata struct usb_device_id edgeport_4i_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_4I) }, {} };
static __devinitdata struct usb_device_id edgeport_2i_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2I) }, {} };
static __devinitdata struct usb_device_id edgeport_prl_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_PARALLEL_PORT) }, {} };
static __devinitdata struct usb_device_id edgeport_421_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_421) }, {} };
static __devinitdata struct usb_device_id edgeport_21_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_21) }, {} };
static __devinitdata struct usb_device_id edgeport_8dual_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8_DUAL_CPU) }, {} };
static __devinitdata struct usb_device_id edgeport_8_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8) }, {} };
static __devinitdata struct usb_device_id edgeport_2din_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2_DIN) }, {} };
static __devinitdata struct usb_device_id edgeport_4din_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_4_DIN) }, {} };
static __devinitdata struct usb_device_id edgeport_16dual_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_16_DUAL_CPU) }, {} };
static __devinitdata struct usb_device_id edgeport_compat_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_COMPATIBLE) }, {} };
static __devinitdata struct usb_device_id edgeport_8i_id_table [] =	{{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8I) }, {} };


/* Devices that this driver supports */
static __devinitdata struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_RAPIDPORT_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_4T) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_2) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_4I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_2I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_PARALLEL_PORT) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_421) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_21) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_8_DUAL_CPU) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_8) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_2_DIN) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_4_DIN) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_16_DUAL_CPU) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_COMPATIBLE) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_8I) },
	{ }							/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);


/* build up the list of devices that this driver supports */
struct usb_serial_device_type edgeport_4_device = {
	name:			"Edgeport 4",
	id_table:		edgeport_4_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		4,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type rapidport_4_device = {
	name:			"Rapidport 4",
	id_table:		rapidport_4_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		4,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_4t_device = {
	name:			"Edgeport 4t",
	id_table:		edgeport_4t_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		4,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_2_device = {
	name:			"Edgeport 2",
	id_table:		edgeport_2_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		2,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_4i_device = {
	name:			"Edgeport 4i",
	id_table:		edgeport_4i_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		4,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_2i_device = {
	name:			"Edgeport 2i",
	id_table:		edgeport_2i_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		2,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_prl_device = {
	name:			"Edgeport Parallel",
	id_table:		edgeport_prl_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		1,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_421_device = {
	name:			"Edgeport 421",
	id_table:		edgeport_421_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		2,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_21_device = {
	name:			"Edgeport 21",
	id_table:		edgeport_21_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		2,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_8dual_device = {
	name:			"Edgeport 8 dual cpu",
	id_table:		edgeport_8dual_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		4,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_8_device = {
	name:			"Edgeport 8",
	id_table:		edgeport_8_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		8,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_2din_device = {
	name:			"Edgeport 2din",
	id_table:		edgeport_2din_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		2,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_4din_device = {
	name:			"Edgeport 4din",
	id_table:		edgeport_4din_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		4,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_16dual_device = {
	name:			"Edgeport 16 dual cpu",
	id_table:		edgeport_16dual_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		8,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};

struct usb_serial_device_type edgeport_compat_id_device = {
	name:			"Edgeport Compatible",
	id_table:		edgeport_compat_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		4,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};


struct usb_serial_device_type edgeport_8i_device = {
	name:			"Edgeport 8i",
	id_table:		edgeport_8i_id_table,
	needs_interrupt_in:	MUST_HAVE,
	needs_bulk_in:		MUST_HAVE,
	needs_bulk_out:		MUST_HAVE,
	num_interrupt_in:	1,
	num_bulk_in:		1,
	num_bulk_out:		1,
	num_ports:		8,
	open:			edge_open,
	close:			edge_close,
	throttle:		edge_throttle,
	unthrottle:		edge_unthrottle,
	startup:		edge_startup,
	shutdown:		edge_shutdown,
	ioctl:			edge_ioctl,
	set_termios:		edge_set_termios,
	write:			edge_write,
	write_room:		edge_write_room,
	chars_in_buffer:	edge_chars_in_buffer,
	break_ctl:		edge_break,
};




