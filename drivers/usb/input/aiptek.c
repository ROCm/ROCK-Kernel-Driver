/*
 *  Native support for the Aiptek 8000U
 *
 *  Copyright (c) 2001 Chris Atenasio		<chris@crud.net>
 *
 *  based on wacom.c by
 *     Vojtech Pavlik      <vojtech@suse.cz>
 *     Andreas Bach Aaen   <abach@stofanet.dk>
 *     Clifford Wolf       <clifford@clifford.at>
 *     Sam Mosel           <sam.mosel@computer.org>
 *     James E. Blair      <corvus@gnu.org>
 *     Daniel Egger        <egger@suse.de>
 *
 *
 *  Many thanks to Oliver Kuechemann for his support.
 *
 *  ChangeLog:
 *      v0.1 - Initial release
 *      v0.2 - Hack to get around fake event 28's.
 *      v0.3 - Make URB dynamic (Bryan W. Headley, Jun-8-2002)
 *             (kernel 2.5.x variant, June-14-2002)
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
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.3"
#define DRIVER_AUTHOR "Chris Atenasio <chris@crud.net>"
#define DRIVER_DESC "USB Aiptek 6000U/8000U tablet driver (Linux 2.5.x)"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Aiptek status packet:
 *
 *        bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
 * byte0   0     0     0     0     0     0     1     0
 * byte1  X7    X6    X5    X4    X3    X2    X1    X0
 * byte2  X15   X14   X13   X12   X11   X10   X9    X8
 * byte3  Y7    Y6    Y5    Y4    Y3    Y2    Y1    Y0
 * byte4  Y15   Y14   Y13   Y12   Y11   Y10   Y9    Y8
 * byte5   *     *     *    BS2   BS1   Tip   DV    IR
 * byte6  P7    P6    P5    P4    P3    P2    P1    P0
 * byte7  P15   P14   P13   P12   P11   P10   P9    P8
 *
 * IR: In Range = Proximity on
 * DV = Data Valid
 *
 * 
 * Command Summary:
 *
 * Command/Data    Description     Return Bytes    Return Value
 * 0x10/0x00       SwitchToMouse       0
 * 0x10/0x01       SwitchToTablet      0
 * 0x18/0x04       Resolution500LPI    0
 * 0x17/0x00       FilterOn            0
 * 0x12/0xFF       AutoGainOn          0
 * 0x01/0x00       GetXExtension       2           MaxX
 * 0x01/0x01       GetYExtension       2           MaxY
 * 0x02/0x00       GetModelCode        2           ModelCode = LOBYTE
 * 0x03/0x00       GetODMCode          2           ODMCode
 * 0x08/0x00       GetPressureLevels   2           =512
 * 0x04/0x00       GetFirmwareVersion  2           Firmware Version
 *
 *
 * To initialize the tablet:
 *
 * (1) Send command Resolution500LPI
 * (2) Option Commands (GetXExtension, GetYExtension)
 * (3) Send command SwitchToTablet
 */

#define USB_VENDOR_ID_AIPTEK   0x08ca

struct aiptek_features {
	char *name;
	int pktlen;
	int x_max;
	int y_max;
	int pressure_min;
	int pressure_max;
	void (*irq) (struct urb * urb);
	unsigned long evbit;
	unsigned long absbit;
	unsigned long relbit;
	unsigned long btnbit;
	unsigned long digibit;
};

struct aiptek {
	signed char data[10];
	struct input_dev dev;
	struct usb_device *usbdev;
	struct urb *irq;
	struct aiptek_features *features;
	int tool;
	int open;
};

static void
aiptek_irq(struct urb *urb)
{
	struct aiptek *aiptek = urb->context;
	unsigned char *data = aiptek->data;
	struct input_dev *dev = &aiptek->dev;
	int x;
	int y;
	int pressure;
	int proximity;

	if (urb->status)
		return;

	if ((data[0] & 2) == 0) {
		dbg("received unknown report #%d", data[0]);
	}

	proximity = data[5] & 0x01;
	input_report_key(dev, BTN_TOOL_PEN, proximity);

	x = ((__u32) data[1]) | ((__u32) data[2] << 8);
	y = ((__u32) data[3]) | ((__u32) data[4] << 8);
	pressure = ((__u32) data[6]) | ((__u32) data[7] << 8);
	pressure -= aiptek->features->pressure_min;

	if (pressure < 0) {
		pressure = 0;
	}

	if (proximity) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
		input_report_abs(dev, ABS_PRESSURE, pressure);
		input_report_key(dev, BTN_TOUCH, data[5] & 0x04);
		input_report_key(dev, BTN_STYLUS, data[5] & 0x08);
		input_report_key(dev, BTN_STYLUS2, data[5] & 0x10);
	}

	input_sync(dev);

}

struct aiptek_features aiptek_features[] = {
	{"Aiptek 6000U/8000U",
	 8, 3000, 2250, 26, 511, aiptek_irq, 0, 0, 0, 0},
	{NULL, 0}
};

struct usb_device_id aiptek_ids[] = {
	{USB_DEVICE(USB_VENDOR_ID_AIPTEK, 0x20), .driver_info = 0},
	{}
};

