/* Driver for USB Mass Storage compliant devices
 * SCSI layer glue code
 *
 * $Id: scsiglue.c,v 1.26 2002/04/22 03:39:43 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *   (c) 2000 Stephen J. Gowdy (SGowdy@lbl.gov)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
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
#include "scsiglue.h"
#include "usb.h"
#include "debug.h"
#include "transport.h"

#include <linux/slab.h>
#include <linux/module.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_host.h>


/***********************************************************************
 * Host functions 
 ***********************************************************************/

static const char* host_info(struct Scsi_Host *host)
{
	return "SCSI emulation for USB Mass Storage devices";
}

static int slave_alloc (struct scsi_device *sdev)
{
	/*
	 * Set default bflags. These can be overridden for individual
	 * models and vendors via the scsi devinfo mechanism.
	 */
	sdev->sdev_bflags = (BLIST_MS_SKIP_PAGE_08 | BLIST_MS_SKIP_PAGE_3F |
			     BLIST_USE_10_BYTE_MS);
	return 0;
}

static int slave_configure(struct scsi_device *sdev)
{
	struct us_data *us = (struct us_data *) sdev->host->hostdata[0];

	/* Scatter-gather buffers (all but the last) must have a length
	 * divisible by the bulk maxpacket size.  Otherwise a data packet
	 * would end up being short, causing a premature end to the data
	 * transfer.  Since high-speed bulk pipes have a maxpacket size
	 * of 512, we'll use that as the scsi device queue's DMA alignment
	 * mask.  Guaranteeing proper alignment of the first buffer will
	 * have the desired effect because, except at the beginning and
	 * the end, scatter-gather buffers follow page boundaries. */
	blk_queue_dma_alignment(sdev->request_queue, (512 - 1));

	/* Devices using Genesys Logic chips cause a lot of trouble for
	 * high-speed transfers; they die unpredictably when given more
	 * than 64 KB of data at a time.  If we detect such a device,
	 * reduce the maximum transfer size to 64 KB = 128 sectors. */

#define USB_VENDOR_ID_GENESYS	0x05e3		// Needs a standard location
	if (us->pusb_dev->descriptor.idVendor == USB_VENDOR_ID_GENESYS &&
			us->pusb_dev->speed == USB_SPEED_HIGH)
		blk_queue_max_sectors(sdev->request_queue, 128);

	/* this is to satisify the compiler, tho I don't think the 
	 * return code is ever checked anywhere. */
	return 0;
}

/* queue a command */
/* This is always called with scsi_lock(srb->host) held */
static int queuecommand( Scsi_Cmnd *srb , void (*done)(Scsi_Cmnd *))
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];

	US_DEBUGP("%s called\n", __FUNCTION__);
	srb->host_scribble = (unsigned char *)us;

	/* enqueue the command */
	if (us->sm_state != US_STATE_IDLE || us->srb != NULL) {
		printk(KERN_ERR USB_STORAGE "Error in %s: " 
			"state = %d, us->srb = %p\n",
			__FUNCTION__, us->sm_state, us->srb);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	srb->scsi_done = done;
	us->srb = srb;

	/* wake up the process task */
	up(&(us->sema));

	return 0;
}

/***********************************************************************
 * Error handling functions
 ***********************************************************************/

/* Command abort */
/* This is always called with scsi_lock(srb->host) held */
static int command_abort( Scsi_Cmnd *srb )
{
	struct Scsi_Host *host = srb->device->host;
	struct us_data *us = (struct us_data *) host->hostdata[0];

	US_DEBUGP("%s called\n", __FUNCTION__);

	/* Is this command still active? */
	if (us->srb != srb) {
		US_DEBUGP ("-- nothing to abort\n");
		return FAILED;
	}

	/* Normally the current state is RUNNING.  If the control thread
	 * hasn't even started processing this command, the state will be
	 * IDLE.  Anything else is a bug. */
	if (us->sm_state != US_STATE_RUNNING
				&& us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* Set state to ABORTING and set the ABORTING bit, but only if
	 * a device reset isn't already in progress (to avoid interfering
	 * with the reset).  To prevent races with auto-reset, we must
	 * stop any ongoing USB transfers while still holding the host
	 * lock. */
	us->sm_state = US_STATE_ABORTING;
	if (!test_bit(US_FLIDX_RESETTING, &us->flags)) {
		set_bit(US_FLIDX_ABORTING, &us->flags);
		usb_stor_stop_transport(us);
	}
	scsi_unlock(host);

	/* Wait for the aborted command to finish */
	wait_for_completion(&us->notify);

	/* Reacquire the lock and allow USB transfers to resume */
	scsi_lock(host);
	clear_bit(US_FLIDX_ABORTING, &us->flags);
	return SUCCESS;
}

