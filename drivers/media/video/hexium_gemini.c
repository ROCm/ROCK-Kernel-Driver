/*
    hexium_gemini.c - v4l2 driver for Hexium Gemini frame grabber cards
               
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
static int hexium_num = 0;

#include "hexium_gemini.h"

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
	for (i = 0; i < sizeof(hexium_ks0127b); i++) {
		data.byte = hexium_ks0127b[i];
		if (0 != i2c_smbus_xfer(&hexium->i2c_adapter, 0x6c, 0, I2C_SMBUS_WRITE, i, I2C_SMBUS_BYTE_DATA, &data)) {
			printk("failed for address 0x%02x\n", i);
		}
	}

	return 0;
}

static int hexium_set_input(struct hexium *hexium, int input)
{
	union i2c_smbus_data data;

	DEB_D((".\n"));

	data.byte = hexium_input_select[input].byte;
	if (0 != i2c_smbus_xfer(&hexium->i2c_adapter, 0x6c, 0, I2C_SMBUS_WRITE, hexium_input_select[input].adr, I2C_SMBUS_BYTE_DATA, &data)) {
		return -1;
	}

	return 0;
}

static int hexium_set_standard(struct hexium *hexium, struct hexium_data *vdec)
{
	union i2c_smbus_data data;
	int i = 0;

	DEB_D((".\n"));

	while (vdec[i].adr != -1) {
		data.byte = vdec[i].byte;
		if (0 != i2c_smbus_xfer(&hexium->i2c_adapter, 0x6c, 0, I2C_SMBUS_WRITE, vdec[i].adr, I2C_SMBUS_BYTE_DATA, &data)) {
			printk("failed for address 0x%02x\n", i);
			return -1;
		}
		i++;
	}
	return 0;
}

static struct saa7146_ext_vv vv_data;

/* this function only gets called when the probing was successful */
static int hexium_attach(struct saa7146_dev *dev, struct saa7146_pci_extension_data *info)
{
	struct hexium *hexium = (struct hexium *) dev->ext_priv;

	DEB_EE((".\n"));

	hexium = (struct hexium *) kmalloc(sizeof(struct hexium), GFP_KERNEL);
	if (NULL == hexium) {
		printk("hexium_v4l2.o: hexium_probe: not enough kernel memory.\n");
		return -ENOMEM;
	}
	memset(hexium, 0x0, sizeof(struct hexium));
	(struct hexium *) dev->ext_priv = hexium;

	/* FIXME: enable i2c-port pins, video-port-pins
	   video port pins should be enabled here ?! */
	saa7146_write(dev, MC1, (MASK_08 | MASK_24 | MASK_10 | MASK_26));

	saa7146_i2c_adapter_prepare(dev, &hexium->i2c_adapter, SAA7146_I2C_BUS_BIT_RATE_480);
	if (i2c_add_adapter(&hexium->i2c_adapter) < 0) {
		DEB_S(("cannot register i2c-device. skipping.\n"));
		kfree(hexium);
		return -EFAULT;
	}

	/*  set HWControl GPIO number 2 */
	saa7146_setgpio(dev, 2, SAA7146_GPIO_OUTHI);

	saa7146_write(dev, DD1_INIT, 0x07000700);
	saa7146_write(dev, DD1_STREAM_B, 0x00000000);
	saa7146_write(dev, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	/* the rest */
	hexium->cur_input = 0;
	hexium_init_done(dev);

	hexium_set_standard(hexium, hexium_pal);
	hexium->cur_std = V4L2_STD_PAL;

	hexium_set_input(hexium, 0);
	hexium->cur_input = 0;

	saa7146_vv_init(dev, &vv_data);
	if (0 != saa7146_register_device(&hexium->video_dev, dev, "hexium", VFL_TYPE_GRABBER)) {
		ERR(("cannot register capture v4l2 device. skipping.\n"));
		return -1;
	}

	printk("hexium: found 'hexium frame grabber'-%d.\n", hexium_num);
	hexium_num++;

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

			DEB_EE(("VIDIOC_S_INPUT %d.\n", input));

			if (input < 0 || input >= HEXIUM_INPUTS) {
				return -EINVAL;
			}

			hexium->cur_input = input;
			hexium_set_input(hexium, input);

			return 0;
		}
		/* the saa7146 provides some controls (brightness, contrast, saturation)
		   which gets registered *after* this function. because of this we have
		   to return with a value != 0 even if the function succeded.. */
	case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *qc = arg;
			int i;

			for (i = HEXIUM_CONTROLS - 1; i >= 0; i--) {
				if (hexium_controls[i].id == qc->id) {
					*qc = hexium_controls[i];
					DEB_D(("VIDIOC_QUERYCTRL %d.\n", qc->id));
					return 0;
				}
			}
			return -EAGAIN;
		}
	case VIDIOC_G_CTRL:
		{
			struct v4l2_control *vc = arg;
			int i;

			for (i = HEXIUM_CONTROLS - 1; i >= 0; i--) {
				if (hexium_controls[i].id == vc->id) {
					break;
				}
			}

			if (i < 0) {
				return -EAGAIN;
			}

			switch (vc->id) {
			case V4L2_CID_PRIVATE_BASE:{
					vc->value = hexium->cur_bw;
					DEB_D(("VIDIOC_G_CTRL BW:%d.\n", vc->value));
					return 0;
				}
			}
			return -EINVAL;
		}

	case VIDIOC_S_CTRL:
		{
			struct v4l2_control *vc = arg;
			int i = 0;

			for (i = HEXIUM_CONTROLS - 1; i >= 0; i--) {
				if (hexium_controls[i].id == vc->id) {
					break;
				}
			}

			if (i < 0) {
				return -EAGAIN;
			}

			switch (vc->id) {
			case V4L2_CID_PRIVATE_BASE:{
					hexium->cur_bw = vc->value;
					break;
				}
			}

			DEB_D(("VIDIOC_S_CTRL BW:%d.\n", hexium->cur_bw));

			if (0 == hexium->cur_bw && V4L2_STD_PAL == hexium->cur_std) {
				hexium_set_standard(hexium, hexium_pal);
				return 0;
			}
			if (0 == hexium->cur_bw && V4L2_STD_NTSC == hexium->cur_std) {
				hexium_set_standard(hexium, hexium_ntsc);
				return 0;
			}
			if (0 == hexium->cur_bw && V4L2_STD_SECAM == hexium->cur_std) {
				hexium_set_standard(hexium, hexium_secam);
				return 0;
			}
			if (1 == hexium->cur_bw && V4L2_STD_PAL == hexium->cur_std) {
				hexium_set_standard(hexium, hexium_pal_bw);
				return 0;
			}
			if (1 == hexium->cur_bw && V4L2_STD_NTSC == hexium->cur_std) {
				hexium_set_standard(hexium, hexium_ntsc_bw);
				return 0;
			}
			if (1 == hexium->cur_bw && V4L2_STD_SECAM == hexium->cur_std) {
				/* fixme: is there no bw secam mode? */
				return -EINVAL;
			}

			return -EINVAL;
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
	struct hexium *hexium = (struct hexium *) dev->ext_priv;

	if (V4L2_STD_PAL == std->id) {
		hexium_set_standard(hexium, hexium_pal);
		hexium->cur_std = V4L2_STD_PAL;
		return 0;
	} else if (V4L2_STD_NTSC == std->id) {
		hexium_set_standard(hexium, hexium_ntsc);
		hexium->cur_std = V4L2_STD_NTSC;
		return 0;
	} else if (V4L2_STD_SECAM == std->id) {
		hexium_set_standard(hexium, hexium_secam);
		hexium->cur_std = V4L2_STD_SECAM;
		return 0;
	}

	return -1;
}

