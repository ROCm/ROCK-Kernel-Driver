/*
 * printer.c  Version 0.6
 *
 * Copyright (c) 1999 Michael Gee	<michael@linuxspecific.com>
 * Copyright (c) 1999 Pavel Machek	<pavel@suse.cz>
 * Copyright (c) 2000 Randy Dunlap	<randy.dunlap@intel.com>
 * Copyright (c) 2000 Vojtech Pavlik	<vojtech@suse.cz>
 *
 * USB Printer Device Class driver for USB printers and printer cables
 *
 * Sponsored by SuSE
 *
 * ChangeLog:
 *	v0.1 - thorough cleaning, URBification, almost a rewrite
 *	v0.2 - some more cleanups
 *	v0.3 - cleaner again, waitqueue fixes
 *	v0.4 - fixes in unidirectional mode
 *	v0.5 - add DEVICE_ID string support
 *	v0.6 - never time out
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/lp.h>
#undef DEBUG
#include <linux/usb.h>

#define USBLP_BUF_SIZE		8192
#define DEVICE_ID_SIZE		1024

#define IOCNR_GET_DEVICE_ID	1
#define LPIOC_GET_DEVICE_ID(len) _IOC(_IOC_READ, 'P', IOCNR_GET_DEVICE_ID, len)	/* get device_id string */
#define LPGETSTATUS		0x060b		/* same as in drivers/char/lp.c */

/*
 * A DEVICE_ID string may include the printer's serial number.
 * It should end with a semi-colon (';').
 * An example from an HP 970C DeskJet printer is (this is one long string,
 * with the serial number changed):
MFG:HEWLETT-PACKARD;MDL:DESKJET 970C;CMD:MLC,PCL,PML;CLASS:PRINTER;DESCRIPTION:Hewlett-Packard DeskJet 970C;SERN:US970CSEPROF;VSTATUS:$HB0$NC0,ff,DN,IDLE,CUT,K1,C0,DP,NR,KP000,CP027;VP:0800,FL,B0;VJ:                    ;
 */

/*
 * USB Printer Requests
 */

#define USBLP_REQ_GET_ID	0x00
#define USBLP_REQ_GET_STATUS	0x01
#define USBLP_REQ_RESET		0x02

#define USBLP_MINORS		16
#define USBLP_MINOR_BASE	0

#define USBLP_WRITE_TIMEOUT	(5*HZ)			/* 5 seconds */

struct usblp {
	struct usb_device 	*dev;			/* USB device */
	struct urb		readurb, writeurb;	/* The urbs */
	wait_queue_head_t	wait;			/* Zzzzz ... */
	int			readcount;		/* Counter for reads */
	int			ifnum;			/* Interface number */
	int			minor;			/* minor number of device */
	unsigned int		quirks;			/* quirks flags */
	unsigned char		used;			/* True if open */
	unsigned char		bidir;			/* interface is bidirectional */
	unsigned char		*device_id_string;	/* IEEE 1284 DEVICE ID string (ptr) */
							/* first 2 bytes are (big-endian) length */
};

static struct usblp *usblp_table[USBLP_MINORS];

/* Quirks: various printer quirks are handled by this table & its flags. */

struct quirk_printer_struct {
	__u16 vendorId;
	__u16 productId;
	unsigned int quirks;
};

#define USBLP_QUIRK_BIDIR	0x1	/* reports bidir but requires unidirectional mode (no INs/reads) */
#define USBLP_QUIRK_USB_INIT	0x2	/* needs vendor USB init string */

static struct quirk_printer_struct quirk_printers[] = {
	{ 0x03f0, 0x0004, USBLP_QUIRK_BIDIR }, /* HP DeskJet 895C */
	{ 0x03f0, 0x0104, USBLP_QUIRK_BIDIR }, /* HP DeskJet 880C */
	{ 0x03f0, 0x0204, USBLP_QUIRK_BIDIR }, /* HP DeskJet 815C */
	{ 0x03f0, 0x0304, USBLP_QUIRK_BIDIR }, /* HP DeskJet 810C/812C */
	{ 0x03f0, 0x0404, USBLP_QUIRK_BIDIR }, /* HP DeskJet 830C */
	{ 0, 0 }
};

/*
 * Functions for usblp control messages.
 */

