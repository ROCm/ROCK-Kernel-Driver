/* lirc_sasem.c - USB remote support for LIRC
 * Version 0.1  [beta status]
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
 * 2004/06/13	-	1st version
 *
 * TODO
 *	- keypresses seem to be rather sluggish sometimes; check
 *	  intervall and timing
 *	- simulate USBLCD device to work with LCDProc
 *	- include fs operations
 *	- analyse LCD command set
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

#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)
#error "**********************************************************"
#error " Sorry, this driver is not yet available for 2.6 kernels. "
#error "**********************************************************"
#endif

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
#include "lirc_sasem.h"

#include "kcompat.h"
#include "lirc.h"
#include "lirc_dev.h"

static int debug = 0;

static t_usb_device_id s_sasemID [] = {
	{ USB_DEVICE(0x11ba, 0x0101) },
	{ }
};

static t_usb_driver s_SasemDriver =
{
	owner:			THIS_MODULE,
	name:			"Sasem",
	probe:			s_sasemProbe,
	disconnect:		s_sasemDisconnect,
	minor:			SASEM_MINOR,
	id_table:		s_sasemID,
};

static int __init s_sasemInit (void)
{
	printk(BANNER);

	if (usb_register(&s_SasemDriver)) {
		printk("USB registration failed");
		return -ENOSYS;
	}

	return 0;
}

static void __exit s_sasemExit (void)
{
	usb_deregister (&s_SasemDriver);
}

module_init (s_sasemInit);
module_exit (s_sasemExit);

static void * s_sasemProbe(t_usb_device *p_dev, unsigned p_iInterfaceNum,
			   const t_usb_device_id *p_id)
{
	t_sasemDevice *l_sasemDevice = NULL;
	t_usb_endpoint_descriptor *l_endpoint;
	t_usb_interface_descriptor *l_currentInterfaceDescriptor;
	int l_iPipe;
	int l_iDevnum;
	t_lirc_plugin *l_lircPlugin = NULL;
	t_lirc_buffer *l_lircBuffer = NULL;
	int l_iLircMinor = -1;
	int l_iMemFailure;
	char l_cBuf[63], l_cName[128]="";

	if (debug) printk("onair probe\n");
	
	l_currentInterfaceDescriptor = p_dev->actconfig->interface->
		altsetting;
	l_endpoint = l_currentInterfaceDescriptor->endpoint;
	
	if (!(l_endpoint->bEndpointAddress & 0x80) ||
		((l_endpoint->bmAttributes & 3) != 0x03)) {
		printk("OnAir config endpoint error");
		return NULL;
	}

	l_iDevnum = p_dev->devnum;

	l_iMemFailure = 0;
	if (!(l_sasemDevice = kmalloc(sizeof(t_sasemDevice), GFP_KERNEL))) {
		printk("kmalloc(sizeof(t_sasemDevice), GFP_KERNEL)) failed");
		l_iMemFailure = 1;
	}
	else {
		memset(l_sasemDevice, 0, sizeof(t_sasemDevice));
		if (!(l_lircPlugin = kmalloc(sizeof(t_lirc_plugin), GFP_KERNEL))) {
			printk("kmalloc(sizeof(t_lirc_plugin), GFP_KERNEL)) failed");
			l_iMemFailure = 2;
		}
		else if (!(l_lircBuffer = kmalloc(sizeof(t_lirc_buffer), GFP_KERNEL))) {
			printk("kmalloc(sizeof(t_lirc_buffer), GFP_KERNEL)) failed");
			l_iMemFailure = 3;
		}
		else if (lirc_buffer_init(l_lircBuffer, MAX_INTERRUPT_DATA, 4)) {
			printk("lirc_buffer_init failed");
			l_iMemFailure = 4;
		}
		else if (!(l_sasemDevice->m_urbIn = usb_alloc_urb(0))) {
			printk("usb_alloc_urb(0) failed");
			l_iMemFailure = 5;
		} else {

			memset(l_lircPlugin, 0, sizeof(t_lirc_plugin));
			strcpy(l_lircPlugin->name, DRIVER_NAME " ");
			l_lircPlugin->minor = -1;
			l_lircPlugin->code_length = MAX_INTERRUPT_DATA*8;
			l_lircPlugin->features = LIRC_CAN_REC_LIRCCODE;
			l_lircPlugin->data = l_sasemDevice;
			
			l_lircPlugin->rbuf = l_lircBuffer;
			l_lircPlugin->set_use_inc = &s_lirc_set_use_inc;
			l_lircPlugin->set_use_dec = &s_lirc_set_use_dec;

			if ((l_iLircMinor = lirc_register_plugin(l_lircPlugin)) < 0) {
				printk("lirc_register_plugin(l_lircPlugin)) failed");
				l_iMemFailure = 9;
			}
		}
	}
	switch (l_iMemFailure) {
	case 9:
		usb_free_urb(l_sasemDevice->m_urbIn);
	case 5:
	case 4:
		kfree(l_lircBuffer);
	case 3:
		kfree(l_lircPlugin);
	case 2:
		kfree(l_sasemDevice);
	case 1:
		return NULL;
	}
	
	l_lircPlugin->minor = l_iLircMinor;	
	
	init_MUTEX(&l_sasemDevice->m_semLock);
	down_interruptible(&l_sasemDevice->m_semLock);
	l_sasemDevice->m_descriptorIn = l_endpoint;
	l_sasemDevice->m_device = p_dev;
	l_sasemDevice->m_lircPlugin = l_lircPlugin;
	up(&l_sasemDevice->m_semLock);

	l_iPipe = usb_rcvintpipe(l_sasemDevice->m_device,
				 l_sasemDevice->m_descriptorIn->
				 bEndpointAddress);
	
	usb_fill_int_urb(l_sasemDevice->m_urbIn, l_sasemDevice->m_device,
			 l_iPipe, l_sasemDevice->m_cBufferIn,
			 sizeof(l_sasemDevice->m_cBufferIn),
			 s_sasemCallbackIn, l_sasemDevice, 
			 l_sasemDevice->m_descriptorIn->bInterval);

	if (p_dev->descriptor.iManufacturer &&
	    usb_string(p_dev, p_dev->descriptor.iManufacturer, l_cBuf, 63) > 0)
	{
		strncpy(l_cName, l_cBuf, 128);
	}
	if (p_dev->descriptor.iProduct &&
	    usb_string(p_dev, p_dev->descriptor.iProduct, l_cBuf, 63) > 0)
	{
		snprintf(l_cName, 128, "%s %s", l_cName, l_cBuf);
	}
	printk(DRIVER_NAME "[%d]: %s on usb%d\n", l_iDevnum, l_cName,
	       p_dev->bus->busnum);

	return l_sasemDevice;
}


static void s_sasemDisconnect(t_usb_device *p_dev, void *p_ptr) {
	t_sasemDevice *l_sasemDevice = p_ptr;
	if (debug) printk("s_sasemDisconnect\n");

	down_interruptible(&l_sasemDevice->m_semLock);
	usb_unlink_urb(l_sasemDevice->m_urbIn);
	usb_free_urb(l_sasemDevice->m_urbIn);
	s_unregister_from_lirc(l_sasemDevice);
	up(&l_sasemDevice->m_semLock);
	kfree (l_sasemDevice);
}

static void s_sasemCallbackIn(t_urb *p_urb)
{
	t_sasemDevice *l_sasemDevice;
	int l_iDevnum;
	int l_iLen;
	char l_cBuf[MAX_INTERRUPT_DATA];
	int i;

	if (debug) printk("s_sasemCallbackIn\n");
	
	if (!p_urb)
	{
		return;
	}

	if (!(l_sasemDevice = p_urb->context)) {
		usb_unlink_urb(p_urb);
		return;
	}

	l_iDevnum = l_sasemDevice->m_iDevnum;
	if (debug) {
		printk(DRIVER_NAME "[%d]: data received (length %d)\n",
		       l_iDevnum, p_urb->actual_length);
		printk(DRIVER_NAME 
		       " intr_callback called %x %x %x %x %x %x %x %x\n", 
		       l_sasemDevice->m_cBufferIn[0],
		       l_sasemDevice->m_cBufferIn[1],
		       l_sasemDevice->m_cBufferIn[2],
		       l_sasemDevice->m_cBufferIn[3],
		       l_sasemDevice->m_cBufferIn[4],
		       l_sasemDevice->m_cBufferIn[5],
		       l_sasemDevice->m_cBufferIn[6],
		       l_sasemDevice->m_cBufferIn[7]);
	}

	switch (p_urb->status) {

	/* success */
	case 0:
		l_iLen = p_urb->actual_length;
		if (l_iLen > MAX_INTERRUPT_DATA) return;

		memcpy(l_cBuf,p_urb->transfer_buffer,l_iLen);

		// is this needed? The OnAir device should always
		// return 8 bytes
		for (i = l_iLen; i < MAX_INTERRUPT_DATA; i++) l_cBuf[i] = 0;

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
		
		if (memcmp(l_cBuf, sc_cSasemCode, MAX_INTERRUPT_DATA) == 0) {
			// the repeat code is being sent, so we copy
			// the old code to LIRC
			if (l_sasemDevice->m_iCodeSaved != 0) {
				memcpy(l_cBuf, &l_sasemDevice->m_cLastCode,
				       MAX_INTERRUPT_DATA);
			}
			// there was no old code so what to do?
			else {
				// TODO
			}
		}
		else
		{
			// save the current valid code for repeats
			memcpy(&l_sasemDevice->m_cLastCode, l_cBuf,
			       MAX_INTERRUPT_DATA);
			// set flag to signal a valid code was save;
			// just for safety reasons
			l_sasemDevice->m_iCodeSaved = 1;
		}
		
		/* copy 1 code to lirc_buffer */
		lirc_buffer_write_1(l_sasemDevice->m_lircPlugin->rbuf,
				    l_cBuf);
		wake_up(&l_sasemDevice->m_lircPlugin->rbuf->wait_poll);
		break;

	/* unlink */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(p_urb);
		return;
	}

	/* resubmit urb */
	usb_submit_urb(p_urb);
}

