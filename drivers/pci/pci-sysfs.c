/*
 * drivers/pci/pci-sysfs.c
 *
 * (C) Copyright 2002 Greg Kroah-Hartman
 * (C) Copyright 2002 IBM Corp.
 * (C) Copyright 2003 Matthew Wilcox
 * (C) Copyright 2003 Hewlett-Packard
 *
 * File attributes for PCI devices
 *
 * Modeled after usb's driverfs.c 
 *
 */


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/stat.h>

#include "pci.h"

/* show configuration fields */
#define pci_config_attr(field, format_string)				\
static ssize_t								\
show_##field (struct device *dev, char *buf)				\
{									\
	struct pci_dev *pdev;						\
									\
	pdev = to_pci_dev (dev);					\
	return sprintf (buf, format_string, pdev->field);		\
}									\
static DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

pci_config_attr(vendor, "0x%04x\n");
pci_config_attr(device, "0x%04x\n");
pci_config_attr(subsystem_vendor, "0x%04x\n");
pci_config_attr(subsystem_device, "0x%04x\n");
pci_config_attr(class, "0x%06x\n");
pci_config_attr(irq, "%u\n");

/* show resources */
static ssize_t
pci_show_resources(struct device * dev, char * buf)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	char * str = buf;
	int i;
	int max = 7;

	if (pci_dev->subordinate)
		max = DEVICE_COUNT_RESOURCE;

	for (i = 0; i < max; i++) {
		str += sprintf(str,"0x%016lx 0x%016lx 0x%016lx\n",
			       pci_resource_start(pci_dev,i),
			       pci_resource_end(pci_dev,i),
			       pci_resource_flags(pci_dev,i));
	}
	return (str - buf);
}

static DEVICE_ATTR(resource,S_IRUGO,pci_show_resources,NULL);

static ssize_t
pci_read_config(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(container_of(kobj,struct device,kobj));
	unsigned int size = 64;
	loff_t init_off = off;

	/* Several chips lock up trying to read undefined config space */
	if (capable(CAP_SYS_ADMIN)) {
		size = 256;
	} else if (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS) {
		size = 128;
	}

	if (off > size)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	} else {
		size = count;
	}

	while (off & 3) {
		unsigned char val;
		pci_read_config_byte(dev, off, &val);
		buf[off - init_off] = val;
		off++;
		if (--size == 0)
			break;
	}

	while (size > 3) {
		unsigned int val;
		pci_read_config_dword(dev, off, &val);
		buf[off - init_off] = val & 0xff;
		buf[off - init_off + 1] = (val >> 8) & 0xff;
		buf[off - init_off + 2] = (val >> 16) & 0xff;
		buf[off - init_off + 3] = (val >> 24) & 0xff;
		off += 4;
		size -= 4;
	}

	while (size > 0) {
		unsigned char val;
		pci_read_config_byte(dev, off, &val);
		buf[off - init_off] = val;
		off++;
		--size;
	}

	return count;
}

static ssize_t
pci_write_config(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct pci_dev *dev = to_pci_dev(container_of(kobj,struct device,kobj));
	unsigned int size = count;
	loff_t init_off = off;

	if (off > 256)
		return 0;
	if (off + count > 256) {
		size = 256 - off;
		count = size;
	}

	while (off & 3) {
		pci_write_config_byte(dev, off, buf[off - init_off]);
		off++;
		if (--size == 0)
			break;
	}

	while (size > 3) {
		unsigned int val = buf[off - init_off];
		val |= (unsigned int) buf[off - init_off + 1] << 8;
		val |= (unsigned int) buf[off - init_off + 2] << 16;
		val |= (unsigned int) buf[off - init_off + 3] << 24;
		pci_write_config_dword(dev, off, val);
		off += 4;
		size -= 4;
	}

	while (size > 0) {
		pci_write_config_byte(dev, off, buf[off - init_off]);
		off++;
		--size;
	}

	return count;
}

static struct bin_attribute pci_config_attr = {
	.attr =	{
		.name = "config",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = 256,
	.read = pci_read_config,
	.write = pci_write_config,
};

void pci_create_sysfs_dev_files (struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;

	/* current configuration's attributes */
	device_create_file (dev, &dev_attr_vendor);
	device_create_file (dev, &dev_attr_device);
	device_create_file (dev, &dev_attr_subsystem_vendor);
	device_create_file (dev, &dev_attr_subsystem_device);
	device_create_file (dev, &dev_attr_class);
	device_create_file (dev, &dev_attr_irq);
	device_create_file (dev, &dev_attr_resource);
	sysfs_create_bin_file(&dev->kobj, &pci_config_attr);
}
