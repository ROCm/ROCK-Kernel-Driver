/*
    hexium_orion.c - v4l2 driver for the Hexium Orion frame grabber cards

    Visit http://www.mihu.de/linux/saa7146/ and follow the link
    to "hexium" for further details about this card.
    
    Copyright (C) 2003 Michael Hunold <michael@mihu.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define DEBUG_VARIABLE debug

#include <media/saa7146_vv.h>

static int debug = 0;
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "debug verbosity");

/* global variables */
int hexium_num = 0;

#include "hexium_orion.h"

/* this is only called for old HV-PCI6/Orion cards
   without eeprom */
static int hexium_probe(struct saa7146_dev *dev)
{
	struct hexium *hexium = 0;
	union i2c_smbus_data data;
	int err = 0;

	DEB_EE((".\n"));

	/* there are no hexium orion cards with revision 0 saa7146s */
	if (0 == dev->revision) {
		return -EFAULT;
	}

	hexium = (struct hexium *) kmalloc(sizeof(struct hexium), GFP_KERNEL);
	if (NULL == hexium) {
		printk("hexium_orion.o: hexium_probe: not enough kernel memory.\n");
		return -ENOMEM;
	}
	memset(hexium, 0x0, sizeof(struct hexium));

	/* FIXME: enable i2c-port pins, video-port-pins
	   video port pins should be enabled here ?! */
	saa7146_write(dev, MC1, (MASK_08 | MASK_24 | MASK_10 | MASK_26));

	saa7146_write(dev, DD1_INIT, 0x02000200);
	saa7146_write(dev, DD1_STREAM_B, 0x00000000);
	saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	saa7146_i2c_adapter_prepare(dev, &hexium->i2c_adapter, SAA7146_I2C_BUS_BIT_RATE_480);
	if (i2c_add_adapter(&hexium->i2c_adapter) < 0) {
		DEB_S(("cannot register i2c-device. skipping.\n"));
		kfree(hexium);
		return -EFAULT;
	}

	/* set SAA7110 control GPIO 0 */
	saa7146_setgpio(dev, 0, SAA7146_GPIO_OUTHI);
	/*  set HWControl GPIO number 2 */
	saa7146_setgpio(dev, 2, SAA7146_GPIO_OUTHI);

	mdelay(10);

	/* detect newer Hexium Orion cards by subsystem ids */
	if (0x17c8 == dev->pci->subsystem_vendor && 0x0101 == dev->pci->subsystem_device) {
		printk("hexium_orion.o: device is a Hexium Orion w/ 1 SVHS + 3 BNC inputs.\n");
		/* we store the pointer in our private data field */
		(struct hexium *) dev->ext_priv = hexium;
		hexium->type = HEXIUM_ORION_1SVHS_3BNC;
		return 0;
	}

	if (0x17c8 == dev->pci->subsystem_vendor && 0x2101 == dev->pci->subsystem_device) {
		printk("hexium_orion.o: device is a Hexium Orion w/ 4 BNC inputs.\n");
		/* we store the pointer in our private data field */
		(struct hexium *) dev->ext_priv = hexium;
		hexium->type = HEXIUM_ORION_4BNC;
		return 0;
	}

	/* check if this is an old hexium Orion card by looking at 
	   a saa7110 at address 0x4e */
	if (0 == (err = i2c_smbus_xfer(&hexium->i2c_adapter, 0x4e, 0, I2C_SMBUS_READ, 0x00, I2C_SMBUS_BYTE_DATA, &data))) {
		printk("hexium_orion.o: device is a Hexium HV-PCI6/Orion (old).\n");
		/* we store the pointer in our private data field */
		(struct hexium *) dev->ext_priv = hexium;
		hexium->type = HEXIUM_HV_PCI6_ORION;
		return 0;
	}

	i2c_del_adapter(&hexium->i2c_adapter);
	kfree(hexium);
	return -EFAULT;
}

/* bring hardware to a sane state. this has to be done, just in case someone
   wants to capture from this device before it has been properly initialized.
   the capture engine would badly fail, because no valid signal arrives on the
   saa7146, thus leading to timeouts and stuff. */
static int hexium_init_done(struct saa7146_dev *dev)
{
	struct hexium *hexium = (struct hexium *) dev->ext_priv;
	union i2c_smbus_data data;
	int i = 0;

	DEB_D(("hexium_init_done called.\n"));

	/* initialize the helper ics to useful values */
	for (i = 0; i < sizeof(hexium_saa7110); i++) {
		data.byte = hexium_saa7110[i];
		if (0 != i2c_smbus_xfer(&hexium->i2c_adapter, 0x4e, 0, I2C_SMBUS_WRITE, i, I2C_SMBUS_BYTE_DATA, &data)) {
			printk("hexium_orion: failed for address 0x%02x\n", i);
		}
	}

	return 0;
}

static struct saa7146_ext_vv vv_data;

/* this function only gets called when the probing was successful */
static int hexium_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct hexium *hexium = (struct hexium *) dev->ext_priv;

	DEB_EE((".\n"));

	saa7146_vv_init(dev, &vv_data);
	if (0 != saa7146_register_device(&hexium->video_dev, dev, "hexium", VFL_TYPE_GRABBER)) {
		ERR(("cannot register capture v4l2 device. skipping.\n"));
		return -1;
	}

	printk("hexium_orion.o: found 'hexium orion' frame grabber-%d.\n", hexium_num);
	hexium_num++;

	/* the rest */
	hexium->cur_input = 0;
	hexium_init_done(dev);

	return 0;
}

