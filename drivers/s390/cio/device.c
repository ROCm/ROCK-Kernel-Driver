/*
 *  drivers/s390/cio/device.c
 *  bus driver for ccw devices
 *   $Revision: 1.44 $
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/device.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>

#include "cio.h"
#include "css.h"
#include "device.h"

/******************* bus type handling ***********************/

/* The Linux driver model distinguishes between a bus type and
 * the bus itself. Of course we only have one channel
 * subsystem driver and one channel system per machine, but
 * we still use the abstraction. T.R. says it's a good idea. */
static int
ccw_bus_match (struct device * dev, struct device_driver * drv)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_driver *cdrv = to_ccwdrv(drv);
	const struct ccw_device_id *ids = cdrv->ids, *found;

	if (!ids)
		return 0;

	found = ccw_device_id_match(ids, &cdev->id);
	if (!found)
		return 0;

	cdev->id.driver_info = found->driver_info;

	return 1;
}

/*
 * Hotplugging interface for ccw devices.
 * Heavily modeled on pci and usb hotplug.
 */
static int
ccw_hotplug (struct device *dev, char **envp, int num_envp,
	     char *buffer, int buffer_size)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	int i = 0;
	int length = 0;

	if (!cdev)
		return -ENODEV;

	if (cdev->private->state == DEV_STATE_NOT_OPER)
		return -ENODEV;

	/* what we want to pass to /sbin/hotplug */

	envp[i++] = buffer;
	length += snprintf(buffer, buffer_size - length, "CU_TYPE=%04X",
			   cdev->id.cu_type);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	buffer += length;

	envp[i++] = buffer;
	length += snprintf(buffer, buffer_size - length, "CU_MODEL=%02X",
			   cdev->id.cu_model);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	buffer += length;

	/* The next two can be zero, that's ok for us */
	envp[i++] = buffer;
	length += snprintf(buffer, buffer_size - length, "DEV_TYPE=%04X",
			   cdev->id.dev_type);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	buffer += length;

	envp[i++] = buffer;
	length += snprintf(buffer, buffer_size - length, "DEV_MODEL=%02X",
			   cdev->id.dev_model);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;

	envp[i] = 0;

	return 0;
}

struct bus_type ccw_bus_type = {
	.name  = "ccw",
	.match = &ccw_bus_match,
	.hotplug = &ccw_hotplug,
};

static int io_subchannel_probe (struct device *);
void io_subchannel_irq (struct device *);

static struct css_driver io_subchannel_driver = {
	.subchannel_type = SUBCHANNEL_TYPE_IO,
	.drv = {
		.name = "io_subchannel",
		.bus  = &css_bus_type,
		.probe = &io_subchannel_probe,
	},
	.irq = io_subchannel_irq,
};

static int __init
init_ccw_bus_type (void)
{
	int ret;
	if ((ret = bus_register (&ccw_bus_type)))
		return ret;

	return driver_register(&io_subchannel_driver.drv);
}

static void __exit
cleanup_ccw_bus_type (void)
{
	driver_unregister(&io_subchannel_driver.drv);
	bus_unregister(&ccw_bus_type);
}

subsys_initcall(init_ccw_bus_type);
module_exit(cleanup_ccw_bus_type);

/************************ device handling **************************/

/*
 * A ccw_device has some interfaces in sysfs in addition to the
 * standard ones.
 * The following entries are designed to export the information which
 * resided in 2.4 in /proc/subchannels. Subchannel and device number
 * are obvious, so they don't have an entry :)
 * TODO: Split chpids and pimpampom up? Where is "in use" in the tree?
 */
static ssize_t
chpids_show (struct device * dev, char * buf, size_t count, loff_t off)
{
	struct subchannel *sch = to_subchannel(dev);
	struct ssd_info *ssd = &sch->ssd_info;
	ssize_t ret = 0;
	int chp;

	for (chp = 0; chp < 8; chp++)
		ret += sprintf (buf+ret, "%02x ", ssd->chpid[chp]);

	ret += sprintf (buf+ret, "\n");
	return off ? 0 : min((ssize_t)count, ret);
}

