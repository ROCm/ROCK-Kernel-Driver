/*      $Id: lirc_sasem.c,v 1.7 2005/03/29 17:51:45 lirc Exp $      */

/* lirc_sasem.c - USB remote support for LIRC
 * Version 0.3  [beta status]
 *
 * Copyright (C) 2004 Oliver Stabel <oliver.stabel@gmx.de>
 *
 * This driver was derived from:
 *   Paul Miller <pmiller9@users.sourceforge.net>'s 2003-2004
 *      "lirc_atiusb - USB remote support for LIRC"
 *   Culver Consulting Services <henry@culcon.com>'s 2003
 *      "Sasem OnAir VFD/IR USB driver"
 *
 *
 * 2004/06/13   -   0.1
 *                  initial version
 *
 * 2004/06/28   -   0.2
 *                  added file system support to write data to VFD device (used  
 *                  in conjunction with LCDProc)
 *
 * 2004/11/22   -   0.3
 *                  Ported to 2.6 kernel - Tim Davies <tim@opensystems.net.au>
 *
 * 2005/03/29   -   0.4
 *                  A few tidyups and keypress timings - Tim Davies <tim@opensystems.net.au>
 *
 * TODO
 *	- check USB Minor allocation
 *	- param to enable/disable LIRC communication (??)
 *
 */

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/devfs_fs_kernel.h>

#include "lirc_sasem.h"
#include "lirc.h"
#include "lirc_dev.h"

//#define dbg(format, arg...) printk(KERN_DEBUG "%s: " format "\n" , __FILE__ , ## arg)

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

static int debug = 0;

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "enable debug = 1, disable = 0 (default)");

static struct usb_device_id SasemID [] = {
	{ USB_DEVICE(0x11ba, 0x0101) },
	{ }
};

MODULE_DEVICE_TABLE (usb, SasemID);

static struct file_operations SasemFileOps =
{
	owner:      THIS_MODULE,
	read:       SasemFSRead,
	write:      SasemFSWrite,
	ioctl:      SasemFSIoctl,
	open:       SasemFSOpen,
	release:    SasemFSRelease,
	poll:       SasemFSPoll,
};

#ifdef KERNEL_2_5
static struct usb_class_driver SasemClass =
{
	name:		"usb/lcd",
	fops:		&SasemFileOps,
	mode:		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH,
	minor_base:	SASEM_MINOR,
};
#endif

static struct usb_driver SasemDriver =
{
	owner:		THIS_MODULE,
	name:		"Sasem",
	probe:          SasemProbe,
	disconnect:     SasemDisconnect,
#ifndef KERNEL_2_5
	fops:           &SasemFileOps,
	minor:		SASEM_MINOR,
#endif
	id_table:       SasemID,
};

static struct SasemDevice *SasemDevice = NULL;

static int __init SasemInit (void)
{
	printk(BANNER);

	if (usb_register(&SasemDriver)) {
		err("USB registration failed");
		return -ENOSYS;
	}

	return 0;
}

static void __exit SasemExit (void)
{
	usb_deregister (&SasemDriver);
}

module_init (SasemInit);
module_exit (SasemExit);

