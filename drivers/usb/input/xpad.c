/*
 * USB XBOX HID Gamecontroller - v0.0.3
 *
 * Copyright (c) 2002 Marko Friedemann <mfr@bmx-chemnitz.de>
 *
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *
 * This driver is based on:
 *  - information from      http://euc.jp/periphs/xbox-controller.ja.html
 *  - the iForce driver     drivers/char/joystick/iforce.c
 *  - the skeleton-driver   drivers/usb/usb-skeleton.c
 *
 * Thanks to:
 *  - ITO Takayuki for providing xpad information on his website
 *  - Vojtech Pavlik      - iforce driver / input subsystem
 *  - Greg Kroah-Hartman  - usb-skeleton driver
 *
 * TODO:
 *	- get the black button to work
 *      - fine tune axes
 *      - fix "analog" buttons
 *	- get rumble working
 *
 * History:
 *
 * 2002-06-27 - 0.0.1 - first version, just said "XBOX HID controller"
 *
 * 2002-07-02 - 0.0.2 - basic working version
 *      all axes and 9 of the 10 buttons work (german InterAct device)
 *      the black button does not work
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/sched.h>
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
#include <linux/devfs_fs_kernel.h>
#include <linux/usb.h>

#define DRIVER_VERSION "v0.0.3"
#define DRIVER_AUTHOR "Marko Friedemann <mfr@bmx-chemnitz.de>"
#define DRIVER_DESC "X-Box pad driver"

#define XPAD_PKT_LEN 32

static struct xpad_device {
	u16 idVendor;
	u16 idProduct;
	char *name;
} xpad_device[] = {
	{ 0x045e, 0x0202, "Microsoft X-Box pad (US)" },
	{ 0x045e, 0x0285, "Microsoft X-Box pad (Japan)" },
	{ 0x05fd, 0x107a, "InterAct X-Box pad (Germany)" },
	{ 0x0000, 0x0000, "X-Box pad" }
};

static signed short xpad_btn[] = {
	BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z,       /* 6 "analog" buttons */
	BTN_START, BTN_BACK, BTN_THUMBL, BTN_THUMBR,    /* start/back + stick press */
	-1                                              /* terminating entry */
};

static signed short xpad_abs[] = {
	ABS_X, ABS_Y,		/* left stick */
	ABS_RX, ABS_RY,		/* right stick */
	ABS_Z, ABS_RZ,		/* triggers left/right */
	ABS_HAT0X, ABS_HAT0Y,	/* dpad */
	-1                          /* terminating entry */
};

static struct {
	__s32 x;
	__s32 y;
} xpad_hat_to_axis[] = { {0, 0}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1} };

static struct usb_device_id xpad_table [] = {
	{ USB_INTERFACE_INFO('X', 'B', 0) },	/* X-Box USB-IF not approved class */
	{ }
};

MODULE_DEVICE_TABLE (usb, xpad_table);

struct usb_xpad {
	struct input_dev dev;			/* input device interface */
	struct usb_device *udev;		/* usb device */
	
	struct urb *irq_in;			/* urb for interrupt in report */
	unsigned char idata[XPAD_PKT_LEN];	/* input data */
	
	char phys[65];				/* physical device path */
	int open_count;				/* how many times has this been opened */
};

/*
 *      xpad_process_packet
 *
 *      Completes a request by converting the data into events for the input subsystem.
 *      
 *      The used report descriptor given below was taken from ITO Takayukis website:
 *          http://euc.jp/periphs/xbox-controller.ja.html
 *
 * ----------------------------------------------------------------------------------------------------------------
 * |  padding | byte-cnt | dpad sb12 | reserved |   bt A   |   bt B   |   bt X   |   bt Y   | bt black | bt white |
 * | 01234567 | 01234567 | 0123 4567 | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 |
 * |    0     |    1     |     2     |    3     |    4     |    5     |    6     |    7     |    8     |    9     |
 * ----------------------------------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------------------------------
 * |  trig L  |  trig R  |     left stick X    |     left stick Y    |     right stick X   |     right stick Y   |
 * | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 | 01234567 |
 * |    10    |    11    |    12    |    13    |    14    |    15    |    16    |    17    |    18    |    19    |
 * ---------------------------------------------------------------------------------------------------------------
 */

static void xpad_process_packet(struct usb_xpad *xpad, u16 cmd, unsigned char *data)
{
	struct input_dev *dev = &xpad->dev;
	
	/* left stick */
	input_report_abs(dev, ABS_X, (__s16) (((__s16)data[13] << 8) | data[12]));
	input_report_abs(dev, ABS_Y, (__s16) (((__s16)data[15] << 8) | data[14]));
	
	/* right stick */
	input_report_abs(dev, ABS_RX, (__s16) (((__s16)data[17] << 8) | data[16]));
	input_report_abs(dev, ABS_RY, (__s16) (((__s16)data[19] << 8) | data[18]));
	
	/* triggers left/right */
	input_report_abs(dev, ABS_Z, data[10]);
	input_report_abs(dev, ABS_RZ, data[11]);
	
	/* digital pad */
	input_report_abs(dev, ABS_HAT0X, xpad_hat_to_axis[data[2] & 0x0f].x);
	input_report_abs(dev, ABS_HAT0Y, xpad_hat_to_axis[data[2] & 0x0f].y);
	
	/* start/back buttons and stick press left/right */
	input_report_key(dev, BTN_START, (data[2] & 0x10) >> 4);
	input_report_key(dev, BTN_BACK, (data[2] & 0x20) >> 5);
	input_report_key(dev, BTN_THUMBL, (data[2] & 0x40) >> 6);
	input_report_key(dev, BTN_THUMBR, data[2] >> 7);
	
	/* "analog" buttons A, B, X, Y */
	input_report_key(dev, BTN_A, data[4]);
	input_report_key(dev, BTN_B, data[5]);
	input_report_key(dev, BTN_X, data[6]);
	input_report_key(dev, BTN_Y, data[7]);
	
	/* "analog" buttons black, white */
	input_report_key(dev, BTN_C, data[8]);
	input_report_key(dev, BTN_Z, data[9]);
}