static ssize_t
pimpampom_show (struct device * dev, char * buf, size_t count, loff_t off)
{
	struct subchannel *sch = to_subchannel(dev);
	struct pmcw *pmcw = &sch->schib.pmcw;

	return off ? 0 : snprintf (buf, count, "%02x %02x %02x\n",
				   pmcw->pim, pmcw->pam, pmcw->pom);
}

static ssize_t
devtype_show (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_device_id *id = &(cdev->id);

	if (id->dev_type != 0)
		return off ? 0 : snprintf(buf, count, "%04x/%02x\n",
					  id->dev_type, id->dev_model);
	else
		return off ? 0 : snprintf(buf, count, "n/a\n");
}

static ssize_t
cutype_show (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_device_id *id = &(cdev->id);

	return off ? 0 : snprintf(buf, count, "%04x/%02x\n",
				  id->cu_type, id->cu_model);
}

static ssize_t
online_show (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct ccw_device *cdev = to_ccwdev(dev);

	return off ? 0 : snprintf(buf, count, cdev->online ? "yes\n" : "no\n");
}

void
ccw_device_set_offline(struct ccw_device *cdev)
{
	if (!cdev)
		return;
	if (!cdev->online || !cdev->drv)
		return;

	if (cdev->drv->set_offline)
		if (cdev->drv->set_offline(cdev) != 0)
			return;

	cdev->online = 0;
	spin_lock_irq(cdev->ccwlock);
	ccw_device_offline(cdev);
	spin_unlock_irq(cdev->ccwlock);
	wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
}

void
ccw_device_set_online(struct ccw_device *cdev)
{
	if (!cdev || !cdev->handler)
		return;
	if (cdev->online || !cdev->drv)
		return;

	spin_lock_irq(cdev->ccwlock);
	ccw_device_online(cdev);
	spin_unlock_irq(cdev->ccwlock);
	wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	if (cdev->private->state != DEV_STATE_ONLINE)
		return;
	if (!cdev->drv->set_online || cdev->drv->set_online(cdev) == 0) {
		cdev->online = 1;
		return;
	}
	spin_lock_irq(cdev->ccwlock);
	ccw_device_offline(cdev);
	spin_unlock_irq(cdev->ccwlock);
	wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
}

static ssize_t
online_store (struct device *dev, const char *buf, size_t count, loff_t off)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	unsigned int value;

	if (off)
		return 0;

	if (!cdev->drv)
		return count;

	sscanf(buf, "%u", &value);

	if (value) {
		if (cdev->drv->set_online)
			ccw_device_set_online(cdev);
	} else {
		if (cdev->drv->set_offline)
			ccw_device_set_offline(cdev);
	}

	return count;
}

static DEVICE_ATTR(chpids, 0444, chpids_show, NULL);
static DEVICE_ATTR(pimpampom, 0444, pimpampom_show, NULL);
static DEVICE_ATTR(devtype, 0444, devtype_show, NULL);
static DEVICE_ATTR(cutype, 0444, cutype_show, NULL);
static DEVICE_ATTR(online, 0644, online_show, online_store);

static inline int
subchannel_add_files (struct device *dev)
{
	int ret;

	if ((ret = device_create_file(dev, &dev_attr_chpids)) ||
	    (ret = device_create_file(dev, &dev_attr_pimpampom))) {
		device_remove_file(dev, &dev_attr_chpids);
	}
	return ret;
}

static inline int
device_add_files (struct device *dev)
{
	int ret;

	if ((ret = device_create_file(dev, &dev_attr_devtype)) ||
	    (ret = device_create_file(dev, &dev_attr_cutype))  ||
	    (ret = device_create_file(dev, &dev_attr_online))) {
		device_remove_file(dev, &dev_attr_cutype);
		device_remove_file(dev, &dev_attr_devtype);
	}
	return ret;
}

/* this is a simple abstraction for device_register that sets the
 * correct bus type and adds the bus specific files */