static int hexium_detach(struct saa7146_dev *dev)
{
	struct hexium *hexium = (struct hexium *) dev->ext_priv;

	DEB_EE(("dev:%p\n", dev));

	saa7146_unregister_device(&hexium->video_dev, dev);
	saa7146_vv_release(dev);

	hexium_num--;

	i2c_del_adapter(&hexium->i2c_adapter);
	kfree(hexium);
	return 0;
}

static int hexium_ioctl(struct saa7146_fh *fh, unsigned int cmd, void *arg)
{
	struct saa7146_dev *dev = fh->dev;
	struct hexium *hexium = (struct hexium *) dev->ext_priv;
/*
	struct saa7146_vv *vv = dev->vv_data; 
*/
	switch (cmd) {
	case VIDIOC_ENUMINPUT:
		{
			struct v4l2_input *i = arg;
			DEB_EE(("VIDIOC_ENUMINPUT %d.\n", i->index));

			if (i->index < 0 || i->index >= HEXIUM_INPUTS) {
				return -EINVAL;
			}

			memcpy(i, &hexium_inputs[i->index], sizeof(struct v4l2_input));

			DEB_D(("v4l2_ioctl: VIDIOC_ENUMINPUT %d.\n", i->index));
			return 0;
		}
	case VIDIOC_G_INPUT:
		{
			int *input = (int *) arg;
			*input = hexium->cur_input;

			DEB_D(("VIDIOC_G_INPUT: %d\n", *input));
			return 0;
		}
	case VIDIOC_S_INPUT:
		{
			int input = *(int *) arg;

			if (input < 0 || input >= HEXIUM_INPUTS) {
				return -EINVAL;
			}

			hexium->cur_input = input;

			/* fixme: switch input here, switch audio, too! */
//              saa7146_set_hps_source_and_sync(dev, input_port_selection[input].hps_source, input_port_selection[input].hps_sync);
			printk("hexium_orion.o: VIDIOC_S_INPUT: fixme switch input.\n");

			return 0;
		}
	default:
/*
		DEB_D(("v4l2_ioctl does not handle this ioctl.\n"));
*/
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int std_callback(struct saa7146_dev *dev, struct saa7146_standard *std)
{
	return 0;
}

static struct saa7146_extension extension;

static struct saa7146_pci_extension_data hexium_hv_pci6 = {
	.ext_priv = "Hexium HV-PCI6 / Orion",
	.ext = &extension,
};

static struct saa7146_pci_extension_data hexium_orion_1svhs_3bnc = {
	.ext_priv = "Hexium HV-PCI6 / Orion (1 SVHS/3 BNC)",
	.ext = &extension,
};

static struct saa7146_pci_extension_data hexium_orion_4bnc = {
	.ext_priv = "Hexium HV-PCI6 / Orion (4 BNC)",
	.ext = &extension,
};

static struct pci_device_id pci_tbl[] = {
	{
	 .vendor = PCI_VENDOR_ID_PHILIPS,
	 .device = PCI_DEVICE_ID_PHILIPS_SAA7146,
	 .subvendor = 0x0000,
	 .subdevice = 0x0000,
	 .driver_data = (unsigned long) &hexium_hv_pci6,
	 },
	{
	 .vendor = PCI_VENDOR_ID_PHILIPS,
	 .device = PCI_DEVICE_ID_PHILIPS_SAA7146,
	 .subvendor = 0x17c8,
	 .subdevice = 0x0101,
	 .driver_data = (unsigned long) &hexium_orion_1svhs_3bnc,
	 },
	{
	 .vendor = PCI_VENDOR_ID_PHILIPS,
	 .device = PCI_DEVICE_ID_PHILIPS_SAA7146,
	 .subvendor = 0x17c8,
	 .subdevice = 0x2101,
	 .driver_data = (unsigned long) &hexium_orion_4bnc,
	 },
	{
	 .vendor = 0,
	 }
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static struct saa7146_ext_vv vv_data = {
	.inputs = HEXIUM_INPUTS,
	.capabilities = 0,
	.stds = &hexium_standards[0],
	.num_stds = sizeof(hexium_standards) / sizeof(struct saa7146_standard),
	.std_callback = &std_callback,
	.ioctls = &ioctls[0],
	.ioctl = hexium_ioctl,
};

static struct saa7146_extension extension = {
	.name = "hexium HV-PCI6/Orion",
	.flags = 0,		// SAA7146_USE_I2C_IRQ,

	.pci_tbl = &pci_tbl[0],
	.module = THIS_MODULE,

	.probe = hexium_probe,
	.attach = hexium_attach,
	.detach = hexium_detach,

	.irq_mask = 0,
	.irq_func = NULL,
};

int __init hexium_init_module(void)
{
	if (0 != saa7146_register_extension(&extension)) {
		DEB_S(("failed to register extension.\n"));
		return -ENODEV;
	}

	return 0;
}

void __exit hexium_cleanup_module(void)
{
	saa7146_unregister_extension(&extension);
}

module_init(hexium_init_module);
module_exit(hexium_cleanup_module);

MODULE_DESCRIPTION("video4linux-2 driver for Hexium Orion frame grabber cards");
MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_LICENSE("GPL");
