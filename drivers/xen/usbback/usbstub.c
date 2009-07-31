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

static LIST_HEAD(usbstub_ids);
static DEFINE_SPINLOCK(usbstub_ids_lock);
static LIST_HEAD(grabbed_devices);
static DEFINE_SPINLOCK(grabbed_devices_lock);

struct usbstub *find_grabbed_device(int dom_id, int dev_id, int portnum)
{
	struct usbstub *stub;
	int found = 0;
	unsigned long flags;

	spin_lock_irqsave(&grabbed_devices_lock, flags);
	list_for_each_entry(stub, &grabbed_devices, grabbed_list) {
		if (stub->id->dom_id == dom_id
				&& stub->id->dev_id == dev_id
				&& stub->id->portnum == portnum) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&grabbed_devices_lock, flags);

	if (found)
		return stub;

	return NULL;
}

static struct usbstub *usbstub_alloc(struct usb_interface *interface,
						struct usbstub_id *stub_id)
{
	struct usbstub *stub;

	stub = kzalloc(sizeof(*stub), GFP_KERNEL);
	if (!stub) {
		printk(KERN_ERR "no memory for alloc usbstub\n");
		return NULL;
	}

	stub->udev = usb_get_dev(interface_to_usbdev(interface));
	stub->interface = interface;
	stub->id = stub_id;
	spin_lock_init(&stub->submitting_lock);
	INIT_LIST_HEAD(&stub->submitting_list);

	return stub;
}

static int usbstub_free(struct usbstub *stub)
{
	if (!stub)
		return -EINVAL;

	usb_put_dev(stub->udev);
	stub->interface = NULL;
	stub->udev = NULL;
	stub->id = NULL;
	kfree(stub);

	return 0;
}

static int usbstub_match_one(struct usb_interface *interface,
		struct usbstub_id *stub_id)
{
	const char *udev_busid = dev_name(interface->dev.parent);

	if (!(strncmp(stub_id->bus_id, udev_busid, USBBACK_BUS_ID_SIZE))) {
		return 1;
	}

	return 0;
}

static struct usbstub_id *usbstub_match(struct usb_interface *interface)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usbstub_id *stub_id;
	unsigned long flags;
	int found = 0;

	/* hub currently not supported, so skip. */
	if (udev->descriptor.bDeviceClass ==  USB_CLASS_HUB)
		return NULL;

	spin_lock_irqsave(&usbstub_ids_lock, flags);
	list_for_each_entry(stub_id, &usbstub_ids, id_list) {
		if (usbstub_match_one(interface, stub_id)) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&usbstub_ids_lock, flags);

	if (found)
		return stub_id;

	return NULL;
}

static void add_to_grabbed_devices(struct usbstub *stub)
{
	unsigned long flags;

	spin_lock_irqsave(&grabbed_devices_lock, flags);
	list_add(&stub->grabbed_list, &grabbed_devices);
	spin_unlock_irqrestore(&grabbed_devices_lock, flags);
}

static void remove_from_grabbed_devices(struct usbstub *stub)
{
	unsigned long flags;

	spin_lock_irqsave(&grabbed_devices_lock, flags);
	list_del(&stub->grabbed_list);
	spin_unlock_irqrestore(&grabbed_devices_lock, flags);
}

static int usbstub_probe(struct usb_interface *interface,
		const struct usb_device_id *id)
{
	struct usbstub_id *stub_id = NULL;
	struct usbstub *stub = NULL;
	usbif_t *usbif = NULL;
	int retval = 0;

	if ((stub_id = usbstub_match(interface))) {
		stub = usbstub_alloc(interface, stub_id);
		if (!stub)
			return -ENOMEM;

		usb_set_intfdata(interface, stub);
		add_to_grabbed_devices(stub);
		usbif = find_usbif(stub_id->dom_id, stub_id->dev_id);
		if (usbif) {
			usbbk_plug_device(usbif, stub);
			usbback_reconfigure(usbif);
		}

	} else
		retval = -ENODEV;

	return retval;
}

static void usbstub_disconnect(struct usb_interface *interface)
{
	struct usbstub *stub
		= (struct usbstub *) usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);

	if (!stub)
		return;

	if (stub->usbif) {
		usbback_reconfigure(stub->usbif);
		usbbk_unplug_device(stub->usbif, stub);
	}

	usbbk_unlink_urbs(stub);

	remove_from_grabbed_devices(stub);

	usbstub_free(stub);

	return;
}

static inline int str_to_vport(const char *buf,
					char *phys_bus,
					int *dom_id,
					int *dev_id,
					int *port)
{
	char *p;
	int len;
	int err;

	/* no physical bus */
	if (!(p = strchr(buf, ':')))
		return -EINVAL;

	len = p - buf;

	/* bad physical bus */
	if (len + 1 > USBBACK_BUS_ID_SIZE)
		return -EINVAL;

	strlcpy(phys_bus, buf, len + 1);
	err = sscanf(p + 1, "%d:%d:%d", dom_id, dev_id, port);
	if (err == 3)
		return 0;
	else
		return -EINVAL;
}

