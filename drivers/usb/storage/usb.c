/* Driver for USB Mass Storage compliant devices
 *
 * $Id: usb.c,v 1.75 2002/04/22 03:39:43 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * usb_device_id support by Adam J. Richter (adam@yggdrasil.com):
 *   (c) 2000 Yggdrasil Computing, Inc.
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include "usb.h"
#include "scsiglue.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#include "initializers.h"

#ifdef CONFIG_USB_STORAGE_HP8200e
#include "shuttle_usbat.h"
#endif
#ifdef CONFIG_USB_STORAGE_SDDR09
#include "sddr09.h"
#endif
#ifdef CONFIG_USB_STORAGE_SDDR55
#include "sddr55.h"
#endif
#ifdef CONFIG_USB_STORAGE_DPCM
#include "dpcm.h"
#endif
#ifdef CONFIG_USB_STORAGE_FREECOM
#include "freecom.h"
#endif
#ifdef CONFIG_USB_STORAGE_ISD200
#include "isd200.h"
#endif
#ifdef CONFIG_USB_STORAGE_DATAFAB
#include "datafab.h"
#endif
#ifdef CONFIG_USB_STORAGE_JUMPSHOT
#include "jumpshot.h"
#endif


#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>

/* Some informational data */
MODULE_AUTHOR("Matthew Dharm <mdharm-usb@one-eyed-alien.net>");
MODULE_DESCRIPTION("USB Mass Storage driver for Linux");
MODULE_LICENSE("GPL");

static int storage_probe(struct usb_interface *iface,
			 const struct usb_device_id *id);

static void storage_disconnect(struct usb_interface *iface);

/* The entries in this table, except for final ones here
 * (USB_MASS_STORAGE_CLASS and the empty entry), correspond,
 * line for line with the entries of us_unsuaul_dev_list[].
 * For now, we duplicate idVendor and idProduct in us_unsual_dev_list,
 * just to avoid alignment bugs.
 */

#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName,useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin,bcdDeviceMax) }

static struct usb_device_id storage_usb_ids [] = {

#	include "unusual_devs.h"
#undef UNUSUAL_DEV
	/* Control/Bulk transport for all SubClass values */
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_RBC, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8020, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_QIC, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_UFI, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8070, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_SCSI, US_PR_CB) },

	/* Control/Bulk/Interrupt transport for all SubClass values */
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_RBC, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8020, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_QIC, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_UFI, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8070, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_SCSI, US_PR_CBI) },

	/* Bulk-only transport for all SubClass values */
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_RBC, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8020, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_QIC, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_UFI, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8070, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_SCSI, US_PR_BULK) },

	/* Terminating entry */
	{ }
};

MODULE_DEVICE_TABLE (usb, storage_usb_ids);

/* This is the list of devices we recognize, along with their flag data */

/* The vendor name should be kept at eight characters or less, and
 * the product name should be kept at 16 characters or less. If a device
 * has the US_FL_FIX_INQUIRY flag, then the vendor and product names
 * normally generated by a device thorugh the INQUIRY response will be
 * taken from this list, and this is the reason for the above size
 * restriction. However, if the flag is not present, then you
 * are free to use as many characters as you like.
 */

#undef UNUSUAL_DEV
#define UNUSUAL_DEV(idVendor, idProduct, bcdDeviceMin, bcdDeviceMax, \
		    vendor_name, product_name, use_protocol, use_transport, \
		    init_function, Flags) \
{ \
	.vendorName = vendor_name,	\
	.productName = product_name,	\
	.useProtocol = use_protocol,	\
	.useTransport = use_transport,	\
	.initFunction = init_function,	\
	.flags = Flags, \
}

static struct us_unusual_dev us_unusual_dev_list[] = {
#	include "unusual_devs.h" 
#	undef UNUSUAL_DEV
	/* Control/Bulk transport for all SubClass values */
	{ .useProtocol = US_SC_RBC,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_8020,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_QIC,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_UFI,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_8070,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_SCSI,
	  .useTransport = US_PR_CB},

	/* Control/Bulk/Interrupt transport for all SubClass values */
	{ .useProtocol = US_SC_RBC,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_8020,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_QIC,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_UFI,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_8070,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_SCSI,
	  .useTransport = US_PR_CBI},

