/*
 * $Id: wacom.c,v 1.14 2000/11/23 09:34:32 vojtech Exp $
 *
 *  Copyright (c) 2000 Vojtech Pavlik		<vojtech@suse.cz>
 *  Copyright (c) 2000 Andreas Bach Aaen	<abach@stofanet.dk>
 *  Copyright (c) 2000 Clifford Wolf		<clifford@clifford.at>
 *  Copyright (c) 2000 Sam Mosel		<sam.mosel@computer.org>
 *  Copyright (c) 2000 James E. Blair		<corvus@gnu.org>
 *  Copyright (c) 2000 Daniel Egger		<egger@suse.de>
 *
 *  USB Wacom Graphire and Wacom Intuos tablet support
 *
 *  Sponsored by SuSE
 *
 *  ChangeLog:
 *      v0.1 (vp)  - Initial release
 *      v0.2 (aba) - Support for all buttons / combinations
 *      v0.3 (vp)  - Support for Intuos added
 *	v0.4 (sm)  - Support for more Intuos models, menustrip
 *			relative mode, proximity.
 *	v0.5 (vp)  - Big cleanup, nifty features removed,
 * 			they belong in userspace
 *	v1.8 (vp)  - Submit URB only when operating, moved to CVS,
 *			use input_report_key instead of report_btn and
 *			other cleanups
 *	v1.11 (vp) - Add URB ->dev setting for new kernels
 *	v1.11 (jb) - Add support for the 4D Mouse & Lens
 *	v1.12 (de) - Add support for two more inking pen IDs
 *	v1.14 (vp) - Use new USB device id probing scheme.
 *		     Fix Wacom Graphire mouse wheel
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
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("USB Wacom Graphire and Wacom Intuos tablet driver");

/*
 * Wacom Graphire packet:
 *
 * byte 0: report ID (2)
 * byte 1: bit7		pointer in range
 *	   bit5-6	pointer type 0 - pen, 1 - rubber, 2 - mouse
 *	   bit4		1 ?
 *	   bit3		0 ?
 *	   bit2		mouse middle button / pen button2
 *	   bit1		mouse right button / pen button1
 *	   bit0		mouse left button / touch
 * byte 2: X low bits
 * byte 3: X high bits
 * byte 4: Y low bits
 * byte 5: Y high bits
 * byte 6: pen pressure low bits / mouse wheel
 * byte 7: pen presure high bits / mouse distance
 *
 * There are also two single-byte feature reports (2 and 3).
 *
 * Wacom Intuos status packet:
 *
 * byte 0: report ID (2)
 * byte 1: bit7		1 - sync bit
 *	   bit6		pointer in range
 *	   bit5		pointer type report
 *	   bit4		0 ?
 *	   bit3		mouse packet type
 *	   bit2		pen button2
 *	   bit1		pen button1
 *	   bit0		0 ?
 * byte 2: X high bits
 * byte 3: X low bits
 * byte 4: Y high bits
 * byte 5: Y low bits
 *
 * Pen packet:
 *
 * byte 6: bits 0-7: pressure	(bits 2-9)
 * byte 7: bits 6-7: pressure	(bits 0-1)
 * byte 7: bits 0-5: X tilt	(bits 1-6)
 * byte 8: bit    7: X tilt	(bit  0)
 * byte 8: bits 0-6: Y tilt	(bits 0-6)
 * byte 9: bits 4-7: distance
 *
 * Mouse packet type 0:
 *
 * byte 6: bits 0-7: wheel	(bits 2-9)
 * byte 7: bits 6-7: wheel	(bits 0-1)
 * byte 7: bits 0-5: 0
 * byte 8: bits 6-7: 0
 * byte 8: bit    5: left extra button
 * byte 8: bit    4: right extra button
 * byte 8: bit    3: wheel      (sign)
 * byte 8: bit    2: right button
 * byte 8: bit    1: middle button
 * byte 8: bit    0: left button
 * byte 9: bits 4-7: distance
 *
 * Mouse packet type 1:
 *
 * byte 6: bits 0-7: rotation	(bits 2-9)
 * byte 7: bits 6-7: rotation	(bits 0-1)
 * byte 7: bit    5: rotation	(sign)
 * byte 7: bits 0-4: 0
 * byte 8: bits 0-7: 0
 * byte 9: bits 4-7: distance
 */

#define USB_VENDOR_ID_WACOM	0x056a