static void xpad_irq_in(struct urb *urb)
{
	struct usb_xpad *xpad = urb->context;
	
	if (urb->status)
		return;
	
	xpad_process_packet(xpad, 0, xpad->idata);
}

static int xpad_open (struct input_dev *dev)
{
	struct usb_xpad *xpad = dev->private;
	
	if (xpad->open_count++)
		return 0;
	
	xpad->irq_in->dev = xpad->udev;
	if (usb_submit_urb(xpad->irq_in, GFP_KERNEL))
		return -EIO;
	
	return 0;
}

static void xpad_close (struct input_dev *dev)
{
	struct usb_xpad *xpad = dev->private;
	
	if (!--xpad->open_count)
		usb_unlink_urb(xpad->irq_in);
}

static void * xpad_probe(struct usb_device *udev, unsigned int ifnum, const struct usb_device_id *id)
{
	struct usb_xpad *xpad = NULL;
	struct usb_endpoint_descriptor *ep_irq_in;
	char path[64];
	int i;
	
	for (i = 0; xpad_device[i].idVendor; i++) {
		if ((udev->descriptor.idVendor == xpad_device[i].idVendor) &&
		    (udev->descriptor.idProduct == xpad_device[i].idProduct))
			break;
	}
	
	if ((xpad = kmalloc (sizeof(struct usb_xpad), GFP_KERNEL)) == NULL) {
		err("cannot allocate memory for new pad");
		return NULL;
	}
	memset(xpad, 0, sizeof(struct usb_xpad));

	xpad->irq_in = usb_alloc_urb(0, GFP_KERNEL);
        if (!xpad->irq_in) {
		err("cannot allocate memory for new pad irq urb");
                kfree(xpad);
                return NULL;
        }
	
	ep_irq_in = udev->actconfig->interface[ifnum].altsetting[0].endpoint + 0;
	
	FILL_INT_URB(xpad->irq_in, udev, usb_rcvintpipe(udev, ep_irq_in->bEndpointAddress),
	xpad->idata, XPAD_PKT_LEN, xpad_irq_in, xpad, ep_irq_in->bInterval);
	
	xpad->udev = udev;

	xpad->dev.idbus = BUS_USB;
	xpad->dev.idvendor = udev->descriptor.idVendor;
	xpad->dev.idproduct = udev->descriptor.idProduct;
	xpad->dev.idversion = udev->descriptor.bcdDevice;
	xpad->dev.private = xpad;
	xpad->dev.name = xpad_device[i].name;
	xpad->dev.phys = xpad->phys;
	xpad->dev.open = xpad_open;
	xpad->dev.close = xpad_close;

	usb_make_path(udev, path, 64);
	snprintf(xpad->phys, 64,  "%s/input0", path);
	
	xpad->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

	for (i = 0; xpad_btn[i] >= 0; i++)
	set_bit(xpad_btn[i], xpad->dev.keybit);
	
	for (i = 0; xpad_abs[i] >= 0; i++) {

		signed short t = xpad_abs[i];

		set_bit(t, xpad->dev.absbit);
		
		switch (t) {
			case ABS_X:
			case ABS_Y:
			case ABS_RX:
			case ABS_RY:
				xpad->dev.absmax[t] =  32767;
				xpad->dev.absmin[t] = -32768;
				xpad->dev.absflat[t] = 128;
				xpad->dev.absfuzz[t] = 16;
				break;
			case ABS_Z:
			case ABS_RZ:
				xpad->dev.absmax[t] = 255;
				xpad->dev.absmin[t] = 0;
				break;
			case ABS_HAT0X:
			case ABS_HAT0Y:
				xpad->dev.absmax[t] =  1;
				xpad->dev.absmin[t] = -1;
				break;
		}
	}
	
	input_register_device(&xpad->dev);
	
	printk(KERN_INFO "input: %s on %s", xpad->dev.name, path);
	
	return xpad;
}

static void xpad_disconnect(struct usb_device *udev, void *ptr)
{
	struct usb_xpad *xpad = ptr;
	
	usb_unlink_urb(xpad->irq_in);
	input_unregister_device(&xpad->dev);
	usb_free_urb(xpad->irq_in);
	kfree(xpad);
}

static struct usb_driver xpad_driver = {
	name:       "xpad",
	probe:      xpad_probe,
	disconnect: xpad_disconnect,
	id_table:   xpad_table,
};

static int __init usb_xpad_init(void)
{
	usb_register(&xpad_driver);
	info(DRIVER_DESC ":" DRIVER_VERSION);
	return 0;
}

static void __exit usb_xpad_exit(void)
{
	usb_deregister(&xpad_driver);
}

module_init(usb_xpad_init);
module_exit(usb_xpad_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