MODULE_DEVICE_TABLE(usb, aiptek_ids);

static int
aiptek_open(struct input_dev *dev)
{
	struct aiptek *aiptek = dev->private;

	if (aiptek->open++)
		return 0;

	aiptek->irq->dev = aiptek->usbdev;
	if (usb_submit_urb(aiptek->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void
aiptek_close(struct input_dev *dev)
{
	struct aiptek *aiptek = dev->private;

	if (!--aiptek->open)
		usb_unlink_urb(aiptek->irq);
}

static void
aiptek_command(struct usb_device *dev, unsigned int ifnum,
	       unsigned char command, unsigned char data)
{
	__u8 buf[3];

	buf[0] = 4;
	buf[1] = command;
	buf[2] = data;

	/* 
	 * FIXME, either remove this call, or talk the maintainer into 
	 * adding it back into the core.
	 */
#if 0
	if (usb_set_report(dev, ifnum, 3, 2, buf, 3) != 3) {
		dbg("aiptek_command: 0x%x 0x%x\n", command, data);
	}
#endif
}

static void*
aiptek_probe(struct usb_device *dev, unsigned int ifnum,
	     const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *endpoint;
	struct aiptek *aiptek;

	if (!(aiptek = kmalloc(sizeof (struct aiptek), GFP_KERNEL)))
		return NULL;

	memset(aiptek, 0, sizeof (struct aiptek));

    aiptek->irq = usb_alloc_urb(0, GFP_KERNEL);
    if (!aiptek->irq) {
        kfree(aiptek);
        return NULL;
    }

	// Resolution500LPI
	aiptek_command(dev, ifnum, 0x18, 0x04);

	// SwitchToTablet
	aiptek_command(dev, ifnum, 0x10, 0x01);

	aiptek->features = aiptek_features + id->driver_info;

	aiptek->dev.evbit[0] |= BIT(EV_KEY) | BIT(EV_ABS) | BIT(EV_MSC) |
	    aiptek->features->evbit;

	aiptek->dev.absbit[0] |= BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE) |
	    BIT(ABS_MISC) | aiptek->features->absbit;

	aiptek->dev.relbit[0] |= aiptek->features->relbit;

	aiptek->dev.keybit[LONG(BTN_LEFT)] |= BIT(BTN_LEFT) | BIT(BTN_RIGHT) |
	    BIT(BTN_MIDDLE) | aiptek->features->btnbit;

	aiptek->dev.keybit[LONG(BTN_DIGI)] |= BIT(BTN_TOOL_PEN) |
	    BIT(BTN_TOOL_MOUSE) | BIT(BTN_TOUCH) |
	    BIT(BTN_STYLUS) | BIT(BTN_STYLUS2) | aiptek->features->digibit;

	aiptek->dev.mscbit[0] = BIT(MSC_SERIAL);

	aiptek->dev.absmax[ABS_X] = aiptek->features->x_max;
	aiptek->dev.absmax[ABS_Y] = aiptek->features->y_max;
	aiptek->dev.absmax[ABS_PRESSURE] = aiptek->features->pressure_max -
	    aiptek->features->pressure_min;

	aiptek->dev.absfuzz[ABS_X] = 0;
	aiptek->dev.absfuzz[ABS_Y] = 0;

	aiptek->dev.private = aiptek;
	aiptek->dev.open = aiptek_open;
	aiptek->dev.close = aiptek_close;

	aiptek->dev.name = aiptek->features->name;
	aiptek->dev.id.bustype = BUS_USB;
	aiptek->dev.id.vendor = dev->descriptor.idVendor;
	aiptek->dev.id.product = dev->descriptor.idProduct;
	aiptek->dev.id.version = dev->descriptor.bcdDevice;
	aiptek->usbdev = dev;

	endpoint = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;

	FILL_INT_URB(aiptek->irq,
		  dev,
		  usb_rcvintpipe(dev, endpoint->bEndpointAddress),
		         aiptek->data,
		  aiptek->features->pktlen,
		         aiptek->features->irq,
		  aiptek,
		  endpoint->bInterval);

	input_register_device(&aiptek->dev);

	printk(KERN_INFO "input: %s on usb%d:%d.%d\n",
	       aiptek->features->name, dev->bus->busnum, dev->devnum, ifnum);

	return aiptek;
}

static void
aiptek_disconnect(struct usb_device *dev, void *ptr)
{
	struct aiptek *aiptek = ptr;
	usb_unlink_urb(aiptek->irq);
	input_unregister_device(&aiptek->dev);
    usb_free_urb(aiptek->irq);
	kfree(aiptek);
}

static struct usb_driver aiptek_driver = {
	.name ="aiptek",
	.probe =aiptek_probe,
	.disconnect =aiptek_disconnect,
	.id_table =aiptek_ids,
};

static int __init
aiptek_init(void)
{
	usb_register(&aiptek_driver);
	info(DRIVER_VERSION " " DRIVER_AUTHOR);
	info(DRIVER_DESC);
	return 0;
}

static void __exit
aiptek_exit(void)
{
	usb_deregister(&aiptek_driver);
}

module_init(aiptek_init);
module_exit(aiptek_exit);