static struct saa7146_extension hexium_extension;

static struct saa7146_pci_extension_data hexium_gemini_4bnc = {
	.ext_priv = "Hexium Gemini (4 BNC)",
	.ext = &hexium_extension,
};

static struct saa7146_pci_extension_data hexium_gemini_dual_4bnc = {
	.ext_priv = "Hexium Gemini Dual (4 BNC)",
	.ext = &hexium_extension,
};

static struct pci_device_id pci_tbl[] = {
	{
	 .vendor = PCI_VENDOR_ID_PHILIPS,
	 .device = PCI_DEVICE_ID_PHILIPS_SAA7146,
	 .subvendor = 0x17c8,
	 .subdevice = 0x2401,
	 .driver_data = (unsigned long) &hexium_gemini_4bnc,
	 },
	{
	 .vendor = PCI_VENDOR_ID_PHILIPS,
	 .device = PCI_DEVICE_ID_PHILIPS_SAA7146,
	 .subvendor = 0x17c8,
	 .subdevice = 0x2402,
	 .driver_data = (unsigned long) &hexium_gemini_dual_4bnc,
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

static struct saa7146_extension hexium_extension = {
	.name = "hexium gemini",
	.flags = SAA7146_USE_I2C_IRQ,

	.pci_tbl = &pci_tbl[0],
	.module = THIS_MODULE,

	.attach = hexium_attach,
	.detach = hexium_detach,

	.irq_mask = 0,
	.irq_func = NULL,
};

static int __init hexium_init_module(void)
{
	if (0 != saa7146_register_extension(&hexium_extension)) {
		DEB_S(("failed to register extension.\n"));
		return -ENODEV;
	}

	return 0;
}

static void __exit hexium_cleanup_module(void)
{
	saa7146_unregister_extension(&hexium_extension);
}

module_init(hexium_init_module);
module_exit(hexium_cleanup_module);

MODULE_DESCRIPTION("video4linux-2 driver for Hexium Gemini frame grabber cards");
MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_LICENSE("GPL");
