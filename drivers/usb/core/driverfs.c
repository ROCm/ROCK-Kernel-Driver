/*
 * drivers/usb/core/driverfs.c
 *
 * (C) Copyright 2002 David Brownell
 * (C) Copyright 2002 Greg Kroah-Hartman
 * (C) Copyright 2002 IBM Corp.
 *
 * All of the driverfs file attributes for usb devices and interfaces.
 *
 */


#include <linux/config.h>
#include <linux/kernel.h>

#ifdef CONFIG_USB_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif
#include <linux/usb.h>

#include "usb.h"

/* Active configuration fields */
#define usb_actconfig_show(field, multiplier, format_string)		\
static ssize_t  show_##field (struct device *dev, char *buf)		\
{									\
	struct usb_device *udev;					\
									\
	udev = to_usb_device (dev);					\
	if (udev->actconfig)						\
		return sprintf (buf, format_string,			\
				udev->actconfig->desc.field * multiplier);	\
	else								\
		return 0;						\
}									\

#define usb_actconfig_attr(field, multiplier, format_string)		\
usb_actconfig_show(field, multiplier, format_string)			\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_actconfig_attr (bNumInterfaces, 1, "%2d\n")
usb_actconfig_attr (bmAttributes, 1, "%2x\n")
usb_actconfig_attr (bMaxPower, 2, "%3dmA\n")

/* configuration value is always present, and r/w */
usb_actconfig_show(bConfigurationValue, 1, "%u\n");

static ssize_t
set_bConfigurationValue (struct device *dev, const char *buf, size_t count)
{
	struct usb_device	*udev = udev = to_usb_device (dev);
	int			config, value;

	if (sscanf (buf, "%u", &config) != 1 || config > 255)
		return -EINVAL;
	down(&udev->serialize);
	value = usb_set_configuration (udev, config);
	up(&udev->serialize);
	return (value < 0) ? value : count;
}

static DEVICE_ATTR(bConfigurationValue, S_IRUGO | S_IWUSR, 
		show_bConfigurationValue, set_bConfigurationValue);

/* String fields */
#define usb_string_attr(name, field)		\
static ssize_t  show_##name(struct device *dev, char *buf)		\
{									\
	struct usb_device *udev;					\
	int len;							\
									\
	udev = to_usb_device (dev);					\
	len = usb_string(udev, udev->descriptor.field, buf, PAGE_SIZE);	\
	if (len < 0)							\
		return 0;						\
	buf[len] = '\n';						\
	buf[len+1] = 0;							\
	return len+1;							\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL);

usb_string_attr(product, iProduct);
usb_string_attr(manufacturer, iManufacturer);
usb_string_attr(serial, iSerialNumber);

static ssize_t
show_speed (struct device *dev, char *buf)
{
	struct usb_device *udev;
	char *speed;

	udev = to_usb_device (dev);

	switch (udev->speed) {
	case USB_SPEED_LOW:
		speed = "1.5";
		break;
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_FULL:
		speed = "12";
		break;
	case USB_SPEED_HIGH:
		speed = "480";
		break;
	default:
		speed = "unknown";
	}
	return sprintf (buf, "%s\n", speed);
}
static DEVICE_ATTR(speed, S_IRUGO, show_speed, NULL);

static ssize_t
show_devnum (struct device *dev, char *buf)
{
	struct usb_device *udev;

	udev = to_usb_device (dev);
	return sprintf (buf, "%d\n", udev->devnum);
}
static DEVICE_ATTR(devnum, S_IRUGO, show_devnum, NULL);

static ssize_t
show_version (struct device *dev, char *buf)
{
	struct usb_device *udev;

	udev = to_usb_device (dev);
	return sprintf (buf, "%2x.%02x\n", udev->descriptor.bcdUSB >> 8, 
			udev->descriptor.bcdUSB & 0xff);
}
static DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static ssize_t
show_maxchild (struct device *dev, char *buf)
{
	struct usb_device *udev;

	udev = to_usb_device (dev);
	return sprintf (buf, "%d\n", udev->maxchild);
}
static DEVICE_ATTR(maxchild, S_IRUGO, show_maxchild, NULL);

