/* Driver for USB Mass Storage compliant devices
 *
 * $Id: usb.c,v 1.75 2002/04/22 03:39:43 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2003 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *   (c) 2003 Alan Stern (stern@rowland.harvard.edu)
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
		unsigned int data_len)
{
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

	usb_stor_set_xfer_buf(data, data_len, us->srb);
}

static int usb_stor_control_thread(void * __us)
{
	struct us_data *us = (struct us_data *)__us;

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources.
	 */
	daemonize("usb-storage");

	current->flags |= PF_NOFREEZE;

	unlock_kernel();

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
		if (us->sm_state == US_STATE_ABORTING) {
			us->srb->result = DID_ABORT << 16;
			goto SkipForAbort;
		}

		/* set the state and release the lock */
		us->sm_state = US_STATE_RUNNING;
		scsi_unlock(host);

		/* lock the device pointers */
		down(&(us->dev_semaphore));

		/* don't do anything if we are disconnecting */
		if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
			US_DEBUGP("No command during disconnect\n");
			us->srb->result = DID_BAD_TARGET << 16;
		}

		/* reject the command if the direction indicator 
		 * is UNKNOWN
		 */
		else if (us->srb->sc_data_direction == SCSI_DATA_UNKNOWN) {
			US_DEBUGP("UNKNOWN data direction\n");
			us->srb->result = DID_ERROR << 16;
		}

		/* reject if target != 0 or if LUN is higher than
		 * the maximum known LUN
		 */
		else if (us->srb->device->id && 
				!(us->flags & US_FL_SCM_MULT_TARG)) {
			US_DEBUGP("Bad target number (%d:%d)\n",
				  us->srb->device->id, us->srb->device->lun);
			us->srb->result = DID_BAD_TARGET << 16;
		}

		else if (us->srb->device->lun > us->max_lun) {
			US_DEBUGP("Bad LUN (%d:%d)\n",
				  us->srb->device->id, us->srb->device->lun);
			us->srb->result = DID_BAD_TARGET << 16;
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
		if (us->sm_state == US_STATE_ABORTING)
			complete(&(us->notify));

		/* empty the queue, reset the state, and release the lock */
		us->srb = NULL;
		us->sm_state = US_STATE_IDLE;
		scsi_unlock(host);
	} /* for (;;) */

	/* notify the exit routine that we're actually exiting now 
	 *
	 * complete()/wait_for_completion() is similar to up()/down(),
	 * except that complete() is safe in the case where the structure
	 * is getting deleted in a parallel mode of execution (i.e. just
	 * after the down() -- that's necessary for the thread-shutdown
	 * case.
	 *
	 * complete_and_exit() goes even further than this -- it is safe in
	 * the case that the thread of the caller is going away (not just
	 * the structure) -- this is necessary for the module-remove case.
	 * This is important in preemption kernels, which transfer the flow
	 * of execution immediately upon a complete().
	 */
	complete_and_exit(&(us->notify), 0);
}	

/***********************************************************************
 * Device probing and disconnecting
 ***********************************************************************/

/* Associate our private data with the USB device */
static int associate_dev(struct us_data *us, struct usb_interface *intf)
{
	US_DEBUGP("-- %s\n", __FUNCTION__);

	/* Fill in the device-related fields */
	us->pusb_dev = interface_to_usbdev(intf);
	us->pusb_intf = intf;
	us->ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

	/* Store our private data in the interface and increment the
	 * device's reference count */
	usb_set_intfdata(intf, us);
	usb_get_dev(us->pusb_dev);

	/* Allocate the device-related DMA-mapped buffers */
	us->cr = usb_buffer_alloc(us->pusb_dev, sizeof(*us->cr),
			GFP_KERNEL, &us->cr_dma);
	if (!us->cr) {
		US_DEBUGP("usb_ctrlrequest allocation failed\n");
		return -ENOMEM;
	}

	us->iobuf = usb_buffer_alloc(us->pusb_dev, US_IOBUF_SIZE,
			GFP_KERNEL, &us->iobuf_dma);
	if (!us->iobuf) {
		US_DEBUGP("I/O buffer allocation failed\n");
		return -ENOMEM;
	}
	return 0;
}

