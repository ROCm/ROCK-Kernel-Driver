/*
 * drivers/pci/pci-sysfs.c
 *
 * (C) Copyright 2002 Greg Kroah-Hartman
 * (C) Copyright 2002 IBM Corp.
 *
 * File attributes for PCI devices
 *
 * Modeled after usb's driverfs.c 
 *
 */


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "pci.h"

#if BITS_PER_LONG == 32
#define LONG_FORMAT "\t%08lx"
#else
#define LONG_FORMAT "\t%16lx"
#endif

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

pci_config_attr(vendor, "%04x\n");
pci_config_attr(device, "%04x\n");
pci_config_attr(subsystem_vendor, "%04x\n");
pci_config_attr(subsystem_device, "%04x\n");
pci_config_attr(class, "%06x\n");
pci_config_attr(irq, "%u\n");

/* show resources */
static ssize_t
pci_show_resources(struct device * dev, char * buf)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	char * str = buf;
	int i;

	for (i = 0; i < DEVICE_COUNT_RESOURCE && pci_resource_start(pci_dev,i); i++) {
		str += sprintf(str,LONG_FORMAT LONG_FORMAT LONG_FORMAT "\n",
			       pci_resource_start(pci_dev,i),
			       pci_resource_end(pci_dev,i),
			       pci_resource_flags(pci_dev,i));
	}
	return (str - buf);
}

static DEVICE_ATTR(resource,S_IRUGO,pci_show_resources,NULL);

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
}
