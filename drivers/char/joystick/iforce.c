/*
 * $Id: iforce.c,v 1.7 2000/06/04 14:03:36 vojtech Exp $
 *
 *  Copyright (c) 2000 Vojtech Pavlik
 *
 *  USB/RS232 I-Force joysticks and wheels.
 *
 *  Sponsored by SuSE
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
#include <linux/serio.h>
#include <linux/config.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("USB/RS232 I-Force joysticks and wheels driver");

#define USB_VENDOR_ID_LOGITECH		0x046d
#define USB_DEVICE_ID_LOGITECH_WMFORCE	0xc281

#define IFORCE_MAX_LENGTH		16

#if defined(CONFIG_INPUT_IFORCE_232) || defined(CONFIG_INPUT_IFORCE_232_MODULE)
#define IFORCE_232
#endif
#if defined(CONFIG_INPUT_IFORCE_USB) || defined(CONFIG_INPUT_IFORCE_USB_MODULE)
#define IFORCE_USB
#endif

struct iforce {
	signed char data[IFORCE_MAX_LENGTH];
	struct usb_device *usbdev;
	struct input_dev dev;
	struct urb irq;
	int open;
	int idx, pkt, len, id;
	unsigned char csum;
};

static struct {
        __s32 x;
        __s32 y;
} iforce_hat_to_axis[16] = {{ 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

static char *iforce_name = "I-Force joystick/wheel";

static void iforce_process_packet(struct input_dev *dev, unsigned char id, int idx, unsigned char *data)
{
	switch (id) {

		case 1:	/* joystick position data */
		case 3: /* wheel position data */

			if (id == 1) {
				input_report_abs(dev, ABS_X, (__s16) (((__s16)data[1] << 8) | data[0]));
				input_report_abs(dev, ABS_Y, (__s16) (((__s16)data[3] << 8) | data[2]));
				input_report_abs(dev, ABS_THROTTLE, 255 - data[4]);
			} else {
				input_report_abs(dev, ABS_WHEEL, (__s16) (((__s16)data[1] << 8) | data[0]));
				input_report_abs(dev, ABS_GAS,   255 - data[2]);
				input_report_abs(dev, ABS_BRAKE, 255 - data[3]);
			}

			input_report_abs(dev, ABS_HAT0X, iforce_hat_to_axis[data[6] >> 4].x);
			input_report_abs(dev, ABS_HAT0Y, iforce_hat_to_axis[data[6] >> 4].y);

			input_report_key(dev, BTN_TRIGGER, data[5] & 0x01);
			input_report_key(dev, BTN_TOP,     data[5] & 0x02);
			input_report_key(dev, BTN_THUMB,   data[5] & 0x04);
			input_report_key(dev, BTN_TOP2,    data[5] & 0x08);
			input_report_key(dev, BTN_BASE,    data[5] & 0x10);
			input_report_key(dev, BTN_BASE2,   data[5] & 0x20);
			input_report_key(dev, BTN_BASE3,   data[5] & 0x40);
			input_report_key(dev, BTN_BASE4,   data[5] & 0x80);
			input_report_key(dev, BTN_BASE5,   data[6] & 0x01);
			input_report_key(dev, BTN_A,       data[6] & 0x02);
			input_report_key(dev, BTN_B,       data[6] & 0x04);
			input_report_key(dev, BTN_C,       data[6] & 0x08);
			break;

		case 2: /* force feedback effect status */
			break;
	}
}

#ifdef IFORCE_USB

static int iforce_open(struct input_dev *dev)
{
	struct iforce *iforce = dev->private;

	if (dev->idbus == BUS_USB && !iforce->open++) {
		iforce->irq.dev = iforce->usbdev;
		if (usb_submit_urb(&iforce->irq))
			return -EIO;
	}

	return 0;
}

static void iforce_close(struct input_dev *dev)
{
	struct iforce *iforce = dev->private;

	if (dev->idbus == BUS_USB && !--iforce->open)
		usb_unlink_urb(&iforce->irq);
}

#endif

static void iforce_input_setup(struct iforce *iforce)
{
	int i;

	iforce->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	iforce->dev.keybit[LONG(BTN_JOYSTICK)] |= BIT(BTN_TRIGGER) | BIT(BTN_TOP) | BIT(BTN_THUMB) | BIT(BTN_TOP2) |
					BIT(BTN_BASE) | BIT(BTN_BASE2) | BIT(BTN_BASE3) | BIT(BTN_BASE4) | BIT(BTN_BASE5);
	iforce->dev.keybit[LONG(BTN_GAMEPAD)] |= BIT(BTN_A) | BIT(BTN_B) | BIT(BTN_C);
	iforce->dev.absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_THROTTLE) | BIT(ABS_HAT0X) | BIT(ABS_HAT0Y)
				| BIT(ABS_WHEEL) | BIT(ABS_GAS) | BIT(ABS_BRAKE);

	for (i = ABS_X; i <= ABS_Y; i++) {
		iforce->dev.absmax[i] =  1920;
		iforce->dev.absmin[i] = -1920;
		iforce->dev.absflat[i] = 128;
		iforce->dev.absfuzz[i] = 16;
	}

	for (i = ABS_THROTTLE; i <= ABS_RUDDER; i++) {
		iforce->dev.absmax[i] = 255;
		iforce->dev.absmin[i] = 0;
	}

	for (i = ABS_HAT0X; i <= ABS_HAT0Y; i++) {
		iforce->dev.absmax[i] =  1;
		iforce->dev.absmin[i] = -1;
	}

	iforce->dev.private = iforce;

