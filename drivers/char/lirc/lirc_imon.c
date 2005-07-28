/*
 *   lirc_imon.c:  LIRC plugin/VFD driver for Ahanix/Soundgraph IMON IR/VFD
 *
 *   $Id: lirc_imon.c,v 1.6 2005/04/16 18:10:02 venkyr Exp $
 *
 *   Version 0.3 
 *   		Supports newer iMON models that send decoded IR signals.
 *   			This includes the iMON PAD model.
 *   		Removed module option for vfd_proto_6p. This driver supports
 *   			multiple iMON devices so it is meaningless to have
 *   			a global option to set protocol variants.
 *
 *   Version 0.2 beta 2 [January 31, 2005]
 *		USB disconnect/reconnect no longer causes problems for lircd
 *   
 *   Version 0.2 beta 1 [January 29, 2005]
 *		Added support for original iMON receiver (ext USB)
 *   
 *   Version 0.2 alpha 2 [January 24, 2005]
 *   		Added support for VFDs with 6-packet protocol
 *
 *   Version 0.2 alpha 1 [January 23, 2005]
 *   		Added support for 2.6 kernels
 *   		Reworked disconnect handling
 *   		Incorporated Changwoo Ryu's algorithm
 *
 *   Version 0.1 alpha 1 [July 5, 2004]
 *
 *   Copyright (C) 2004  Venky Raju (dev@venky.ws)
 *   
 *   lirc_imon is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,22)
#error "*** Sorry, this driver requires kernel version 2.4.22 or higher"
#endif

#include <linux/config.h>

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/devfs_fs_kernel.h>

#include "lirc.h"
#include "kcompat.h"
#include "lirc_dev.h"


#define MOD_AUTHOR	"Venky Raju <dev@venky.ws>"
#define MOD_DESC	"Driver for Soundgraph iMON MultiMedian IR/VFD"
#define MOD_NAME	"lirc_imon"
#define MOD_VERSION	"0.3"

#define VFD_MINOR_BASE	144	/* Same as LCD */
#define DEVFS_MODE	S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH
#define DEVFS_NAME	"usb/lcd%d"

#define BUF_CHUNK_SIZE	4
#define BUF_SIZE	128

#define BIT_DURATION	250 	/* each bit received is 250us */

#define SUCCESS		0
#define	TRUE		1
#define FALSE		0


/* ------------------------------------------------------------
 *                     P R O T O T Y P E S
 * ------------------------------------------------------------
 */

/* USB Callback prototypes */
#ifdef KERNEL_2_5
static int imon_probe (struct usb_interface *interface,
			const struct usb_device_id *id);
static void imon_disconnect (struct usb_interface *interface);
static void usb_rx_callback (struct urb *urb, struct pt_regs *regs);
static void usb_tx_callback (struct urb *urb, struct pt_regs *regs);
#else
static void * imon_probe (struct usb_device * dev, unsigned int intf,
				const struct usb_device_id *id);
static void imon_disconnect (struct usb_device *dev, void *data);
static void usb_rx_callback (struct urb *urb);
static void usb_tx_callback (struct urb *urb);
#endif

/* VFD file_operations function prototypes */
static int vfd_open (struct inode *inode, struct file *file);
static int vfd_close (struct inode *inode, struct file *file);
static ssize_t vfd_write (struct file *file, const char *buf,
				size_t n_bytes, loff_t *pos);

/* LIRC plugin function prototypes */
static int ir_open (void *data);
static void ir_close (void *data);

/* Driver init/exit prototypes */
static int __init imon_init (void);
static void __exit imon_exit (void);

/* ------------------------------------------------------------
 *                     G L O B A L S
 * ------------------------------------------------------------
 */

struct imon_context {

	struct usb_device *dev;
	int vfd_supported;		/* not all controllers do         */
	int vfd_isopen;			/* VFD port has been opened       */
#if !defined (KERNEL_2_5)
	int subminor;			/* index into minor_table         */
	devfs_handle_t devfs;
#endif
	int ir_isopen;			/* IR port has been opened        */
	int dev_present;		/* USB device presence            */
	struct semaphore sem;		/* to lock this object            */
	wait_queue_head_t remove_ok;	/* For unexpected USB disconnects */

	int vfd_proto_6p;		/* VFD requires 6th packet        */
	int ir_onboard_decode;		/* IR signals decoded onboard     */