/* Get the unusual_devs entries and the string descriptors */
static void get_device_info(struct us_data *us, int id_index)
{
	struct usb_device *dev = us->pusb_dev;
	struct usb_interface_descriptor *idesc =
		&us->pusb_intf->cur_altsetting->desc;
	struct us_unusual_dev *unusual_dev = &us_unusual_dev_list[id_index];
	struct usb_device_id *id = &storage_usb_ids[id_index];

	if (unusual_dev->vendorName)
		US_DEBUGP("Vendor: %s\n", unusual_dev->vendorName);
	if (unusual_dev->productName)
		US_DEBUGP("Product: %s\n", unusual_dev->productName);

	/* Store the entries */
	us->unusual_dev = unusual_dev;
	us->subclass = (unusual_dev->useProtocol == US_SC_DEVICE) ?
			idesc->bInterfaceSubClass :
			unusual_dev->useProtocol;
	us->protocol = (unusual_dev->useTransport == US_PR_DEVICE) ?
			idesc->bInterfaceProtocol :
			unusual_dev->useTransport;
	us->flags = unusual_dev->flags;

	/* Log a message if a non-generic unusual_dev entry contains an
	 * unnecessary subclass or protocol override.  This may stimulate
	 * reports from users that will help us remove unneeded entries
	 * from the unusual_devs.h table.
	 */
	if (id->idVendor || id->idProduct) {
		static char *msgs[3] = {
			"an unneeded SubClass entry",
			"an unneeded Protocol entry",
			"unneeded SubClass and Protocol entries"};
		struct usb_device_descriptor *ddesc = &dev->descriptor;
		int msg = -1;

		if (unusual_dev->useProtocol != US_SC_DEVICE &&
			us->subclass == idesc->bInterfaceSubClass)
			msg += 1;
		if (unusual_dev->useTransport != US_PR_DEVICE &&
			us->protocol == idesc->bInterfaceProtocol)
			msg += 2;
		if (msg >= 0 && !(unusual_dev->flags & US_FL_NEED_OVERRIDE))
			printk(KERN_NOTICE USB_STORAGE "This device "
				"(%04x,%04x,%04x S %02x P %02x)"
				" has %s in unusual_devs.h\n"
				"   Please send a copy of this message to "
				"<linux-usb-devel@lists.sourceforge.net>\n",
				ddesc->idVendor, ddesc->idProduct,
				ddesc->bcdDevice,
				idesc->bInterfaceSubClass,
				idesc->bInterfaceProtocol,
				msgs[msg]);
	}

	/* Read the device's string descriptors */
	if (dev->descriptor.iManufacturer)
		usb_string(dev, dev->descriptor.iManufacturer, 
			   us->vendor, sizeof(us->vendor));
	if (dev->descriptor.iProduct)
		usb_string(dev, dev->descriptor.iProduct, 
			   us->product, sizeof(us->product));
	if (dev->descriptor.iSerialNumber)
		usb_string(dev, dev->descriptor.iSerialNumber, 
			   us->serial, sizeof(us->serial));

	/* Use the unusual_dev strings if the device didn't provide them */
	if (strlen(us->vendor) == 0) {
		if (unusual_dev->vendorName)
			strlcpy(us->vendor, unusual_dev->vendorName,
				sizeof(us->vendor));
		else
			strcpy(us->vendor, "Unknown");
	}
	if (strlen(us->product) == 0) {
		if (unusual_dev->productName)
			strlcpy(us->product, unusual_dev->productName,
				sizeof(us->product));
		else
			strcpy(us->product, "Unknown");
	}
	if (strlen(us->serial) == 0)
		strcpy(us->serial, "None");
}

/* Get the transport settings */
static int get_transport(struct us_data *us)
{
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
		return -EIO;
	}
	US_DEBUGP("Transport: %s\n", us->transport_name);

	/* fix for single-lun devices */
	if (us->flags & US_FL_SINGLE_LUN)
		us->max_lun = 0;
	return 0;
}

/* Get the protocol settings */
static int get_protocol(struct us_data *us)
{
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
		return -EIO;
	}
	US_DEBUGP("Protocol: %s\n", us->protocol_name);
	return 0;
}

