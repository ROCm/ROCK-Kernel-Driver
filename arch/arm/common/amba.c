/*
 *  linux/arch/arm/common/amba.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/io.h>
#include <asm/hardware/amba.h>
#include <asm/sizes.h>

#define to_amba_device(d)	container_of(d, struct amba_device, dev)
#define to_amba_driver(d)	container_of(d, struct amba_driver, drv)

static struct amba_id *
amba_lookup(struct amba_id *table, struct amba_device *dev)
{
	int ret = 0;

	while (table->mask) {
		ret = (dev->periphid & table->mask) == table->id;
		if (ret)
			break;
		table++;
	}

	return ret ? table : NULL;
}

static int amba_match(struct device *dev, struct device_driver *drv)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *pcdrv = to_amba_driver(drv);

	return amba_lookup(pcdrv->id_table, pcdev) != NULL;
}

static int amba_suspend(struct device *dev, u32 state)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	int ret = 0;

	if (dev->driver && drv->suspend)
		ret = drv->suspend(to_amba_device(dev), state);
	return ret;
}

static int amba_resume(struct device *dev)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	int ret = 0;

	if (dev->driver && drv->resume)
		ret = drv->resume(to_amba_device(dev));
	return ret;
}

/*
 * Primecells are part of the Advanced Microcontroller Bus Architecture,
 * so we call the bus "amba".
 */
static struct bus_type amba_bustype = {
	.name		= "amba",
	.match		= amba_match,
	.suspend	= amba_suspend,
	.resume		= amba_resume,
};

static int __init amba_init(void)
{
	return bus_register(&amba_bustype);
}

postcore_initcall(amba_init);

/*
 * These are the device model conversion veneers; they convert the
 * device model structures to our more specific structures.
 */
static int amba_probe(struct device *dev)
{
	struct amba_device *pcdev = to_amba_device(dev);
	struct amba_driver *pcdrv = to_amba_driver(dev->driver);
	struct amba_id *id;

	id = amba_lookup(pcdrv->id_table, pcdev);

	return pcdrv->probe(pcdev, id);
}

static int amba_remove(struct device *dev)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	return drv->remove(to_amba_device(dev));
}

static void amba_shutdown(struct device *dev)
{
	struct amba_driver *drv = to_amba_driver(dev->driver);
	drv->shutdown(to_amba_device(dev));
}

/**
 *	amba_driver_register - register an AMBA device driver
 *	@drv: amba device driver structure
 *
 *	Register an AMBA device driver with the Linux device model
 *	core.  If devices pre-exist, the drivers probe function will
 *	be called.
 */
int amba_driver_register(struct amba_driver *drv)
{
	drv->drv.bus = &amba_bustype;

#define SETFN(fn)	if (drv->fn) drv->drv.fn = amba_##fn
	SETFN(probe);
	SETFN(remove);
	SETFN(shutdown);

	return driver_register(&drv->drv);
}

/**
 *	amba_driver_unregister - remove an AMBA device driver
 *	@drv: AMBA device driver structure to remove
 *
 *	Unregister an AMBA device driver from the Linux device
 *	model.  The device model will call the drivers remove function
 *	for each device the device driver is currently handling.
 */
void amba_driver_unregister(struct amba_driver *drv)
{
	driver_unregister(&drv->drv);
}


static void amba_device_release(struct device *dev)
{
	struct amba_device *d = to_amba_device(dev);

	if (d->res.parent)
		release_resource(&d->res);
	kfree(d);
}

static ssize_t show_id(struct device *_dev, char *buf)
{
	struct amba_device *dev = to_amba_device(_dev);
	return sprintf(buf, "%08x\n", dev->periphid);
}
static DEVICE_ATTR(id, S_IRUGO, show_id, NULL);

static ssize_t show_irq(struct device *_dev, char *buf)
{
	struct amba_device *dev = to_amba_device(_dev);
	return sprintf(buf, "%u\n", dev->irq);
}
static DEVICE_ATTR(irq, S_IRUGO, show_irq, NULL);

static ssize_t show_res(struct device *_dev, char *buf)
{
	struct amba_device *dev = to_amba_device(_dev);
	return sprintf(buf, "\t%08lx\t%08lx\t%08lx\n",
			dev->res.start, dev->res.end, dev->res.flags);
}
static DEVICE_ATTR(resource, S_IRUGO, show_res, NULL);

/**
 *	amba_device_register - register an AMBA device
 *	@dev: AMBA device to register
 *	@parent: parent memory resource
 *
 *	Setup the AMBA device, reading the cell ID if present.
 *	Claim the resource, and register the AMBA device with
 *	the Linux device manager.
 */
int amba_device_register(struct amba_device *dev, struct resource *parent)
{
	u32 pid, cid;
	void *tmp;
	int i, ret;

	dev->dev.release = amba_device_release;
	dev->dev.bus = &amba_bustype;
	dev->res.name = dev->dev.bus_id;

	ret = request_resource(parent, &dev->res);
	if (ret == 0) {
		tmp = ioremap(dev->res.start, SZ_4K);
		if (!tmp) {
			ret = -ENOMEM;
			goto out;
		}

		for (pid = 0, i = 0; i < 4; i++)
			pid |= (readl(tmp + 0xfe0 + 4 * i) & 255) << (i * 8);
		for (cid = 0, i = 0; i < 4; i++)
			cid |= (readl(tmp + 0xff0 + 4 * i) & 255) << (i * 8);

		iounmap(tmp);

		if (cid == 0xb105f00d)
			dev->periphid = pid;

		ret = device_register(&dev->dev);
		if (ret == 0) {
			device_create_file(&dev->dev, &dev_attr_id);
			device_create_file(&dev->dev, &dev_attr_irq);
			device_create_file(&dev->dev, &dev_attr_resource);
		} else {
 out:
			release_resource(&dev->res);
		}
	}
	return ret;
}

/**
 *	amba_device_unregister - unregister an AMBA device
 *	@dev: AMBA device to remove
 *
 *	Remove the specified AMBA device from the Linux device
 *	manager.  All files associated with this object will be
 *	destroyed, and device drivers notified that the device has
 *	been removed.  The AMBA device's resources including
 *	the amba_device structure will be freed once all
 *	references to it have been dropped.
 */
void amba_device_unregister(struct amba_device *dev)
{
	device_unregister(&dev->dev);
}

EXPORT_SYMBOL(amba_driver_register);
EXPORT_SYMBOL(amba_driver_unregister);
EXPORT_SYMBOL(amba_device_register);
EXPORT_SYMBOL(amba_device_unregister);