static int usblp_ctrl_msg(struct usblp *usblp, int request, int dir, int recip, int value, void *buf, int len)
{
	int retval = usb_control_msg(usblp->dev,
		dir ? usb_rcvctrlpipe(usblp->dev, 0) : usb_sndctrlpipe(usblp->dev, 0),
		request, USB_TYPE_CLASS | dir | recip, value, usblp->ifnum, buf, len, HZ * 5);
	dbg("usblp_control_msg: rq: 0x%02x dir: %d recip: %d value: %d len: %#x result: %d",
		request, !!dir, recip, value, len, retval);
	return retval < 0 ? retval : 0;
}

#define usblp_read_status(usblp, status)\
	usblp_ctrl_msg(usblp, USBLP_REQ_GET_STATUS, USB_DIR_IN, USB_RECIP_INTERFACE, 0, status, 1)
#define usblp_get_id(usblp, config, id, maxlen)\
	usblp_ctrl_msg(usblp, USBLP_REQ_GET_ID, USB_DIR_IN, USB_RECIP_INTERFACE, config, id, maxlen)
#define usblp_reset(usblp)\
	usblp_ctrl_msg(usblp, USBLP_REQ_RESET, USB_DIR_OUT, USB_RECIP_OTHER, 0, NULL, 0)

/*
 * URB callback.
 */

static void usblp_bulk(struct urb *urb)
{
	struct usblp *usblp = urb->context;

	if (!usblp || !usblp->dev || !usblp->used)
		return;

	if (urb->status)
		warn("usblp%d: nonzero read/write bulk status received: %d",
			usblp->minor, urb->status);

	wake_up_interruptible(&usblp->wait);
}

/*
 * Get and print printer errors.
 */

static char *usblp_messages[] = { "ok", "out of paper", "off-line", "on fire" };

static int usblp_check_status(struct usblp *usblp, int err)
{
	unsigned char status, newerr = 0;

	if (usblp_read_status(usblp, &status)) {
		err("usblp%d: failed reading printer status", usblp->minor);
		return 0;
	}

	if (~status & LP_PERRORP) {
		newerr = 3;
		if (status & LP_POUTPA) newerr = 1;
		if (~status & LP_PSELECD) newerr = 2;
	}

	if (newerr != err)
		info("usblp%d: %s", usblp->minor, usblp_messages[newerr]);

	return newerr;
}

/*
 * File op functions.
 */

static int usblp_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev) - USBLP_MINOR_BASE;
	struct usblp *usblp;
	int retval;

	if (minor < 0 || minor >= USBLP_MINORS)
		return -ENODEV;

	lock_kernel();
	usblp  = usblp_table[minor];

	retval = -ENODEV;
	if (!usblp || !usblp->dev)
		goto out;

	retval = -EBUSY;
	if (usblp->used)
		goto out;

	/*
	 * TODO: need to implement LP_ABORTOPEN + O_NONBLOCK as in drivers/char/lp.c ???
	 * This is #if 0-ed because we *don't* want to fail an open
	 * just because the printer is off-line.
	 */
#if 0
	if ((retval = usblp_check_status(usblp, 0))) {
		retval = retval > 1 ? -EIO : -ENOSPC;
		goto out;
	}
#else
	retval = 0;	
#endif

	usblp->used = 1;
	file->private_data = usblp;

	usblp->writeurb.transfer_buffer_length = 0;
	usblp->writeurb.status = 0;

	if (usblp->bidir) {
		usblp->readcount = 0;
		usblp->readurb.dev = usblp->dev;
		usb_submit_urb(&usblp->readurb);
	}
out:
	unlock_kernel();
	return retval;
}

static int usblp_release(struct inode *inode, struct file *file)
{
	struct usblp *usblp = file->private_data;

	usblp->used = 0;

	if (usblp->dev) {
		if (usblp->bidir)
			usb_unlink_urb(&usblp->readurb);
		usb_unlink_urb(&usblp->writeurb);
		return 0;
	}

	usblp_table[usblp->minor] = NULL;
	kfree(usblp->device_id_string);
	kfree(usblp);

	return 0;
}