	/* Bulk-only transport for all SubClass values */
	{ .useProtocol = US_SC_RBC,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_8020,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_QIC,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_UFI,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_8070,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_SCSI,
	  .useTransport = US_PR_BULK},

	/* Terminating entry */
	{ 0 }
};

struct usb_driver usb_storage_driver = {
	.owner =	THIS_MODULE,
	.name =		"usb-storage",
	.probe =	storage_probe,
	.disconnect =	storage_disconnect,
	.id_table =	storage_usb_ids,
};

/*
 * fill_inquiry_response takes an unsigned char array (which must
 * be at least 36 characters) and populates the vendor name,
 * product name, and revision fields. Then the array is copied
 * into the SCSI command's response buffer (oddly enough
 * called request_buffer). data_len contains the length of the
 * data array, which again must be at least 36.
 */

void fill_inquiry_response(struct us_data *us, unsigned char *data,
		unsigned int data_len) {

	int i;
	struct scatterlist *sg;
	int len =
		us->srb->request_bufflen > data_len ? data_len :
		us->srb->request_bufflen;
	int transferred;
	int amt;

	if (data_len<36) // You lose.
		return;

	if(data[0]&0x20) { /* USB device currently not connected. Return
			      peripheral qualifier 001b ("...however, the
			      physical device is not currently connected
			      to this logical unit") and leave vendor and
			      product identification empty. ("If the target
			      does store some of the INQUIRY data on the
			      device, it may return zeros or ASCII spaces 
			      (20h) in those fields until the data is
			      available from the device."). */
		memset(data+8,0,28);
	} else {
		memcpy(data+8, us->unusual_dev->vendorName, 
			strlen(us->unusual_dev->vendorName) > 8 ? 8 :
			strlen(us->unusual_dev->vendorName));
		memcpy(data+16, us->unusual_dev->productName, 
			strlen(us->unusual_dev->productName) > 16 ? 16 :
			strlen(us->unusual_dev->productName));
		data[32] = 0x30 + ((us->pusb_dev->descriptor.bcdDevice>>12) & 0x0F);
		data[33] = 0x30 + ((us->pusb_dev->descriptor.bcdDevice>>8) & 0x0F);
		data[34] = 0x30 + ((us->pusb_dev->descriptor.bcdDevice>>4) & 0x0F);
		data[35] = 0x30 + ((us->pusb_dev->descriptor.bcdDevice) & 0x0F);
	}

	if (us->srb->use_sg) {
		sg = (struct scatterlist *)us->srb->request_buffer;
		for (i=0; i<us->srb->use_sg; i++)
			memset(sg_address(sg[i]), 0, sg[i].length);
		for (i=0, transferred=0; 
				i<us->srb->use_sg && transferred < len;
				i++) {
			amt = sg[i].length > len-transferred ? 
					len-transferred : sg[i].length;
			memcpy(sg_address(sg[i]), data+transferred, amt);
			transferred -= amt;
		}
	} else {
		memset(us->srb->request_buffer, 0, us->srb->request_bufflen);
		memcpy(us->srb->request_buffer, data, len);
	}
}