struct wacom_features {
	char *name;
	int pktlen;
	int x_max;
	int y_max;
	int pressure_max;
	int distance_max;
	void (*irq)(struct urb *urb);
	unsigned long evbit;
	unsigned long absbit;
	unsigned long relbit;
	unsigned long btnbit;
	unsigned long digibit;
};

struct wacom {
	signed char data[10];
	struct input_dev dev;
	struct usb_device *usbdev;
	struct urb irq;
	struct wacom_features *features;
	int tool;
	int open;
};

static void wacom_graphire_irq(struct urb *urb)
{
	struct wacom *wacom = urb->context;
	unsigned char *data = wacom->data;
	struct input_dev *dev = &wacom->dev;

	if (urb->status) return;

	if (data[0] != 2)
		dbg("received unknown report #%d", data[0]);

	if ( data[1] & 0x80 ) {
		input_report_abs(dev, ABS_X, data[2] | ((__u32)data[3] << 8));
		input_report_abs(dev, ABS_Y, data[4] | ((__u32)data[5] << 8));
	}

	switch ((data[1] >> 5) & 3) {

		case 0:	/* Pen */
			input_report_key(dev, BTN_TOOL_PEN, data[1] & 0x80);
			break;

		case 1: /* Rubber */
			input_report_key(dev, BTN_TOOL_RUBBER, data[1] & 0x80);
			break;

		case 2: /* Mouse */
			input_report_key(dev, BTN_TOOL_MOUSE, data[7] > 24);
			input_report_key(dev, BTN_LEFT, data[1] & 0x01);
			input_report_key(dev, BTN_RIGHT, data[1] & 0x02);
			input_report_key(dev, BTN_MIDDLE, data[1] & 0x04);
			input_report_abs(dev, ABS_DISTANCE, data[7]);
			input_report_rel(dev, REL_WHEEL, (signed char) data[6]);
			return;
	}

	input_report_abs(dev, ABS_PRESSURE, data[6] | ((__u32)data[7] << 8));

	input_report_key(dev, BTN_TOUCH, data[1] & 0x01);
	input_report_key(dev, BTN_STYLUS, data[1] & 0x02);
	input_report_key(dev, BTN_STYLUS2, data[1] & 0x04);
}

static void wacom_intuos_irq(struct urb *urb)
{
	struct wacom *wacom = urb->context;
	unsigned char *data = wacom->data;
	struct input_dev *dev = &wacom->dev;
	unsigned int t;

	if (urb->status) return;

	if (data[0] != 2)
		dbg("received unknown report #%d", data[0]);

	if (((data[1] >> 5) & 0x3) == 0x2) {				/* Enter report */

		switch (((__u32)data[2] << 4) | (data[3] >> 4)) {
			case 0x832:
			case 0x812:
			case 0x012: wacom->tool = BTN_TOOL_PENCIL;	break;	/* Inking pen */
			case 0x822:
			case 0x022: wacom->tool = BTN_TOOL_PEN;		break;	/* Pen */
			case 0x032: wacom->tool = BTN_TOOL_BRUSH;	break;	/* Stroke pen */
			case 0x094: wacom->tool = BTN_TOOL_MOUSE;	break;	/* Mouse 4D */
			case 0x096: wacom->tool = BTN_TOOL_LENS;	break;	/* Lens cursor */
			case 0x82a:
			case 0x0fa: wacom->tool = BTN_TOOL_RUBBER;	break;	/* Eraser */
			case 0x112: wacom->tool = BTN_TOOL_AIRBRUSH;	break;	/* Airbrush */
			default:    wacom->tool = BTN_TOOL_PEN;		break;	/* Unknown tool */
		}	
		input_report_key(dev, wacom->tool, 1);
		return;
	}

	if ((data[1] | data[2] | data[3] | data[4] | data[5] |
	     data[6] | data[7] | data[8] | data[9]) == 0x80) {		/* Exit report */
		input_report_key(dev, wacom->tool, 0);
		return;
	}

	input_report_abs(dev, ABS_X, ((__u32)data[2] << 8) | data[3]);
	input_report_abs(dev, ABS_Y, ((__u32)data[4] << 8) | data[5]);
	input_report_abs(dev, ABS_DISTANCE, data[9] >> 4);

	switch (wacom->tool) {

		case BTN_TOOL_PENCIL:
		case BTN_TOOL_PEN:
		case BTN_TOOL_BRUSH:
		case BTN_TOOL_RUBBER:
		case BTN_TOOL_AIRBRUSH:

			input_report_abs(dev, ABS_PRESSURE, t = ((__u32)data[6] << 2) | ((data[7] >> 6) & 3));
			input_report_abs(dev, ABS_TILT_X, ((data[7] << 1) & 0x7e) | (data[8] >> 7));
			input_report_abs(dev, ABS_TILT_Y, data[8] & 0x7f);
			input_report_key(dev, BTN_STYLUS, data[1] & 2);
			input_report_key(dev, BTN_STYLUS2, data[1] & 4);
			input_report_key(dev, BTN_TOUCH, t > 10);
			break;

		case BTN_TOOL_MOUSE:
		case BTN_TOOL_LENS:

			if (data[1] & 0x02) {			/* Rotation packet */
				input_report_abs(dev, ABS_RZ, (data[7] & 0x20) ?
					((__u32)data[6] << 2) | ((data[7] >> 6) & 3):
					(-(((__u32)data[6] << 2) | ((data[7] >> 6) & 3))) - 1);
				break;
			}

			input_report_key(dev, BTN_LEFT,   data[8] & 0x01);
			input_report_key(dev, BTN_MIDDLE, data[8] & 0x02);
			input_report_key(dev, BTN_RIGHT,  data[8] & 0x04);
			input_report_key(dev, BTN_SIDE,   data[8] & 0x20);
			input_report_key(dev, BTN_EXTRA,  data[8] & 0x10);
			input_report_abs(dev, ABS_THROTTLE,  (data[8] & 0x08) ?
				((__u32)data[6] << 2) | ((data[7] >> 6) & 3) :
				-((__u32)data[6] << 2) | ((data[7] >> 6) & 3));
			break;
	  }
}