/* No kernel lock - fine */
static unsigned int usblp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct usblp *usblp = file->private_data;
	poll_wait(file, &usblp->wait, wait);
	return ((usblp->bidir || usblp->readurb.status  == -EINPROGRESS) ? 0 : POLLIN  | POLLRDNORM)
	     		      | (usblp->writeurb.status == -EINPROGRESS  ? 0 : POLLOUT | POLLWRNORM);
}

static int usblp_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usblp *usblp = file->private_data;
	int length, err;
	unsigned char status;

	if (_IOC_TYPE(cmd) == 'P')	/* new-style ioctl number */
	
		switch (_IOC_NR(cmd)) {

			case IOCNR_GET_DEVICE_ID: /* get the DEVICE_ID string */
				if (_IOC_DIR(cmd) != _IOC_READ)
					return -EINVAL;

				err = usblp_get_id(usblp, 0, usblp->device_id_string, DEVICE_ID_SIZE - 1);
				if (err < 0) {
					dbg ("usblp%d: error = %d reading IEEE-1284 Device ID string",
						usblp->minor, err);
					usblp->device_id_string[0] = usblp->device_id_string[1] = '\0';
					return -EIO;
				}

				length = (usblp->device_id_string[0] << 8) + usblp->device_id_string[1]; /* big-endian */
				if (length < DEVICE_ID_SIZE)
					usblp->device_id_string[length] = '\0';
				else
					usblp->device_id_string[DEVICE_ID_SIZE - 1] = '\0';

				dbg ("usblp%d Device ID string [%d/max %d]='%s'",
					usblp->minor, length, _IOC_SIZE(cmd), &usblp->device_id_string[2]);

				if (length > _IOC_SIZE(cmd)) length = _IOC_SIZE(cmd); /* truncate */

				if (copy_to_user((unsigned char *) arg, usblp->device_id_string, (unsigned long) length))
					return -EFAULT;

				break;

			default:
				return -EINVAL;
		}
	else	/* old-style ioctl value */
		switch (cmd) {

			case LPGETSTATUS:
				if (usblp_read_status(usblp, &status)) {
					err("usblp%d: failed reading printer status", usblp->minor);
					return -EIO;
				}
				if (copy_to_user ((unsigned char *)arg, &status, 1))
					return -EFAULT;
				break;

			default:
				return -EINVAL;
		}

	return 0;
}

static ssize_t usblp_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct usblp *usblp = file->private_data;
	int timeout, err = 0, writecount = 0;

	while (writecount < count) {

		if (usblp->writeurb.status == -EINPROGRESS) {

			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;

			timeout = USBLP_WRITE_TIMEOUT;
			while (timeout && usblp->writeurb.status == -EINPROGRESS) {

				if (signal_pending(current))
					return writecount ? writecount : -EINTR;

				timeout = interruptible_sleep_on_timeout(&usblp->wait, timeout);
			}
		}

		if (!usblp->dev)
			return -ENODEV;

		if (usblp->writeurb.status) {
			if (usblp->quirks & USBLP_QUIRK_BIDIR) {
				if (usblp->writeurb.status != -EINPROGRESS)
					err("usblp%d: error %d writing to printer",
						usblp->minor, usblp->writeurb.status);
				err = usblp->writeurb.status;
				continue;
			}
			else {
				err = usblp_check_status(usblp, err);
				continue;
			}
		}

		writecount += usblp->writeurb.transfer_buffer_length;
		usblp->writeurb.transfer_buffer_length = 0;

		if (writecount == count)
			continue;

		usblp->writeurb.transfer_buffer_length = (count - writecount) < USBLP_BUF_SIZE ?
							 (count - writecount) : USBLP_BUF_SIZE;

		if (copy_from_user(usblp->writeurb.transfer_buffer, buffer + writecount,
				usblp->writeurb.transfer_buffer_length)) return -EFAULT;

		usblp->writeurb.dev = usblp->dev;
		usb_submit_urb(&usblp->writeurb);
	}

	return count;
}