static int s_unregister_from_lirc(t_sasemDevice *p_sasemDevice) {
	t_lirc_plugin *l_lircPlugin = p_sasemDevice->m_lircPlugin;
	int l_iDevnum;
	int l_iReturn;

	l_iDevnum = p_sasemDevice->m_iDevnum;
	if (debug) printk(DRIVER_NAME "[%d]: unregister from lirc called\n",
			  l_iDevnum);
	
	if ((l_iReturn = lirc_unregister_plugin(l_lircPlugin->minor)) > 0) {
		printk(DRIVER_NAME "[%d]: error in lirc_unregister minor: %d\n"
		       "Trying again...\n", l_iDevnum, l_lircPlugin->minor);
		if (l_iReturn == -EBUSY) {
			printk(DRIVER_NAME "[%d]: device is opened, "
			       "will unregister on close\n", l_iDevnum);
			return -EAGAIN;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);

		if ((l_iReturn = lirc_unregister_plugin(l_lircPlugin->minor)) > 0) {
			printk(DRIVER_NAME "[%d]: lirc_unregister failed\n",
			       l_iDevnum);
		}
	}
	
	if (l_iReturn != 0) {
		printk(DRIVER_NAME "[%d]: didn't free resources\n",
		       l_iDevnum);
		return -EAGAIN;
	}
	
	printk(DRIVER_NAME "[%d]: usb remote disconnected\n", l_iDevnum);
	
	lirc_buffer_free(l_lircPlugin->rbuf);
	kfree(l_lircPlugin->rbuf);
	kfree(l_lircPlugin);
	return 0;
}