static int usb_stor_control_thread(void * __us)
{
	struct us_data *us = (struct us_data *)__us;

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources..
	 */
	daemonize("usb-storage");

	current->flags |= PF_IOTHREAD;

	unlock_kernel();

	/* set up for wakeups by new commands */
	init_MUTEX_LOCKED(&us->sema);

	/* signal that we've started the thread */
	complete(&(us->notify));

	for(;;) {
		struct Scsi_Host *host;
		US_DEBUGP("*** thread sleeping.\n");
		if(down_interruptible(&us->sema))
			break;
			
		US_DEBUGP("*** thread awakened.\n");

		/* if us->srb is NULL, we are being asked to exit */
		if (us->srb == NULL) {
			US_DEBUGP("-- exit command received\n");
			break;
		}
		host = us->srb->device->host;

		/* lock access to the state */
		scsi_lock(host);

		/* has the command been aborted *already* ? */
		if (atomic_read(&us->sm_state) == US_STATE_ABORTING) {
			us->srb->result = DID_ABORT << 16;
			goto SkipForAbort;
		}

		/* set the state and release the lock */
		atomic_set(&us->sm_state, US_STATE_RUNNING);
		scsi_unlock(host);

		/* lock the device pointers */
		down(&(us->dev_semaphore));

		/* reject the command if the direction indicator 
		 * is UNKNOWN
		 */
		if (us->srb->sc_data_direction == SCSI_DATA_UNKNOWN) {
			US_DEBUGP("UNKNOWN data direction\n");
			us->srb->result = DID_ERROR << 16;
		}

		/* reject if target != 0 or if LUN is higher than
		 * the maximum known LUN
		 */
		else if (us->srb->device->id && 
				!(us->flags & US_FL_SCM_MULT_TARG)) {
			US_DEBUGP("Bad target number (%d/%d)\n",
				  us->srb->device->id, us->srb->device->lun);
			us->srb->result = DID_BAD_TARGET << 16;
		}

		else if (us->srb->device->lun > us->max_lun) {
			US_DEBUGP("Bad LUN (%d:%d)\n",
				  us->srb->device->id, us->srb->device->lun);
			us->srb->result = DID_BAD_TARGET << 16;
		}

		/* handle requests for EVPD, which most USB devices do
		 * not support */
		else if((us->srb->cmnd[0] == INQUIRY) &&
				(us->srb->cmnd[1] & 0x1)) {
				US_DEBUGP("Faking INQUIRY command for EVPD\n");
				memcpy(us->srb->sense_buffer, 
				       usb_stor_sense_invalidCDB, 
				       sizeof(usb_stor_sense_invalidCDB));
				us->srb->result = SAM_STAT_CHECK_CONDITION;
		}

		/* Handle those devices which need us to fake 
		 * their inquiry data */
		else if ((us->srb->cmnd[0] == INQUIRY) &&
			    (us->flags & US_FL_FIX_INQUIRY)) {
			unsigned char data_ptr[36] = {
			    0x00, 0x80, 0x02, 0x02,
			    0x1F, 0x00, 0x00, 0x00};

			US_DEBUGP("Faking INQUIRY command\n");
			fill_inquiry_response(us, data_ptr, 36);
			us->srb->result = SAM_STAT_GOOD;
		}

		/* we've got a command, let's do it! */
		else {
			US_DEBUG(usb_stor_show_command(us->srb));
			us->proto_handler(us->srb, us);
		}

		/* unlock the device pointers */
		up(&(us->dev_semaphore));

		/* lock access to the state */
		scsi_lock(host);

		/* indicate that the command is done */
		if (us->srb->result != DID_ABORT << 16) {
			US_DEBUGP("scsi cmd done, result=0x%x\n", 
				   us->srb->result);
			us->srb->scsi_done(us->srb);
		} else {
			SkipForAbort:
			US_DEBUGP("scsi command aborted\n");
		}

		/* If an abort request was received we need to signal that
		 * the abort has finished.  The proper test for this is
		 * sm_state == US_STATE_ABORTING, not srb->result == DID_ABORT,
		 * because an abort request might be received after all the
		 * USB processing was complete. */
		if (atomic_read(&us->sm_state) == US_STATE_ABORTING)
			complete(&(us->notify));

		/* empty the queue, reset the state, and release the lock */
		us->srb = NULL;
		atomic_set(&us->sm_state, US_STATE_IDLE);
		scsi_unlock(host);
	} /* for (;;) */

	/* notify the exit routine that we're actually exiting now */
	complete(&(us->notify));

	return 0;
}	

/* Set up the URB and the usb_ctrlrequest.
 * us->dev_semaphore must already be locked.
 * Note that this function assumes that all the data in the us_data
 * structure is current.
 * Returns non-zero on failure, zero on success
 */ 
