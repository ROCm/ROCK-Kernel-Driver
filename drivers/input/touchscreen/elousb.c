/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  Elo USB touchscreen support
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
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
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/input.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.1"
#define DRIVER_AUTHOR "Vojtech Pavlik <vojtech@suse.cz>"
#define DRIVER_DESC "Elo USB touchscreen driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

struct elousb {
	char name[128];
	char phys[64];
	struct usb_device *usbdev;
	struct input_dev *dev;
	struct urb *irq;

	unsigned char *data;
	dma_addr_t data_dma;
};

static void elousb_irq(struct urb *urb)
{
	struct elousb *elo = urb->context;
	unsigned char *data = elo->data;
	struct input_dev *dev = elo->dev;
	int status;

	switch (urb->status) {
		case 0:            /* success */
			break;
		case -ECONNRESET:    /* unlink */
		case -ENOENT:
		case -ESHUTDOWN:
			return;
			/* -EPIPE:  should clear the halt */
		default:        /* error */
			goto resubmit;
	}

	if (data[0] != 'T')    /* Mandatory ELO packet marker */
		return;


	input_report_abs(dev, ABS_X, ((u32)data[3] << 8) | data[2]);
	input_report_abs(dev, ABS_Y, ((u32)data[5] << 8) | data[4]);

	input_report_abs(dev, ABS_PRESSURE,
			(data[1] & 0x80) ? (((u32)data[7] << 8) | data[6]): 0);

	if (data[1] & 0x03) {
		input_report_key(dev, BTN_TOUCH, 1);
		input_sync(dev);
	}

	if (data[1] & 0x04)
		input_report_key(dev, BTN_TOUCH, 0);

	input_sync(dev);

resubmit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		pr_err("can't resubmit intr, %s-%s/input0, status %d",
				elo->usbdev->bus->bus_name,
				elo->usbdev->devpath, status);
}

static int elousb_open(struct input_dev *dev)
{
	struct elousb *elo = input_get_drvdata(dev);

	elo->irq->dev = elo->usbdev;
	if (usb_submit_urb(elo->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void elousb_close(struct input_dev *dev)
{
	struct elousb *elo = input_get_drvdata(dev);

	usb_kill_urb(elo->irq);
}

static int elousb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct hid_descriptor *hdesc;
	struct elousb *elo;
	struct input_dev *input_dev;
	int pipe, i;
	unsigned int rsize = 0;
	int error = -ENOMEM;
	char *rdesc;

	interface = intf->cur_altsetting;

	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;
	if (!(endpoint->bEndpointAddress & USB_DIR_IN))
		return -ENODEV;
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT)
		return -ENODEV;

	if (usb_get_extra_descriptor(interface, HID_DT_HID, &hdesc) &&
			(!interface->desc.bNumEndpoints ||
			 usb_get_extra_descriptor(&interface->endpoint[0], HID_DT_HID, &hdesc))) {
		pr_err("HID class descriptor not present");
		return -ENODEV;
	}

	for (i = 0; i < hdesc->bNumDescriptors; i++)
		if (hdesc->desc[i].bDescriptorType == HID_DT_REPORT)
			rsize = le16_to_cpu(hdesc->desc[i].wDescriptorLength);

	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
		pr_err("weird size of report descriptor (%u)", rsize);
		return -ENODEV;
	}


	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);

	elo = kzalloc(sizeof(struct elousb), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!elo || !input_dev)
		goto fail1;

	elo->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &elo->data_dma);
	if (!elo->data)
		goto fail1;

	elo->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!elo->irq)
		goto fail2;

	if (!(rdesc = kmalloc(rsize, GFP_KERNEL)))
		goto fail3;

	elo->usbdev = dev;
	elo->dev = input_dev;

	if ((error = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
					HID_REQ_SET_IDLE, USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0,
					interface->desc.bInterfaceNumber,
					NULL, 0, USB_CTRL_SET_TIMEOUT)) < 0) {
		pr_err("setting HID idle timeout failed, error %d", error);
		error = -ENODEV;
		goto fail4;
	}

	if ((error = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
					USB_REQ_GET_DESCRIPTOR, USB_RECIP_INTERFACE | USB_DIR_IN,
					HID_DT_REPORT << 8, interface->desc.bInterfaceNumber,
					rdesc, rsize, USB_CTRL_GET_TIMEOUT)) < rsize) {
		pr_err("reading HID report descriptor failed, error %d", error);
		error = -ENODEV;
		goto fail4;
	}

	if (dev->manufacturer)
		strlcpy(elo->name, dev->manufacturer, sizeof(elo->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(elo->name, " ", sizeof(elo->name));
		strlcat(elo->name, dev->product, sizeof(elo->name));
	}

	if (!strlen(elo->name))
		snprintf(elo->name, sizeof(elo->name),
				"Elo touchscreen %04x:%04x",
				le16_to_cpu(dev->descriptor.idVendor),
				le16_to_cpu(dev->descriptor.idProduct));

	usb_make_path(dev, elo->phys, sizeof(elo->phys));
	strlcat(elo->phys, "/input0", sizeof(elo->phys));

	input_dev->name = elo->name;
	input_dev->phys = elo->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	set_bit(BTN_TOUCH, input_dev->keybit);
	input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y);
	set_bit(ABS_PRESSURE, input_dev->absbit);

	input_set_abs_params(input_dev, ABS_X, 0, 4000, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 3840, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 256, 0, 0);

	input_set_drvdata(input_dev, elo);

	input_dev->open = elousb_open;
	input_dev->close = elousb_close;

	usb_fill_int_urb(elo->irq, dev, pipe, elo->data, 8,
			elousb_irq, elo, endpoint->bInterval);
	elo->irq->transfer_dma = elo->data_dma;
	elo->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(elo->dev);
	if (error)
		goto fail4;

	usb_set_intfdata(intf, elo);
	return 0;

fail4:
	kfree(rdesc);
fail3:
	usb_free_urb(elo->irq);
fail2:
	usb_free_coherent(dev, 8, elo->data, elo->data_dma);
fail1:
	input_free_device(input_dev);
	kfree(elo);
	return -ENOMEM;
}

static void elousb_disconnect(struct usb_interface *intf)
{
	struct elousb *elo = usb_get_intfdata (intf);

	usb_set_intfdata(intf, NULL);
	if (elo) {
		usb_kill_urb(elo->irq);
		input_unregister_device(elo->dev);
		usb_free_urb(elo->irq);
		usb_free_coherent(interface_to_usbdev(intf), 8, elo->data, elo->data_dma);
		kfree(elo);
	}
}

static struct usb_device_id elousb_id_table [] = {
	{ USB_DEVICE(0x04e7, 0x0009) }, /* CarrolTouch 4000U */
	{ USB_DEVICE(0x04e7, 0x0030) }, /* CarrolTouch 4500U */
	{ }    /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, elousb_id_table);

static struct usb_driver elousb_driver = {
	.name        = "elousb",
	.probe        = elousb_probe,
	.disconnect    = elousb_disconnect,
	.id_table    = elousb_id_table,
};

static int __init elousb_init(void)
{
	int retval = usb_register(&elousb_driver);
	if (retval == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":" DRIVER_DESC);
	return retval;
}

static void __exit elousb_exit(void)
{
	usb_deregister(&elousb_driver);
}

module_init(elousb_init);
module_exit(elousb_exit);