#define WACOM_INTUOS_TOOLS	(BIT(BTN_TOOL_BRUSH) | BIT(BTN_TOOL_PENCIL) | BIT(BTN_TOOL_AIRBRUSH) | BIT(BTN_TOOL_LENS))
#define WACOM_INTUOS_BUTTONS	(BIT(BTN_SIDE) | BIT(BTN_EXTRA))
#define WACOM_INTUOS_ABS	(BIT(ABS_TILT_X) | BIT(ABS_TILT_Y) | BIT(ABS_RZ) | BIT(ABS_THROTTLE))

struct wacom_features wacom_features[] = {
	{ "Wacom Graphire",      8, 10206,  7422,  511, 32, wacom_graphire_irq,
		BIT(EV_REL), 0, REL_WHEEL, 0 },
	{ "Wacom Intuos 4x5",   10, 12700, 10360, 1023, 15, wacom_intuos_irq,
		0, WACOM_INTUOS_ABS, 0, WACOM_INTUOS_BUTTONS, WACOM_INTUOS_TOOLS },
	{ "Wacom Intuos 6x8",   10, 20320, 15040, 1023, 15, wacom_intuos_irq,
		0, WACOM_INTUOS_ABS, 0, WACOM_INTUOS_BUTTONS, WACOM_INTUOS_TOOLS },
	{ "Wacom Intuos 9x12",  10, 30480, 23060, 1023, 15, wacom_intuos_irq,
		0, WACOM_INTUOS_ABS, 0, WACOM_INTUOS_BUTTONS, WACOM_INTUOS_TOOLS },
	{ "Wacom Intuos 12x12", 10, 30480, 30480, 1023, 15, wacom_intuos_irq,
		0, WACOM_INTUOS_ABS, 0, WACOM_INTUOS_BUTTONS, WACOM_INTUOS_TOOLS },
	{ "Wacom Intuos 12x18", 10, 47720, 30480, 1023, 15, wacom_intuos_irq,
		0, WACOM_INTUOS_ABS, 0, WACOM_INTUOS_BUTTONS, WACOM_INTUOS_TOOLS },
	{ NULL , 0 }
};

struct usb_device_id wacom_ids[] = {
	{ USB_DEVICE(USB_VENDOR_ID_WACOM, 0x10), driver_info: 0 },
	{ USB_DEVICE(USB_VENDOR_ID_WACOM, 0x20), driver_info: 1 },
	{ USB_DEVICE(USB_VENDOR_ID_WACOM, 0x21), driver_info: 2 },
	{ USB_DEVICE(USB_VENDOR_ID_WACOM, 0x22), driver_info: 3 },
	{ USB_DEVICE(USB_VENDOR_ID_WACOM, 0x23), driver_info: 4 },
	{ USB_DEVICE(USB_VENDOR_ID_WACOM, 0x24), driver_info: 5 },
	{ }
};

