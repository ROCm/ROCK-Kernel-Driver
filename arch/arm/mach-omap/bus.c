/*
 * linux/arch/arm/mach-omap/bus.c
 *
 * Virtual bus for OMAP. Allows better power management, such as managing
 * shared clocks, and mapping of bus addresses to Local Bus addresses.
 *
 * See drivers/usb/host/ohci-omap.c or drivers/video/omap/omapfb.c for
 * examples on how to register drivers to this bus.
 *
 * Copyright (C) 2003 - 2004 Nokia Corporation
 * Written by Tony Lindgren <tony@atomide.com>
 * Portions of code based on sa1111.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>

#include <asm/arch/bus.h>

static int omap_bus_match(struct device *_dev, struct device_driver *_drv);
static int omap_bus_suspend(struct device *dev, u32 state);
static int omap_bus_resume(struct device *dev);

/*
 * OMAP bus definitions
 *
 * NOTE: Most devices should use TIPB. LBUS does automatic address mapping
 *	 to Local Bus addresses, and should only be used for Local Bus devices.
 *	 We may add new buses later on for power management reasons. Basically
 *	 we want to be able to turn off any bus if it's not used by device
 *	 drivers.
 */
static struct device omap_bus_devices[OMAP_NR_BUSES] = {
	{
		.bus_id		= OMAP_BUS_NAME_TIPB
	}, {
		.bus_id		= OMAP_BUS_NAME_LBUS
	},
};

static struct bus_type omap_bus_types[OMAP_NR_BUSES] = {
	{
		.name		= OMAP_BUS_NAME_TIPB,
		.match		= omap_bus_match,
		.suspend	= omap_bus_suspend,
		.resume		= omap_bus_resume,
	}, {
		.name		= OMAP_BUS_NAME_LBUS,	/* Local bus on 1510 */
		.match		= omap_bus_match,
		.suspend	= omap_bus_suspend,
		.resume		= omap_bus_resume,
	},
};

#ifdef CONFIG_ARCH_OMAP1510
/*
 * NOTE: This code _should_ go somewhere else. But let's wait for the
 *	 dma-mapping code to settle down first.
 */

/*
 * Test for Local Bus device in order to do address translation between
 * dma_handle and Local Bus address.
 */
inline int dmadev_uses_omap_lbus(struct device * dev)
{
	if (dev == NULL || !cpu_is_omap1510())
		return 0;
	return dev->bus == &omap_bus_types[OMAP_BUS_LBUS] ? 1 : 0;
}

/*
 * Translate bus address to Local Bus address for dma-mapping
 */
inline int dmadev_to_lbus(dma_addr_t addr)
{
	return bus_to_lbus(addr);
}

/*
 * Translate Local Bus address to bus address for dma-mapping
 */
inline int lbus_to_dmadev(dma_addr_t addr)
{
	return lbus_to_bus(addr);
}
#endif

static int omap_bus_match(struct device *dev, struct device_driver *drv)
{
	struct omap_dev *omapdev = OMAP_DEV(dev);
	struct omap_driver *omapdrv = OMAP_DRV(drv);

	return omapdev->devid == omapdrv->devid;
}

static int omap_bus_suspend(struct device *dev, u32 state)
{
	struct omap_dev *omapdev = OMAP_DEV(dev);
	struct omap_driver *omapdrv = OMAP_DRV(dev->driver);
	int ret = 0;

	if (omapdrv && omapdrv->suspend)
		ret = omapdrv->suspend(omapdev, state);
	return ret;
}

static int omap_bus_resume(struct device *dev)
{
	struct omap_dev *omapdev = OMAP_DEV(dev);
	struct omap_driver *omapdrv = OMAP_DRV(dev->driver);
	int ret = 0;

	if (omapdrv && omapdrv->resume)
		ret = omapdrv->resume(omapdev);
	return ret;
}

static int omap_device_probe(struct device *dev)
{
	struct omap_dev *omapdev = OMAP_DEV(dev);
	struct omap_driver *omapdrv = OMAP_DRV(dev->driver);
	int ret = -ENODEV;

	if (omapdrv && omapdrv->probe)
		ret = omapdrv->probe(omapdev);

	return ret;
}