static int usb_stor_allocate_urbs(struct us_data *us)
{
	/* calculate and store the pipe values */
	us->send_ctrl_pipe = usb_sndctrlpipe(us->pusb_dev, 0);
	us->recv_ctrl_pipe = usb_rcvctrlpipe(us->pusb_dev, 0);
	us->send_bulk_pipe = usb_sndbulkpipe(us->pusb_dev, us->ep_out);
	us->recv_bulk_pipe = usb_rcvbulkpipe(us->pusb_dev, us->ep_in);
	us->recv_intr_pipe = usb_rcvintpipe(us->pusb_dev, us->ep_int);

	/* allocate the usb_ctrlrequest for control packets */
	US_DEBUGP("Allocating usb_ctrlrequest\n");
	us->dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_NOIO);
	if (!us->dr) {
		US_DEBUGP("allocation failed\n");
		return 1;
	}

	/* allocate the URB we're going to use */
	US_DEBUGP("Allocating URB\n");
	us->current_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!us->current_urb) {
		US_DEBUGP("allocation failed\n");
		return 2;
	}

	US_DEBUGP("Allocating scatter-gather request block\n");
	us->current_sg = kmalloc(sizeof(*us->current_sg), GFP_KERNEL);
	if (!us->current_sg) {
		US_DEBUGP("allocation failed\n");
		return 5;
	}

	return 0;	/* success */
}

/* Deallocate the URB, the usb_ctrlrequest, and the IRQ pipe.
 * us->dev_semaphore must already be locked.
 */
static void usb_stor_deallocate_urbs(struct us_data *us)
{
	/* free the scatter-gather request block */
	if (us->current_sg) {
		kfree(us->current_sg);
		us->current_sg = NULL;
	}

	/* free up the main URB for this device */
	if (us->current_urb) {
		US_DEBUGP("-- releasing main URB\n");
		usb_free_urb(us->current_urb);
		us->current_urb = NULL;
	}

	/* free the usb_ctrlrequest buffer */
	if (us->dr) {
		kfree(us->dr);
		us->dr = NULL;
	}

	/* mark the device as gone */
	usb_put_dev(us->pusb_dev);
	us->pusb_dev = NULL;
}

/* Probe to see if a new device is actually a SCSI device */
static int storage_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int ifnum = intf->altsetting->desc.bInterfaceNumber;
	int i;
	const int id_index = id - storage_usb_ids; 
	char mf[USB_STOR_STRING_LEN];		     /* manufacturer */
	char prod[USB_STOR_STRING_LEN];		     /* product */
	char serial[USB_STOR_STRING_LEN];	     /* serial number */
	unsigned int flags;
	struct us_unusual_dev *unusual_dev;
	struct us_data *us = NULL;
	int result;

	/* these are temporary copies -- we test on these, then put them
	 * in the us-data structure 
	 */
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct usb_endpoint_descriptor *ep_int = NULL;
	u8 subclass = 0;
	u8 protocol = 0;

	/* the altsetting on the interface we're probing that matched our
	 * usb_match_id table
	 */
	struct usb_host_interface *altsetting =
		intf[ifnum].altsetting + intf[ifnum].act_altsetting;
	US_DEBUGP("act_altsetting is %d\n", intf[ifnum].act_altsetting);

	/* clear the temporary strings */
	memset(mf, 0, sizeof(mf));
	memset(prod, 0, sizeof(prod));
	memset(serial, 0, sizeof(serial));

	/* 
	 * Can we support this device, either because we know about it
	 * from our unusual device list, or because it advertises that it's
	 * compliant to the specification?
	 *
	 * id_index is calculated in the declaration to be the index number
	 * of the match from the usb_device_id table, so we can find the
	 * corresponding entry in the private table.
	 */
	US_DEBUGP("id_index calculated to be: %d\n", id_index);
	US_DEBUGP("Array length appears to be: %d\n", sizeof(us_unusual_dev_list) / sizeof(us_unusual_dev_list[0]));
	if (id_index <
	    sizeof(us_unusual_dev_list) / sizeof(us_unusual_dev_list[0])) {
		unusual_dev = &us_unusual_dev_list[id_index];
		if (unusual_dev->vendorName)
			US_DEBUGP("Vendor: %s\n", unusual_dev->vendorName);
		if (unusual_dev->productName)
			US_DEBUGP("Product: %s\n", unusual_dev->productName);
	} else
		/* no, we can't support it */
		return -EIO;

	/* At this point, we know we've got a live one */
	US_DEBUGP("USB Mass Storage device detected\n");

	/* Determine subclass and protocol, or copy from the interface */
	subclass = unusual_dev->useProtocol;
	protocol = unusual_dev->useTransport;
	flags = unusual_dev->flags;

	/*
	 * Find the endpoints we need
	 * We are expecting a minimum of 2 endpoints - in and out (bulk).
	 * An optional interrupt is OK (necessary for CBI protocol).
	 * We will ignore any others.
	 */
	for (i = 0; i < altsetting->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep;

		ep = &altsetting->endpoint[i].desc;

		/* is it an BULK endpoint? */
		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_BULK) {
			/* BULK in or out? */
			if (ep->bEndpointAddress & USB_DIR_IN)
				ep_in = ep;
			else
				ep_out = ep;
		}

		/* is it an interrupt endpoint? */
		else if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_INT) {
			ep_int = ep;
		}
	}
	US_DEBUGP("Endpoints: In: 0x%p Out: 0x%p Int: 0x%p (Period %d)\n",
		  ep_in, ep_out, ep_int, ep_int ? ep_int->bInterval : 0);