static int
ccw_device_register(struct ccw_device *cdev)
{
	struct device *dev = &cdev->dev;
	int ret;

	dev->bus = &ccw_bus_type;

	if ((ret = device_add(dev)))
		return ret;

	if ((ret = device_add_files(dev)))
		device_unregister(dev);

	return ret;
}

static void
ccw_device_release(struct device *dev)
{
	struct ccw_device *cdev;

	cdev = to_ccwdev(dev);
	kfree(cdev->private);
	kfree(cdev);
}

/*
 * Register recognized device.
 */
void
io_subchannel_register(void *data)
{
	struct ccw_device *cdev;
	struct subchannel *sch;
	int ret;

	cdev = (struct ccw_device *) data;
	sch = to_subchannel(cdev->dev.parent);

	/* make it known to the system */
	ret = ccw_device_register(cdev);
	if (ret) {
		printk (KERN_WARNING "%s: could not register %s\n",
			__func__, cdev->dev.bus_id);
		sch->dev.driver_data = 0;
		kfree (cdev->private);
		kfree (cdev);
		return;
	}

	ret = subchannel_add_files(cdev->dev.parent);
	if (ret)
		printk(KERN_WARNING "%s: could not add attributes to %04x\n",
		       __func__, sch->irq);
}

static void
io_subchannel_recog(struct ccw_device *cdev, struct subchannel *sch)
{
	sch->dev.driver_data = cdev;
	sch->driver = &io_subchannel_driver;
	cdev->ccwlock = &sch->lock;
	*cdev->private = (struct ccw_device_private) {
		.devno	= sch->schib.pmcw.dev,
		.irq	= sch->irq,
		.state	= DEV_STATE_NOT_OPER,
	};
	init_waitqueue_head(&cdev->private->wait_q);
	init_timer(&cdev->private->timer);

	/* Set an initial name for the device. */
	snprintf (cdev->dev.name, DEVICE_NAME_SIZE,"ccw device");
	snprintf (cdev->dev.bus_id, DEVICE_ID_SIZE, "0:%04x",
		  sch->schib.pmcw.dev);

	/* Do first half of device_register. */
	device_initialize(&cdev->dev);
	get_device(&sch->dev);	/* keep parent refcount in sync. */

	/* Start async. device sensing. */
	spin_lock_irq(cdev->ccwlock);
	ccw_device_recognition(cdev);
	spin_unlock_irq(cdev->ccwlock);
}