static int omap_device_remove(struct device *dev)
{
	struct omap_dev *omapdev = OMAP_DEV(dev);
	struct omap_driver *omapdrv = OMAP_DRV(dev->driver);
	int ret = 0;

	if (omapdrv && omapdrv->remove)
		ret = omapdrv->remove(omapdev);
	return ret;
}

int omap_device_register(struct omap_dev *odev)
{
	if (!odev)
		return -EINVAL;

	if (odev->busid < 0 || odev->busid >= OMAP_NR_BUSES) {
		printk(KERN_ERR "%s: busid invalid: %s: bus: %i\n",
		       __FUNCTION__, odev->name, odev->busid);
		return -EINVAL;
	}

	odev->dev.parent = &omap_bus_devices[odev->busid];
	odev->dev.bus = &omap_bus_types[odev->busid];

	/* This is needed for USB OHCI to work */
	if (odev->dma_mask)
		odev->dev.dma_mask = odev->dma_mask;

	if (odev->coherent_dma_mask)
		odev->dev.coherent_dma_mask = odev->coherent_dma_mask;

	snprintf(odev->dev.bus_id, BUS_ID_SIZE, "%s%u",
		 odev->name, odev->devid);

	printk("Registering OMAP device '%s'. Parent at %s\n",
		 odev->dev.bus_id, odev->dev.parent->bus_id);

	return device_register(&odev->dev);
}

void omap_device_unregister(struct omap_dev *odev)
{
	if (odev)
		device_unregister(&odev->dev);
}

int omap_driver_register(struct omap_driver *driver)
{
	int ret;

	if (driver->busid < 0 || driver->busid >= OMAP_NR_BUSES) {
		printk(KERN_ERR "%s: busid invalid: bus: %i device: %i\n",
		       __FUNCTION__, driver->busid, driver->devid);
		return -EINVAL;
	}

	driver->drv.probe = omap_device_probe;
	driver->drv.remove = omap_device_remove;
	driver->drv.bus = &omap_bus_types[driver->busid];

	/*
	 * driver_register calls bus_add_driver
	 */
	ret = driver_register(&driver->drv);

	return ret;
}

void omap_driver_unregister(struct omap_driver *driver)
{
	driver_unregister(&driver->drv);
}

static int __init omap_bus_init(void)
{
	int i, ret;

	/* Initialize all OMAP virtual buses */
	for (i = 0; i < OMAP_NR_BUSES; i++) {
		ret = device_register(&omap_bus_devices[i]);
		if (ret != 0) {
			printk(KERN_ERR "Unable to register bus device %s\n",
			       omap_bus_devices[i].bus_id);
			continue;
		}
		ret = bus_register(&omap_bus_types[i]);
		if (ret != 0) {
			printk(KERN_ERR "Unable to register bus %s\n",
			       omap_bus_types[i].name);
			device_unregister(&omap_bus_devices[i]);
		}
	}
	printk("OMAP virtual buses initialized\n");

	return ret;
}

static void __exit omap_bus_exit(void)
{
	int i;

	/* Unregister all OMAP virtual buses */
	for (i = 0; i < OMAP_NR_BUSES; i++) {
		bus_unregister(&omap_bus_types[i]);
		device_unregister(&omap_bus_devices[i]);
	}
}

module_init(omap_bus_init);
module_exit(omap_bus_exit);

MODULE_DESCRIPTION("Virtual bus for OMAP");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(omap_bus_types);
EXPORT_SYMBOL(omap_driver_register);
EXPORT_SYMBOL(omap_driver_unregister);
EXPORT_SYMBOL(omap_device_register);
EXPORT_SYMBOL(omap_device_unregister);

#ifdef CONFIG_ARCH_OMAP1510
EXPORT_SYMBOL(dmadev_uses_omap_lbus);
EXPORT_SYMBOL(dmadev_to_lbus);
EXPORT_SYMBOL(lbus_to_dmadev);
#endif