#ifndef KERNEL_2_5
static void * SasemProbe(struct usb_device *Device,
			   unsigned InterfaceNum,
			   const struct usb_device_id *ID)
{
	struct usb_interface_descriptor *lCurrentInterfaceDescriptor;
#else
static int SasemProbe(struct usb_interface *Int,
		      const struct usb_device_id *ID)
{
	struct usb_device *Device = NULL;
	struct usb_host_interface *iface_host = NULL;
#endif
	struct SasemDevice *lSasemDevice = NULL;
	struct usb_endpoint_descriptor *lEndpoint, *lEndpoint2;
	int lPipe;
	int lDevnum;
	struct lirc_plugin *lLircPlugin = NULL;
	struct lirc_buffer *lLircBuffer = NULL;
	int lLircMinor = -1;
	int lMemFailure;
	char lBuf[63], lName[128]="";

	dbg(" {\n");
	
#ifndef KERNEL_2_5
	lCurrentInterfaceDescriptor = Device->actconfig->interface->
		altsetting;
	lEndpoint = lCurrentInterfaceDescriptor->endpoint;
	lEndpoint2 = lEndpoint + 1;
#else
	Device = interface_to_usbdev(Int);
	iface_host = Int->cur_altsetting;
	lEndpoint = &(iface_host->endpoint[0].desc);
	lEndpoint2 = &(iface_host->endpoint[1].desc);
#endif
	
	if (!(lEndpoint->bEndpointAddress & 0x80) ||
		((lEndpoint->bmAttributes & 3) != 0x03)) {
		err("OnAir config endpoint error");
#ifndef KERNEL_2_5
		return NULL;
#else
		return -ENODEV;
#endif
	}

	lDevnum = Device->devnum;

	lMemFailure = 0;
	if (!(lSasemDevice = kmalloc(sizeof(*lSasemDevice), GFP_KERNEL))) {
		err("kmalloc(sizeof(*lSasemDevice), GFP_KERNEL)) failed");
		lMemFailure = 1;
	}
	else {
		memset(lSasemDevice, 0, sizeof(*lSasemDevice));
		if (!(lLircPlugin = 
			kmalloc(sizeof(*lLircPlugin), GFP_KERNEL))) {
			err("kmalloc(sizeof(*lLircPlugin), GFP_KERNEL))"
				"failed");
			lMemFailure = 2;
		}
		else if (!(lLircBuffer = 
			kmalloc(sizeof(*lLircBuffer), GFP_KERNEL))) {
			err("kmalloc(sizeof(*lLircBuffer), GFP_KERNEL))"  
				" failed");
			lMemFailure = 3;
		}
		else if (lirc_buffer_init(lLircBuffer, MAX_INTERRUPT_DATA,
				4)) {
			err("lirc_buffer_init failed");
			lMemFailure = 4;
		}
#ifndef KERNEL_2_5
		else if (!(lSasemDevice->UrbIn = usb_alloc_urb(0))) {
#else
		else if (!(lSasemDevice->UrbIn = usb_alloc_urb(0, GFP_KERNEL))) {
#endif
			err("usb_alloc_urb(0) failed");
			lMemFailure = 5;
		} else {

			memset(lLircPlugin, 0, sizeof(*lLircPlugin));
			strcpy(lLircPlugin->name, DRIVER_NAME " ");
			lLircPlugin->minor = -1;
			lLircPlugin->code_length = MAX_INTERRUPT_DATA*8;
			lLircPlugin->features = LIRC_CAN_REC_LIRCCODE;
			lLircPlugin->data = lSasemDevice;
			
			lLircPlugin->rbuf = lLircBuffer;
			lLircPlugin->set_use_inc = &LircSetUseInc;
			lLircPlugin->set_use_dec = &LircSetUseDec;
			lLircPlugin->owner = THIS_MODULE;

			if ((lLircMinor = 
				lirc_register_plugin(lLircPlugin)) < 0) {
				err("lirc_register_plugin(lLircPlugin))"  
					" failed");
				lMemFailure = 9;
			}
		}
	}
	switch (lMemFailure) {
	case 9:
		usb_free_urb(lSasemDevice->UrbIn);
	case 5:
	case 4:
		kfree(lLircBuffer);
	case 3:
		kfree(lLircPlugin);
	case 2:
		kfree(lSasemDevice);
	case 1:
#ifndef KERNEL_2_5
		return NULL;
#else
		return -ENOMEM;
#endif
	}
	
	lLircPlugin->minor = lLircMinor; 
	
	dbg(": init device structure\n");
	init_MUTEX(&lSasemDevice->SemLock);
	down(&lSasemDevice->SemLock);
	lSasemDevice->DescriptorIn = lEndpoint;
	lSasemDevice->DescriptorOut = lEndpoint2;    
	lSasemDevice->Device = Device;
	lSasemDevice->LircPlugin = lLircPlugin;
	lSasemDevice->Open = 0;
	lSasemDevice->UrbOut = NULL;
	lSasemDevice->PressTime.tv_sec = 0; 
	lSasemDevice->PressTime.tv_usec = 0;
	init_waitqueue_head(&lSasemDevice->QueueOpen);
	init_waitqueue_head(&lSasemDevice->QueueWrite);

	dbg(": init inbound URB\n");
	lPipe = usb_rcvintpipe(lSasemDevice->Device,
		lSasemDevice->DescriptorIn->bEndpointAddress);
	
	usb_fill_int_urb(lSasemDevice->UrbIn, 
			 lSasemDevice->Device,
			 lPipe, lSasemDevice->BufferIn,
			 sizeof(lSasemDevice->BufferIn),
			 SasemCallbackIn, lSasemDevice, 
			 lSasemDevice->DescriptorIn->bInterval);

	dbg(": get USB device info\n");
	if (Device->descriptor.iManufacturer &&
			usb_string(Device, 
			Device->descriptor.iManufacturer, 
			lBuf, 63) > 0) {
		strncpy(lName, lBuf, 128);
	}
	if (Device->descriptor.iProduct &&
			usb_string(Device, Device->descriptor.iProduct, 
			lBuf, 63) > 0) {
		snprintf(lName, 128, "%s %s", lName, lBuf);
	}
	printk(DRIVER_NAME "[%d]: %s on usb%d\n", lDevnum, lName,
		Device->bus->busnum);

	SasemDevice = lSasemDevice;
	up(&lSasemDevice->SemLock);
	dbg(" }\n");
#ifndef KERNEL_2_5
	return lSasemDevice;
#else
	usb_set_intfdata(Int, lSasemDevice);
	if (usb_register_dev(Int, &SasemClass)) {
		dbg(": Can't get minor for this device\n");
		usb_set_intfdata(Int, NULL);
		return -ENODEV;
	}
	return 0;
#endif
}


#ifndef KERNEL_2_5
static void SasemDisconnect(struct usb_device *Device, void *Ptr) {
	struct SasemDevice *lSasemDevice = Ptr;
#else
static void SasemDisconnect(struct usb_interface *Int) {
	struct SasemDevice *lSasemDevice = usb_get_intfdata(Int);
	usb_set_intfdata(Int, NULL);
#endif

	dbg(" {\n");

	down(&lSasemDevice->SemLock);

#ifdef KERNEL_2_5
	usb_deregister_dev(Int, &SasemClass);
#endif

	dbg(": free inbound URB\n");    
	usb_unlink_urb(lSasemDevice->UrbIn);
	usb_free_urb(lSasemDevice->UrbIn);
	UnregisterFromLirc(lSasemDevice);

	if (lSasemDevice->UrbOut != NULL) {
		dbg(": free outbound URB\n");    
		usb_unlink_urb(lSasemDevice->UrbOut);
		usb_free_urb(lSasemDevice->UrbOut);
}
	up(&lSasemDevice->SemLock);
	kfree (lSasemDevice);
	dbg(" }\n");
}

#ifndef KERNEL_2_5
static void SasemCallbackIn(struct urb *Urb) {
#else
static void SasemCallbackIn(struct urb *Urb, struct pt_regs *regs) {
#endif
	struct SasemDevice *lSasemDevice;
	int lDevnum;
	int lLen;
	char lBuf[MAX_INTERRUPT_DATA];
	int li;
	struct timeval lstv;
	long llms;

	dbg(" {\n");
	
	if (!Urb) {
		dbg(": Urb == NULL\n");
		return;
	}

	if (!(lSasemDevice = Urb->context)) {
		dbg(": no context\n");
#ifdef KERNEL_2_5
		Urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
		usb_unlink_urb(Urb);
		return;
	}

	lDevnum = lSasemDevice->Devnum;
	if (debug) {
		printk(DRIVER_NAME "[%d]: data received (length %d)\n",
		       lDevnum, Urb->actual_length);
		printk(DRIVER_NAME 
		       " intr_callback called %x %x %x %x %x %x %x %x\n", 
		       lSasemDevice->BufferIn[0],
		       lSasemDevice->BufferIn[1],
		       lSasemDevice->BufferIn[2],
		       lSasemDevice->BufferIn[3],
		       lSasemDevice->BufferIn[4],
		       lSasemDevice->BufferIn[5],
		       lSasemDevice->BufferIn[6],
		       lSasemDevice->BufferIn[7]);
	}

	switch (Urb->status) {

	/* success */
	case 0:
		lLen = Urb->actual_length;
		if (lLen > MAX_INTERRUPT_DATA) return;

		memcpy(lBuf,Urb->transfer_buffer,lLen);

		// is this needed? The OnAir device should always
		// return 8 bytes
		for (li = lLen; li < MAX_INTERRUPT_DATA; li++) 
			lBuf[li] = 0;

		// the OnAir device seems not to be able to signal a
		// pressed button by repeating its code. Keeping a
		// button pressed first sends the real code (e.g. 0C
		// 80 7F 41 BE 00 00 00) and then keeps sending 08 00
		// 00 00 00 00 00 00 as long as the button is pressed
		// (notice that in the real key code 80 = !7F and 41 =
		// !BE is this important? maybe for validation?) maybe
		// 08 00 00 00 00 00 00 00 is the number of presses?
		// who knows ...
		// so lets do the following: if a code != the 08 code
		// arrives, store it to repeat it if necessary for
		// LIRC. If an 08 code follows afterwards, send the
		// old code again to the buffer do this as long as the
		// 08 code is being sent
		// example:
		// Code from Remote          Lirc Buffer
		//  0C 80 7F 41 BE 00 00 00   0C 80 7F 41 BE 00 00 00
		//  08 00 00 00 00 00 00 00   0C 80 7F 41 BE 00 00 00
		//  08 00 00 00 00 00 00 00   0C 80 7F 41 BE 00 00 00
		//  08 00 00 00 00 00 00 00   0C 80 7F 41 BE 00 00 00
		//  08 00 00 00 00 00 00 00   0C 80 7F 41 BE 00 00 00
		//  0C 80 7F 40 BF 00 00 00   0C 80 7F 40 BF 00 00 00
		//  08 00 00 00 00 00 00 00   0C 80 7F 40 BF 00 00 00
		//  08 00 00 00 00 00 00 00   0C 80 7F 40 BF 00 00 00
		//  0C 80 7F 41 BE 00 00 00   0C 80 7F 41 BE 00 00 00
		
		// get the time since the last button press
		do_gettimeofday(&lstv);
		llms = (lstv.tv_sec - lSasemDevice->PressTime.tv_sec) * 1000 + (lstv.tv_usec - lSasemDevice->PressTime.tv_usec) / 1000;

		if (memcmp(lBuf, SasemCode, MAX_INTERRUPT_DATA) == 0) {
			// the repeat code is being sent, so we copy
			// the old code to LIRC
			
			// NOTE: Only if the last code was less than 250ms ago
			// - no one should be able to push another (undetected) button
			//   in that time and then get a false repeat of the previous press
			// - but it is long enough for a genuine repeat
			if ((llms < 250) && (lSasemDevice->CodeSaved != 0)) {
				memcpy(lBuf, &lSasemDevice->LastCode,
				       MAX_INTERRUPT_DATA);
				lSasemDevice->PressTime.tv_sec = lstv.tv_sec; 
				lSasemDevice->PressTime.tv_usec = lstv.tv_usec;
			}
			// there was no old code
			else {
				// Do Nothing!
			}
		}
		else {
			// save the current valid code for repeats
			memcpy(&lSasemDevice->LastCode, lBuf,
			       MAX_INTERRUPT_DATA);
			// set flag to signal a valid code was save;
			// just for safety reasons
			lSasemDevice->CodeSaved = 1;
			lSasemDevice->PressTime.tv_sec = lstv.tv_sec; 
			lSasemDevice->PressTime.tv_usec = lstv.tv_usec;
		}
		
		/* copy 1 code to lirc_buffer */
		lirc_buffer_write_1(lSasemDevice->LircPlugin->rbuf,
			lBuf);
		wake_up(&lSasemDevice->LircPlugin->rbuf->wait_poll);
		break;

	/* unlink */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		err(": urb failed, status %d\n", Urb->status);
#ifdef KERNEL_2_5
		Urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif
		usb_unlink_urb(Urb);
		return;
	}

#ifdef KERNEL_2_5
	/* resubmit urb */
	usb_submit_urb(Urb, SLAB_ATOMIC);
#endif
	dbg(" }\n");
}

/* lirc stuff */

static int UnregisterFromLirc(struct SasemDevice *SasemDevice) {
	struct lirc_plugin *lLircPlugin = SasemDevice->LircPlugin;
	int lDevnum;

	dbg(" {\n");
	lDevnum = SasemDevice->Devnum;
	
	lirc_unregister_plugin(lLircPlugin->minor);
	
	printk(DRIVER_NAME "[%d]: usb remote disconnected\n", lDevnum);
	
	lirc_buffer_free(lLircPlugin->rbuf);
	kfree(lLircPlugin->rbuf);
	kfree(lLircPlugin);
	dbg(" }\n");
	return 0;
}

static int LircSetUseInc(void *Data) {
	struct SasemDevice *lSasemDevice = Data;
	int lDevnum;

	dbg(" {\n");
	if (!lSasemDevice) {
		err(" no context\n");
		return -EIO;
	}
	
	lDevnum = lSasemDevice->Devnum;

	if (!lSasemDevice->Connected) {
		
		/*
			this is the trigger from LIRC to start
			transfering data so the URB is being submitted
		*/

		if (!lSasemDevice->Device)
			return -ENOENT;
		
		/* set USB device in URB */
		lSasemDevice->UrbIn->dev = lSasemDevice->Device;
		
		/* start communication by submitting URB */
#ifndef KERNEL_2_5
		if (usb_submit_urb(lSasemDevice->UrbIn)) {
#else
		if (usb_submit_urb(lSasemDevice->UrbIn, SLAB_ATOMIC)) {
#endif
			err(" URB submit failed\n");
			return -EIO;
		}
		
		/* indicate that URB has been submitted */
		lSasemDevice->Connected = 1;
	}

	dbg(" }\n");
	return 0;
}

static void LircSetUseDec(void *Data) {
	struct SasemDevice *lSasemDevice = Data;
	int lDevnum;
	
	dbg(" {\n");
	if (!lSasemDevice) {
		err(" no context\n");
		return;
	}

	lDevnum = lSasemDevice->Devnum;

	if (lSasemDevice->Connected) {

		/*
			URB has been submitted before so it can be unlinked
		*/

		down(&lSasemDevice->SemLock);
		usb_unlink_urb(lSasemDevice->UrbIn);
		lSasemDevice->Connected = 0;
		up(&lSasemDevice->SemLock);
	}
	dbg(" }\n");
}

/* FS Operations for LCDProc */

static int SasemFSOpen(struct inode *Inode, struct file *File) {
	struct SasemDevice *lSasemDevice;
	int lReturn = 0;
	int lPipe;

	dbg(" {\n");
	if (SasemDevice == NULL) {
		err(" no device\n");
		return -ENODEV;
	}

	lSasemDevice = SasemDevice;
	down(&lSasemDevice->SemLock);

	if (lSasemDevice->Open) {
		// already open
		dbg(": already open\n");

		// return error immediately
		if (File->f_flags & O_NONBLOCK) {
			up(&lSasemDevice->SemLock);
			return -EAGAIN;
		}

		dbg(": open & block\n");
		// wait for release, on global variable
		if ((lReturn = 
			wait_event_interruptible(lSasemDevice->QueueOpen, 
				SasemDevice !=NULL))) {
			up(&lSasemDevice->SemLock);
			return lReturn;
		}
	}

	// indicate status as open
	lSasemDevice->Open=1;

	// handle URB for Out Interface
#ifndef KERNEL_2_5
	lSasemDevice->UrbOut = usb_alloc_urb(0);
#else
	lSasemDevice->UrbOut = usb_alloc_urb(0, GFP_KERNEL);
#endif

/*	lPipe = usb_sndintpipe(lSasemDevice->Device,
			lSasemDevice->DescriptorOut->bEndpointAddress);
*/    
	dbg(": init outbound URB\n");
	lPipe = usb_sndbulkpipe(lSasemDevice->Device,
			lSasemDevice->DescriptorOut->bEndpointAddress);

	usb_fill_int_urb(lSasemDevice->UrbOut, 
			lSasemDevice->Device,
			lPipe, lSasemDevice->BufferOut,
			sizeof(lSasemDevice->BufferOut),
			SasemCallbackOut, lSasemDevice, 
			lSasemDevice->DescriptorOut->bInterval);

	// store pointer to device in file handle
	File->private_data = lSasemDevice;
	up(&lSasemDevice->SemLock);
	dbg(" }\n");
	return 0;
}

static int SasemFSRelease(struct inode *Inode, struct file *File) {
	struct SasemDevice *lpSasemDevice;

	dbg(" {\n");
	// get pointer to device
	lpSasemDevice = (struct SasemDevice *)File->private_data;
	down(&lpSasemDevice->SemLock);

	// is the device open?
	if ((lpSasemDevice) && (lpSasemDevice->Open)) {
		dbg(": check open\n");

		// yes, free URB and set status to closed
		usb_unlink_urb(lpSasemDevice->UrbOut);
		usb_free_urb(lpSasemDevice->UrbOut);
		lpSasemDevice->UrbOut = NULL;
		lpSasemDevice->Open = 0;
	}
	up(&lpSasemDevice->SemLock);

	// wake up any anybody who is waiting for release
	dbg(": wake up\n");
	wake_up_interruptible(&lpSasemDevice->QueueOpen);
	dbg(" }\n");
	return 0;
}

static ssize_t SasemFSWrite(struct file *File, const char *cBuffer,
				size_t Count, loff_t *Pos) {
	struct SasemDevice *lSasemDevice;
	int lCount = 0;
	int lResult;

	dbg(" {\n");
	// sanity check
	lSasemDevice = (struct SasemDevice *)File->private_data;
	dbg(": lock\n");
	down(&lSasemDevice->SemLock);

	dbg(": check device \n");
	if (lSasemDevice->Device == NULL) {
		dbg(": device is null \n");
		up(&lSasemDevice->SemLock);
		return -ENODEV;
	}

	// is device open?
	dbg(": check open \n");
	if (lSasemDevice->Open == 0) {
		dbg(": device not open\n");
		up(&lSasemDevice->SemLock);
		return -EBADF;
	}

	if (Count == 0) {
		up(&lSasemDevice->SemLock);
		return 0;
	}

	if (lSasemDevice->UrbOut->status == -EINPROGRESS) {
		dbg(": status -EINPROGRESS \n");
		up(&lSasemDevice->SemLock);
		return 0;
	}

	if (lSasemDevice->UrbOut->status) {
		err(": status %d \n", lSasemDevice->UrbOut->status);
		up(&lSasemDevice->SemLock);
		return -EAGAIN;
	}

	memset(lSasemDevice->BufferOut, 0, 
			sizeof(lSasemDevice->BufferOut));
	lCount = (Count>MAX_INTERRUPT_DATA)?MAX_INTERRUPT_DATA:Count;
	copy_from_user(lSasemDevice->BufferOut, cBuffer, lCount);

#ifndef KERNEL_2_5
	lResult = usb_submit_urb(lSasemDevice->UrbOut);
#else
	lResult = usb_submit_urb(lSasemDevice->UrbOut, SLAB_ATOMIC);
#endif

	if (lResult) {
		err(": usb_submit_urb failed %d\n", lResult);
		lCount = lResult;
	}
	else {
		// wait for write to finish
		//interruptible_sleep_on(&lSasemDevice->QueueWrite);
		wait_event_interruptible(lSasemDevice->QueueWrite, lSasemDevice->UrbOut->status != -EINPROGRESS);
	}

	up(&lSasemDevice->SemLock);
	dbg(" }\n");
	return lCount;
}

static ssize_t SasemFSRead(struct file *File, char *cBuffer,
			   size_t Count, loff_t *Unused_pos)
{
	dbg(" {}\n");
	// no read support
	return -EINVAL;
}

static int SasemFSIoctl(struct inode *Inode, struct file *File,
			unsigned Cmd, unsigned long lArg)
{
	int l;
	char lBuf[30];
	struct SasemDevice *lSasemDevice;

	dbg(" {\n");
	// sanity check
	lSasemDevice = (struct SasemDevice *)File->private_data;
	if (!lSasemDevice->Device)
	return -ENOLINK;

	switch (Cmd) {

	case IOCTL_GET_HARD_VERSION:
		// return device information
		dbg(": IOCTL_GET_HARD_VERSION\n");
		l = (lSasemDevice->Device)->descriptor.bcdDevice;
		sprintf(lBuf,"%1d%1d.%1d%1d",
				(l & 0xF000)>>12,(l & 0xF00)>>8,
				(l & 0xF0)>>4,(l & 0xF));
		if (copy_to_user((void *)lArg, lBuf, strlen(lBuf))!=0)
			return -EFAULT;
		break;

	case IOCTL_GET_DRV_VERSION:
		// return driver information
		// sprintf(lBuf,"USBLCD Driver Version 1.03");
		dbg(": IOCTL_GET_DRV_VERSION\n");
		sprintf(lBuf,DRIVER_DESC);
		if (copy_to_user((void *)lArg, lBuf, strlen(lBuf))!=0)
			return -EFAULT;
		break;  

	default:
		dbg(": unknown command\n");
		// command not supported
		return -ENOIOCTLCMD;
		break;
	}
	dbg(" }\n");
	return 0;
}

static unsigned SasemFSPoll(struct file *File, poll_table *Wait) {

	dbg(" {}\n");
	// no poll support
	return -EINVAL;
}

#ifndef KERNEL_2_5
static void SasemCallbackOut(struct urb *Urb)
#else
static void SasemCallbackOut(struct urb *Urb, struct pt_regs *regs)
#endif
{
	struct SasemDevice *lSasemDevice;

	dbg(" {\n");

	lSasemDevice = Urb->context;  

	// sanity check
	if (Urb->status != 0) {
		err(": urb failed, status %d\n", Urb->status);
	}
	if (waitqueue_active(&lSasemDevice->QueueWrite)) {
		dbg(": wake up \n");
		lSasemDevice->UrbOut->dev = lSasemDevice->Device;
		wake_up(&lSasemDevice->QueueWrite);
	}
	dbg(" }\n");
}
