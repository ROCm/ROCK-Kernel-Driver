/*
 * USB Skeleton driver - 1.1
 *
 * Copyright (C) 2001-2003 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 *
 * This driver is to be used as a skeleton driver to be able to create a
 * USB driver quickly.  The design of it is based on the usb-serial and
 * dc2xx drivers.
 *
 * Thanks to Oliver Neukum, David Brownell, and Alan Stern for their help
 * in debugging this driver.
 *
 *
 * History:
 *
 * 2003-05-06 - 1.1 - changes due to usb core changes with usb_register_dev()
 * 2003-02-25 - 1.0 - fix races involving urb->status, unlink_urb(), and
 *			disconnect.  Fix transfer amount in read().  Use
 *			macros instead of magic numbers in probe().  Change
 *			size variables to size_t.  Show how to eliminate
 *			DMA bounce buffer.
 * 2002_12_12 - 0.9 - compile fixes and got rid of fixed minor array.
 * 2002_09_26 - 0.8 - changes due to USB core conversion to struct device
 *			driver.
 * 2002_02_12 - 0.7 - zero out dev in probe function for devices that do
 *			not have both a bulk in and bulk out endpoint.
 *			Thanks to Holger Waechtler for the fix.
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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/usb.h>

#ifdef CONFIG_USB_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif

/* Use our own dbg macro */
#undef dbg
#define dbg(format, arg...) do { if (debug) printk(KERN_DEBUG __FILE__ ": " format "\n" , ## arg); } while (0)


/* Version Information */
#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "Greg Kroah-Hartman, greg@kroah.com"
#define DRIVER_DESC "USB Skeleton Driver"

/* Module parameters */
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");


/* Define these values to match your devices */
#define USB_SKEL_VENDOR_ID	0xfff0
#define USB_SKEL_PRODUCT_ID	0xfff0

/* table of devices that work with this driver */
static struct usb_device_id skel_table [] = {
	{ USB_DEVICE(USB_SKEL_VENDOR_ID, USB_SKEL_PRODUCT_ID) },
	/* "Gadget Zero" firmware runs under Linux */
	{ USB_DEVICE(0x0525, 0xa4a0) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, skel_table);


/* Get a minor range for your devices from the usb maintainer */
#define USB_SKEL_MINOR_BASE	192

/* Structure to hold all of our device specific stuff */
struct usb_skel {
	struct usb_device *	udev;			/* save off the usb device pointer */
	struct usb_interface *	interface;		/* the interface for this device */
	unsigned char		minor;			/* the starting minor number for this device */
	unsigned char		num_ports;		/* the number of ports this device has */
	char			num_interrupt_in;	/* number of interrupt in endpoints we have */
	char			num_bulk_in;		/* number of bulk in endpoints we have */
	char			num_bulk_out;		/* number of bulk out endpoints we have */

	unsigned char *		bulk_in_buffer;		/* the buffer to receive data */
	size_t			bulk_in_size;		/* the size of the receive buffer */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */

	unsigned char *		bulk_out_buffer;	/* the buffer to send data */
	size_t			bulk_out_size;		/* the size of the send buffer */
	struct urb *		write_urb;		/* the urb used to send data */
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	atomic_t		write_busy;		/* true iff write urb is busy */
	struct completion	write_finished;		/* wait for the write to finish */

