/*
 * USB Microsoft IR Transceiver driver - 0.1
 *
 * Copyright (c) 2003-2004 Dan Conti (dconti@acm.wwu.edu)
 *
 * This driver is based on the USB skeleton driver packaged with the
 * kernel, and the notice from that package has been retained below.
 *
 * The Microsoft IR Transceiver is a neat little IR receiver with two
 * emitters on it designed for Windows Media Center. This driver might
 * work for all media center remotes, but I have only tested it with
 * the philips model. The first revision of this driver only supports
 * the receive function - the transmit function will be much more
 * tricky due to the nature of the hardware. Microsoft chose to build
 * this device inexpensively, therefore making it extra dumb.  There
 * is no interrupt endpoint on this device; all usb traffic happens
 * over two bulk endpoints. As a result of this, poll() for this
 * device is an actual hardware poll (instead of a receive queue
 * check) and is rather expensive.
 *
 * This driver is structured in three basic layers
 *  - lower  - interface with the usb device and manage usb data
 *  - middle - api to convert usb data into mode2 and provide this in
 *    _read calls
 *  - mceusb_* - linux driver interface
 *
 * The key routines are as follows:
 *  msir_fetch_more_data - this reads incoming data, strips off the
 *                         start codes the ir receiver places on them,
 *                         and dumps it in an * internal buffer
 *  msir_generate_mode2  - this takes the above data, depacketizes it,
 *                         and generates mode2 data to feed out
 *                         through read calls
 *
 *
 * All trademarks property of their respective owners.
 *
 * 2003_11_11 - Restructured to minimalize code interpretation in the
 *              driver. The normal use case will be with lirc.
 *
 * 2004_01_01 - Removed all code interpretation. Generate mode2 data
 *              for passing off to lirc. Cleanup
 *
 * 2004_01_04 - Removed devfs handle. Put in a temporary workaround
 *              for a known issue where repeats generate two
 *              sequential spaces * (last_was_repeat_gap)
 *
 * TODO
 *   - Fix up minor number, registration of major/minor with usb subsystem
 *   - Fix up random EINTR being sent
 *   - Fix problem where third key in a repeat sequence is randomly truncated
 *
 */
/*
 * USB Skeleton driver - 0.6
 *
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 *
 * This driver is to be used as a skeleton driver to be able to create a
 * USB driver quickly.  The design of it is based on the usb-serial and
 * dc2xx drivers.
 *
 * Thanks to Oliver Neukum and David Brownell for their help in debugging
 * this driver.
 *
 * TODO:
 *	- fix urb->status race condition in write sequence
 *	- move minor_table to a dynamic list.
 *
 * History:
 *
 * 2001_11_05 - 0.6 - fix minor locking problem in skel_disconnect.
 *			Thanks to Pete Zaitcev for the fix.
 * 2001_09_04 - 0.5 - fix devfs bug in skel_disconnect. Thanks to wim delvaux
 * 2001_08_21 - 0.4 - more small bug fixes.
 * 2001_05_29 - 0.3 - more bug fixes based on review from linux-usb-devel
 * 2001_05_24 - 0.2 - bug fixes based on review from linux-usb-devel people
 * 2001_05_01 - 0.1 - first version
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/smp_lock.h>
#include <linux/usb.h>

#ifdef CONFIG_USB_DEBUG
	static int debug = 1;
#else
	static int debug = 1;
#endif

#include <linux/lirc.h>
#include "lirc_dev.h"

/* Version Information */
#define DRIVER_VERSION "v0.1"
#define DRIVER_AUTHOR "Dan Conti, dconti@acm.wwu.edu"
#define DRIVER_DESC "USB Microsoft IR Transceiver Driver"

/* Module paramaters */
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");

/* Define these values to match your device */
#define USB_MCEUSB_VENDOR_ID	0x045e
#define USB_MCEUSB_PRODUCT_ID	0x006d

/* table of devices that work with this driver */
static struct usb_device_id mceusb_table [] = {
	{ USB_DEVICE(USB_MCEUSB_VENDOR_ID, USB_MCEUSB_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, mceusb_table);

/* XXX TODO, 244 is likely unused but not reserved */
/* Get a minor range for your devices from the usb maintainer */
#define USB_MCEUSB_MINOR_BASE	244


/* we can have up to this number of device plugged in at once */
#define MAX_DEVICES		16

/* Structure to hold all of our device specific stuff */
struct usb_skel	     {
	/* save off the usb device pointer */
	struct usb_device *	    udev;
	/* the interface for this device */
	struct usb_interface *	interface;
	/* the starting minor number for this device */
	unsigned char   minor;
	/* the number of ports this device has */
	unsigned char   num_ports;
	/* number of interrupt in endpoints we have */
	char            num_interrupt_in;
	/* number of bulk in endpoints we have */
	char            num_bulk_in;
	/* number of bulk out endpoints we have */
	char            num_bulk_out;

	/* the buffer to receive data */
	unsigned char *    bulk_in_buffer;
	/* the size of the receive buffer */
	int                bulk_in_size;
	/* the address of the bulk in endpoint */
	__u8               bulk_in_endpointAddr;

	/* the buffer to send data */
	unsigned char *    bulk_out_buffer;
	/* the size of the send buffer */
	int	           bulk_out_size;
	/* the urb used to send data */
	struct urb *       write_urb;
	/* the address of the bulk out endpoint */
	__u8               bulk_out_endpointAddr;

	wait_queue_head_t  wait_q;      /* for timeouts */
	int                open_count;	/* number of times this port
					 * has been opened */
	struct semaphore   sem;		/* locks this structure */

	struct lirc_plugin* plugin;