	struct lirc_plugin *plugin;
	struct usb_endpoint_descriptor *rx_endpoint;
	struct usb_endpoint_descriptor *tx_endpoint;
	struct urb *rx_urb;
	struct urb *tx_urb;
	unsigned char usb_rx_buf [8];
	unsigned char usb_tx_buf [8];

	struct rx_data {
		int count;		/* length of 0 or 1 sequence      */
		int prev_bit;		/* logic level of sequence        */
		int initial_space;	/* initial space flag             */

	} rx;

	struct tx_t {
		unsigned char data_buf [35]; /* user data buffer          */
		struct completion finished;  /* wait for write to finish  */
		atomic_t busy;		     /* write in progress         */
		int status;		     /* status of tx completion   */
	} tx;
};

#define LOCK_CONTEXT	down (&context ->sem)
#define UNLOCK_CONTEXT	up (&context ->sem)

/* VFD file operations */
static struct file_operations vfd_fops = {

	.owner		= THIS_MODULE,
	.open		= &vfd_open,
	.write		= &vfd_write,
	.release	= &vfd_close
};

/* USB Device ID for IMON USB Control Board */
static struct usb_device_id imon_usb_id_table [] = {
	{ USB_DEVICE(0x0aa8, 0xffda) },		/* IR & VFD    */
	{ USB_DEVICE(0x0aa8, 0x8001) },		/* IR only     */
	{ USB_DEVICE(0x15c2, 0xffda) },		/* IR & VFD    */
	{ USB_DEVICE(0x15c2, 0xffdc) },		/* IR & VFD    */
	{ USB_DEVICE(0x04e8, 0xff30) },		/* ext IR only */
	{}
};

/* Some iMON VFD models requires a 6th packet */
static unsigned short vfd_proto_6p_vendor_list [] = {
			/* terminate this list with a 0 */
			0x15c2,
			0 };
static unsigned char vfd_packet6 [] = {
		0x01, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF };

/* Newer iMON models decode the signal onboard */
static unsigned short ir_onboard_decode_product_list [] = {
			/* terminate this list with a 0 */
			0xffdc,
			0 };

/* USB Device data */
static struct usb_driver imon_driver = {
	.owner 		= THIS_MODULE,
	.name 		= MOD_NAME,
	.probe 		= imon_probe,
	.disconnect 	= imon_disconnect,
	.id_table 	= imon_usb_id_table,
#if !defined(KERNEL_2_5)
	.fops		= &vfd_fops,
	.minor		= VFD_MINOR_BASE,
#endif
};

#ifdef KERNEL_2_5
static struct usb_class_driver imon_class = {
	.name 		= DEVFS_NAME,
	.fops		= &vfd_fops,
	.mode		= DEVFS_MODE,
	.minor_base	= VFD_MINOR_BASE,
};
#endif

/* to prevent races between open() and disconnect() */
static DECLARE_MUTEX (disconnect_sem);

static int debug = 0;

#if !defined(KERNEL_2_5)

#define MAX_DEVICES	4	/* In case there's more than one iMON device */
static struct imon_context * minor_table [MAX_DEVICES];

/*
static DECLARE_MUTEX (minor_table_sem);
#define LOCK_MINOR_TABLE	down (&minor_table_sem)
#define UNLOCK_MINOR_TABLE	up (&minor_table_sem)
*/

/* the global usb devfs handle */
extern devfs_handle_t usb_devfs_handle;

#endif

/* ------------------------------------------------------------
 *                     M O D U L E   C O D E
 * ------------------------------------------------------------
 */

MODULE_AUTHOR (MOD_AUTHOR);
MODULE_DESCRIPTION (MOD_DESC);
MODULE_LICENSE ("GPL");
module_param (debug, int, 0);
MODULE_PARM_DESC (debug, "Debug messages: 0=no, 1=yes (default: no)");

static inline void delete_context (struct imon_context *context) {

	if (context ->vfd_supported)
		usb_free_urb (context ->tx_urb);
	usb_free_urb (context ->rx_urb);
	lirc_buffer_free (context ->plugin ->rbuf);
	kfree (context ->plugin ->rbuf);
	kfree (context ->plugin);
	kfree (context);

	if (debug) info ("%s: context deleted", __FUNCTION__);
}