/* Get the pipe settings */
static int get_pipes(struct us_data *us)
{
	struct usb_host_interface *altsetting =
		us->pusb_intf->cur_altsetting;
	int i;
	struct usb_endpoint_descriptor *ep;
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct usb_endpoint_descriptor *ep_int = NULL;

	/*
	 * Find the endpoints we need.
	 * We are expecting a minimum of 2 endpoints - in and out (bulk).
	 * An optional interrupt is OK (necessary for CBI protocol).
	 * We will ignore any others.
	 */
	for (i = 0; i < altsetting->desc.bNumEndpoints; i++) {
		ep = &altsetting->endpoint[i].desc;

		/* Is it a BULK endpoint? */
		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_BULK) {
			/* BULK in or out? */
			if (ep->bEndpointAddress & USB_DIR_IN)
				ep_in = ep;
			else
				ep_out = ep;
		}

		/* Is it an interrupt endpoint? */
		else if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_INT) {
			ep_int = ep;
		}
	}
	US_DEBUGP("Endpoints: In: 0x%p Out: 0x%p Int: 0x%p (Period %d)\n",
		  ep_in, ep_out, ep_int, ep_int ? ep_int->bInterval : 0);

	if (!ep_in || !ep_out || (us->protocol == US_PR_CBI && !ep_int)) {
		US_DEBUGP("Endpoint sanity check failed! Rejecting dev.\n");
		return -EIO;
	}

	/* Calculate and store the pipe values */
	us->send_ctrl_pipe = usb_sndctrlpipe(us->pusb_dev, 0);
	us->recv_ctrl_pipe = usb_rcvctrlpipe(us->pusb_dev, 0);
	us->send_bulk_pipe = usb_sndbulkpipe(us->pusb_dev,
		ep_out->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	us->recv_bulk_pipe = usb_rcvbulkpipe(us->pusb_dev, 
		ep_in->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	if (ep_int) {
		us->recv_intr_pipe = usb_rcvintpipe(us->pusb_dev,
			ep_int->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
		us->ep_bInterval = ep_int->bInterval;
	}
	return 0;
}

/* Initialize all the dynamic resources we need */
static int usb_stor_acquire_resources(struct us_data *us)
{
	int p;

	us->current_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!us->current_urb) {
		US_DEBUGP("URB allocation failed\n");
		return -ENOMEM;
	}

	/* Lock the device while we carry out the next two operations */
	down(&us->dev_semaphore);

	/* For bulk-only devices, determine the max LUN value */
	if (us->protocol == US_PR_BULK)
		us->max_lun = usb_stor_Bulk_max_lun(us);

	/* Just before we start our control thread, initialize
	 * the device if it needs initialization */
	if (us->unusual_dev->initFunction)
		us->unusual_dev->initFunction(us);

	up(&us->dev_semaphore);

	/* Start up our control thread */
	us->sm_state = US_STATE_IDLE;
	p = kernel_thread(usb_stor_control_thread, us, CLONE_VM);
	if (p < 0) {
		printk(KERN_WARNING USB_STORAGE 
		       "Unable to start control thread\n");
		return p;
	}
	us->pid = p;

	/* Wait for the thread to start */
	wait_for_completion(&(us->notify));

	/*
	 * Since this is a new device, we need to register a SCSI
	 * host definition with the higher SCSI layers.
	 */
	us->host = scsi_host_alloc(&usb_stor_host_template, sizeof(us));
	if (!us->host) {
		printk(KERN_WARNING USB_STORAGE
			"Unable to register the scsi host\n");
		return -EBUSY;
	}

	/* Set the hostdata to prepare for scanning */
	us->host->hostdata[0] = (unsigned long) us;

	return 0;
}

/* Dissociate from the USB device */
static void dissociate_dev(struct us_data *us)
{
	US_DEBUGP("-- %s\n", __FUNCTION__);
	down(&us->dev_semaphore);

	/* Free the device-related DMA-mapped buffers */
	if (us->cr) {
		usb_buffer_free(us->pusb_dev, sizeof(*us->cr), us->cr,
				us->cr_dma);
		us->cr = NULL;
	}
	if (us->iobuf) {
		usb_buffer_free(us->pusb_dev, US_IOBUF_SIZE, us->iobuf,
				us->iobuf_dma);
		us->iobuf = NULL;
	}

	/* Remove our private data from the interface and decrement the
	 * device's reference count */
	usb_set_intfdata(us->pusb_intf, NULL);
	usb_put_dev(us->pusb_dev);

	us->pusb_dev = NULL;
	us->pusb_intf = NULL;
	up(&us->dev_semaphore);
}

/* Release all our static and dynamic resources */
void usb_stor_release_resources(struct us_data *us)
{
	/*
	 * The host must already have been removed
	 * and dissociate_dev() must have been called.
	 */

	/* Finish the SCSI host removal sequence */
	if (us->host) {
		us->host->hostdata[0] = 0;
		scsi_host_put(us->host);
	}

	/* Kill the control thread
	 *
	 * Enqueue the command, wake up the thread, and wait for 
	 * notification that it has exited.
	 */
	if (us->pid) {
		US_DEBUGP("-- sending exit command to thread\n");
		BUG_ON(us->sm_state != US_STATE_IDLE);
		us->srb = NULL;
		up(&(us->sema));
		wait_for_completion(&(us->notify));
	}

	/* Call the destructor routine, if it exists */
	if (us->extra_destructor) {
		US_DEBUGP("-- calling extra_destructor()\n");
		us->extra_destructor(us->extra);
	}

	/* Free the extra data and the URB */
	if (us->extra)
		kfree(us->extra);
	if (us->current_urb)
		usb_free_urb(us->current_urb);

	/* Free the structure itself */
	kfree(us);
	US_DEBUGP("-- %s finished\n", __FUNCTION__);
}

/* Probe to see if we can drive a newly-connected USB device */
static int storage_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct us_data *us;
	const int id_index = id - storage_usb_ids; 
	int result;

	US_DEBUGP("USB Mass Storage device detected\n");
	US_DEBUGP("altsetting is %d, id_index is %d\n",
			intf->cur_altsetting->desc.bAlternateSetting,
			id_index);

	/* Allocate the us_data structure and initialize the mutexes */
	us = (struct us_data *) kmalloc(sizeof(*us), GFP_KERNEL);
	if (!us) {
		printk(KERN_WARNING USB_STORAGE "Out of memory\n");
		return -ENOMEM;
	}
	memset(us, 0, sizeof(struct us_data));
	init_MUTEX(&(us->dev_semaphore));
	init_MUTEX_LOCKED(&(us->sema));
	init_completion(&(us->notify));

	/* Associate the us_data structure with the USB device */
	result = associate_dev(us, intf);
	if (result)
		goto BadDevice;

	/*
	 * Get the unusual_devs entries and the descriptors
	 *
	 * id_index is calculated in the declaration to be the index number
	 * of the match from the usb_device_id table, so we can find the
	 * corresponding entry in the private table.
	 */
	get_device_info(us, id_index);

#ifdef CONFIG_USB_STORAGE_SDDR09
	if (us->protocol == US_PR_EUSB_SDDR09 ||
			us->protocol == US_PR_DPCM_USB) {
		/* set the configuration -- STALL is an acceptable response here */
		if (us->pusb_dev->actconfig->desc.bConfigurationValue != 1) {
			US_DEBUGP("active config #%d != 1 ??\n", us->pusb_dev
				->actconfig->desc.bConfigurationValue);
			goto BadDevice;
		}
		result = usb_reset_configuration(us->pusb_dev);

		US_DEBUGP("Result of usb_reset_configuration is %d\n", result);
		if (result == -EPIPE) {
			US_DEBUGP("-- stall on control interface\n");
		} else if (result != 0) {
			/* it's not a stall, but another error -- time to bail */
			US_DEBUGP("-- Unknown error.  Rejecting device\n");
			goto BadDevice;
		}
	}
#endif

	/* Get the transport, protocol, and pipe settings */
	result = get_transport(us);
	if (result)
		goto BadDevice;
	result = get_protocol(us);
	if (result)
		goto BadDevice;
	result = get_pipes(us);
	if (result)
		goto BadDevice;

	/* Acquire all the other resources */
	result = usb_stor_acquire_resources(us);
	if (result)
		goto BadDevice;

	/* Finally, add the host (this does SCSI device scanning) */
	result = scsi_add_host(us->host, &intf->dev);
	if (result) {
		printk(KERN_WARNING USB_STORAGE
			"Unable to add the scsi host\n");
		goto BadDevice;
	}

	scsi_scan_host(us->host);

	printk(KERN_DEBUG 
	       "USB Mass Storage device found at %d\n", us->pusb_dev->devnum);
	return 0;

	/* We come here if there are any problems */
BadDevice:
	US_DEBUGP("storage_probe() failed\n");
	dissociate_dev(us);
	usb_stor_release_resources(us);
	return result;
}

/* Handle a disconnect event from the USB core */
static void storage_disconnect(struct usb_interface *intf)
{
	struct us_data *us = usb_get_intfdata(intf);

	US_DEBUGP("storage_disconnect() called\n");

	/* Prevent new USB transfers and stop the current command */
	set_bit(US_FLIDX_DISCONNECTING, &us->flags);
	usb_stor_stop_transport(us);

	/* Dissociate from the USB device */
	dissociate_dev(us);

	scsi_remove_host(us->host);

	/* TODO: somehow, wait for the device to
	 * be 'idle' (tasklet completion) */

	/* Release all our other resources */
	usb_stor_release_resources(us);
}

/***********************************************************************
 * Initialization and registration
 ***********************************************************************/

static int __init usb_stor_init(void)
{
	int retval;
	printk(KERN_INFO "Initializing USB Mass Storage driver...\n");

	/* register the driver, return usb_register return code if error */
	retval = usb_register(&usb_storage_driver);
	if (retval)
		goto out;

	/* we're all set */
	printk(KERN_INFO "USB Mass Storage support registered.\n");
out:
	return retval;
}

static void __exit usb_stor_exit(void)
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