	/* Used in converting to mode2 and storing */
        /* buffer for the mode2 data, since lirc reads 4bytes */
	int    mode2_data[256];
	int    mode2_idx;               /* read index */
	int    mode2_count;             /* words available (i.e. write
					 * index) */
	int    mode2_partial_pkt_size;
	int    mode2_once;

	/* Used for storing preprocessed usb data before converting to mode2*/
	char   usb_dbuffer[1024];
	int    usb_dstart;
	int    usb_dcount;
	int    usb_valid_bytes_in_bulk_buffer;

	/* Set to 1 if the last value we adjusted was a repeat gap; we
	 * need to hold this value around until we process a lead
	 * space on the repeat code, otherwise we pass off two
	 * sequential spaces */
	int    last_was_repeat_gap;
};

/* driver api */
static ssize_t mceusb_read	(struct file *file, char *buffer,
				 size_t count, loff_t *ppos);
static ssize_t mceusb_write	(struct file *file, const char *buffer,
				 size_t count, loff_t *ppos);
static unsigned int mceusb_poll (struct file* file, poll_table* wait);

static int mceusb_open		(struct inode *inode, struct file *file);
static int mceusb_release	(struct inode *inode, struct file *file);

static void * mceusb_probe	(struct usb_device *dev, unsigned int ifnum,
				 const struct usb_device_id *id);
static void *mceusb_disconnect	(struct usb_interface *intf);

static void mceusb_write_bulk_callback	(struct urb *urb);

/* lower level api */
static int msir_fetch_more_data( struct usb_skel* dev, int dont_block );
static int msir_read_from_buffer( struct usb_skel* dev, char* buffer, int len );
static int msir_mark_as_read( struct usb_skel* dev, int count );
static int msir_available_data( struct usb_skel* dev );

/* middle */
static int msir_generate_mode2( struct usb_skel* dev, signed char* usb_data,
				int bytecount );
static int msir_copy_mode2( struct usb_skel* dev, int* mode2_data, int count );
static int msir_available_mode2( struct usb_skel* dev );

/* helper functions */
static void msir_cleanup( struct usb_skel* dev );
static int set_use_inc(void* data);
static void set_use_dec(void* data);

/* array of pointers to our devices that are currently connected */
static struct usb_skel		*minor_table[MAX_DEVICES];

/* lock to protect the minor_table structure */
static DECLARE_MUTEX (minor_table_mutex);

/*
 * File operations needed when we register this driver.
 * This assumes that this driver NEEDS file operations,
 * of course, which means that the driver is expected
 * to have a node in the /dev directory. If the USB
 * device were for a network interface then the driver
 * would use "struct net_driver" instead, and a serial
 * device would use "struct tty_driver".
 */
static struct file_operations mceusb_fops = {
	/*
	 * The owner field is part of the module-locking
	 * mechanism. The idea is that the kernel knows
	 * which module to increment the use-counter of
	 * BEFORE it calls the device's open() function.
	 * This also means that the kernel can decrement
	 * the use-counter again before calling release()
	 * or should the open() function fail.
	 *
	 * Not all device structures have an "owner" field
	 * yet. "struct file_operations" and "struct net_device"
	 * do, while "struct tty_driver" does not. If the struct
	 * has an "owner" field, then initialize it to the value
	 * THIS_MODULE and the kernel will handle all module
	 * locking for you automatically. Otherwise, you must
	 * increment the use-counter in the open() function
	 * and decrement it again in the release() function
	 * yourself.
	 */
	owner:		THIS_MODULE,

	read:		mceusb_read,
	write:		mceusb_write,
	poll:           mceusb_poll,
	ioctl:          NULL,
	open:		mceusb_open,
	release:	mceusb_release,
};


/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver mceusb_driver = {
	name:		"ir_transceiver",
	probe:		mceusb_probe,
	disconnect:	mceusb_disconnect,
	fops:		&mceusb_fops,
	minor:		USB_MCEUSB_MINOR_BASE,
	id_table:	mceusb_table,
};


/**
 *	usb_mceusb_debug_data
 */
static inline void usb_mceusb_debug_data (const char *function, int size,
					  const unsigned char *data)
{
	int i;

	if (!debug)
		return;

	printk (KERN_DEBUG __FILE__": %s - length = %d, data = ",
		function, size);
	for (i = 0; i < size; ++i) {
		printk ("%.2x ", data[i]);
	}
	printk ("\n");
}


/**
 *	mceusb_delete
 */
static inline void mceusb_delete (struct usb_skel *dev)
{
	minor_table[dev->minor] = NULL;
	if (dev->bulk_in_buffer != NULL)
		kfree (dev->bulk_in_buffer);
	if (dev->bulk_out_buffer != NULL)
		kfree (dev->bulk_out_buffer);
	if (dev->write_urb != NULL)
		usb_free_urb (dev->write_urb);
	kfree (dev);
}

static void mceusb_setup( struct usb_device *udev )
{
	char data[8];
	int res;
	memset( data, 0, 8 );

	/* Get Status */
	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      USB_REQ_GET_STATUS, USB_DIR_IN,
			      0, 0, data, 2, HZ * 3);

	/*    res = usb_get_status( udev, 0, 0, data ); */
	dbg(__FUNCTION__ " res = %d status = 0x%x 0x%x",
	    res, data[0], data[1] );

	/* This is a strange one. They issue a set address to the
	 * device on the receive control pipe and expect a certain
	 * value pair back
	 */
	memset( data, 0, 8 );