	int			open;			/* if the port is open or not */
	int			present;		/* if the device is not disconnected */
	struct semaphore	sem;			/* locks this structure */
};


/* prevent races between open() and disconnect() */
static DECLARE_MUTEX (disconnect_sem);

/* local function prototypes */
static ssize_t skel_read	(struct file *file, char *buffer, size_t count, loff_t *ppos);
static ssize_t skel_write	(struct file *file, const char *buffer, size_t count, loff_t *ppos);
static int skel_ioctl		(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
static int skel_open		(struct inode *inode, struct file *file);
static int skel_release		(struct inode *inode, struct file *file);

static int skel_probe		(struct usb_interface *interface, const struct usb_device_id *id);
static void skel_disconnect	(struct usb_interface *interface);

static void skel_write_bulk_callback	(struct urb *urb, struct pt_regs *regs);

/*
 * File operations needed when we register this driver.
 * This assumes that this driver NEEDS file operations,
 * of course, which means that the driver is expected
 * to have a node in the /dev directory. If the USB
 * device were for a network interface then the driver
 * would use "struct net_driver" instead, and a serial
 * device would use "struct tty_driver".
 */
static struct file_operations skel_fops = {
	/*
	 * The owner field is part of the module-locking
	 * mechanism. The idea is that the kernel knows
	 * which module to increment the use-counter of
	 * BEFORE it calls the device's open() function.
	 * This also means that the kernel can decrement
	 * the use-counter again before calling release()
	 * or should the open() function fail.
	 */
	.owner =	THIS_MODULE,

	.read =		skel_read,
	.write =	skel_write,
	.ioctl =	skel_ioctl,
	.open =		skel_open,
	.release =	skel_release,
};

/* 
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with devfs and the driver core
 */
static struct usb_class_driver skel_class = {
	.name =		"usb/skel%d",
	.fops =		&skel_fops,
	.mode =		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH,
	.minor_base =	USB_SKEL_MINOR_BASE,
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver skel_driver = {
	.owner =	THIS_MODULE,
	.name =		"skeleton",
	.probe =	skel_probe,
	.disconnect =	skel_disconnect,
	.id_table =	skel_table,
};


/**
 *	usb_skel_debug_data
 */
static inline void usb_skel_debug_data (const char *function, int size, const unsigned char *data)
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
 *	skel_delete
 */
static inline void skel_delete (struct usb_skel *dev)
{
	kfree (dev->bulk_in_buffer);
	usb_buffer_free (dev->udev, dev->bulk_out_size,
				dev->bulk_out_buffer,
				dev->write_urb->transfer_dma);
	usb_free_urb (dev->write_urb);
	kfree (dev);
}


/**
 *	skel_open
 */
static int skel_open (struct inode *inode, struct file *file)
{
	struct usb_skel *dev = NULL;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	dbg("%s", __FUNCTION__);

	subminor = iminor(inode);

	/* prevent disconnects */
	down (&disconnect_sem);

	interface = usb_find_interface (&skel_driver, subminor);
	if (!interface) {
		err ("%s - error, can't find device for minor %d",
		     __FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit_no_device;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit_no_device;
	}

	/* lock this device */
	down (&dev->sem);

	/* increment our usage count for the driver */
	++dev->open;

	/* save our object in the file's private structure */
	file->private_data = dev;

	/* unlock this device */
	up (&dev->sem);

exit_no_device:
	up (&disconnect_sem);
	return retval;
}


/**
 *	skel_release
 */
static int skel_release (struct inode *inode, struct file *file)
{
	struct usb_skel *dev;
	int retval = 0;

	dev = (struct usb_skel *)file->private_data;
	if (dev == NULL) {
		dbg ("%s - object is NULL", __FUNCTION__);
		return -ENODEV;
	}

	dbg("%s - minor %d", __FUNCTION__, dev->minor);

	/* lock our device */
	down (&dev->sem);

	if (dev->open <= 0) {
		dbg ("%s - device not opened", __FUNCTION__);
		retval = -ENODEV;
		goto exit_not_opened;
	}

	/* wait for any bulk writes that might be going on to finish up */
	if (atomic_read (&dev->write_busy))
		wait_for_completion (&dev->write_finished);

	--dev->open;

	if (!dev->present && !dev->open) {
		/* the device was unplugged before the file was released */
		up (&dev->sem);
		skel_delete (dev);
		return 0;
	}

exit_not_opened:
	up (&dev->sem);

	return retval;
}


/**
 *	skel_read
 */
static ssize_t skel_read (struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct usb_skel *dev;
	int retval = 0;

	dev = (struct usb_skel *)file->private_data;

	dbg("%s - minor %d, count = %d", __FUNCTION__, dev->minor, count);

	/* lock this object */
	down (&dev->sem);

	/* verify that the device wasn't unplugged */
	if (!dev->present) {
		up (&dev->sem);
		return -ENODEV;
	}

	/* do a blocking bulk read to get data from the device */
	retval = usb_bulk_msg (dev->udev,
			       usb_rcvbulkpipe (dev->udev,
						dev->bulk_in_endpointAddr),
			       dev->bulk_in_buffer,
			       min (dev->bulk_in_size, count),
			       &count, HZ*10);

	/* if the read was successful, copy the data to userspace */
	if (!retval) {
		if (copy_to_user (buffer, dev->bulk_in_buffer, count))
			retval = -EFAULT;
		else
			retval = count;
	}

	/* unlock the device */
	up (&dev->sem);
	return retval;
}


/**
 *	skel_write
 *
 *	A device driver has to decide how to report I/O errors back to the
 *	user.  The safest course is to wait for the transfer to finish before
 *	returning so that any errors will be reported reliably.  skel_read()
 *	works like this.  But waiting for I/O is slow, so many drivers only
 *	check for errors during I/O initiation and do not report problems
 *	that occur during the actual transfer.  That's what we will do here.
 *
 *	A driver concerned with maximum I/O throughput would use double-
 *	buffering:  Two urbs would be devoted to write transfers, so that
 *	one urb could always be active while the other was waiting for the
 *	user to send more data.
 */
static ssize_t skel_write (struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct usb_skel *dev;
	ssize_t bytes_written = 0;
	int retval = 0;

	dev = (struct usb_skel *)file->private_data;

	dbg("%s - minor %d, count = %d", __FUNCTION__, dev->minor, count);

	/* lock this object */
	down (&dev->sem);

	/* verify that the device wasn't unplugged */
	if (!dev->present) {
		retval = -ENODEV;
		goto exit;
	}

	/* verify that we actually have some data to write */
	if (count == 0) {
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		goto exit;
	}

	/* wait for a previous write to finish up; we don't use a timeout
	 * and so a nonresponsive device can delay us indefinitely.
	 */
	if (atomic_read (&dev->write_busy))
		wait_for_completion (&dev->write_finished);

	/* we can only write as much as our buffer will hold */
	bytes_written = min (dev->bulk_out_size, count);

	/* copy the data from userspace into our transfer buffer;
	 * this is the only copy required.
	 */
	if (copy_from_user(dev->write_urb->transfer_buffer, buffer,
			   bytes_written)) {
		retval = -EFAULT;
		goto exit;
	}

	usb_skel_debug_data (__FUNCTION__, bytes_written,
			     dev->write_urb->transfer_buffer);

	/* this urb was already set up, except for this write size */
	dev->write_urb->transfer_buffer_length = bytes_written;

	/* send the data out the bulk port */
	/* a character device write uses GFP_KERNEL,
	 unless a spinlock is held */
	init_completion (&dev->write_finished);
	atomic_set (&dev->write_busy, 1);
	retval = usb_submit_urb(dev->write_urb, GFP_KERNEL);
	if (retval) {
		atomic_set (&dev->write_busy, 0);
		err("%s - failed submitting write urb, error %d",
		    __FUNCTION__, retval);
	} else {
		retval = bytes_written;
	}

exit:
	/* unlock the device */
	up (&dev->sem);

	return retval;
}


/**
 *	skel_ioctl
 */
static int skel_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usb_skel *dev;

	dev = (struct usb_skel *)file->private_data;

	/* lock this object */
	down (&dev->sem);

	/* verify that the device wasn't unplugged */
	if (!dev->present) {
		up (&dev->sem);
		return -ENODEV;
	}

	dbg("%s - minor %d, cmd 0x%.4x, arg %ld", __FUNCTION__,
	    dev->minor, cmd, arg);

	/* fill in your device specific stuff here */

	/* unlock the device */
	up (&dev->sem);

	/* return that we did not understand this ioctl call */
	return -ENOTTY;
}


/**
 *	skel_write_bulk_callback
 */
static void skel_write_bulk_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_skel *dev = (struct usb_skel *)urb->context;

	dbg("%s - minor %d", __FUNCTION__, dev->minor);

	/* sync/async unlink faults aren't errors */
	if (urb->status && !(urb->status == -ENOENT ||
				urb->status == -ECONNRESET)) {
		dbg("%s - nonzero write bulk status received: %d",
		    __FUNCTION__, urb->status);
	}

	/* notify anyone waiting that the write has finished */
	atomic_set (&dev->write_busy, 0);
	complete (&dev->write_finished);
}


/**
 *	skel_probe
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 */
static int skel_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_skel *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;

	/* See if the device offered us matches what we can accept */
	if ((udev->descriptor.idVendor != USB_SKEL_VENDOR_ID) ||
	    (udev->descriptor.idProduct != USB_SKEL_PRODUCT_ID)) {
		return -ENODEV;
	}

	/* allocate memory for our device state and initialize it */
	dev = kmalloc (sizeof(struct usb_skel), GFP_KERNEL);
	if (dev == NULL) {
		err ("Out of memory");
		return -ENOMEM;
	}
	memset (dev, 0x00, sizeof (*dev));

	init_MUTEX (&dev->sem);
	dev->udev = udev;
	dev->interface = interface;

	/* set up the endpoint information */
	/* check out the endpoints */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = &interface->altsetting[0];
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    (endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
					== USB_ENDPOINT_XFER_BULK)) {
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

		if (!dev->bulk_out_endpointAddr &&
		    !(endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
					== USB_ENDPOINT_XFER_BULK)) {
			/* we found a bulk out endpoint */
			/* a probe() may sleep and has no restrictions on memory allocations */
			dev->write_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!dev->write_urb) {
				err("No free urbs available");
				goto error;
			}
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;

			/* on some platforms using this kind of buffer alloc
			 * call eliminates a dma "bounce buffer".
			 *
			 * NOTE: you'd normally want i/o buffers that hold
			 * more than one packet, so that i/o delays between
			 * packets don't hurt throughput.
			 */
			buffer_size = endpoint->wMaxPacketSize;
			dev->bulk_out_size = buffer_size;
			dev->write_urb->transfer_flags = (URB_NO_TRANSFER_DMA_MAP |
					URB_ASYNC_UNLINK);
			dev->bulk_out_buffer = usb_buffer_alloc (udev,
					buffer_size, GFP_KERNEL,
					&dev->write_urb->transfer_dma);
			if (!dev->bulk_out_buffer) {
				err("Couldn't allocate bulk_out_buffer");
				goto error;
			}
			usb_fill_bulk_urb(dev->write_urb, udev,
				      usb_sndbulkpipe(udev,
						      endpoint->bEndpointAddress),
				      dev->bulk_out_buffer, buffer_size,
				      skel_write_bulk_callback, dev);
		}
	}
	if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
		err("Couldn't find both bulk-in and bulk-out endpoints");
		goto error;
	}

	/* allow device read, write and ioctl */
	dev->present = 1;

	/* we can register the device now, as it is ready */
	usb_set_intfdata (interface, dev);
	retval = usb_register_dev (interface, &skel_class);
	if (retval) {
		/* something prevented us from registering this driver */
		err ("Not able to get a minor for this device.");
		usb_set_intfdata (interface, NULL);
		goto error;
	}

	dev->minor = interface->minor;

	/* let the user know what node this device is now attached to */
	info ("USB Skeleton device now attached to USBSkel-%d", dev->minor);
	return 0;

