/*
 * $Id: hid-ff.c,v 1.3 2002/06/09 11:06:38 jdeneux Exp $
 *
 *  Force feedback support for hid devices.
 *  Not all hid devices use the same protocol. For example, some use PID,
 *  other use their own proprietary procotol.
 *
 *  Copyright (c) 2002 Johann Deneux
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
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <deneux@ifrance.com>
 */

#include <linux/input.h>

#define DEBUG
#include <linux/usb.h>

#include "hid.h"

/* Drivers' initializing functions */
extern int hid_lgff_init(struct hid_device* hid);
extern int hid_lg3d_init(struct hid_device* hid);
extern int hid_pid_init(struct hid_device* hid);

/*
 * This table contains pointers to initializers. To add support for new
 * devices, you need to add the USB vendor and product ids here.
 */
struct hid_ff_initializer {
	__u16 idVendor;
	__u16 idProduct;
	int (*init)(struct hid_device*);
};

static struct hid_ff_initializer inits[] = {
#ifdef CONFIG_LOGITECH_RUMBLE
	{0x46d, 0xc211, hid_lgff_init},
#endif
#ifdef CONFIG_LOGITECH_3D
	{0x46d, 0xc283, hid_lg3d_init},
#endif
#ifdef CONFIG_HID_PID
	{0x45e, 0x001b, hid_pid_init},
#endif
	{0, 0, NULL} /* Terminating entry */
};

static struct hid_ff_initializer *hid_get_ff_init(__u16 idVendor,
						  __u16 idProduct)
{
	struct hid_ff_initializer *init;
	for (init = inits;
	     init->idVendor
		     && !(init->idVendor == idVendor
			  && init->idProduct == idProduct);
	     init++);

	return init->idVendor? init : NULL;
}

int hid_ff_init(struct hid_device* hid)
{
	struct hid_ff_initializer *init;

	init = hid_get_ff_init(hid->dev->descriptor.idVendor,
			       hid->dev->descriptor.idProduct);

	if (!init) {
		dbg("hid_ff_init could not find initializer");
		return -ENOSYS;
	}
	return init->init(hid);
}