	res = usb_control_msg( udev, usb_rcvctrlpipe(udev, 0),
			       5, USB_TYPE_VENDOR, 0, 0,
			       data, 2, HZ * 3 );
	dbg(__FUNCTION__ " res = %d, devnum = %d", res, udev->devnum);
	dbg(__FUNCTION__ " data[0] = %d, data[1] = %d", data[0], data[1] );

	/* set feature */
	res = usb_control_msg( udev, usb_sndctrlpipe(udev, 0),
			       USB_REQ_SET_FEATURE, USB_TYPE_VENDOR,
			       0xc04e, 0x0000, NULL, 0, HZ * 3 );

	dbg(__FUNCTION__ " res = %d", res);

	/* These two are sent by the windows driver, but stall for
	 * me. I dont have an analyzer on the linux side so i can't
	 * see what is actually different and why * the device takes
	 * issue with them
	 */
#if 0
	/* this is some custom control message they send */
	res = usb_control_msg( udev, usb_sndctrlpipe(udev, 0),
			       0x04, USB_TYPE_VENDOR,
			       0x0808, 0x0000, NULL, 0, HZ * 3 );

	dbg(__FUNCTION__ " res = %d", res);

	/* this is another custom control message they send */
	res = usb_control_msg( udev, usb_sndctrlpipe(udev, 0),
			       0x02, USB_TYPE_VENDOR,
			       0x0000, 0x0100, NULL, 0, HZ * 3 );