/* This invokes the transport reset mechanism to reset the state of the
 * device */
/* This is always called with scsi_lock(srb->host) held */
static int device_reset( Scsi_Cmnd *srb )
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];
	int result;

	US_DEBUGP("%s called\n", __FUNCTION__);
	if (us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* set the state and release the lock */
	us->sm_state = US_STATE_RESETTING;
	scsi_unlock(srb->device->host);

	/* lock the device pointers and do the reset */
	down(&(us->dev_semaphore));
	if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
		result = FAILED;
		US_DEBUGP("No reset during disconnect\n");
	} else
		result = us->transport_reset(us);
	up(&(us->dev_semaphore));

	/* lock access to the state and clear it */
	scsi_lock(srb->device->host);
	us->sm_state = US_STATE_IDLE;
	return result;
}

/* This resets the device's USB port. */
/* It refuses to work if there's more than one interface in
 * the device, so that other users are not affected. */
/* This is always called with scsi_lock(srb->host) held */
static int bus_reset( Scsi_Cmnd *srb )
{
	struct us_data *us = (struct us_data *)srb->device->host->hostdata[0];
	int result;

	US_DEBUGP("%s called\n", __FUNCTION__);
	if (us->sm_state != US_STATE_IDLE) {
		printk(KERN_ERR USB_STORAGE "Error in %s: "
			"invalid state %d\n", __FUNCTION__, us->sm_state);
		return FAILED;
	}

	/* set the state and release the lock */
	us->sm_state = US_STATE_RESETTING;
	scsi_unlock(srb->device->host);

	/* The USB subsystem doesn't handle synchronisation between
	 * a device's several drivers. Therefore we reset only devices
	 * with just one interface, which we of course own. */

	down(&(us->dev_semaphore));
	if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
		result = -EIO;
		US_DEBUGP("No reset during disconnect\n");
	} else if (us->pusb_dev->actconfig->desc.bNumInterfaces != 1) {
		result = -EBUSY;
		US_DEBUGP("Refusing to reset a multi-interface device\n");
	} else {
		result = usb_reset_device(us->pusb_dev);
		US_DEBUGP("usb_reset_device returns %d\n", result);
	}
	up(&(us->dev_semaphore));

	/* lock access to the state and clear it */
	scsi_lock(srb->device->host);
	us->sm_state = US_STATE_IDLE;
	return result < 0 ? FAILED : SUCCESS;
}

/* Report a driver-initiated device reset to the SCSI layer.
 * Calling this for a SCSI-initiated reset is unnecessary but harmless.
 * The caller must own the SCSI host lock. */
void usb_stor_report_device_reset(struct us_data *us)
{
	int i;

	scsi_report_device_reset(us->host, 0, 0);
	if (us->flags & US_FL_SCM_MULT_TARG) {
		for (i = 1; i < us->host->max_id; ++i)
			scsi_report_device_reset(us->host, 0, i);
	}
}

/***********************************************************************
 * /proc/scsi/ functions
 ***********************************************************************/