static int s_lirc_set_use_inc(void *p_data)
{
	t_sasemDevice *l_sasemDevice = p_data;
	int l_iDevnum;

	if (!l_sasemDevice) {
		printk(DRIVER_NAME "[?]: s_lirc_set_use_inc called with no context\n");
		return -EIO;
	}
	
	l_iDevnum = l_sasemDevice->m_iDevnum;
	if (debug) printk(DRIVER_NAME "[%d]: s_lirc_set_use_inc\n", 
			  l_iDevnum);

	if (!l_sasemDevice->m_iConnected) {
		
		/*
			this is the trigger from LIRC to start
			transfering data so the URB is being submitted
		*/

		if (!l_sasemDevice->m_device)
			return -ENOENT;
		
		/* set USB device in URB */
		l_sasemDevice->m_urbIn->dev = l_sasemDevice->m_device;
		
		/* start communication by submitting URB */
		if (usb_submit_urb(l_sasemDevice->m_urbIn)) {
			printk(DRIVER_NAME "[%d]: open result = -EIO error "
				"submitting urb\n", l_iDevnum);
			return -EIO;
		}
		
		/* indicate that URB has been submitted */
		l_sasemDevice->m_iConnected = 1;
	}

	return 0;
}

static void s_lirc_set_use_dec(void *p_data) {
	t_sasemDevice *l_sasemDevice = p_data;
	int l_iDevnum;
	
	if (!l_sasemDevice) {
		printk(DRIVER_NAME "[?]: s_lirc_set_use_dec called with no context\n");
		return;
	}

	l_iDevnum = l_sasemDevice->m_iDevnum;
	if (debug) printk(DRIVER_NAME "[%d]: s_lirc_set_use_dec\n", 
			  l_iDevnum);

	if (l_sasemDevice->m_iConnected) {

		/*
			URB has been submitted before so it can be unlinked
		*/

		down_interruptible(&l_sasemDevice->m_semLock);
		usb_unlink_urb(l_sasemDevice->m_urbIn);
		l_sasemDevice->m_iConnected = 0;
		up(&l_sasemDevice->m_semLock);
	}
}

MODULE_DESCRIPTION("Infrared receiver driver for Dign HV5 HTPC and Sasem OnAir Remocon-V");
MODULE_AUTHOR("Oliver Stabel <oliver.stabel@gmx.de>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE (usb, s_sasemID);

module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable debugging messages");

EXPORT_NO_SYMBOLS;