	dbg(__FUNCTION__ " res = %d", res);
#endif
}

/**
 *	mceusb_open
 */
static int mceusb_open (struct inode *inode, struct file *file)
{
	struct usb_skel *dev = NULL;
	struct usb_device* udev = NULL;
	int subminor;
	int retval = 0;

	dbg(__FUNCTION__);

	/* This is a very sucky point. On lirc, we get passed the
	 * minor number of the lirc device, which is totally
	 * retarded. We want to support people opening /dev/usb/msir0
	 * directly though, so try and determine who the hell is
	 * calling us here
	 */
	if( MAJOR( inode->i_rdev ) != USB_MAJOR )
	{
		/* This is the lirc device just passing on the
		 * request. We probably mismatch minor numbers here,
		 * but the lucky fact is that nobody will ever use two
		 * of the exact same remotes with two recievers on one
		 * machine
		 */
		subminor = 0;
	} else {
		subminor = MINOR (inode->i_rdev) - USB_MCEUSB_MINOR_BASE;
	}
	if ((subminor < 0) ||
	    (subminor >= MAX_DEVICES)) {
		dbg("subminor %d", subminor);
		return -ENODEV;
	}

	/* Increment our usage count for the module.
	 * This is redundant here, because "struct file_operations"
	 * has an "owner" field. This line is included here soley as
	 * a reference for drivers using lesser structures... ;-)
	 */
	try_module_get(THIS_MODULE);

	/* lock our minor table and get our local data for this minor */
	down (&minor_table_mutex);
	dev = minor_table[subminor];
	if (dev == NULL) {
		dbg("dev == NULL");
		up (&minor_table_mutex);
		module_put(THIS_MODULE);
		return -ENODEV;
	}
	udev = dev->udev;

	/* lock this device */
	down (&dev->sem);

	/* unlock the minor table */
	up (&minor_table_mutex);

	/* increment our usage count for the driver */
	++dev->open_count;

	/* save our object in the file's private structure */
	file->private_data = dev;

	/* init the waitq */
	init_waitqueue_head( &dev->wait_q );

	/* clear off the first few messages. these look like
	 * calibration or test data, i can't really tell
	 * this also flushes in case we have random ir data queued up
	 */
	{
		char junk[64];
		int partial = 0, retval, i;
		for( i = 0; i < 40; i++ )
		{
			retval = usb_bulk_msg (udev,
					       usb_rcvbulkpipe
					       (udev,
						dev->bulk_in_endpointAddr),
					       junk, 64,
					       &partial, HZ*10);
		}
	}

	msir_cleanup( dev );

	/* unlock this device */
	up (&dev->sem);

	return retval;
}


/**
 *	mceusb_release
 */
static int mceusb_release (struct inode *inode, struct file *file)
{
	struct usb_skel *dev;
	int retval = 0;

	dev = (struct usb_skel *)file->private_data;
	if (dev == NULL) {
		dbg (__FUNCTION__ " - object is NULL");
		return -ENODEV;
	}

	dbg(__FUNCTION__ " - minor %d", dev->minor);

	/* lock our minor table */
	down (&minor_table_mutex);

	/* lock our device */
	down (&dev->sem);

	if (dev->open_count <= 0) {
		dbg (__FUNCTION__ " - device not opened");
		retval = -ENODEV;
		goto exit_not_opened;
	}

	if (dev->udev == NULL) {
		/* the device was unplugged before the file was released */
		up (&dev->sem);
		mceusb_delete (dev);
		up (&minor_table_mutex);
		module_put(THIS_MODULE);
		return 0;
	}

	/* decrement our usage count for the device */
	--dev->open_count;
	if (dev->open_count <= 0) {
		/* shutdown any bulk writes that might be going on */
		usb_unlink_urb (dev->write_urb);
		dev->open_count = 0;
	}

	/* decrement our usage count for the module */
	module_put(THIS_MODULE);

  exit_not_opened:
	up (&dev->sem);
	up (&minor_table_mutex);

	return retval;
}

static void msir_cleanup( struct usb_skel* dev )
{
	memset( dev->bulk_in_buffer, 0, dev->bulk_in_size );

	memset( dev->usb_dbuffer, 0, sizeof(dev->usb_dbuffer) );
	dev->usb_dstart = 0;
	dev->usb_dcount = 0;
	dev->usb_valid_bytes_in_bulk_buffer = 0;

	memset( dev->mode2_data, 0, sizeof(dev->mode2_data) );
	dev->mode2_partial_pkt_size = 0;
	dev->mode2_count = 0;
	dev->mode2_idx = 0;
	dev->mode2_once = 0;
	dev->last_was_repeat_gap = 0;
}

static int set_use_inc(void* data)
{
	/*    struct usb_skel* skel = (struct usb_skel*)data; */

	try_module_get(THIS_MODULE);
	return 0;
}

static void set_use_dec(void* data)
{
	/* check for unplug here */
	struct usb_skel* dev = (struct usb_skel*) data;
	if( !dev->udev )
	{
		lirc_unregister_plugin( dev->minor );
		lirc_buffer_free( dev->plugin->rbuf );
		kfree( dev->plugin->rbuf );
		kfree( dev->plugin );
	}

	module_put(THIS_MODULE);
}

static int msir_available_mode2( struct usb_skel* dev )
{
	return dev->mode2_count - dev->last_was_repeat_gap;
}

static int msir_available_data( struct usb_skel* dev )
{
	return dev->usb_dcount;
}

static int msir_copy_mode2( struct usb_skel* dev, int* mode2_data, int count )
{
	int words_to_read = count;
	//    int words_avail   = dev->mode2_count;
	int words_avail = msir_available_mode2( dev );

	if( !dev->mode2_once && words_avail )
	{
		int space = PULSE_MASK;
		count--;
		copy_to_user( mode2_data, &space, 4 );
		dev->mode2_once = 1;

		if( count )
		{
			mode2_data++;
		}
		else
		{
			return 1;
		}
	}

	if( !words_avail )
	{
		return 0;
	}

	if( words_to_read > words_avail )
	{
		words_to_read = words_avail;
	}

	dbg(__FUNCTION__ " dev->mode2_count %d, dev->mode2_idx %d",
	    dev->mode2_count, dev->mode2_idx);
	dbg(__FUNCTION__ " words_avail %d words_to_read %d",
	    words_avail, words_to_read);
	copy_to_user( mode2_data, &( dev->mode2_data[dev->mode2_idx] ),
		      words_to_read<<2 );
	dbg(__FUNCTION__ " would copy_to_user() %d w", words_to_read);

	dev->mode2_idx += words_to_read;
	dev->mode2_count -= words_to_read;

	if( dev->mode2_count == 0 )
	{
		dev->mode2_idx = 0;
	}
	else if( dev->mode2_count == 1 && dev->last_was_repeat_gap )
	{
		// shift down the repeat gap and map it up to a
		// lirc-acceptable value
		dev->mode2_data[0] = dev->mode2_data[dev->mode2_idx];
		if( dev->mode2_data[0] >= 60000 &&
		    dev->mode2_data[0] <= 70000 )
			dev->mode2_data[0] = 95000;
		//printk(__FUNCTION__ " shifting value %d down from %d prev %d\n", dev->mode2_data[0], dev->mode2_idx,
		//    dev->mode2_data[dev->mode2_idx-1]);
		dev->mode2_idx = 0;
	}

	return words_to_read;
}

static int msir_read_from_buffer( struct usb_skel* dev, char* buffer, int len )
{
	if( len > dev->usb_dcount )
	{
		len = dev->usb_dcount;
	}
	memcpy( buffer, dev->usb_dbuffer + dev->usb_dstart, len );
	return len;
}

static int msir_mark_as_read( struct usb_skel* dev, int count )
{
	//    if( count != dev->usb_dcount )
	//        printk(KERN_INFO __FUNCTION__ " count %d dev->usb_dcount %d dev->usb_dstart %d", count, dev->usb_dcount, dev->usb_dstart );
	if( count > dev->usb_dcount )
		count = dev->usb_dcount;
	dev->usb_dcount -= count;
	dev->usb_dstart += count;

	if( !dev->usb_dcount )
		dev->usb_dstart = 0;

	return 0;
}


/*
 * msir_fetch_more_data
 *
 * The goal here is to read in more remote codes from the remote. In
 * the event that the remote isn't sending us anything, the caller
 * will block until a key is pressed (i.e. this performs phys read,
 * filtering, and queueing of data) unless dont_block is set to 1; in
 * this situation, it will perform a few reads and will exit out if it
 * does not see any appropriate data
 *
 * dev->sem should be locked when this function is called - fine grain
 * locking isn't really important here anyways
 *
 * TODO change this to do partials based on term codes, or not always fill
 */

static int msir_fetch_more_data( struct usb_skel* dev, int dont_block )
{
	int retries = 0;
	int count, this_read, partial;
	int retval;
	int writeindex, terminators = 0;
	int bytes_to_read = sizeof(dev->usb_dbuffer) - dev->usb_dcount;
	signed char* ibuf;
	int sequential_empty_reads = 0;

	/* special case where we are already full */
	if( bytes_to_read == 0 )
		return dev->usb_dcount;

	/* shift down */
	if( dev->usb_dcount && dev->usb_dstart != 0 )
	{
		printk( __FUNCTION__ " shifting %d bytes from %d\n",
			dev->usb_dcount, dev->usb_dstart );
		memcpy( dev->usb_dbuffer, dev->usb_dbuffer + dev->usb_dstart,
			dev->usb_dcount );
	}

	dev->usb_dstart = 0;

	writeindex = dev->usb_dcount;

	count = bytes_to_read;

	ibuf = (signed char*)dev->bulk_in_buffer;
	if( !dev->usb_valid_bytes_in_bulk_buffer )
	{
		memset( ibuf, 0, dev->bulk_in_size );
	}

#if 0
	printk( __FUNCTION__ " going to read, dev->usb_dcount %d, bytes_to_read %d vbb %d\n", dev->usb_dcount, bytes_to_read,
		dev->usb_valid_bytes_in_bulk_buffer );
#endif
	/* 8 is the minimum read size */
	while( count > 8 )
	{
		int i, goodbytes = 0;

		/* break out if we were interrupted */
		if( signal_pending(current) )
		{
			printk( __FUNCTION__ " got signal %ld\n",
				current->pending.signal.sig[0]);
			return dev->usb_dcount ? dev->usb_dcount : -EINTR;
		}

		/* or if we were unplugged */
		if( !dev->udev )
		{
			return -ENODEV;
		}

		/* or on data issues */
		if( writeindex == sizeof(dev->usb_dbuffer) )
		{
			printk( __FUNCTION__ " buffer full, returning\n");
			return dev->usb_dcount;
		}

		// always read the maximum
		this_read = dev->bulk_in_size;

		partial = 0;

		if( dev->usb_valid_bytes_in_bulk_buffer ) {
			retval = 0;
			this_read = partial = dev->usb_valid_bytes_in_bulk_buffer;
			dev->usb_valid_bytes_in_bulk_buffer = 0;
		} else {
			// This call always returns almost immediately
			// with data, since this device will always
			// provide a 2 byte response on a bulk
			// read. Not exactly friendly to the usb bus
			// or our load avg. We attempt to compensate
			// for this on 2 byte reads below

			memset( ibuf, 0, dev->bulk_in_size );
			retval = usb_bulk_msg (dev->udev,
					       usb_rcvbulkpipe
					       (dev->udev,
						dev->bulk_in_endpointAddr),
					       (unsigned char*)ibuf, this_read,
					       &partial, HZ*10);
		}

		if( retval )
		{
			/* break out on errors */
			printk(__FUNCTION__ " got retval %d %d %d",
			       retval, this_read, partial );
			if( retval == USB_ST_DATAOVERRUN && retries < 5 )
			{
				retries++;
				interruptible_sleep_on_timeout
					( &dev->wait_q, HZ );
				continue;
			}
			else
			{
				return -EIO;
			}
		} else {
			retries = 0;
		}

		if( partial )
		{
			this_read = partial;
		}

		/* All packets i've seen start with b1 60. If no data
		 * was actually available, the transceiver still gives
		 * this byte pair back. We only care about actual
		 * codes, so we can safely ignore these 2 byte reads
		 */
		if( this_read > 2 )
		{
#if 0
			printk( __FUNCTION__ " read %d bytes partial %d goodbytes %d writeidx %d\n",
				this_read, partial, goodbytes, writeindex );
#endif
			sequential_empty_reads = 0;
			/* copy from the input buffer to the capture buffer */
			for( i = 0; i < this_read; i++ )
			{
				if( (((unsigned char*)ibuf)[i] == 0xb1) ||
				    (ibuf[i] == 0x60) )
					;
				else
				{
					if( writeindex == sizeof(dev->usb_dbuffer) )
					{
						/* this can happen in
						 * repeats, where
						 * basically the bulk
						 * buffer is getting
						 * spammed and we
						 * aren't processing
						 * data fast enough
						 */
#if 1
						dev->usb_valid_bytes_in_bulk_buffer = this_read - i;
						memcpy( ibuf, &( ibuf[i] ),
							dev->usb_valid_bytes_in_bulk_buffer );
#endif
						break;
					}
					dev->usb_dbuffer[writeindex++] = ibuf[i];
					goodbytes++;

					if( ibuf[i] == 0x7f )
					{
						terminators++;

						/* This is a bug - we should either get 10 or 15 */
						if( terminators > 15 )
						{
							dbg("bugbug - terminators %d at %d gb %d", terminators, i, goodbytes );
						} else
							dbg("terminator %d at %d gb %d", terminators, i, goodbytes );
						dbg("writeindex %d", writeindex);
					}
					else if( terminators )
					{
						if( ((unsigned char*)ibuf)[i] == 128 )
						{
							/* copy back any remainder and break out */
							dev->usb_valid_bytes_in_bulk_buffer = this_read - (i + 1);
							if( dev->usb_valid_bytes_in_bulk_buffer )
							{
								memcpy( ibuf, &( ibuf[i+1] ), dev->usb_valid_bytes_in_bulk_buffer );
							}

							count = 0;
							break;
						}
						if( terminators == 10 ||
						    terminators == 15 )
							dbg("post-termination data %d idx %d %d", ibuf[i], dev->usb_dcount, i);
					}
				}
			}
			dev->usb_dcount += goodbytes;
			count -= goodbytes;
		} else {
			sequential_empty_reads++;

			// assume no data
			if( dont_block && sequential_empty_reads == 5 )
				break;

			// Try to be nice to the usb bus by sleeping
			// for a bit here before going in to the next
			// read
			interruptible_sleep_on_timeout( &dev->wait_q, 1 );
		}

	}
	/* return the number of bytes available now */
	return dev->usb_dcount;
}

// layout of data, per Christoph Bartelmus
// The protocol is:
// 1 byte: -length of following packet
// the following bytes of the packet are:
// negative value:
//   -(number of time units) of pulse
// positive value:
//   (number of time units) of space
// one time unit is 50us

#define MCE_TIME_UNIT 50

// returns the number of bytes processed from the 'usb_data' array
static int msir_generate_mode2( struct usb_skel* dev, signed char* usb_data,
				int bytecount )
{
	int bytes_left_in_packet = 0;
	int pos = 0;
	int mode2count = 0;
	int last_was_pulse = 1;
	int last_pkt = 0;
	int split_pkt_size = 0;
	// XXX no bounds checking here
	int* mode2_data;
	int mode2_limit = sizeof( dev->mode2_data ) - dev->mode2_count;

	// If data exists in the buffer, we have to point to the last
	// item there so we can append consecutive pulse/space
	// ops. Otherwise, set last_was_pulse 1 (since the first byte
	// is a pulse, and we want to store in the first array
	// location
	if( dev->mode2_count == 0 )
	{
		mode2_data = &( dev->mode2_data[0] );
		last_was_pulse = (dev->mode2_once ? 1 : 0);
		mode2_data[0] = 0;
	}
	else
	{
		mode2_data = &( dev->mode2_data[dev->mode2_idx +
						dev->mode2_count - 1] );
		last_was_pulse = (mode2_data[0] & PULSE_BIT) ? 1 : 0;
	}

	while( pos < bytecount && !last_pkt &&
	       (mode2_limit > (dev->mode2_count + mode2count)) )
	{
		if( dev->mode2_partial_pkt_size )
		{
			bytes_left_in_packet = dev->mode2_partial_pkt_size;
			dev->mode2_partial_pkt_size = 0;
		}
		else {
			bytes_left_in_packet = 128 + usb_data[pos];

			// XXX out of sync? find the next packet
			// header, establish a distance, and fix the
			// packet size
			if( bytes_left_in_packet > 4 )
			{
				int i;
				for( i = pos + 1; i < pos + 4; i++ )
				{
					if( (int)(128 + usb_data[i]) <= 4 )
					{
						bytes_left_in_packet = i - pos;
						break;
					}
				}
			}
			else
			{
				// otherwise, increment past the header
				pos++;
			}
		}

		// special case where we have a terminator at the
		// start but not at the end of this packet, indicating
		// potential repeat, or the packet is less than 4
		// bytes, indicating end also special case a split
		// starting packet
		if( pos > 1 && bytes_left_in_packet < 4 )
		{
			// end
			last_pkt = 1;
		}
		else if( usb_data[pos] == 127 &&
			 usb_data[pos+bytes_left_in_packet-1] != 127 )
		{
			// the genius ir transciever is blending data
			// from the repeat events into a single
			// packet. how we handle this is by splitting
			// the packet (and truncating the packet size
			// value we read), then rewriting a new packet
			// header onto the outbound data.  it's
			// ultraghetto.
			while( usb_data[pos+bytes_left_in_packet-1] != 127 )
			{
				bytes_left_in_packet--;
				split_pkt_size++;
			}
			// repeat code
			last_pkt = 2;
		}
		while( bytes_left_in_packet && pos < bytecount )
		{
			int keycode = usb_data[pos];
			int pulse = 0;

			pos++;
			if( keycode < 0 )
			{
				pulse = 1;
				keycode += 128;
			}
			keycode *= MCE_TIME_UNIT;

			// on a state change, increment the position
			// for the output buffer and initialize the
			// current spot to 0; otherwise we need to
			// concatenate pulse/gap values for lirc to be
			// happy
			if( pulse != last_was_pulse &&
			    (mode2count || mode2_data[mode2count]))
			{
				if( dev->last_was_repeat_gap )
				{
					//printk( __FUNCTION__ " transition with lwrg set lastval %d idx1 %d idx2 %d\n",
					//  mode2_data[mode2count],mode2count, dev->mode2_count+dev->mode2_idx-1 );
				}
				mode2count++;
				mode2_data[mode2count] = 0;
			}

			mode2_data[mode2count] += keycode;

			// Or in the pulse bit, and map all gap
			// lengths to a fixed value; this makes lirc
			// happy, sort of.
			if( pulse ) {
				mode2_data[mode2count] |= PULSE_BIT;
				dev->last_was_repeat_gap = 0;
			}

			last_was_pulse = pulse;
			bytes_left_in_packet--;
		}
	}

	// If the last value in the data array is a repeat gap, set
	// the last_was_repeat_gap flag
	if( mode2_data[mode2count] > 20000 && mode2_data[mode2count] < 70000 )
	{
		// printk(__FUNCTION__ " setting lwrg for val %d idx1 %d idx2 %d\n",
		//    mode2_data[mode2count], mode2count, dev->mode2_count+dev->mode2_idx-1 );
		dev->last_was_repeat_gap = 1;
	} else {
		dev->last_was_repeat_gap = 0;
	}

	// this is a bit tricky; we need to change to a counter, but
	// if we already had data in dev->mode2_data, then byte 0
	// actually was pre-existing data and shouldn't be counted
	if( mode2count && !dev->mode2_count )
	{
		mode2count++;
		//        printk(__FUNCTION__ " mode2count++ to %d\n", mode2count);
	}

	// never lie about how much output we have
	dev->mode2_count += mode2count;

	if( last_pkt == 1 )
	{
		return bytecount;
	}
	else
	{
		//  note the partial pkt size, and make sure we only claim
		//  the bytes we processed
		if( last_pkt == 2 )
		{
			dev->mode2_partial_pkt_size = split_pkt_size;
		}
#if 1
		// XXX this i am not sure about; it seems like this should be required, but it
		// isn't, and seems to cause problems
		else
		{
			dev->mode2_partial_pkt_size = bytes_left_in_packet;
		}
#endif
		return pos;
	}
}

static ssize_t mceusb_read( struct file* file, char* buffer,
			    size_t count, loff_t* ppos)
{
	char _data_buffer[128];
	struct usb_skel* dev;
	int read_count;
	int bytes_copied = 0;

	dev = (struct usb_skel*) file->private_data;

	if( (count % 4) != 0 )
	{
		return -EINVAL;
	}

	down( &dev->sem );

	/* verify that the device wasn't unplugged */
	if (dev->udev == NULL) {
		up( &dev->sem );
		return -ENODEV;
	}

	dbg(__FUNCTION__ " (1) calling msir_copy_mode2 with %d", count);
	bytes_copied = 4 * msir_copy_mode2( dev, (int*)buffer, count >> 2 );
	if( bytes_copied == count )
	{
		up( &dev->sem );
		return count;
	}

	/* we didn't get enough mode2 data. the process now is a bit complex
	 * 1. see if we have data read from the usb device that hasn't
	 *    been converted to mode2; if so, convert that, and try to
	 *    copy that out
	 * 2. otherwise, go ahead and read more, then convert that, then copy
	 */

	if( dev->usb_dcount )
	{
		read_count = msir_read_from_buffer( dev, _data_buffer, 128 );
		read_count = msir_generate_mode2
			( dev, (signed char*)_data_buffer, read_count );
		msir_mark_as_read( dev, read_count );
		bytes_copied += (4 * msir_copy_mode2
				 ( dev, (int*)(buffer + bytes_copied),
				   (count-bytes_copied) >> 2 ));
	}

	if( bytes_copied == count )
	{
		up( &dev->sem );
		return count;
	}

	/* read more data in a loop until we get enough */
	while( bytes_copied < count )
	{
		read_count = msir_fetch_more_data
			( dev, (file->f_flags & O_NONBLOCK ? 1 : 0) );

		if( read_count <= 0 )
		{
			up( &dev->sem );
			return (read_count ? read_count : -EWOULDBLOCK);
		}

		read_count = msir_read_from_buffer( dev, _data_buffer, 128 );
		read_count = msir_generate_mode2
			( dev, (signed char*)_data_buffer, read_count );
		msir_mark_as_read( dev, read_count );

		bytes_copied += (4 * msir_copy_mode2
				 ( dev, (int*)(buffer + bytes_copied),
				   (count-bytes_copied) >> 2 ));
	}

	up( &dev->sem );
	return bytes_copied;
}

/**
 * mceusb_poll
 */
static unsigned int mceusb_poll(struct file* file, poll_table* wait)
{
	struct usb_skel* dev;
	int data;
	dev = (struct usb_skel*)file->private_data;

	// So this is a crummy poll. Unfortunately all the lirc tools
	// assume your hardware is interrupt driven. Instead, we have
	// to actually read here to see whether or not there is data
	// (unless we have a key saved up - unlikely )

	//    if( dev->usb_dcount || dev->mode2_count )
	if( msir_available_data( dev ) || msir_available_mode2( dev ) )
	{
		return POLLIN | POLLRDNORM;
	}
	else {
		down( &dev->sem );
		data = msir_fetch_more_data( dev, 1 );
		up( &dev->sem );

		if( data )
			return POLLIN | POLLRDNORM;
	}

	return 0;
}

/**
 *	mceusb_write
 */
static ssize_t mceusb_write (struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct usb_skel *dev;
	ssize_t bytes_written = 0;
	int retval = 0;

	dev = (struct usb_skel *)file->private_data;

	dbg(__FUNCTION__ " - minor %d, count = %d", dev->minor, count);

	/* lock this object */
	down (&dev->sem);

	/* verify that the device wasn't unplugged */
	if (dev->udev == NULL) {
		retval = -ENODEV;
		goto exit;
	}

	/* verify that we actually have some data to write */
	if (count == 0) {
		dbg(__FUNCTION__ " - write request of 0 bytes");
		goto exit;
	}

	/* see if we are already in the middle of a write */
	if (dev->write_urb->status == -EINPROGRESS) {
		dbg (__FUNCTION__ " - already writing");
		goto exit;
	}

	/* we can only write as much as 1 urb will hold */
	bytes_written = (count > dev->bulk_out_size) ?
		dev->bulk_out_size : count;

	/* copy the data from userspace into our urb */
	if (copy_from_user(dev->write_urb->transfer_buffer, buffer,
			   bytes_written)) {
		retval = -EFAULT;
		goto exit;
	}

	usb_mceusb_debug_data (__FUNCTION__, bytes_written,
			       dev->write_urb->transfer_buffer);

	/* set up our urb */
	FILL_BULK_URB(dev->write_urb, dev->udev,
		      usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
		      dev->write_urb->transfer_buffer, bytes_written,
		      mceusb_write_bulk_callback, dev);

	/* send the data out the bulk port */
	retval = usb_submit_urb(dev->write_urb);
	if (retval) {
		err(__FUNCTION__ " - failed submitting write urb, error %d",
		    retval);
	} else {
		retval = bytes_written;
	}

  exit:
	/* unlock the device */
	up (&dev->sem);

	return retval;
}

/*
 *	mceusb_write_bulk_callback
 */

static void mceusb_write_bulk_callback (struct urb *urb)
{
	struct usb_skel *dev = (struct usb_skel *)urb->context;

	dbg(__FUNCTION__ " - minor %d", dev->minor);

	if ((urb->status != -ENOENT) &&
	    (urb->status != -ECONNRESET)) {
		dbg(__FUNCTION__ " - nonzero write bulk status received: %d",
		    urb->status);
		return;
	}

	return;
}

/**
 *	mceusb_probe
 *
 *	Called by the usb core when a new device is connected that it
 *	thinks this driver might be interested in.
 */
static void * mceusb_probe(struct usb_device *udev, unsigned int ifnum,
			   const struct usb_device_id *id)
{
	struct usb_skel *dev = NULL;
	struct usb_interface *interface;
	struct usb_interface_descriptor *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct lirc_plugin* plugin;
	struct lirc_buffer* rbuf;

	int minor;
	int buffer_size;
	int i;
	char name[10];


	/* See if the device offered us matches what we can accept */
	if ((udev->descriptor.idVendor != USB_MCEUSB_VENDOR_ID) ||
	    (udev->descriptor.idProduct != USB_MCEUSB_PRODUCT_ID)) {
		return NULL;
	}

	/* select a "subminor" number (part of a minor number) */
	down (&minor_table_mutex);
	for (minor = 0; minor < MAX_DEVICES; ++minor) {
		if (minor_table[minor] == NULL)
			break;
	}
	if (minor >= MAX_DEVICES) {
		info ("Too many devices plugged in, can not handle this device.");
		goto exit;
	}

	/* allocate memory for our device state and intialize it */
	dev = kmalloc (sizeof(struct usb_skel), GFP_KERNEL);
	if (dev == NULL) {
		err ("Out of memory");
		goto exit;
	}
	minor_table[minor] = dev;

	interface = &udev->actconfig->interface[ifnum];

	init_MUTEX (&dev->sem);
	dev->udev = udev;
	dev->interface = interface;
	dev->minor = minor;

	/* set up the endpoint information */
	/* check out the endpoints */
	iface_desc = &interface->altsetting[0];
	for (i = 0; i < iface_desc->bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i];

		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk in endpoint */
			buffer_size = endpoint->wMaxPacketSize;
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
			if (!dev->bulk_in_buffer) {
				err("Couldn't allocate bulk_in_buffer");
				goto error;
			}
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk out endpoint */
			dev->write_urb = usb_alloc_urb(0);
			if (!dev->write_urb) {
				err("No free urbs available");
				goto error;
			}
			buffer_size = endpoint->wMaxPacketSize;
			dev->bulk_out_size = buffer_size;
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_out_buffer = kmalloc (buffer_size, GFP_KERNEL);
			if (!dev->bulk_out_buffer) {
				err("Couldn't allocate bulk_out_buffer");
				goto error;
			}
			FILL_BULK_URB(dev->write_urb, udev,
				      usb_sndbulkpipe
				      (udev, endpoint->bEndpointAddress),
				      dev->bulk_out_buffer, buffer_size,
				      mceusb_write_bulk_callback, dev);
		}
	}

	memset( dev->mode2_data, 0, sizeof( dev->mode2_data ) );
	dev->mode2_idx = 0;
	dev->mode2_count = 0;
	dev->mode2_partial_pkt_size = 0;
	dev->mode2_once = 0;
	dev->last_was_repeat_gap = 0;

	/* Set up our lirc plugin */
	if(!(plugin = kmalloc(sizeof(struct lirc_plugin), GFP_KERNEL))) {
		err("out of memory");
		goto error;
	}
	memset( plugin, 0, sizeof(struct lirc_plugin) );

	if(!(rbuf = kmalloc(sizeof(struct lirc_buffer), GFP_KERNEL))) {
		err("out of memory");
		kfree( plugin );
		goto error;
	}
	/* the lirc_atiusb module doesn't memset rbuf here ... ? */
	if( lirc_buffer_init( rbuf, sizeof(lirc_t),
			      sizeof(struct lirc_buffer))) {
		err("out of memory");
		kfree( plugin );
		kfree( rbuf );
		goto error;
	}
	strcpy(plugin->name, "lirc_mce ");
	plugin->minor       = minor;
	plugin->code_length = sizeof(lirc_t);
	plugin->features    = LIRC_CAN_REC_MODE2; // | LIRC_CAN_SEND_MODE2;
	plugin->data        = dev;
	plugin->rbuf        = rbuf;
	plugin->ioctl       = NULL;
	plugin->set_use_inc = &set_use_inc;
	plugin->set_use_dec = &set_use_dec;
	plugin->fops        = &mceusb_fops;
	if( lirc_register_plugin( plugin ) < 0 )
	{
		kfree( plugin );
		lirc_buffer_free( rbuf );
		kfree( rbuf );
		goto error;
	}
	dev->plugin = plugin;

	mceusb_setup( udev );

	/* let the user know what node this device is now attached to */
	info ("USB Microsoft IR Transceiver device now attached to msir%d",
	      dev->minor);
	goto exit;

  error:
	mceusb_delete (dev);
	dev = NULL;

  exit:
	up (&minor_table_mutex);
	return dev;
}

/**
 *	mceusb_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 */
static void mceusb_disconnect(struct usb_device *udev, void *ptr)
{
	int minor;

	down (&minor_table_mutex);
	down (&dev->sem);

	minor = dev->minor;

	/* unhook lirc things */
	lirc_unregister_plugin( dev->minor );
	lirc_buffer_free( dev->plugin->rbuf );
	kfree( dev->plugin->rbuf );
	kfree( dev->plugin );

	/* if the device is not opened, then we clean up right now */
	if (!dev->open_count) {
		up (&dev->sem);
		mceusb_delete (dev);
	} else {
		dev->udev = NULL;
		up (&dev->sem);
	}

	info("USB Skeleton #%d now disconnected", minor);
	up (&minor_table_mutex);
	return NULL;
}



/**
 *	usb_mceusb_init
 */
static int __init usb_mceusb_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&mceusb_driver);
	if (result < 0) {
		err("usb_register failed for the "__FILE__" driver. Error number %d",
		    result);
		return -1;
	}

	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}


/**
 *	usb_mceusb_exit
 */
static void __exit usb_mceusb_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&mceusb_driver);
}


module_init (usb_mceusb_init);
module_exit (usb_mceusb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