#ifdef CONFIG_USB_STORAGE_SDDR09
	if (protocol == US_PR_EUSB_SDDR09 || protocol == US_PR_DPCM_USB) {
		/* set the configuration -- STALL is an acceptable response here */
		result = usb_set_configuration(dev, 1);

		US_DEBUGP("Result from usb_set_configuration is %d\n", result);
		if (result == -EPIPE) {
			US_DEBUGP("-- stall on control interface\n");
		} else if (result != 0) {
			/* it's not a stall, but another error -- time to bail */
			US_DEBUGP("-- Unknown error.  Rejecting device\n");
			return -EIO;
		}
	}
#endif

	/* Do some basic sanity checks, and bail if we find a problem */
	if (!ep_in || !ep_out || (protocol == US_PR_CBI && !ep_int)) {
		US_DEBUGP("Endpoint sanity check failed! Rejecting dev.\n");
		return -EIO;
	}

	/* At this point, we've decided to try to use the device */
	usb_get_dev(dev);

	/* fetch the strings */
	if (dev->descriptor.iManufacturer)
		usb_string(dev, dev->descriptor.iManufacturer, 
			   mf, sizeof(mf));
	if (dev->descriptor.iProduct)
		usb_string(dev, dev->descriptor.iProduct, 
			   prod, sizeof(prod));
	if (dev->descriptor.iSerialNumber && !(flags & US_FL_IGNORE_SER))
		usb_string(dev, dev->descriptor.iSerialNumber, 
			   serial, sizeof(serial));

	/* New device -- allocate memory and initialize */
	if ((us = (struct us_data *)kmalloc(sizeof(struct us_data), 
					    GFP_KERNEL)) == NULL) {
		printk(KERN_WARNING USB_STORAGE "Out of memory\n");
		usb_put_dev(dev);
		return -ENOMEM;
	}
	memset(us, 0, sizeof(struct us_data));

	/* Initialize the mutexes only when the struct is new */
	init_completion(&(us->notify));
	init_MUTEX_LOCKED(&(us->dev_semaphore));

	/* copy over the subclass and protocol data */
	us->subclass = subclass;
	us->protocol = protocol;
	us->flags = flags;
	us->unusual_dev = unusual_dev;

	/* copy over the endpoint data */
	us->ep_in = ep_in->bEndpointAddress & 
		USB_ENDPOINT_NUMBER_MASK;
	us->ep_out = ep_out->bEndpointAddress & 
		USB_ENDPOINT_NUMBER_MASK;
	if (ep_int) {
		us->ep_int = ep_int->bEndpointAddress & 
			USB_ENDPOINT_NUMBER_MASK;
		us->ep_bInterval = ep_int->bInterval;
	}
	else
		us->ep_int = us->ep_bInterval = 0;

	/* establish the connection to the new device */
	us->ifnum = ifnum;
	us->pusb_dev = dev;

	/* copy over the identifiying strings */
	strncpy(us->vendor, mf, USB_STOR_STRING_LEN);
	strncpy(us->product, prod, USB_STOR_STRING_LEN);
	strncpy(us->serial, serial, USB_STOR_STRING_LEN);
	if (strlen(us->vendor) == 0) {
		if (unusual_dev->vendorName)
			strncpy(us->vendor, unusual_dev->vendorName,
				USB_STOR_STRING_LEN);
		else
			strncpy(us->vendor, "Unknown",
				USB_STOR_STRING_LEN);
	}
	if (strlen(us->product) == 0) {
		if (unusual_dev->productName)
			strncpy(us->product, unusual_dev->productName,
				USB_STOR_STRING_LEN);
		else
			strncpy(us->product, "Unknown",
				USB_STOR_STRING_LEN);
	}
	if (strlen(us->serial) == 0)
		strncpy(us->serial, "None", USB_STOR_STRING_LEN);

	/* 
	 * Set the handler pointers based on the protocol
	 * Again, this data is persistent across reattachments
	 */
	switch (us->protocol) {
	case US_PR_CB:
		us->transport_name = "Control/Bulk";
		us->transport = usb_stor_CB_transport;
		us->transport_reset = usb_stor_CB_reset;
		us->max_lun = 7;
		break;

	case US_PR_CBI:
		us->transport_name = "Control/Bulk/Interrupt";
		us->transport = usb_stor_CBI_transport;
		us->transport_reset = usb_stor_CB_reset;
		us->max_lun = 7;
		break;

	case US_PR_BULK:
		us->transport_name = "Bulk";
		us->transport = usb_stor_Bulk_transport;
		us->transport_reset = usb_stor_Bulk_reset;
		break;

#ifdef CONFIG_USB_STORAGE_HP8200e
	case US_PR_SCM_ATAPI:
		us->transport_name = "SCM/ATAPI";
		us->transport = hp8200e_transport;
		us->transport_reset = usb_stor_CB_reset;
		us->max_lun = 1;
		break;
#endif

#ifdef CONFIG_USB_STORAGE_SDDR09
	case US_PR_EUSB_SDDR09:
		us->transport_name = "EUSB/SDDR09";
		us->transport = sddr09_transport;
		us->transport_reset = usb_stor_CB_reset;
		us->max_lun = 0;
		break;
#endif

#ifdef CONFIG_USB_STORAGE_SDDR55
	case US_PR_SDDR55:
		us->transport_name = "SDDR55";
		us->transport = sddr55_transport;
		us->transport_reset = sddr55_reset;
		us->max_lun = 0;
		break;
#endif

#ifdef CONFIG_USB_STORAGE_DPCM
	case US_PR_DPCM_USB:
		us->transport_name = "Control/Bulk-EUSB/SDDR09";
		us->transport = dpcm_transport;
		us->transport_reset = usb_stor_CB_reset;
		us->max_lun = 1;
		break;
#endif

#ifdef CONFIG_USB_STORAGE_FREECOM
	case US_PR_FREECOM:
		us->transport_name = "Freecom";
		us->transport = freecom_transport;
		us->transport_reset = usb_stor_freecom_reset;
		us->max_lun = 0;
		break;
#endif

#ifdef CONFIG_USB_STORAGE_DATAFAB
	case US_PR_DATAFAB:
		us->transport_name  = "Datafab Bulk-Only";
		us->transport = datafab_transport;
		us->transport_reset = usb_stor_Bulk_reset;
		us->max_lun = 1;
		break;
#endif

#ifdef CONFIG_USB_STORAGE_JUMPSHOT
	case US_PR_JUMPSHOT:
		us->transport_name  = "Lexar Jumpshot Control/Bulk";
		us->transport = jumpshot_transport;
		us->transport_reset = usb_stor_Bulk_reset;
		us->max_lun = 1;
		break;
#endif

	default:
		/* us->transport_name = "Unknown"; */
		goto BadDevice;
	}
	US_DEBUGP("Transport: %s\n", us->transport_name);

	/* fix for single-lun devices */
	if (us->flags & US_FL_SINGLE_LUN)
		us->max_lun = 0;

	switch (us->subclass) {
	case US_SC_RBC:
		us->protocol_name = "Reduced Block Commands (RBC)";
		us->proto_handler = usb_stor_transparent_scsi_command;
		break;

	case US_SC_8020:
		us->protocol_name = "8020i";
		us->proto_handler = usb_stor_ATAPI_command;
		us->max_lun = 0;
		break;

	case US_SC_QIC:
		us->protocol_name = "QIC-157";
		us->proto_handler = usb_stor_qic157_command;
		us->max_lun = 0;
		break;

	case US_SC_8070:
		us->protocol_name = "8070i";
		us->proto_handler = usb_stor_ATAPI_command;
		us->max_lun = 0;
		break;

	case US_SC_SCSI:
		us->protocol_name = "Transparent SCSI";
		us->proto_handler = usb_stor_transparent_scsi_command;
		break;

	case US_SC_UFI:
		us->protocol_name = "Uniform Floppy Interface (UFI)";
		us->proto_handler = usb_stor_ufi_command;
		break;

#ifdef CONFIG_USB_STORAGE_ISD200
	case US_SC_ISD200:
		us->protocol_name = "ISD200 ATA/ATAPI";
		us->proto_handler = isd200_ata_command;
		break;
#endif

	default:
		/* us->protocol_name = "Unknown"; */
		goto BadDevice;
	}
	US_DEBUGP("Protocol: %s\n", us->protocol_name);

	/* allocate the URB, the usb_ctrlrequest, and the IRQ URB */
	if (usb_stor_allocate_urbs(us))
		goto BadDevice;

	/* For bulk-only devices, determine the max LUN value */
	if (us->protocol == US_PR_BULK)
		us->max_lun = usb_stor_Bulk_max_lun(us);

	/*
	 * Since this is a new device, we need to generate a scsi 
	 * host definition, and register with the higher SCSI layers
	 */

	/* Just before we start our control thread, initialize
	 * the device if it needs initialization */
	if (unusual_dev && unusual_dev->initFunction)
		unusual_dev->initFunction(us);

	/* start up our control thread */
	atomic_set(&us->sm_state, US_STATE_IDLE);
	us->pid = kernel_thread(usb_stor_control_thread, us,
				CLONE_VM);
	if (us->pid < 0) {
		printk(KERN_WARNING USB_STORAGE 
		       "Unable to start control thread\n");
		goto BadDevice;
	}

	/* wait for the thread to start */
	wait_for_completion(&(us->notify));

	/* unlock the device pointers */
	up(&(us->dev_semaphore));

	/* now register	*/
	us->host = scsi_host_alloc(&usb_stor_host_template, sizeof(us));
	if (!us->host) {
		printk(KERN_WARNING USB_STORAGE
			"Unable to register the scsi host\n");

		/* tell the control thread to exit */
		us->srb = NULL;
		up(&us->sema);
		wait_for_completion(&us->notify);

		/* re-lock the device pointers */
		down(&us->dev_semaphore);
		goto BadDevice;
	}

	/* set the hostdata to prepare for scanning */
	us->host->hostdata[0] = (unsigned long)us;

	/* now add the host */
	result = scsi_add_host(us->host, &intf->dev);
	if (result) {
		printk(KERN_WARNING USB_STORAGE
			"Unable to add the scsi host\n");

		/* tell the control thread to exit */
		us->srb = NULL;
		up(&us->sema);
		wait_for_completion(&us->notify);

		/* re-lock the device pointers */
		down(&us->dev_semaphore);
		goto BadDevice;
	}

	printk(KERN_DEBUG 
	       "WARNING: USB Mass Storage data integrity not assured\n");
	printk(KERN_DEBUG 
	       "USB Mass Storage device found at %d\n", dev->devnum);

	/* save a pointer to our structure */
	usb_set_intfdata(intf, us);
	return 0;

	/* we come here if there are any problems */
	/* us->dev_semaphore must be locked */