error:
	skel_delete (dev);
	return retval;
}


/**
 *	skel_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 *
 *	This routine guarantees that the driver will not submit any more urbs
 *	by clearing dev->udev.  It is also supposed to terminate any currently
 *	active urbs.  Unfortunately, usb_bulk_msg(), used in skel_read(), does
 *	not provide any way to do this.  But at least we can cancel an active
 *	write.
 */
static void skel_disconnect(struct usb_interface *interface)
{
	struct usb_skel *dev;
	int minor;

	/* prevent races with open() */
	down (&disconnect_sem);

	dev = usb_get_intfdata (interface);
	usb_set_intfdata (interface, NULL);

	down (&dev->sem);

	minor = dev->minor;

	/* give back our minor */
	usb_deregister_dev (interface, &skel_class);

	/* terminate an ongoing write */
	if (atomic_read (&dev->write_busy)) {
		usb_unlink_urb (dev->write_urb);
		wait_for_completion (&dev->write_finished);
	}

	/* prevent device read, write and ioctl */
	dev->present = 0;

	up (&dev->sem);

	/* if the device is opened, skel_release will clean this up */
	if (!dev->open)
		skel_delete (dev);

	up (&disconnect_sem);

	info("USB Skeleton #%d now disconnected", minor);
}



/**
 *	usb_skel_init
 */
static int __init usb_skel_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&skel_driver);
	if (result) {
		err("usb_register failed. Error number %d",
		    result);
		return result;
	}

	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}


/**
 *	usb_skel_exit
 */
static void __exit usb_skel_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&skel_driver);
}


module_init (usb_skel_init);
module_exit (usb_skel_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
