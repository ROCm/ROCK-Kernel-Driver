/*
 * USB Serial Converter Bus specific functions
 *
 * Copyright (C) 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>

#ifdef CONFIG_USB_SERIAL_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif

#include "usb-serial.h"


static int usb_serial_device_match (struct device *dev, struct device_driver *drv)
{
	struct usb_serial_device_type *driver;
	const struct usb_serial_port *port;

	/*
	 * drivers are already assigned to ports in serial_probe so it's
	 * a simple check here.
	 */
	port = to_usb_serial_port(dev);
	if (!port)
		return 0;

	driver = to_usb_serial_driver(drv);

	if (driver == port->serial->type)
		return 1;

	return 0;
}

struct bus_type usb_serial_bus_type = {
	.name =		"usb-serial",
	.match =	usb_serial_device_match,
};

static int usb_serial_device_probe (struct device *dev)
{
	struct usb_serial_device_type *driver;
	struct usb_serial_port *port;
	int retval = 0;
	int minor;

	port = to_usb_serial_port(dev);
	if (!port) {
		retval = -ENODEV;
		goto exit;
	}

	driver = port->serial->type;
	if (driver->port_probe) {
		if (!try_module_get(driver->owner)) {
			err ("module get failed, exiting");
			retval = -EIO;
			goto exit;
		}
		retval = driver->port_probe (port);
		module_put(driver->owner);
		if (retval)
			goto exit;
	}

	minor = port->number;

	tty_register_devfs (&usb_serial_tty_driver, 0, minor);
	info("%s converter now attached to ttyUSB%d (or usb/tts/%d for devfs)",
	     driver->name, minor, minor);

exit:
	return retval;
}

static int usb_serial_device_remove (struct device *dev)
{
	struct usb_serial_device_type *driver;
	struct usb_serial_port *port;
	int retval = 0;
	int minor;

	port = to_usb_serial_port(dev);
	if (!port) {
		return -ENODEV;
	}

	driver = port->serial->type;
	if (driver->port_remove) {
		if (!try_module_get(driver->owner)) {
			err ("module get failed, exiting");
			retval = -EIO;
			goto exit;
		}
		retval = driver->port_remove (port);
		module_put(driver->owner);
	}
exit:
	minor = port->number;
	tty_unregister_devfs (&usb_serial_tty_driver, minor);
	info("%s converter now disconnected from ttyUSB%d",
	     driver->name, minor);

	return retval;
}

int usb_serial_bus_register(struct usb_serial_device_type *device)
{
	int retval;

	device->driver.name = (char *)device->name;
	device->driver.bus = &usb_serial_bus_type;
	device->driver.probe = usb_serial_device_probe;
	device->driver.remove = usb_serial_device_remove;

	retval = driver_register(&device->driver);

	return retval;
}

void usb_serial_bus_deregister(struct usb_serial_device_type *device)
{
	driver_unregister (&device->driver);
}