static ssize_t usblp_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct usblp *usblp = file->private_data;

	if (!usblp->bidir)
		return -EINVAL;

	if (usblp->readurb.status == -EINPROGRESS) {

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		while (usblp->readurb.status == -EINPROGRESS) {
			if (signal_pending(current))
				return -EINTR;
			interruptible_sleep_on(&usblp->wait);
		}
	}

	if (!usblp->dev)
		return -ENODEV;

	if (usblp->readurb.status) {
		err("usblp%d: error %d reading from printer",
			usblp->minor, usblp->readurb.status);
		usblp->readurb.dev = usblp->dev;
		usb_submit_urb(&usblp->readurb);
		return -EIO;
	}

	count = count < usblp->readurb.actual_length - usblp->readcount ?
		count :	usblp->readurb.actual_length - usblp->readcount;

	if (copy_to_user(buffer, usblp->readurb.transfer_buffer + usblp->readcount, count))
		return -EFAULT;

	if ((usblp->readcount += count) == usblp->readurb.actual_length) {
		usblp->readcount = 0;
		usblp->readurb.dev = usblp->dev;
		usb_submit_urb(&usblp->readurb);
	}

	return count;
}

/*
 * Checks for printers that have quirks, such as requiring unidirectional
 * communication but reporting bidirectional; currently some HP printers
 * have this flaw (HP 810, 880, 895, etc.), or needing an init string
 * sent at each open (like some Epsons).
 * Returns 1 if found, 0 if not found.
 *
 * HP recommended that we use the bidirectional interface but
 * don't attempt any bulk IN transfers from the IN endpoint.
 * Here's some more detail on the problem:
 * The problem is not that it isn't bidirectional though. The problem
 * is that if you request a device ID, or status information, while
 * the buffers are full, the return data will end up in the print data
 * buffer. For example if you make sure you never request the device ID
 * while you are sending print data, and you don't try to query the
 * printer status every couple of milliseconds, you will probably be OK.
 */
static unsigned int usblp_quirks (__u16 vendor, __u16 product)
{
	int i;

	for (i = 0; quirk_printers[i].vendorId; i++) {
		if (vendor == quirk_printers[i].vendorId &&
		    product == quirk_printers[i].productId)
			return quirk_printers[i].quirks;
 	}
	return 0;
}

static void *usblp_probe(struct usb_device *dev, unsigned int ifnum,
			 const struct usb_device_id *id)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *epread, *epwrite;
	struct usblp *usblp;
	int minor, i, bidir = 0, quirks;
	int alts = dev->actconfig->interface[ifnum].act_altsetting;
	int length, err;
	char *buf;

	/* If a bidirectional interface exists, use it. */
	for (i = 0; i < dev->actconfig->interface[ifnum].num_altsetting; i++) {

		interface = &dev->actconfig->interface[ifnum].altsetting[i];

		if (interface->bInterfaceClass != 7 || interface->bInterfaceSubClass != 1 ||
		    interface->bInterfaceProtocol < 1 || interface->bInterfaceProtocol > 3 ||
		   (interface->bInterfaceProtocol > 1 && interface->bNumEndpoints < 2))
			continue;

		if (interface->bInterfaceProtocol > 1) {
			bidir = 1;
			alts = i;
			break;
		}
	}

	interface = &dev->actconfig->interface[ifnum].altsetting[alts];
	if (usb_set_interface(dev, ifnum, alts))
		err("can't set desired altsetting %d on interface %d", alts, ifnum);

	epwrite = interface->endpoint + 0;
	epread = bidir ? interface->endpoint + 1 : NULL;

	if ((epwrite->bEndpointAddress & 0x80) == 0x80) {
		if (interface->bNumEndpoints == 1)
			return NULL;
		epwrite = interface->endpoint + 1;
		epread = bidir ? interface->endpoint + 0 : NULL;
	}

	if ((epwrite->bEndpointAddress & 0x80) == 0x80)
		return NULL;

	if (bidir && (epread->bEndpointAddress & 0x80) != 0x80)
		return NULL;

	for (minor = 0; minor < USBLP_MINORS && usblp_table[minor]; minor++);
	if (usblp_table[minor]) {
		err("no more free usblp devices");
		return NULL;
	}

	if (!(usblp = kmalloc(sizeof(struct usblp), GFP_KERNEL))) {
		err("out of memory");
		return NULL;
	}
	memset(usblp, 0, sizeof(struct usblp));

	/* lookup quirks for this printer */
	quirks = usblp_quirks(dev->descriptor.idVendor, dev->descriptor.idProduct);

	if (bidir && (quirks & USBLP_QUIRK_BIDIR)) {
		bidir = 0;
		epread = NULL;
		info ("Disabling reads from problem bidirectional printer on usblp%d",
			minor);
	}

	usblp->dev = dev;
	usblp->ifnum = ifnum;
	usblp->minor = minor;
	usblp->bidir = bidir;
	usblp->quirks = quirks;

	init_waitqueue_head(&usblp->wait);

	if (!(buf = kmalloc(USBLP_BUF_SIZE * (bidir ? 2 : 1), GFP_KERNEL))) {
		err("out of memory");
		kfree(usblp);
		return NULL;
	}

	if (!(usblp->device_id_string = kmalloc(DEVICE_ID_SIZE, GFP_KERNEL))) {
		err("out of memory");
		kfree(usblp);
		kfree(buf);
		return NULL;
	}

	FILL_BULK_URB(&usblp->writeurb, dev, usb_sndbulkpipe(dev, epwrite->bEndpointAddress),
		buf, 0, usblp_bulk, usblp);

	if (bidir)
		FILL_BULK_URB(&usblp->readurb, dev, usb_rcvbulkpipe(dev, epread->bEndpointAddress),
			buf + USBLP_BUF_SIZE, USBLP_BUF_SIZE, usblp_bulk, usblp);

	/* Get the device_id string if possible. FIXME: Could make this kmalloc(length). */
	err = usblp_get_id(usblp, 0, usblp->device_id_string, DEVICE_ID_SIZE - 1);
	if (err >= 0) {
		length = (usblp->device_id_string[0] << 8) + usblp->device_id_string[1]; /* big-endian */
		if (length < DEVICE_ID_SIZE)
			usblp->device_id_string[length] = '\0';
		else
			usblp->device_id_string[DEVICE_ID_SIZE - 1] = '\0';
		dbg ("usblp%d Device ID string [%d]=%s",
			minor, length, &usblp->device_id_string[2]);
	}
	else {
		err ("usblp%d: error = %d reading IEEE-1284 Device ID string",
			minor, err);
		usblp->device_id_string[0] = usblp->device_id_string[1] = '\0';
	}