BadDevice:
	US_DEBUGP("storage_probe() failed\n");
	usb_stor_deallocate_urbs(us);
	up(&us->dev_semaphore);
	kfree(us);
	return -EIO;
}

/* Handle a disconnect event from the USB core */
static void storage_disconnect(struct usb_interface *intf)
{
	struct us_data *us;
	struct scsi_device *sdev;

	US_DEBUGP("storage_disconnect() called\n");

	us = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);

	/* set devices offline -- need host lock for this */
	scsi_lock(us->host);
	list_for_each_entry(sdev, &us->host->my_devices, siblings)
		sdev->online = 0;
	scsi_unlock(us->host);

	/* prevent new USB transfers and stop the current command */
	set_bit(US_FLIDX_DISCONNECTING, &us->flags);
	usb_stor_stop_transport(us);

	/* lock device access -- no need to unlock, as we're going away */
	down(&(us->dev_semaphore));

	/* TODO: somehow, wait for the device to
	 * be 'idle' (tasklet completion) */

	/* remove the pointer to the data structure we were using */
	(struct us_data*)us->host->hostdata[0] = NULL;

	/* begin SCSI host removal sequence */
	if(scsi_remove_host(us->host)) {
		US_DEBUGP("-- SCSI refused to unregister\n");
		BUG();
		return;
	};

	/* finish SCSI host removal sequence */
	scsi_host_put(us->host);

	/* Kill the control threads
	 *
	 * Enqueue the command, wake up the thread, and wait for 
	 * notification that it has exited.
	 */
	US_DEBUGP("-- sending exit command to thread\n");
	BUG_ON(atomic_read(&us->sm_state) != US_STATE_IDLE);
	us->srb = NULL;
	up(&(us->sema));
	wait_for_completion(&(us->notify));

	/* free allocated urbs */
	usb_stor_deallocate_urbs(us);

	/* If there's extra data in the us_data structure then
	 * free that first */
	if (us->extra) {
		/* call the destructor routine, if it exists */
		if (us->extra_destructor) {
			US_DEBUGP("-- calling extra_destructor()\n");
			us->extra_destructor(us->extra);
		}

		/* destroy the extra data */
		US_DEBUGP("-- freeing the data structure\n");
		kfree(us->extra);
	}

	/* up the semaphore so auto-code-checkers won't complain about
	 * the down/up imbalance */
	up(&(us->dev_semaphore));

	/* free the structure itself */
	kfree (us);
}