static inline void deregister_from_lirc (struct imon_context *context) {

	int retval;
	int minor = context ->plugin ->minor;

	if ((retval = lirc_unregister_plugin (minor))) {

		err ("%s: unable to deregister from lirc (%d)", 
			__FUNCTION__, retval);
	}
	else
		info ("Deregistered iMON plugin (minor:%d)", minor);

}

/**
 * Called when the VFD device (e.g. /dev/usb/lcd)
 * is opened by the application.
 */
static int vfd_open (struct inode *inode, struct file *file)
{
#ifdef KERNEL_2_5
	struct usb_interface *interface;
#endif
	struct imon_context *context = NULL;
	int subminor;
	int retval = SUCCESS;

	/* prevent races with disconnect */
	down (&disconnect_sem);
	
#ifdef KERNEL_2_5
	subminor = iminor (inode);
	interface = usb_find_interface (&imon_driver, subminor);
	if (!interface) {
		err ("%s: could not find interface for minor %d", __FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit;
	}
	context = usb_get_intfdata (interface);
#else
	subminor = MINOR (inode ->i_rdev) - VFD_MINOR_BASE;
	if (subminor < 0 || subminor >= MAX_DEVICES) {
		err ("%s: no record of minor %d", __FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit;
	}
	context = minor_table [subminor];
#endif

	if (!context) {
		err ("%s: no context found for minor %d", 
					__FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	LOCK_CONTEXT;

	if (!context ->vfd_supported) {
		err ("%s: VFD not supported by device", __FUNCTION__);
		retval = -ENODEV;
	}
	else if (context ->vfd_isopen) {
		
		err ("%s: VFD port is already open", __FUNCTION__);
		retval = -EBUSY;
	}
	else {
		MOD_INC_USE_COUNT;
		context ->vfd_isopen = TRUE;
		file ->private_data = context;
		info ("VFD port opened");
	}

	UNLOCK_CONTEXT;

exit:
	up (&disconnect_sem);
	return retval;
}

/**
 * Called when the VFD device (e.g. /dev/usb/lcd)
 * is closed by the application.
 */
static int vfd_close (struct inode *inode, struct file *file)
{
	struct imon_context *context = NULL;
	int retval = SUCCESS;

	context = (struct imon_context *) file ->private_data;

	if (!context) {
		err ("%s: no context for device", __FUNCTION__);
		return -ENODEV;
	}

	LOCK_CONTEXT;

	if (!context ->vfd_supported) {
		err ("%s: VFD not supported by device", __FUNCTION__);
		retval = -ENODEV;
	}
	else if (!context ->vfd_isopen) {
		err ("%s: VFD is not open", __FUNCTION__);
		retval = -EIO;
	}
	else {
		context ->vfd_isopen = FALSE;
		MOD_DEC_USE_COUNT;
		info ("VFD port closed");
		if (!context ->dev_present && !context ->ir_isopen) {

			/* Device disconnected before close and IR port is not open.  */
			/* If IR port is open, context will be deleted by ir_close.   */
			UNLOCK_CONTEXT;
			delete_context (context);
			return retval;
		}
	}

	UNLOCK_CONTEXT;
	return retval;
}

/**
 * Sends a packet to the VFD.
 */
static inline int send_packet (struct imon_context *context)
{
	unsigned int pipe;
	int interval = 0;
	int retval = SUCCESS;

	pipe = usb_sndintpipe (context ->dev,
			context-> tx_endpoint ->bEndpointAddress);
#ifdef KERNEL_2_5
	interval = context ->tx_endpoint ->bInterval;
#endif	/* Use 0 for 2.4 kernels */

	usb_fill_int_urb (context ->tx_urb, context ->dev, pipe,
		context ->usb_tx_buf, sizeof (context ->usb_tx_buf),
		usb_tx_callback, context, interval);

	context ->tx_urb ->actual_length = 0;

	init_completion (&context ->tx.finished);
	atomic_set (&(context ->tx.busy), 1);

#ifdef KERNEL_2_5
	retval =  usb_submit_urb (context ->tx_urb, GFP_KERNEL);
#else
	retval =  usb_submit_urb (context ->tx_urb);
#endif
	if (retval != SUCCESS) {
		atomic_set (&(context ->tx.busy), 0);
		err ("%s: error submitting urb (%d)", __FUNCTION__, retval);
	}
	else {
		/* Wait for tranmission to complete (or abort) */
		UNLOCK_CONTEXT;
		wait_for_completion (&context ->tx.finished);
		LOCK_CONTEXT;

		retval = context ->tx.status;
		if (retval != SUCCESS)
			err ("%s: packet tx failed (%d)", __FUNCTION__, retval);
	}

	return retval;
}

/**
 * Writes data to the VFD.  The IMON VFD is 2x16 characters
 * and requires data in 5 consecutive USB interrupt packets,
 * each packet but the last carrying 7 bytes.
 *
 * I don't know if the VFD board supports features such as
 * scrolling, clearing rows, blanking, etc. so at 
 * the caller must provide a full screen of data.  If fewer
 * than 32 bytes are provided spaces will be appended to
 * generate a full screen.
 */
static ssize_t vfd_write (struct file *file, const char *buf,
				size_t n_bytes, loff_t *pos)
{

	int i;
	int offset;
	int seq;
	int retval = SUCCESS;
	struct imon_context *context;

	context = (struct imon_context *) file ->private_data;
	if (!context) {
		err ("%s: no context for device", __FUNCTION__);
		return -ENODEV;
	}

	LOCK_CONTEXT;

	if (!context ->dev_present) {
		err ("%s: no iMON device present", __FUNCTION__);
		retval = -ENODEV;
		goto exit;
	}

	if (n_bytes <= 0 || n_bytes > 32) {
		err ("%s: invalid payload size", __FUNCTION__);
		retval = -EINVAL;
		goto exit;
	}

	copy_from_user (context ->tx.data_buf, buf, n_bytes);

	/* Pad with spaces */
	for (i=n_bytes; i < 32; ++i)
		context ->tx.data_buf [i] = ' ';
	
	for (i=32; i < 35; ++i)
		context ->tx.data_buf [i] = 0xFF;

	offset = seq = 0;

	do {
		memcpy (context ->usb_tx_buf, context ->tx.data_buf + offset, 7);
		context ->usb_tx_buf [7] = (unsigned char) seq;

		if ((retval = send_packet (context)) != SUCCESS) {

			err ("%s: send packet failed for packet #%d", 
					__FUNCTION__, seq/2);
			goto exit;
		}
		else {
			seq += 2;
			offset += 7;
		}

	} while (offset < 35);

	if (context ->vfd_proto_6p) {

		/* Send packet #6 */
		memcpy (context ->usb_tx_buf, vfd_packet6, 7);
		context ->usb_tx_buf [7] = (unsigned char) seq;
		if ((retval = send_packet (context)) != SUCCESS)
			err ("%s: send packet failed for packet #%d",
					__FUNCTION__, seq/2);
	}

exit:
	UNLOCK_CONTEXT;

	return (retval == SUCCESS) ? n_bytes : retval;
}

/**
 * Callback function for USB core API: transmit data
 */
#ifdef KERNEL_2_5
static void usb_tx_callback (struct urb *urb, struct pt_regs *regs)
#else
static void usb_tx_callback (struct urb *urb)
#endif
{
	struct imon_context *context;

	if (!urb || !(context = (struct imon_context *) urb->context))
		return;

	context ->tx.status = urb ->status;

	/* notify waiters that write has finished */
	atomic_set (&context ->tx.busy, 0);
	complete (&context ->tx.finished);

	return;
}

/**
 * Called by lirc_dev when the application opens /dev/lirc
 */
static int ir_open (void *data)
{
	int retval = SUCCESS;
	struct imon_context *context;

	/* prevent races with disconnect */
	down (&disconnect_sem);

	context = (struct imon_context *) data;

	LOCK_CONTEXT;

	if (context ->ir_isopen) {
		err ("%s: IR port is already open", __FUNCTION__);
		retval = -EBUSY;
		goto exit;
	}

	/* initial IR protocol decode variables */
	context ->rx.count = 0;
	context ->rx.initial_space = 1;
	context ->rx.prev_bit = 0;

	usb_fill_int_urb (context ->rx_urb, context ->dev,
		usb_rcvintpipe (context ->dev,
				context ->rx_endpoint-> bEndpointAddress),
		context ->usb_rx_buf, sizeof (context ->usb_rx_buf),
		usb_rx_callback, context, context ->rx_endpoint ->bInterval);

#ifdef KERNEL_2_5
	retval = usb_submit_urb (context ->rx_urb, GFP_KERNEL);
#else
	retval = usb_submit_urb (context ->rx_urb);
#endif

	if (retval) {
		err ("%s: usb_submit_urb failed for ir_open (%d)", __FUNCTION__, retval);
	}
	else {
		MOD_INC_USE_COUNT;
		context ->ir_isopen = TRUE;
		info ("IR port opened");
	}

exit:
	UNLOCK_CONTEXT;

	up (&disconnect_sem);
	return SUCCESS;
}

/**
 * Called by lirc_dev when the application closes /dev/lirc
 */
static void ir_close (void *data)
{
	struct imon_context *context;

	context = (struct imon_context *)data;
	if (!context) {
		err ("%s: no context for device", __FUNCTION__);
		return;
	}

	LOCK_CONTEXT;

	usb_unlink_urb (context ->rx_urb);
	context ->ir_isopen = FALSE;
	MOD_DEC_USE_COUNT;
	info ("IR port closed");

	if (!context ->dev_present) {

		/* 
		 * Device disconnected while IR port was 
		 * still open. Plugin was not deregistered 
		 * at disconnect time, so do it now.
		 */
		deregister_from_lirc (context);

		if (!context ->vfd_isopen) {

			UNLOCK_CONTEXT;
			delete_context (context);
			return;
		}
		/* If VFD port is open, context will be deleted by vfd_close */
	}

	UNLOCK_CONTEXT;
	return;
}

/**
 * Convert bit count to time duration (in us) and submit
 * the value to lirc_dev.
 */
static inline void submit_data (struct imon_context *context)
{
	unsigned char buf [4];
	int value = context ->rx.count;
	int i;

	if (debug) info ("submitting data to LIRC");
	
	value *= BIT_DURATION;
	value &= PULSE_MASK;
	if (context ->rx.prev_bit)
		value |= PULSE_BIT;

	for (i=0; i < 4; ++i)
		buf [i] = value >> (i*8);

	lirc_buffer_write_1 (context ->plugin ->rbuf, buf);
	wake_up (&context ->plugin ->rbuf ->wait_poll);
	return;
}

/**
 * Process the incoming packet
 */
static inline void incoming_packet (struct imon_context *context, struct urb *urb)
{
	int len = urb ->actual_length;
	unsigned char *buf = urb ->transfer_buffer;
	int octet, bit;
	unsigned char mask;
	int chunk_num;


	if (len != 8) {
		warn ("%s: invalid incoming packet size (%d)", __FUNCTION__, len);
		return;
	}

	chunk_num = buf [7];
	
	if (chunk_num == 0xFF)
		return;		/* filler frame, no data here */

#ifdef DEBUG	
	{
		int i;
		for (i=0; i < 8; ++i)
			printk ("%02x ", buf [i]);
		printk ("\n");
	}
#endif

	if (context ->ir_onboard_decode) {

		/* The signals have been decoded onboard the iMON controller */

		lirc_buffer_write_1 (context ->plugin ->rbuf, buf);
		wake_up (&context ->plugin ->rbuf ->wait_poll);
		return;
	}
	
	/*
	 * Translate received data to pulse and space lengths.
	 * Received data is active low, i.e. pulses are 0 and
	 * spaces are 1.
	 *
	 * My original algorithm was essentially similar to
	 * Changwoo Ryu's with the exception that he switched
	 * the incoming bits to active high and also fed an
	 * initial space to LIRC at the start of a new sequence
	 * if the previous bit was a pulse.
	 *
	 * I've decided to adopt his algorithm.
	 */

	if (chunk_num == 1 && context ->rx.initial_space) {

		/* LIRC requires a leading space */
		context ->rx.prev_bit = 0;
		context ->rx.count = 4;
		submit_data (context);
		context ->rx.count = 0;
	}

	for (octet=0; octet < 5; ++octet) {

		mask = 0x80;
		for (bit=0; bit < 8; ++bit) {

			int curr_bit = !(buf [octet] & mask);
			if (curr_bit != context ->rx.prev_bit) {

				if (context ->rx.count) {

					submit_data (context);
					context ->rx.count = 0;
				}
				context ->rx.prev_bit = curr_bit;
			}
			++context ->rx.count; 
			mask >>= 1;
		}
	}

	if (chunk_num == 10) {

		if (context ->rx.count) {
			submit_data (context);
			context ->rx.count = 0;
		}
		context ->rx.initial_space = context ->rx.prev_bit;
	}
}

/**
 * Callback function for USB core API: receive data
 */
#ifdef KERNEL_2_5
static void usb_rx_callback (struct urb *urb, struct pt_regs *regs)
#else
static void usb_rx_callback (struct urb *urb)
#endif
{
	struct imon_context *context;

	if (!urb || !(context = (struct imon_context *) urb->context))
		return;

	switch (urb ->status) {

		case -ENOENT: 		/* usbcore unlink successful! */ 
			return;

		case SUCCESS:
			if (context ->ir_isopen)
				incoming_packet (context, urb);
		       	break;

		default	:
			warn ("%s: status (%d): ignored",
				 __FUNCTION__, urb ->status);
			break;
	}

#ifdef KERNEL_2_5
	usb_submit_urb (context ->rx_urb, GFP_KERNEL);
#endif
	return;
}



/**
 * Callback function for USB core API: Probe
 */
#ifdef KERNEL_2_5
static int imon_probe (struct usb_interface *interface,
			const struct usb_device_id *id)
#else
static void * imon_probe (struct usb_device * dev, unsigned int intf,
			const struct usb_device_id *id)
#endif
{
#ifdef KERNEL_2_5
	struct usb_device *dev = NULL;
	struct usb_host_interface *iface_desc = NULL;
#else
	struct usb_interface *interface = NULL;
	struct usb_interface_descriptor *iface_desc = NULL;
	char name [10];
	int subminor = 0;
#endif
	struct usb_endpoint_descriptor *rx_endpoint = NULL;
	struct usb_endpoint_descriptor *tx_endpoint = NULL;
	struct urb *rx_urb = NULL;
	struct urb *tx_urb = NULL;
	struct lirc_plugin *plugin = NULL;
	struct lirc_buffer *rbuf = NULL;
	int lirc_minor = 0;
	int num_endpoints;
	int retval = SUCCESS;
	int vfd_ep_found;
	int ir_ep_found;
	int alloc_status;
	int vfd_proto_6p = FALSE;
	int ir_onboard_decode = FALSE;
	struct imon_context *context = NULL;
	int i;

	info ("%s: found IMON device", __FUNCTION__);

#if !defined(KERNEL_2_5)
	for (subminor = 0; subminor < MAX_DEVICES; ++subminor) {
		if (minor_table [subminor] == NULL)
			break;
	}
	if (subminor == MAX_DEVICES) {
	
		err ("%s: allowed number of devices already present", __FUNCTION__);
		retval = -ENOMEM;
		goto exit;
	}
#endif

#ifdef KERNEL_2_5
	dev = usb_get_dev (interface_to_usbdev (interface));
	iface_desc = interface ->cur_altsetting;
	num_endpoints = iface_desc ->desc.bNumEndpoints;
#else
	interface = &dev ->actconfig ->interface [intf];
	iface_desc = &interface ->altsetting [interface ->act_altsetting];
	num_endpoints = iface_desc ->bNumEndpoints;
#endif

	/*
	 * Scan the endpoint list and set:
	 * 	first input endpoint = IR endpoint
	 * 	first output endpoint = VFD endpoint
	 */

	ir_ep_found = vfd_ep_found = FALSE;

	for (i=0; i < num_endpoints && !(ir_ep_found && vfd_ep_found); ++i) {

		struct usb_endpoint_descriptor *ep;
		int ep_dir;
		int ep_type;
#ifdef KERNEL_2_5
		ep = &iface_desc ->endpoint [i].desc;
#else
		ep = &iface_desc ->endpoint [i];
#endif
		ep_dir = ep ->bEndpointAddress & USB_ENDPOINT_DIR_MASK;
		ep_type = ep ->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;

		if (!ir_ep_found && 
			ep_dir == USB_DIR_IN && 
			ep_type == USB_ENDPOINT_XFER_INT) {

			rx_endpoint = ep;
			ir_ep_found = TRUE;
			if (debug) 
				info ("%s: found IR endpoint", __FUNCTION__);

		}
		else if (!vfd_ep_found &&
			ep_dir == USB_DIR_OUT && 
			ep_type == USB_ENDPOINT_XFER_INT) {

			tx_endpoint = ep;
			vfd_ep_found = TRUE;
			if (debug) 
				info ("%s: found VFD endpoint", __FUNCTION__);
		}
		else
			;

	}

	/* Input endpoint is mandatory */
	if (!ir_ep_found) {

		err ("%s: no valid input (IR) endpoint found.", __FUNCTION__);
		retval = -ENODEV;
		goto exit;
	}
	else {

		/* Determine if the IR signals are decoded onboard */

		unsigned short product_id;
		unsigned short *id_list_item;

		product_id = dev ->descriptor.idProduct;
		id_list_item = ir_onboard_decode_product_list;
		while (*id_list_item) {
			if (*id_list_item++ == product_id) {
				ir_onboard_decode = TRUE;
				break;
			}
		}

		if (debug) info ("ir_onboard_decode: %d", ir_onboard_decode);
	}

	/* Determine if VFD requires 6 packets */
	if (vfd_ep_found) {

		unsigned short vendor_id;
		unsigned short *id_list_item;

		vendor_id = dev ->descriptor.idVendor;
		id_list_item = vfd_proto_6p_vendor_list;
		while (*id_list_item) {
			if (*id_list_item++ == vendor_id) {
				vfd_proto_6p = TRUE;
				break;
			}
		}

		if (debug) info ("vfd_proto_6p: %d", vfd_proto_6p);
	}


	/* Allocate memory */

	alloc_status = SUCCESS;

	if (!(context = kmalloc (sizeof(struct imon_context), GFP_KERNEL))) {
		err ("%s: kmalloc failed for context", __FUNCTION__);
		alloc_status = 1;
	}
	else if (!(plugin = kmalloc (sizeof(struct lirc_plugin), GFP_KERNEL))) {
		err ("%s: kmalloc failed for lirc_plugin", __FUNCTION__);
		alloc_status = 2;
	}
	else if (!(rbuf = kmalloc (sizeof(struct lirc_buffer), GFP_KERNEL))) {
		err ("%s: kmalloc failed for lirc_buffer", __FUNCTION__);
		alloc_status = 3;
	}
	else if (lirc_buffer_init (rbuf, BUF_CHUNK_SIZE, BUF_SIZE)) {
		err ("%s: lirc_buffer_init failed", __FUNCTION__);
		alloc_status = 4;
	}
#ifdef KERNEL_2_5
	else if (!(rx_urb = usb_alloc_urb (0, GFP_KERNEL))) {
#else
	else if (!(rx_urb = usb_alloc_urb (0))) {
#endif
		err ("%s: usb_alloc_urb failed for IR urb", __FUNCTION__);
		alloc_status = 5;
	}
#ifdef KERNEL_2_5
	else if (vfd_ep_found && !(tx_urb = usb_alloc_urb (0, GFP_KERNEL))) {
#else
	else if (vfd_ep_found && !(tx_urb = usb_alloc_urb (0))) {
#endif
		err ("%s: usb_alloc_urb failed for VFD urb", __FUNCTION__);
		alloc_status = 6;
	}
	else {

		/* clear all members of imon_context and lirc_plugin */
		memset (context, 0, sizeof (struct imon_context));
		init_MUTEX (&context ->sem);
		context ->vfd_proto_6p = vfd_proto_6p;
		context ->ir_onboard_decode = ir_onboard_decode;

		memset (plugin, 0, sizeof (struct lirc_plugin));

		strcpy (plugin ->name, MOD_NAME);
		plugin ->minor = -1;
		plugin ->code_length = (ir_onboard_decode) ?
			32 : sizeof (lirc_t) * 8;
		plugin ->sample_rate = 0;
		plugin ->features = (ir_onboard_decode) ?
			LIRC_CAN_REC_LIRCCODE : LIRC_CAN_REC_MODE2;
		plugin ->data = context;
		plugin ->rbuf = rbuf;
		plugin ->set_use_inc = ir_open;
		plugin ->set_use_dec = ir_close;
		plugin->owner = THIS_MODULE;

		LOCK_CONTEXT;

		if ((lirc_minor = lirc_register_plugin (plugin)) < 0) {
			err ("%s: lirc_register_plugin failed", __FUNCTION__);
			alloc_status = 7;
			UNLOCK_CONTEXT;
		}
		else
			info ("%s: Registered iMON plugin (minor:%d)", 
				__FUNCTION__, lirc_minor);
	}

	switch (alloc_status) {

		case 7:		if (vfd_ep_found)
					usb_free_urb (tx_urb);
		case 6:		usb_free_urb (rx_urb);
		case 5:		lirc_buffer_free (rbuf);
		case 4:		kfree (rbuf);
		case 3:		kfree (plugin);
		case 2:		kfree (context);
				context = NULL;
		case 1:		retval = -ENOMEM;
				goto exit;
	}

	// Needed while unregistering!
	plugin ->minor = lirc_minor;

	context ->dev = dev;
	context ->dev_present = TRUE;
	context ->rx_endpoint = rx_endpoint;
	context ->rx_urb = rx_urb;
	if (vfd_ep_found) {
		
		context ->vfd_supported = TRUE;
		context ->tx_endpoint = tx_endpoint;
		context ->tx_urb = tx_urb;
	}
	context ->plugin = plugin;

#ifdef KERNEL_2_5
	usb_set_intfdata (interface, context);
#else
	minor_table [subminor] = context;
	context ->subminor = subminor;
#endif

	if (vfd_ep_found) {

		if (debug) info ("Registering VFD with devfs");
#ifdef KERNEL_2_5
		if (usb_register_dev (interface, &imon_class)) {

			// Not a fatal error, so ignore
			info ("%s: could not get a minor number for VFD", 
				__FUNCTION__);
		}
#else
		sprintf (name, DEVFS_NAME, subminor);
		if (!(context ->devfs = devfs_register (usb_devfs_handle, name, 
					DEVFS_FL_DEFAULT,
					USB_MAJOR, VFD_MINOR_BASE + subminor,
					DEVFS_MODE, &vfd_fops, NULL))) {

			// not a fatal error so ignore
			info ("%s: devfs register failed for VFD",
					__FUNCTION__);
		}
#endif
	}

	info ("%s: iMON device on usb<%d:%d> initialized",
			__FUNCTION__, dev ->bus ->busnum, dev ->devnum);

	UNLOCK_CONTEXT;
exit:
#ifdef KERNEL_2_5
	return retval;
#else
	return (retval == SUCCESS) ? context : NULL;
#endif
}

/**
 * Callback function for USB core API: disonnect
 */
#ifdef KERNEL_2_5
static void imon_disconnect (struct usb_interface *interface)
#else
static void imon_disconnect (struct usb_device *dev, void *data)
#endif
{
	struct imon_context *context;

	/* prevent races with ir_open()/vfd_open() */
	down (&disconnect_sem);

#ifdef KERNEL_2_5
	context = usb_get_intfdata (interface);
#else
	context = (struct imon_context *)data;
#endif
	LOCK_CONTEXT;

	info ("%s: iMON device disconnected", __FUNCTION__);

#ifdef KERNEL_2_5
	usb_set_intfdata (interface, NULL);
#else
	minor_table [context ->subminor] = NULL;
#endif
	context ->dev_present = FALSE;

	/* Stop reception */
	usb_unlink_urb (context ->rx_urb);

	/* Abort ongoing write */
	if (atomic_read (&context ->tx.busy)) {

		usb_unlink_urb (context ->tx_urb);
		wait_for_completion (&context ->tx.finished);
	}

	/* De-register from lirc_dev if IR port is not open */
	if (!context ->ir_isopen)
		deregister_from_lirc (context);

	if (context ->vfd_supported) {
#ifdef KERNEL_2_5
		usb_deregister_dev (interface, &imon_class);
#else
		if (context ->devfs)
			devfs_unregister (context ->devfs);
#endif
	}

	UNLOCK_CONTEXT;

	if (!context ->ir_isopen && !context ->vfd_isopen)
		delete_context (context);
	
	up (&disconnect_sem);
}

static int __init imon_init (void)
{
	int rc;

	info (MOD_DESC ", v" MOD_VERSION);
	info (MOD_AUTHOR);

	if ((rc = usb_register (&imon_driver)) < 0) {
		err ("%s: usb register failed (%d)", __FUNCTION__, rc);
		return -ENODEV;
	}
	return SUCCESS;
}

static void __exit imon_exit (void)
{
	usb_deregister (&imon_driver);
	info ("module removed. Goodbye!");
}


module_init (imon_init);
module_exit (imon_exit);

#if !defined(KERNEL_2_5)
EXPORT_NO_SYMBOLS;
#endif