/* we use this macro to help us write into the buffer */
#undef SPRINTF
#define SPRINTF(args...) \
	do { if (pos < buffer+length) pos += sprintf(pos, ## args); } while (0)
#define DO_FLAG(a) \
	do { if (us->flags & US_FL_##a) pos += sprintf(pos, " " #a); } while(0)

static int proc_info (struct Scsi_Host *hostptr, char *buffer, char **start, off_t offset,
		int length, int inout)
{
	struct us_data *us;
	char *pos = buffer;

	/* if someone is sending us data, just throw it away */
	if (inout)
		return length;

	us = (struct us_data*)hostptr->hostdata[0];

	/* print the controller name */
	SPRINTF("   Host scsi%d: usb-storage\n", hostptr->host_no);

	/* print product, vendor, and serial number strings */
	SPRINTF("       Vendor: %s\n", us->vendor);
	SPRINTF("      Product: %s\n", us->product);
	SPRINTF("Serial Number: %s\n", us->serial);

	/* show the protocol and transport */
	SPRINTF("     Protocol: %s\n", us->protocol_name);
	SPRINTF("    Transport: %s\n", us->transport_name);

	/* show the device flags */
	if (pos < buffer + length) {
		pos += sprintf(pos, "       Quirks:");

		DO_FLAG(SINGLE_LUN);
		DO_FLAG(SCM_MULT_TARG);
		DO_FLAG(FIX_INQUIRY);
		DO_FLAG(FIX_CAPACITY);

		*(pos++) = '\n';
	}

	/*
	 * Calculate start of next buffer, and return value.
	 */
	*start = buffer + offset;

	if ((pos - buffer) < offset)
		return (0);
	else if ((pos - buffer - offset) < length)
		return (pos - buffer - offset);
	else
		return (length);
}

/***********************************************************************
 * Sysfs interface
 ***********************************************************************/

/* Output routine for the sysfs max_sectors file */
static ssize_t show_max_sectors(struct device *dev, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	return sprintf(buf, "%u\n", sdev->request_queue->max_sectors);
}

/* Input routine for the sysfs max_sectors file */
static ssize_t store_max_sectors(struct device *dev, const char *buf,
		size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	unsigned short ms;

	if (sscanf(buf, "%hu", &ms) > 0 && ms <= SCSI_DEFAULT_MAX_SECTORS) {
		blk_queue_max_sectors(sdev->request_queue, ms);
		return strlen(buf);
	}
	return -EINVAL;	
}

static DEVICE_ATTR(max_sectors, S_IRUGO | S_IWUSR, show_max_sectors,
		store_max_sectors);

static struct device_attribute *sysfs_device_attr_list[] = {
		&dev_attr_max_sectors,
		NULL,
		};

/*
 * this defines our host template, with which we'll allocate hosts
 */

struct scsi_host_template usb_stor_host_template = {
	/* basic userland interface stuff */
	.name =				"usb-storage",
	.proc_name =			"usb-storage",
	.proc_info =			proc_info,
	.info =				host_info,

	/* command interface -- queued only */
	.queuecommand =			queuecommand,

	/* error and abort handlers */
	.eh_abort_handler =		command_abort,
	.eh_device_reset_handler =	device_reset,
	.eh_bus_reset_handler =		bus_reset,

	/* queue commands only, only one command per LUN */
	.can_queue =			1,
	.cmd_per_lun =			1,

	/* unknown initiator id */
	.this_id =			-1,

	.slave_alloc =			slave_alloc,
	.slave_configure =		slave_configure,

	/* lots of sg segments can be handled */
	.sg_tablesize =			SG_ALL,

	/* limit the total size of a transfer to 120 KB */
	.max_sectors =                  240,

	/* merge commands... this seems to help performance, but
	 * periodically someone should test to see which setting is more
	 * optimal.
	 */
	.use_clustering =		TRUE,

	/* emulated HBA */
	.emulated =			TRUE,

	/* we do our own delay after a device or bus reset */
	.skip_settle_delay =		1,

	/* sysfs device attributes */
	.sdev_attrs =			sysfs_device_attr_list,

	/* module management */
	.module =			THIS_MODULE
};

/* For a device that is "Not Ready" */
unsigned char usb_stor_sense_notready[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= 0x02,			    /* not ready */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x04,			    /* not ready */
	[13]	= 0x03			    /* manual intervention */
};

/* To Report "Illegal Request: Invalid Field in CDB */
unsigned char usb_stor_sense_invalidCDB[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= ILLEGAL_REQUEST,	    /* Illegal Request = 0x05 */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x24			    /* Invalid Field in CDB */
};

