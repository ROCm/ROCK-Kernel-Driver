/*
 *  drivers/s390/cio/css.c
 *  driver for channel subsystem
 *   $Revision: 1.49 $
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <asm/ccwdev.h> // FIXME: layering violation, remove this

#include "css.h"
#include "cio.h"
#include "cio_debug.h"
#include "device.h" // FIXME: dito
#include "ioasm.h"

struct subchannel *ioinfo[__MAX_SUBCHANNELS];
unsigned int highest_subchannel;
int css_init_done = 0;

struct device css_bus_device = {
	.bus_id = "css0",
};

static int
css_alloc_subchannel(int irq)
{
	struct subchannel *sch;
	int ret;

	if (ioinfo[irq])
		/* There already is a struct subchannel for this irq. */
		return -EBUSY;

	sch = kmalloc (sizeof (*sch), GFP_KERNEL | GFP_DMA);
	if (sch == NULL)
		return -ENOMEM;
	ret = cio_validate_subchannel (sch, irq);
	if (ret < 0) {
		kfree(sch);
		return ret;
	}
	if (irq > highest_subchannel)
		highest_subchannel = irq;

	if (sch->st != SUBCHANNEL_TYPE_IO) {
		/* For now we ignore all non-io subchannels. */
		kfree(sch);
		return -EINVAL;
	}

	ioinfo[irq] = sch;

	return 0;
}

static void
css_free_subchannel(int irq)
{
	struct subchannel *sch;

	sch = ioinfo[irq];
	if (sch) {
		ioinfo[irq] = NULL;
		kfree(sch);
	}
	
}

static int
css_register_subchannel(struct subchannel *sch)
{
	int ret;

	/* Initialize the subchannel structure */
	sch->dev.parent = &css_bus_device;
	sch->dev.bus = &css_bus_type;

	/* Set a name for the subchannel */
	snprintf (sch->dev.bus_id, BUS_ID_SIZE, "0.0.%04x", sch->irq);

	/* make it known to the system */
	ret = device_register(&sch->dev);
	if (ret)
		printk (KERN_WARNING "%s: could not register %s\n",
			__func__, sch->dev.bus_id);
	return ret;
}

int
css_probe_device(int irq)
{
	int ret;

	ret = css_alloc_subchannel(irq);
	if (ret)
		return ret;
	ret = css_register_subchannel(ioinfo[irq]);
	if (ret)
		css_free_subchannel(irq);
	return ret;
}

/*
 * Rescan for new devices. FIXME: This is slow.
 */
static void
do_process_crw(void *ignore)
{
	int irq, ret;

	for (irq = 0; irq < __MAX_SUBCHANNELS; irq++) {
		if (ioinfo[irq])
			continue;
		ret = css_probe_device(irq);
		/* No more memory. It doesn't make sense to continue. No
		 * panic because this can happen in midflight and just
		 * because we can't use a new device is no reason to crash
		 * the system. */
		if (ret == -ENOMEM)
			break;
		/* -ENXIO indicates that there are no more subchannels. */
		if (ret == -ENXIO)
			break;
	}
}

/*
 * Called from the machine check handler for subchannel report words.
 * Note: this is called disabled from the machine check handler itself.
 */
void
css_process_crw(int irq)
{
	static DECLARE_WORK(work, do_process_crw, 0);
	struct subchannel *sch;
	int ccode, devno;

	CIO_CRW_EVENT(2, "source is subchannel %04X\n", irq);

	sch = ioinfo[irq];
	if (sch == NULL) {
		queue_work(ccw_device_work, &work);
		return;
	}
	if (!sch->dev.driver_data)
		return;
	devno = sch->schib.pmcw.dev;
	/* FIXME: css_process_crw must not know about ccw_device */
	dev_fsm_event(sch->dev.driver_data, DEV_EVENT_NOTOPER);
	ccode = stsch(irq, &sch->schib);
	if (!ccode)
		if (devno != sch->schib.pmcw.dev)
			queue_work(ccw_device_work, &work);
}

/*
 * some of the initialization has already been done from init_IRQ(),
 * here we do the rest now that the driver core is running.
 * Currently, this functions scans all the subchannel structures for
 * devices. The long term plan is to remove ioinfo[] and then the
 * struct subchannel's will be created during probing. 
 */
static int __init
init_channel_subsystem (void)
{
	int ret, irq;

	if ((ret = bus_register(&css_bus_type)))
		goto out;
	if ((ret = device_register (&css_bus_device)))
		goto out_bus;

	css_init_done = 1;

	ctl_set_bit(6, 28);

	for (irq = 0; irq < __MAX_SUBCHANNELS; irq++) {
		if (!ioinfo[irq]) {
			ret = css_alloc_subchannel(irq);
			if (ret == -ENOMEM)
				panic("Out of memory in "
				      "init_channel_subsystem\n");
			/* -ENXIO: no more subchannels. */
			if (ret == -ENXIO)
				break;
			if (ret)
				continue;
		}
		/*
		 * We register ALL valid subchannels in ioinfo, even those
		 * that have been present before init_channel_subsystem.
		 * These subchannels can't have been registered yet (kmalloc
		 * not working) so we do it now. This is true e.g. for the
		 * console subchannel.
		 */
		css_register_subchannel(ioinfo[irq]);
	}
	return 0;

out_bus:
	bus_unregister(&css_bus_type);
out:
	return ret;
}

/*
 * find a driver for a subchannel. They identify by the subchannel
 * type with the exception that the console subchannel driver has its own
 * subchannel type although the device is an i/o subchannel
 */
static int
css_bus_match (struct device *dev, struct device_driver *drv)
{
	struct subchannel *sch = container_of (dev, struct subchannel, dev);
	struct css_driver *driver = container_of (drv, struct css_driver, drv);

	if (sch->st == driver->subchannel_type)
		return 1;

	return 0;
}

struct bus_type css_bus_type = {
	.name  = "css",
	.match = &css_bus_match,
};

subsys_initcall(init_channel_subsystem);

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(css_bus_type);

