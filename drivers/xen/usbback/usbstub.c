/*
 * usbstub.c
 *
 * USB stub driver - grabbing and managing USB devices.
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
 *
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * or, by your choice,
 *
 * When distributed separately from the Linux kernel or incorporated into
 * other software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "usbback.h"

static LIST_HEAD(port_list);
static DEFINE_SPINLOCK(port_list_lock);

struct vusb_port_id *find_portid_by_busid(const char *busid)
{
	struct vusb_port_id *portid;
	int found = 0;
	unsigned long flags;

	spin_lock_irqsave(&port_list_lock, flags);
	list_for_each_entry(portid, &port_list, id_list) {
		if (!(strncmp(portid->phys_bus, busid, USBBACK_BUS_ID_SIZE))) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&port_list_lock, flags);

	if (found)
		return portid;

	return NULL;
}

struct vusb_port_id *find_portid(const domid_t domid,
						const unsigned int handle,
						const int portnum)
{
	struct vusb_port_id *portid;
	int found = 0;
	unsigned long flags;

	spin_lock_irqsave(&port_list_lock, flags);
	list_for_each_entry(portid, &port_list, id_list) {
		if ((portid->domid == domid)
				&& (portid->handle == handle)
				&& (portid->portnum == portnum)) {
				found = 1;
				break;
		}
	}
	spin_unlock_irqrestore(&port_list_lock, flags);

	if (found)
		return portid;

	return NULL;
}

int portid_add(const char *busid,
					const domid_t domid,
					const unsigned int handle,
					const int portnum)
{
	struct vusb_port_id *portid;
	unsigned long flags;

	portid = kzalloc(sizeof(*portid), GFP_KERNEL);
	if (!portid)
		return -ENOMEM;

	portid->domid = domid;
	portid->handle = handle;
	portid->portnum = portnum;

	strncpy(portid->phys_bus, busid, USBBACK_BUS_ID_SIZE);

	spin_lock_irqsave(&port_list_lock, flags);
	list_add(&portid->id_list, &port_list);
	spin_unlock_irqrestore(&port_list_lock, flags);

	return 0;
}

int portid_remove(const domid_t domid,
					const unsigned int handle,
					const int portnum)
{
	struct vusb_port_id *portid, *tmp;
	int err = -ENOENT;
	unsigned long flags;

	spin_lock_irqsave(&port_list_lock, flags);
	list_for_each_entry_safe(portid, tmp, &port_list, id_list) {
		if (portid->domid == domid
				&& portid->handle == handle
				&& portid->portnum == portnum) {
			list_del(&portid->id_list);
			kfree(portid);

			err = 0;
		}
	}
	spin_unlock_irqrestore(&port_list_lock, flags);

	return err;
}

static struct usbstub *usbstub_alloc(struct usb_device *udev,
						struct vusb_port_id *portid)
{
	struct usbstub *stub;

	stub = kzalloc(sizeof(*stub), GFP_KERNEL);
	if (!stub) {
		printk(KERN_ERR "no memory for alloc usbstub\n");
		return NULL;
	}
	kref_init(&stub->kref);
	stub->udev = usb_get_dev(udev);
	stub->portid = portid;
	spin_lock_init(&stub->submitting_lock);
	INIT_LIST_HEAD(&stub->submitting_list);

	return stub;
}

static void usbstub_release(struct kref *kref)
{
	struct usbstub *stub;

	stub = container_of(kref, struct usbstub, kref);

	usb_put_dev(stub->udev);
	stub->udev = NULL;
	stub->portid = NULL;
	kfree(stub);
}

static inline void usbstub_get(struct usbstub *stub)
{
	kref_get(&stub->kref);
}

static inline void usbstub_put(struct usbstub *stub)
{
	kref_put(&stub->kref, usbstub_release);
}

static int usbstub_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	const char *busid = dev_name(intf->dev.parent);
	struct vusb_port_id *portid = NULL;
	struct usbstub *stub = NULL;
	usbif_t *usbif = NULL;
	int retval = -ENODEV;

	/* hub currently not supported, so skip. */
	if (udev->descriptor.bDeviceClass ==  USB_CLASS_HUB)
		goto out;

	portid = find_portid_by_busid(busid);
	if (!portid)
		goto out;

	usbif = find_usbif(portid->domid, portid->handle);
	if (!usbif)
		goto out;

	switch (udev->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
		break;
	case USB_SPEED_HIGH:
		if (usbif->usb_ver >= USB_VER_USB20)
			break;
		/* fall through */
	default:
		goto out;
	}

	stub = find_attached_device(usbif, portid->portnum);
	if (!stub) {
		/* new connection */
		stub = usbstub_alloc(udev, portid);
		if (!stub)
			return -ENOMEM;
		usbbk_attach_device(usbif, stub);
		usbbk_hotplug_notify(usbif, portid->portnum, udev->speed);
	} else {
		/* maybe already called and connected by other intf */
		if (strncmp(stub->portid->phys_bus, busid, USBBACK_BUS_ID_SIZE))
			goto out; /* invalid call */
	}

	usbstub_get(stub);
	usb_set_intfdata(intf, stub);
	retval = 0;

out:
	return retval;
}

static void usbstub_disconnect(struct usb_interface *intf)
{
	struct usbstub *stub
		= (struct usbstub *) usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (!stub)
		return;

	if (stub->usbif) {
		usbbk_hotplug_notify(stub->usbif, stub->portid->portnum, 0);
		usbbk_detach_device(stub->usbif, stub);
	}
	usbbk_unlink_urbs(stub);
	usbstub_put(stub);
}

static ssize_t usbstub_show_portids(struct device_driver *driver,
		char *buf)
{
	struct vusb_port_id *portid;
	size_t count = 0;
	unsigned long flags;

	spin_lock_irqsave(&port_list_lock, flags);
	list_for_each_entry(portid, &port_list, id_list) {
		if (count >= PAGE_SIZE)
			break;
		count += scnprintf((char *)buf + count, PAGE_SIZE - count,
				"%s:%d:%d:%d\n",
				&portid->phys_bus[0],
				portid->domid,
				portid->handle,
				portid->portnum);
	}
	spin_unlock_irqrestore(&port_list_lock, flags);

	return count;
}

DRIVER_ATTR(port_ids, S_IRUSR, usbstub_show_portids, NULL);

/* table of devices that matches any usbdevice */
static const struct usb_device_id usbstub_table[] = {
		{ .driver_info = 1 }, /* wildcard, see usb_match_id() */
		{ } /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, usbstub_table);

static struct usb_driver usbback_usb_driver = {
		.name = "usbback",
		.probe = usbstub_probe,
		.disconnect = usbstub_disconnect,
		.id_table = usbstub_table,
		.no_dynamic_id = 1,
};

int __init usbstub_init(void)
{
	int err;

	err = usb_register(&usbback_usb_driver);
	if (err < 0) {
		printk(KERN_ERR "usbback: usb_register failed (error %d)\n", err);
		goto out;
	}

	err = driver_create_file(&usbback_usb_driver.drvwrap.driver,
				&driver_attr_port_ids);
	if (err)
		usb_deregister(&usbback_usb_driver);

out:
	return err;
}

void usbstub_exit(void)
{
	driver_remove_file(&usbback_usb_driver.drvwrap.driver,
				&driver_attr_port_ids);
	usb_deregister(&usbback_usb_driver);
}