MODULE_DEVICE_TABLE(usb, wacom_ids);

static int wacom_open(struct input_dev *dev)
{
	struct wacom *wacom = dev->private;

	if (wacom->open++)
		return 0;

	wacom->irq.dev = wacom->usbdev;
	if (usb_submit_urb(&wacom->irq))
		return -EIO;

	return 0;
}

static void wacom_close(struct input_dev *dev)
{
	struct wacom *wacom = dev->private;

	if (!--wacom->open)
		usb_unlink_urb(&wacom->irq);
}

static void *wacom_probe(struct usb_device *dev, unsigned int ifnum, const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *endpoint;
	struct wacom *wacom;

	if (!(wacom = kmalloc(sizeof(struct wacom), GFP_KERNEL))) return NULL;
	memset(wacom, 0, sizeof(struct wacom));

	wacom->features = wacom_features + id->driver_info;

	wacom->dev.evbit[0] |= BIT(EV_KEY) | BIT(EV_ABS) | wacom->features->evbit;
	wacom->dev.absbit[0] |= BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE) | BIT(ABS_DISTANCE) | wacom->features->absbit;
	wacom->dev.relbit[0] |= wacom->features->relbit;
	wacom->dev.keybit[LONG(BTN_LEFT)] |= BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_MIDDLE) | wacom->features->btnbit;
	wacom->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOOL_PEN) | BIT(BTN_TOOL_RUBBER) | BIT(BTN_TOOL_MOUSE) |
		BIT(BTN_TOUCH) | BIT(BTN_STYLUS) | BIT(BTN_STYLUS2) | wacom->features->digibit;

	wacom->dev.absmax[ABS_X] = wacom->features->x_max;
	wacom->dev.absmax[ABS_Y] = wacom->features->y_max;
	wacom->dev.absmax[ABS_PRESSURE] = wacom->features->pressure_max;
	wacom->dev.absmax[ABS_DISTANCE] = wacom->features->distance_max;
	wacom->dev.absmax[ABS_TILT_X] = 127;
	wacom->dev.absmax[ABS_TILT_Y] = 127;

	wacom->dev.absmin[ABS_RZ] = -900;
	wacom->dev.absmax[ABS_RZ] = 899;
	wacom->dev.absmin[ABS_THROTTLE] = -1023;
	wacom->dev.absmax[ABS_THROTTLE] = 1023;

	wacom->dev.absfuzz[ABS_X] = 4;
	wacom->dev.absfuzz[ABS_Y] = 4;

	wacom->dev.private = wacom;
	wacom->dev.open = wacom_open;
	wacom->dev.close = wacom_close;

	wacom->dev.name = wacom->features->name;
	wacom->dev.idbus = BUS_USB;
	wacom->dev.idvendor = dev->descriptor.idVendor;
	wacom->dev.idproduct = dev->descriptor.idProduct;
	wacom->dev.idversion = dev->descriptor.bcdDevice;
	wacom->usbdev = dev;

	endpoint = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;

	FILL_INT_URB(&wacom->irq, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
		     wacom->data, wacom->features->pktlen, wacom->features->irq, wacom, endpoint->bInterval);

	input_register_device(&wacom->dev);

	printk(KERN_INFO "input%d: %s on usb%d:%d.%d\n",
		 wacom->dev.number, wacom->features->name, dev->bus->busnum, dev->devnum, ifnum);

	return wacom;
}

static void wacom_disconnect(struct usb_device *dev, void *ptr)
{
	struct wacom *wacom = ptr;
	usb_unlink_urb(&wacom->irq);
	input_unregister_device(&wacom->dev);
	kfree(wacom);
}

static struct usb_driver wacom_driver = {
	name:		"wacom",
	probe:		wacom_probe,
	disconnect:	wacom_disconnect,
	id_table:	wacom_ids,
};

static int __init wacom_init(void)
{
	usb_register(&wacom_driver);
	return 0;
}

static void __exit wacom_exit(void)
{
	usb_deregister(&wacom_driver);
}

module_init(wacom_init);
module_exit(wacom_exit);