#ifdef IFORCE_USB
	iforce->dev.open = iforce_open;
	iforce->dev.close = iforce_close;
#endif

	input_register_device(&iforce->dev);
}

#ifdef IFORCE_USB

static void iforce_usb_irq(struct urb *urb)
{
	struct iforce *iforce = urb->context;
	if (urb->status) return;
	iforce_process_packet(&iforce->dev, iforce->data[0], 8, iforce->data + 1);
}

static void *iforce_usb_probe(struct usb_device *dev, unsigned int ifnum,
			      const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *endpoint;
	struct iforce *iforce;

	endpoint = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;

	if (!(iforce = kmalloc(sizeof(struct iforce), GFP_KERNEL))) return NULL;
	memset(iforce, 0, sizeof(struct iforce));

	iforce->dev.name = iforce_name;
	iforce->dev.idbus = BUS_USB;
	iforce->dev.idvendor = dev->descriptor.idVendor;
	iforce->dev.idproduct = dev->descriptor.idProduct;
	iforce->dev.idversion = dev->descriptor.bcdDevice;

	FILL_INT_URB(&iforce->irq, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			iforce->data, 8, iforce_usb_irq, iforce, endpoint->bInterval);

	iforce_input_setup(iforce);

	printk(KERN_INFO "input%d: %s on usb%d:%d.%d\n",
		 iforce->dev.number, iforce_name, dev->bus->busnum, dev->devnum, ifnum);

	return iforce;
}

static void iforce_usb_disconnect(struct usb_device *dev, void *ptr)
{
	struct iforce *iforce = ptr;
	usb_unlink_urb(&iforce->irq);
	input_unregister_device(&iforce->dev);
	kfree(iforce);
}

static struct usb_device_id iforce_usb_ids [] = {
	{ USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WMFORCE) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, iforce_usb_ids);

static struct usb_driver iforce_usb_driver = {
	name:		"iforce",
	probe:		iforce_usb_probe,
	disconnect:	iforce_usb_disconnect,
	id_table:	iforce_usb_ids,
};

#endif

#ifdef IFORCE_232

static void iforce_serio_irq(struct serio *serio, unsigned char data, unsigned int flags)
{
        struct iforce* iforce = serio->private;

	if (!iforce->pkt) {
		if (data != 0x2b) {
			return;
		}
		iforce->pkt = 1;
		return;
	}

        if (!iforce->id) {
		if (data > 3) {
			iforce->pkt = 0;
			return;
		}
                iforce->id = data;
		return;
        }

	if (!iforce->len) {
		if (data > IFORCE_MAX_LENGTH) {
			iforce->pkt = 0;
			iforce->id = 0;
			return;
		}
		iforce->len = data;
		return;
	}

        if (iforce->idx < iforce->len) {
                iforce->csum += iforce->data[iforce->idx++] = data;
		return;
	}

        if (iforce->idx == iforce->len) {
		iforce_process_packet(&iforce->dev, iforce->id, iforce->idx, iforce->data);
		iforce->pkt = 0;
		iforce->id  = 0;
                iforce->len = 0;
                iforce->idx = 0;
		iforce->csum = 0;
        }
}

static void iforce_serio_connect(struct serio *serio, struct serio_dev *dev)
{
	struct iforce *iforce;

	if (serio->type != (SERIO_RS232 | SERIO_IFORCE))
		return;

	if (!(iforce = kmalloc(sizeof(struct iforce), GFP_KERNEL))) return;
	memset(iforce, 0, sizeof(struct iforce));

	iforce->dev.name = iforce_name;
	iforce->dev.idbus = BUS_RS232;
	iforce->dev.idvendor = SERIO_IFORCE;
	iforce->dev.idproduct = 0x0001;
	iforce->dev.idversion = 0x0100;

	serio->private = iforce;

	if (serio_open(serio, dev)) {
		kfree(iforce);
		return;
	}

	iforce_input_setup(iforce);

	printk(KERN_INFO "input%d: %s on serio%d\n",
		 iforce->dev.number, iforce_name, serio->number);
}

static void iforce_serio_disconnect(struct serio *serio)
{
	struct iforce* iforce = serio->private;
	input_unregister_device(&iforce->dev);
	serio_close(serio);
	kfree(iforce);
}

static struct serio_dev iforce_serio_dev = {
	interrupt:	iforce_serio_irq,
	connect:	iforce_serio_connect,
	disconnect:	iforce_serio_disconnect,
};

#endif

static int __init iforce_init(void)
{
#ifdef IFORCE_USB
	usb_register(&iforce_usb_driver);
#endif
#ifdef IFORCE_232
	serio_register_device(&iforce_serio_dev);
#endif
	return 0;
}

static void __exit iforce_exit(void)
{
#ifdef IFORCE_USB
	usb_deregister(&iforce_usb_driver);
#endif
#ifdef IFORCE_232
	serio_unregister_device(&iforce_serio_dev);
#endif
}

module_init(iforce_init);
module_exit(iforce_exit);