static int usbstub_id_add(const char *bus_id,
					const int dom_id,
					const int dev_id,
					const int portnum)
{
	struct usbstub_id *stub_id;
	unsigned long flags;

	stub_id = kzalloc(sizeof(*stub_id), GFP_KERNEL);
	if (!stub_id)
		return -ENOMEM;

	stub_id->dom_id = dom_id;
	stub_id->dev_id = dev_id;
	stub_id->portnum = portnum;

	strncpy(stub_id->bus_id, bus_id, USBBACK_BUS_ID_SIZE);

	spin_lock_irqsave(&usbstub_ids_lock, flags);
	list_add(&stub_id->id_list, &usbstub_ids);
	spin_unlock_irqrestore(&usbstub_ids_lock, flags);

	return 0;
}

static int usbstub_id_remove(const char *phys_bus,
					const int dom_id,
					const int dev_id,
					const int portnum)
{
	struct usbstub_id *stub_id, *tmp;
	int err = -ENOENT;
	unsigned long flags;

	spin_lock_irqsave(&usbstub_ids_lock, flags);
	list_for_each_entry_safe(stub_id, tmp, &usbstub_ids, id_list) {
		if (stub_id->dom_id == dom_id
				&& stub_id->dev_id == dev_id
				&& stub_id->portnum == portnum) {
			list_del(&stub_id->id_list);
			kfree(stub_id);

			err = 0;
		}
	}
	spin_unlock_irqrestore(&usbstub_ids_lock, flags);

	return err;
}

static ssize_t usbstub_vport_add(struct device_driver *driver,
		const char *buf, size_t count)
{
	int err = 0;

	char bus_id[USBBACK_BUS_ID_SIZE];
	int dom_id;
	int dev_id;
	int portnum;

	err = str_to_vport(buf, &bus_id[0], &dom_id, &dev_id, &portnum);
	if (err)
		goto out;

	err = usbstub_id_add(&bus_id[0], dom_id, dev_id, portnum);

out:
	if (!err)
		err = count;
	return err;
}

DRIVER_ATTR(new_vport, S_IWUSR, NULL, usbstub_vport_add);

static ssize_t usbstub_vport_remove(struct device_driver *driver,
		const char *buf, size_t count)
{
	int err = 0;

	char bus_id[USBBACK_BUS_ID_SIZE];
	int dom_id;
	int dev_id;
	int portnum;

	err = str_to_vport(buf, &bus_id[0], &dom_id, &dev_id, &portnum);
	if (err)
		goto out;

	err = usbstub_id_remove(&bus_id[0], dom_id, dev_id, portnum);

out:
	if (!err)
		err = count;
	return err;
}

DRIVER_ATTR(remove_vport, S_IWUSR, NULL, usbstub_vport_remove);

static ssize_t usbstub_vport_show(struct device_driver *driver,
		char *buf)
{
	struct usbstub_id *stub_id;
	size_t count = 0;
	unsigned long flags;

	spin_lock_irqsave(&usbstub_ids_lock, flags);
	list_for_each_entry(stub_id, &usbstub_ids, id_list) {
		if (count >= PAGE_SIZE)
			break;
		count += scnprintf((char *)buf + count, PAGE_SIZE - count,
				"%s:%d:%d:%d\n",
				&stub_id->bus_id[0],
				stub_id->dom_id,
				stub_id->dev_id,
				stub_id->portnum);
	}
	spin_unlock_irqrestore(&usbstub_ids_lock, flags);

	return count;
}

DRIVER_ATTR(vports, S_IRUSR, usbstub_vport_show, NULL);

static ssize_t usbstub_devices_show(struct device_driver *driver,
		char *buf)
{
	struct usbstub *stub;
	size_t count = 0;
	unsigned long flags;

	spin_lock_irqsave(&grabbed_devices_lock, flags);
	list_for_each_entry(stub, &grabbed_devices, grabbed_list) {
		if (count >= PAGE_SIZE)
			break;

		count += scnprintf((char *)buf + count, PAGE_SIZE - count,
					"%u-%s:%u.%u\n",
					stub->udev->bus->busnum,
					stub->udev->devpath,
					stub->udev->config->desc.bConfigurationValue,
					stub->interface->cur_altsetting->desc.bInterfaceNumber);

	}
	spin_unlock_irqrestore(&grabbed_devices_lock, flags);

	return count;
}

DRIVER_ATTR(grabbed_devices, S_IRUSR, usbstub_devices_show, NULL);

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
};

int __init usbstub_init(void)
{
 	int err;

	err = usb_register(&usbback_usb_driver);
	if (err < 0)
		goto out;
	if (!err)
		err = driver_create_file(&usbback_usb_driver.drvwrap.driver,
				&driver_attr_new_vport);
	if (!err)
		err = driver_create_file(&usbback_usb_driver.drvwrap.driver,
				&driver_attr_remove_vport);
	if (!err)
		err = driver_create_file(&usbback_usb_driver.drvwrap.driver,
				&driver_attr_vports);
	if (!err)
		err = driver_create_file(&usbback_usb_driver.drvwrap.driver,
				&driver_attr_grabbed_devices);
	if (err)
		usbstub_exit();

out:
	return err;
}

void usbstub_exit(void)
{
	driver_remove_file(&usbback_usb_driver.drvwrap.driver,
			&driver_attr_new_vport);
	driver_remove_file(&usbback_usb_driver.drvwrap.driver,
			&driver_attr_remove_vport);
	driver_remove_file(&usbback_usb_driver.drvwrap.driver,
				&driver_attr_vports);
	driver_remove_file(&usbback_usb_driver.drvwrap.driver,
				&driver_attr_grabbed_devices);

	usb_deregister(&usbback_usb_driver);
}