#ifdef DEBUG
	usblp_check_status(usblp, 0);
#endif

	info("usblp%d: USB %sdirectional printer dev %d if %d alt %d",
		minor, bidir ? "Bi" : "Uni", dev->devnum, ifnum, alts);

	return usblp_table[minor] = usblp;
}

static void usblp_disconnect(struct usb_device *dev, void *ptr)
{
	struct usblp *usblp = ptr;

	if (!usblp || !usblp->dev) {
		err("disconnect on nonexisting interface");
		return;
	}

	usblp->dev = NULL;

	usb_unlink_urb(&usblp->writeurb);
	if (usblp->bidir)
		usb_unlink_urb(&usblp->readurb);

	kfree(usblp->writeurb.transfer_buffer);

	if (usblp->used) return;

	kfree(usblp->device_id_string);

	usblp_table[usblp->minor] = NULL;
	kfree(usblp);
}

static struct file_operations usblp_fops = {
	owner:		THIS_MODULE,
	read:		usblp_read,
	write:		usblp_write,
	poll:		usblp_poll,
	ioctl:		usblp_ioctl,
	open:		usblp_open,
	release:	usblp_release,
};

static struct usb_device_id usblp_ids [] = {
	{ USB_INTERFACE_INFO(7, 1, 1) },
	{ USB_INTERFACE_INFO(7, 1, 2) },
	{ USB_INTERFACE_INFO(7, 1, 3) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usblp_ids);

static struct usb_driver usblp_driver = {
	name:		"usblp",
	probe:		usblp_probe,
	disconnect:	usblp_disconnect,
	fops:		&usblp_fops,
	minor:		USBLP_MINOR_BASE,
	id_table:	usblp_ids,
};

static int __init usblp_init(void)
{
	if (usb_register(&usblp_driver))
		return -1;
	return 0;
}

static void __exit usblp_exit(void)
{
	usb_deregister(&usblp_driver);
}

module_init(usblp_init);
module_exit(usblp_exit);

MODULE_AUTHOR("Michael Gee, Pavel Machek, Vojtech Pavlik, Randy Dunlap");
MODULE_DESCRIPTION("USB Printer Device Class driver");