/* Descriptor fields */
#define usb_descriptor_attr(field, format_string)			\
static ssize_t								\
show_##field (struct device *dev, char *buf)				\
{									\
	struct usb_device *udev;					\
									\
	udev = to_usb_device (dev);					\
	return sprintf (buf, format_string, udev->descriptor.field);	\
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_descriptor_attr (idVendor, "%04x\n")
usb_descriptor_attr (idProduct, "%04x\n")
usb_descriptor_attr (bcdDevice, "%04x\n")
usb_descriptor_attr (bDeviceClass, "%02x\n")
usb_descriptor_attr (bDeviceSubClass, "%02x\n")
usb_descriptor_attr (bDeviceProtocol, "%02x\n")
usb_descriptor_attr (bNumConfigurations, "%d\n")


void usb_create_driverfs_dev_files (struct usb_device *udev)
{
	struct device *dev = &udev->dev;

	/* current configuration's attributes */
	device_create_file (dev, &dev_attr_bNumInterfaces);
	device_create_file (dev, &dev_attr_bConfigurationValue);
	device_create_file (dev, &dev_attr_bmAttributes);
	device_create_file (dev, &dev_attr_bMaxPower);

	/* device attributes */
	device_create_file (dev, &dev_attr_idVendor);
	device_create_file (dev, &dev_attr_idProduct);
	device_create_file (dev, &dev_attr_bcdDevice);
	device_create_file (dev, &dev_attr_bDeviceClass);
	device_create_file (dev, &dev_attr_bDeviceSubClass);
	device_create_file (dev, &dev_attr_bDeviceProtocol);
	device_create_file (dev, &dev_attr_bNumConfigurations);

	/* speed varies depending on how you connect the device */
	device_create_file (dev, &dev_attr_speed);
	// FIXME iff there are other speed configs, show how many

	if (udev->descriptor.iManufacturer)
		device_create_file (dev, &dev_attr_manufacturer);
	if (udev->descriptor.iProduct)
		device_create_file (dev, &dev_attr_product);
	if (udev->descriptor.iSerialNumber)
		device_create_file (dev, &dev_attr_serial);

	device_create_file (dev, &dev_attr_devnum);
	device_create_file (dev, &dev_attr_version);
	device_create_file (dev, &dev_attr_maxchild);
}

/* Interface fields */
#define usb_intf_attr(field, format_string)				\
static ssize_t								\
show_##field (struct device *dev, char *buf)				\
{									\
	struct usb_interface *intf = to_usb_interface (dev);		\
									\
	return sprintf (buf, format_string, intf->cur_altsetting->desc.field); \
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

usb_intf_attr (bInterfaceNumber, "%02x\n")
usb_intf_attr (bAlternateSetting, "%2d\n")
usb_intf_attr (bNumEndpoints, "%02x\n")
usb_intf_attr (bInterfaceClass, "%02x\n")
usb_intf_attr (bInterfaceSubClass, "%02x\n")
usb_intf_attr (bInterfaceProtocol, "%02x\n")
usb_intf_attr (iInterface, "%02x\n")

void usb_create_driverfs_intf_files (struct usb_interface *intf)
{
	device_create_file (&intf->dev, &dev_attr_bInterfaceNumber);
	device_create_file (&intf->dev, &dev_attr_bAlternateSetting);
	device_create_file (&intf->dev, &dev_attr_bNumEndpoints);
	device_create_file (&intf->dev, &dev_attr_bInterfaceClass);
	device_create_file (&intf->dev, &dev_attr_bInterfaceSubClass);
	device_create_file (&intf->dev, &dev_attr_bInterfaceProtocol);
	device_create_file (&intf->dev, &dev_attr_iInterface);
}
