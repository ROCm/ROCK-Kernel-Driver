/*
 * linux/include/asm-arm/arch-omap/bus.h
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

#ifndef __ASM_ARM_ARCH_OMAP_BUS_H
#define __ASM_ARM_ARCH_OMAP_BUS_H

extern struct bus_type omap_bus_types[];

/*
 * Description for physical device
 */
struct omap_dev {
	struct device	dev;		/* Standard device description */
	char		*name;
	unsigned int	devid;		/* OMAP device id */
	unsigned int	busid;		/* OMAP virtual busid */
	struct resource res;		/* Standard resource description */
	void		*mapbase;	/* OMAP physical address */
	unsigned int	irq[6];		/* OMAP interrupts */
	u64		*dma_mask;	/* Used by USB OHCI only */
};

#define OMAP_DEV(_d)	container_of((_d), struct omap_dev, dev)

#define omap_get_drvdata(d)	dev_get_drvdata(&(d)->dev)
#define omap_set_drvdata(d,p)	dev_set_drvdata(&(d)->dev, p)

/*
 * Description for device driver
 */
struct omap_driver {
	struct device_driver	drv;	/* Standard driver description */
	unsigned int		devid;	/* OMAP device id for bus */
	unsigned int		busid;	/* OMAP virtual busid */
	unsigned int		clocks; /* OMAP shared clocks */
	int (*probe)(struct omap_dev *);
	int (*remove)(struct omap_dev *);
	int (*suspend)(struct omap_dev *, u32);
	int (*resume)(struct omap_dev *);
};

#define OMAP_DRV(_d)	container_of((_d), struct omap_driver, drv)
#define OMAP_DRIVER_NAME(_omapdev) ((_omapdev)->dev.driver->name)

/*
 * Device ID numbers for bus types
 */
#define OMAP_OCP_DEVID_USB	0
#define OMAP_TIPB_DEVID_LCD	1
#define OMAP_TIPB_DEVID_MMC	2

/*
 * Virtual bus definitions for OMAP
 */
#define OMAP_NR_BUSES	2

#define OMAP_BUS_NAME_TIPB	"tipb"
#define OMAP_BUS_NAME_LBUS	"lbus"

enum {
	OMAP_BUS_TIPB = 0,
	OMAP_BUS_LBUS,
};

/* See arch/arm/mach-omap/bus.c for the rest of the bus definitions. */

extern int omap_driver_register(struct omap_driver *driver);
extern void omap_driver_unregister(struct omap_driver *driver);
extern int omap_device_register(struct omap_dev *odev);
extern void omap_device_unregister(struct omap_dev *odev);

#endif
