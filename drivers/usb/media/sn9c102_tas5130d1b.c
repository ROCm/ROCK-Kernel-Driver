/***************************************************************************
 * Driver for TAS5130D1B image sensor connected to the SN9C10[12] PC       *
 * Camera Controllers                                                      *
 *                                                                         *
 * Copyright (C) 2004 by Luca Risolia <luca.risolia@studio.unibo.it>       *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program; if not, write to the Free Software             *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               *
 ***************************************************************************/

#include "sn9c102_sensor.h"


static struct sn9c102_sensor tas5130d1b;


static int tas5130d1b_init(struct sn9c102_device* cam)
{
	const u8 DARKNESS = 0xff, CONTRAST = 0xb0, GAIN = 0x08;
	int err = 0;

	err += sn9c102_write_reg(cam, 0x01, 0x01);
	err += sn9c102_write_reg(cam, 0x20, 0x17);
	err += sn9c102_write_reg(cam, 0x04, 0x01);
	err += sn9c102_write_reg(cam, 0x01, 0x10);
	err += sn9c102_write_reg(cam, 0x00, 0x11);
	err += sn9c102_write_reg(cam, 0x00, 0x14);
	err += sn9c102_write_reg(cam, 0x60, 0x17);
	err += sn9c102_write_reg(cam, 0x07, 0x18);

	err += sn9c102_i2c_try_raw_write(cam, &tas5130d1b, 4, 0x11, 0x00, 0x80,
	                                 0x00, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, &tas5130d1b, 4, 0x11, 0x00, 0xc0,
	                                 GAIN, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, &tas5130d1b, 4, 0x11, 0x00, 0x40,
	                                 CONTRAST, 0, 0);
	err += sn9c102_i2c_try_raw_write(cam, &tas5130d1b, 4, 0x11, 0x02, 0x20,
	                                 DARKNESS, 0, 0);

	return err;
}


static int tas5130d1b_set_crop(struct sn9c102_device* cam, 
                               const struct v4l2_rect* rect)
{
	struct sn9c102_sensor* s = &tas5130d1b;
	u8 h_start = (u8)(rect->left - s->cropcap.bounds.left) + 104,
	   v_start = (u8)(rect->top - s->cropcap.bounds.top) + 12;
	int err = 0;

	err += sn9c102_write_reg(cam, h_start, 0x12);
	err += sn9c102_write_reg(cam, v_start, 0x13);

	/* Do NOT change! */
	err += sn9c102_write_reg(cam, 0x1d, 0x1a);
	err += sn9c102_write_reg(cam, 0x10, 0x1b);
	err += sn9c102_write_reg(cam, 0xf3, 0x19);

	return err;
}


static struct sn9c102_sensor tas5130d1b = {
	.name = "TAS5130D1B",
	.maintainer = "Luca Risolia <luca.risolia@studio.unibo.it>",
	.frequency = SN9C102_I2C_100KHZ,
	.interface = SN9C102_I2C_3WIRES,
	.init = &tas5130d1b_init,
	.cropcap = {
		.bounds = {
			.left = 0,
			.top = 0,
			.width = 640,
			.height = 480,
		},
		.defrect = {
			.left = 0,
			.top = 0,
			.width = 640,
			.height = 480,
		},
	},
	.set_crop = &tas5130d1b_set_crop,
	.pix_format = {
		.width = 640,
		.height = 480,
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.priv = 8,
	}
};


int sn9c102_probe_tas5130d1b(struct sn9c102_device* cam)
{
	/* This sensor has no identifiers, so let's attach it anyway */
	sn9c102_attach_sensor(cam, &tas5130d1b);

	/* At the moment, sensor detection is based on USB pid/vid */
	if (tas5130d1b.usbdev->descriptor.idProduct != 0x6025)
		return -ENODEV;

	return 0;
}
