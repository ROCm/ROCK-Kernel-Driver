#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/usb.h>
#include <asm/atomic.h>
#include <linux/blk.h>
#include "../scsi/scsi.h"
#include "../scsi/hosts.h"
#include "../scsi/sd.h"

#include "hpusbscsi.h"

#define DEBUG(x...) \
	printk( KERN_DEBUG x )

static char *states[]={"FREE", "BEGINNING", "WORKING", "ERROR", "WAIT", "PREMATURE"};

#define TRACE_STATE printk(KERN_DEBUG"hpusbscsi->state = %s at line %d\n", states[hpusbscsi->state], __LINE__)

/* global variables */

struct list_head hpusbscsi_devices;
//LIST_HEAD(hpusbscsi_devices);

/* USB related parts */

static void *
hpusbscsi_usb_probe (struct usb_device *dev, unsigned int interface,
		     const struct usb_device_id *id)
{
	struct hpusbscsi *new;
	struct usb_interface_descriptor *altsetting =
		&(dev->actconfig->interface[interface].altsetting[0]);

	int i, result;

	/* basic check */

	if (altsetting->bNumEndpoints != 3) {
		printk (KERN_ERR "Wrong number of endpoints\n");
		return NULL;
	}

	/* descriptor allocation */

	new =
		(struct hpusbscsi *) kmalloc (sizeof (struct hpusbscsi),
					      GFP_KERNEL);
	if (new == NULL)
		return NULL;
	DEBUG ("Allocated memory\n");
	memset (new, 0, sizeof (struct hpusbscsi));
	spin_lock_init (&new->dataurb.lock);
	spin_lock_init (&new->controlurb.lock);
	new->dev = dev;
	init_waitqueue_head (&new->pending);
	init_waitqueue_head (&new->deathrow);
	INIT_LIST_HEAD (&new->lh);



	/* finding endpoints */

	for (i = 0; i < altsetting->bNumEndpoints; i++) {
		if (
		    (altsetting->endpoint[i].
		     bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		    USB_ENDPOINT_XFER_BULK) {
			if (altsetting->endpoint[i].
			    bEndpointAddress & USB_DIR_IN) {
				new->ep_in =
					altsetting->endpoint[i].
					bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK;
			} else {
				new->ep_out =
					altsetting->endpoint[i].
					bEndpointAddress &
					USB_ENDPOINT_NUMBER_MASK;
			}
		} else {
			new->ep_int =
				altsetting->endpoint[i].
				bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
			new->interrupt_interval= altsetting->endpoint[i].bInterval;
		}
	}

	/* USB initialisation magic for the simple case */

	result = usb_set_interface (dev, altsetting->bInterfaceNumber, 0);

	switch (result) {
	case 0:		/* no error */
		break;

	case -EPIPE:
		usb_clear_halt (dev, usb_sndctrlpipe (dev, 0));
		break;

	default:
		printk (KERN_ERR "unknown error %d from usb_set_interface\n",
			 result);
		goto err_out;
	}

	/* making a template for the scsi layer to fake detection of a scsi device */

	memcpy (&(new->ctempl), &hpusbscsi_scsi_host_template,
		sizeof (hpusbscsi_scsi_host_template));
	(struct hpusbscsi *) new->ctempl.proc_dir = new;
	new->ctempl.module = THIS_MODULE;

	if (scsi_register_module (MODULE_SCSI_HA, &(new->ctempl)))
		goto err_out;

	/* adding to list for module unload */
	list_add (&hpusbscsi_devices, &new->lh);

	return new;

      err_out:
	kfree (new);
	return NULL;
}

static void
hpusbscsi_usb_disconnect (struct usb_device *dev, void *ptr)
{
                 usb_unlink_urb(&(((struct hpusbscsi *) ptr)->controlurb));
	((struct hpusbscsi *) ptr)->dev = NULL;
}

static struct usb_device_id hpusbscsi_usb_ids[] = {
	{USB_DEVICE (0x03f0, 0x0701)},	/* HP 53xx */
	{USB_DEVICE (0x03f0, 0x0801)},	/* HP 7400 */
	{USB_DEVICE (0x0638, 0x026a)},	/*Scan Dual II */
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, hpusbscsi_usb_ids);
MODULE_LICENSE("GPL");


static struct usb_driver hpusbscsi_usb_driver = {
	name:"hpusbscsi",
	probe:hpusbscsi_usb_probe,
	disconnect:hpusbscsi_usb_disconnect,
	id_table:hpusbscsi_usb_ids,
};

/* module initialisation */

int __init
hpusbscsi_init (void)
{
	int result;

	INIT_LIST_HEAD (&hpusbscsi_devices);
	DEBUG ("Driver loaded\n");

	if ((result = usb_register (&hpusbscsi_usb_driver)) < 0) {
		printk (KERN_ERR "hpusbscsi: driver registration failed\n");
		return -1;
	} else {
		return 0;
	}
}

void __exit
hpusbscsi_exit (void)
{
	struct list_head *tmp;
	struct list_head *old;
	struct hpusbscsi * o;

	for (tmp = hpusbscsi_devices.next; tmp != &hpusbscsi_devices;/*nothing */) {
		old = tmp;
		tmp = tmp->next;
		o = (struct hpusbscsi *)old;
		usb_unlink_urb(&o->controlurb);
		scsi_unregister_module(MODULE_SCSI_HA,&o->ctempl);
		kfree(old);
	}

	usb_deregister (&hpusbscsi_usb_driver);
}

module_init (hpusbscsi_init);
module_exit (hpusbscsi_exit);

/* interface to the scsi layer */

static int
hpusbscsi_scsi_detect (struct SHT *sht)
{
	/* Whole function stolen from usb-storage */

	struct hpusbscsi *desc = (struct hpusbscsi *) sht->proc_dir;
	/* What a hideous hack! */

	char local_name[48];


	/* set up the name of our subdirectory under /proc/scsi/ */
	sprintf (local_name, "hpusbscsi-%d", desc->number);
	sht->proc_name = kmalloc (strlen (local_name) + 1, GFP_KERNEL);
	/* FIXME: where is this freed ? */

	if (!sht->proc_name) {
		return 0;
	}

	strcpy (sht->proc_name, local_name);

	sht->proc_dir = NULL;

	/* build and submit an interrupt URB for status byte handling */
 	FILL_INT_URB(&desc->controlurb,
			desc->dev,
			usb_rcvintpipe(desc->dev,desc->ep_int),
			&desc->scsi_state_byte,
			1,
			control_interrupt_callback,
			desc,
			desc->interrupt_interval
	);

	if ( 0  >  usb_submit_urb(&desc->controlurb)) {
		kfree(sht->proc_name);
		return 0;
	}

	/* In host->hostdata we store a pointer to desc */
	desc->host = scsi_register (sht, sizeof (desc));
	if (desc->host == NULL) {
		kfree (sht->proc_name);
		usb_unlink_urb(&desc->controlurb);
		return 0;
	}
	desc->host->hostdata[0] = (unsigned long) desc;


	return 1;
}

static int hpusbscsi_scsi_queuecommand (Scsi_Cmnd *srb, scsi_callback callback)
{
	struct hpusbscsi* hpusbscsi = (struct hpusbscsi*)(srb->host->hostdata[0]);
	usb_urb_callback usb_callback;
	int res;

	hpusbscsi->use_count++;

	/* we don't answer for anything but our single device on any faked host controller */
	if ( srb->device->lun || srb->device->id || srb->device->channel ) {
		if (callback) {
			srb->result = DID_BAD_TARGET;
			callback(srb);
		}
                	goto out;
	}

	/* Now we need to decide which callback to give to the urb we send the command with */

	if (!srb->bufflen) {
		usb_callback = simple_command_callback;
	} else {
        	if (srb->use_sg) {
			usb_callback = scatter_gather_callback;
			hpusbscsi->fragment = 0;
		} else {
                	usb_callback = simple_payload_callback;
		}
		/* Now we find out which direction data is to be transfered in */
		hpusbscsi->current_data_pipe = DIRECTION_IS_IN(srb->cmnd[0]) ?
			usb_rcvbulkpipe(hpusbscsi->dev, hpusbscsi->ep_in)
		:
			usb_sndbulkpipe(hpusbscsi->dev, hpusbscsi->ep_out)
		;
	}


	TRACE_STATE;
	if (hpusbscsi->state != HP_STATE_FREE) {
		printk(KERN_CRIT"hpusbscsi - Ouch: queueing violation!\n");
		return 1; /* This must not happen */
	}

        /* We zero the sense buffer to avoid confusing user space */
        memset(srb->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);

	hpusbscsi->state = HP_STATE_BEGINNING;
	TRACE_STATE;

	/* We prepare the urb for writing out the scsi command */
	FILL_BULK_URB(
		&hpusbscsi->dataurb,
		hpusbscsi->dev,
		usb_sndbulkpipe(hpusbscsi->dev,hpusbscsi->ep_out),
		srb->cmnd,
		srb->cmd_len,
		usb_callback,
		hpusbscsi
	);
	hpusbscsi->scallback = callback;
	hpusbscsi->srb = srb;

	res = usb_submit_urb(&hpusbscsi->dataurb);
	if (res) {
		hpusbscsi->state = HP_STATE_FREE;
		TRACE_STATE;
		if (callback) {
			srb->result = DID_ERROR;
			callback(srb);
		}
	}

out:
	hpusbscsi->use_count--;
	return 0;
}

static int hpusbscsi_scsi_host_reset (Scsi_Cmnd *srb)
{
	struct hpusbscsi* hpusbscsi = (struct hpusbscsi*)(srb->host->hostdata[0]);

	printk(KERN_DEBUG"SCSI reset requested.\n");
	usb_reset_device(hpusbscsi->dev);
	printk(KERN_DEBUG"SCSI reset completed.\n");
	hpusbscsi->state = HP_STATE_FREE;

	return 0;
}

static int hpusbscsi_scsi_abort (Scsi_Cmnd *srb)
{
	struct hpusbscsi* hpusbscsi = (struct hpusbscsi*)(srb->host->hostdata[0]);
	printk(KERN_DEBUG"Requested is canceled.\n");

	usb_unlink_urb(&hpusbscsi->dataurb);
	usb_unlink_urb(&hpusbscsi->controlurb);
	hpusbscsi->state = HP_STATE_FREE;

	return SCSI_ABORT_PENDING;
}

/* usb interrupt handlers - they are all running IN INTERRUPT ! */

static void handle_usb_error (struct hpusbscsi *hpusbscsi)
{
	if (hpusbscsi->scallback != NULL) {
		hpusbscsi->srb->result = DID_ERROR;
		hpusbscsi->scallback(hpusbscsi->srb);
	}
	hpusbscsi->state = HP_STATE_FREE;
}

static void  control_interrupt_callback (struct urb *u)
{
	struct hpusbscsi * hpusbscsi = (struct hpusbscsi *)u->context;

DEBUG("Getting status byte %d \n",hpusbscsi->scsi_state_byte);
	if(u->status < 0) {
                if (hpusbscsi->state != HP_STATE_FREE)
                        handle_usb_error(hpusbscsi);
		return;
	}
	hpusbscsi->srb->result &= SCSI_ERR_MASK;
	hpusbscsi->srb->result |= hpusbscsi->scsi_state_byte<<1;

	if (hpusbscsi->scallback != NULL && hpusbscsi->state == HP_STATE_WAIT)
		/* we do a callback to the scsi layer if and only if all data has been transfered */
		hpusbscsi->scallback(hpusbscsi->srb);

	TRACE_STATE;
	switch (hpusbscsi->state) {
	case HP_STATE_WAIT:
		hpusbscsi->state = HP_STATE_FREE;
	TRACE_STATE;
		break;
	case HP_STATE_WORKING:
	case HP_STATE_BEGINNING:
		hpusbscsi->state = HP_STATE_PREMATURE;
	TRACE_STATE;
		break;
	default:
		printk(KERN_ERR"hpusbscsi: Unexpected status report.\n");
	TRACE_STATE;
		hpusbscsi->state = HP_STATE_FREE;
	TRACE_STATE;
		break;
	}
}

static void simple_command_callback(struct urb *u)
{
	struct hpusbscsi * hpusbscsi = (struct hpusbscsi *)u->context;
	if (u->status<0) {
		handle_usb_error(hpusbscsi);
		return;
        }
	TRACE_STATE;
	if (hpusbscsi->state != HP_STATE_PREMATURE) {
	        TRACE_STATE;
		hpusbscsi->state = HP_STATE_WAIT;
	} else {
		if (hpusbscsi->scallback != NULL)
			hpusbscsi->scallback(hpusbscsi->srb);
		hpusbscsi->state = HP_STATE_FREE;
	TRACE_STATE;
	}
}

static void scatter_gather_callback(struct urb *u)
{
	struct hpusbscsi * hpusbscsi = (struct hpusbscsi *)u->context;
        struct scatterlist *sg = hpusbscsi->srb->buffer;
        usb_urb_callback callback;
        int res;

        DEBUG("Going through scatter/gather\n");
        if (u->status < 0) {
                handle_usb_error(hpusbscsi);
                return;
        }

        if (hpusbscsi->fragment + 1 != hpusbscsi->srb->use_sg)
                callback = scatter_gather_callback;
        else
                callback = simple_done;

	TRACE_STATE;
        if (hpusbscsi->state != HP_STATE_PREMATURE)
		hpusbscsi->state = HP_STATE_WORKING;
	TRACE_STATE;

        FILL_BULK_URB(
                u,
                hpusbscsi->dev,
                hpusbscsi->current_data_pipe,
                sg[hpusbscsi->fragment].address,
                sg[hpusbscsi->fragment++].length,
                callback,
                hpusbscsi
        );

        res = usb_submit_urb(u);
        if (res)
                hpusbscsi->state = HP_STATE_ERROR;
	TRACE_STATE;
}

static void simple_done (struct urb *u)
{
	struct hpusbscsi * hpusbscsi = (struct hpusbscsi *)u->context;

        if (u->status < 0) {
                handle_usb_error(hpusbscsi);
                return;
        }
        DEBUG("Data transfer done\n");
	TRACE_STATE;
	if (hpusbscsi->state != HP_STATE_PREMATURE) {
		if (u->status < 0)
			hpusbscsi->state = HP_STATE_ERROR;
		else
			hpusbscsi->state = HP_STATE_WAIT;
		TRACE_STATE;
	} else {
		if (hpusbscsi->scallback != NULL)
			hpusbscsi->scallback(hpusbscsi->srb);
		hpusbscsi->state = HP_STATE_FREE;
	}
}

static void simple_payload_callback (struct urb *u)
{
	struct hpusbscsi * hpusbscsi = (struct hpusbscsi *)u->context;
	int res;

	if (u->status<0) {
                handle_usb_error(hpusbscsi);
		return;
        }

	FILL_BULK_URB(
		u,
		hpusbscsi->dev,
		hpusbscsi->current_data_pipe,
		hpusbscsi->srb->buffer,
		hpusbscsi->srb->bufflen,
		simple_done,
		hpusbscsi
	);

	res = usb_submit_urb(u);
	if (res) {
                handle_usb_error(hpusbscsi);
		return;
        }
	TRACE_STATE;
	if (hpusbscsi->state != HP_STATE_PREMATURE) {
		hpusbscsi->state = HP_STATE_WORKING;
	TRACE_STATE;
	} else {
		if (hpusbscsi->scallback != NULL)
			hpusbscsi->scallback(hpusbscsi->srb);
		hpusbscsi->state = HP_STATE_FREE;
	TRACE_STATE;
	}
}