static int
io_subchannel_probe (struct device *pdev)
{
	struct subchannel *sch;
	struct ccw_device *cdev;

	sch = to_subchannel(pdev);
	if (sch->dev.driver_data) {
		/*
		 * This subchannel already has an associated ccw_device.
		 * Register it and exit. This happens for all early
		 * device, e.g. the console.
		 */
		ccw_device_register(sch->dev.driver_data);
		subchannel_add_files(&sch->dev);
		return 0;
	}
	cdev  = kmalloc (sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;
	memset(cdev, 0, sizeof(struct ccw_device));
	cdev->private = kmalloc(sizeof(struct ccw_device_private), GFP_DMA);
	if (!cdev->private) {
		kfree(cdev);
		return -ENOMEM;
	}
	memset(cdev->private, 0, sizeof(struct ccw_device_private));
	cdev->dev = (struct device) {
		.parent = pdev,
		.release = ccw_device_release,
	};
	io_subchannel_recog(cdev, to_subchannel(pdev));
	return 0;
}

#ifdef CONFIG_CCW_CONSOLE
static struct ccw_device console_cdev;
static struct ccw_device_private console_private;
static int console_cdev_in_use;

static int
ccw_device_console_enable (struct ccw_device *cdev, struct subchannel *sch)
{
	/* Initialize the ccw_device structure. */
	cdev->dev = (struct device) {
		.parent = &sch->dev,
	};
	/* Initialize the subchannel structure */
	sch->dev = (struct device) {
		.parent = &css_bus_device,
		.bus	= &css_bus_type,
	};
	io_subchannel_recog(cdev, sch);
	/* Now wait for the async. recognition to come to an end. */
	while (!dev_fsm_final_state(cdev))
		wait_cons_dev();
	if (cdev->private->state != DEV_STATE_OFFLINE)
		return -EIO;
	ccw_device_online(cdev);
	while (!dev_fsm_final_state(cdev))
		wait_cons_dev();
	if (cdev->private->state != DEV_STATE_ONLINE)
		return -EIO;
	return 0;
}

struct ccw_device *
ccw_device_probe_console(void)
{
	struct subchannel *sch;
	int ret;

	if (xchg(&console_cdev_in_use, 1) != 0)
		return NULL;
	sch = cio_probe_console();
	if (IS_ERR(sch)) {
		console_cdev_in_use = 0;
		return (void *) sch;
	}
	memset(&console_cdev, 0, sizeof(struct ccw_device));
	memset(&console_private, 0, sizeof(struct ccw_device_private));
	console_cdev.private = &console_private;
	ret = ccw_device_console_enable(&console_cdev, sch);
	if (ret) {
		cio_release_console();
		console_cdev_in_use = 0;
		return ERR_PTR(ret);
	}
	return &console_cdev;
}
#endif

/*
 * get ccw_device matching the busid, but only if owned by cdrv
 */
struct ccw_device *
get_ccwdev_by_busid(struct ccw_driver *cdrv, const char *bus_id)
{
	struct device *d, *dev;
	struct device_driver *drv;

	drv = get_driver(&cdrv->driver);
	if (!drv)
		return 0;

	down_read(&drv->bus->subsys.rwsem);

	dev = NULL;
	list_for_each_entry(d, &drv->devices, driver_list) {
		dev = get_device(d);

		if (dev && !strncmp(bus_id, dev->bus_id, DEVICE_ID_SIZE))
			break;
		else
			put_device(dev);
	}
	up_read(&drv->bus->subsys.rwsem);
	put_driver(drv);

	return dev ? to_ccwdev(dev) : 0;
}

/************************** device driver handling ************************/

/* This is the implementation of the ccw_driver class. The probe, remove
 * and release methods are initially very similar to the device_driver
 * implementations, with the difference that they have ccw_device
 * arguments.
 *
 * A ccw driver also contains the information that is needed for
 * device matching.
 */
static int
ccw_device_probe (struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_driver *cdrv = to_ccwdrv(dev->driver);
	int ret;

	cdev->drv = cdrv; /* to let the driver call _set_online */

	ret = cdrv->probe ? cdrv->probe(cdev) : -ENODEV;

	if (ret) {
		cdev->drv = 0;
		return ret;
	}

	return 0;
}

static int
ccw_device_remove (struct device *dev)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_driver *cdrv = cdev->drv;

	/*
	 * Set device offline, so device drivers don't end up with
	 * doubled code.
	 * This is safe because of the checks in ccw_device_set_offline.
	 */
	pr_debug(KERN_INFO "removing device %s, sch %d, devno %x\n",
		 cdev->dev.name,
		 cdev->private->irq,
		 cdev->private->devno);
	ccw_device_set_offline(cdev);
	return cdrv->remove ? cdrv->remove(cdev) : 0;
}

int
ccw_driver_register (struct ccw_driver *cdriver)
{
	struct device_driver *drv = &cdriver->driver;

	drv->bus = &ccw_bus_type;
	drv->name = cdriver->name;
	drv->probe = ccw_device_probe;
	drv->remove = ccw_device_remove;

	return driver_register(drv);
}

void
ccw_driver_unregister (struct ccw_driver *cdriver)
{
	driver_unregister(&cdriver->driver);
}

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(ccw_device_set_online);
EXPORT_SYMBOL(ccw_device_set_offline);
EXPORT_SYMBOL(ccw_driver_register);
EXPORT_SYMBOL(ccw_driver_unregister);
EXPORT_SYMBOL(get_ccwdev_by_busid);
EXPORT_SYMBOL(ccw_bus_type);