/***********************************************************************
 * Initialization and registration
 ***********************************************************************/

int __init usb_stor_init(void)
{
	printk(KERN_INFO "Initializing USB Mass Storage driver...\n");

	/* register the driver, return -1 if error */
	if (usb_register(&usb_storage_driver) < 0)
		return -1;

	/* we're all set */
	printk(KERN_INFO "USB Mass Storage support registered.\n");
	return 0;
}

void __exit usb_stor_exit(void)
{
	US_DEBUGP("usb_stor_exit() called\n");

	/* Deregister the driver
	 * This will cause disconnect() to be called for each
	 * attached unit
	 */
	US_DEBUGP("-- calling usb_deregister()\n");
	usb_deregister(&usb_storage_driver) ;

#if 0
	/* While there are still virtual hosts, unregister them
	 * Note that it's important to do this completely before removing
	 * the structures because of possible races with the /proc
	 * interface
	 */
	for (next = us_list; next; next = next->next) {
		US_DEBUGP("-- calling scsi_unregister_host()\n");
		scsi_unregister_host(&usb_stor_host_template);
	}

	/* While there are still structures, free them.  Note that we are
	 * now race-free, since these structures can no longer be accessed
	 * from either the SCSI command layer or the /proc interface
	 */
	while (us_list) {
		/* keep track of where the next one is */
		next = us_list->next;

		/* If there's extra data in the us_data structure then
		 * free that first */
		if (us_list->extra) {
			/* call the destructor routine, if it exists */
			if (us_list->extra_destructor) {
				US_DEBUGP("-- calling extra_destructor()\n");
				us_list->extra_destructor(us_list->extra);
			}

			/* destroy the extra data */
			US_DEBUGP("-- freeing the data structure\n");
			kfree(us_list->extra);
		}

		/* free the structure itself */
		kfree (us_list);

		/* advance the list pointer */
		us_list = next;
	}
#endif
}

module_init(usb_stor_init);
module_exit(usb_stor_exit);
