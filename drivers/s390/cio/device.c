/*
 *  drivers/s390/cio/device.c
 *  bus driver for ccw devices
 *   $Revision: 1.70 $
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
#include <linux/workqueue.h>

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

struct workqueue_struct *ccw_device_work;
static wait_queue_head_t ccw_device_init_wq;
static atomic_t ccw_device_init_count;

static int __init
init_ccw_bus_type (void)
{
	int ret;

	init_waitqueue_head(&ccw_device_init_wq);
	atomic_set(&ccw_device_init_count, 0);

	ccw_device_work = create_workqueue("cio");
	if (!ccw_device_work)
		return -ENOMEM; /* FIXME: better errno ? */

	if ((ret = bus_register (&ccw_bus_type)))
		return ret;

	if ((ret = driver_register(&io_subchannel_driver.drv)))
		return ret;

	wait_event(ccw_device_init_wq,
		   atomic_read(&ccw_device_init_count) == 0);
	flush_workqueue(ccw_device_work);
	return 0;
}

static void __exit
cleanup_ccw_bus_type (void)
{
	driver_unregister(&io_subchannel_driver.drv);
	bus_unregister(&ccw_bus_type);
	destroy_workqueue(ccw_device_work);
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
chpids_show (struct device * dev, char * buf)
{
	struct subchannel *sch = to_subchannel(dev);
	struct ssd_info *ssd = &sch->ssd_info;
	ssize_t ret = 0;
	int chp;

	for (chp = 0; chp < 8; chp++)
		ret += sprintf (buf+ret, "%02x ", ssd->chpid[chp]);

	ret += sprintf (buf+ret, "\n");
	return min((ssize_t)PAGE_SIZE, ret);
}

static ssize_t
pimpampom_show (struct device * dev, char * buf)
{
	struct subchannel *sch = to_subchannel(dev);
	struct pmcw *pmcw = &sch->schib.pmcw;

	return sprintf (buf, "%02x %02x %02x\n",
			pmcw->pim, pmcw->pam, pmcw->pom);
}

static ssize_t
devtype_show (struct device *dev, char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_device_id *id = &(cdev->id);

	if (id->dev_type != 0)
		return sprintf(buf, "%04x/%02x\n",
				id->dev_type, id->dev_model);
	else
		return sprintf(buf, "n/a\n");
}

static ssize_t
cutype_show (struct device *dev, char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct ccw_device_id *id = &(cdev->id);

	return sprintf(buf, "%04x/%02x\n",
		       id->cu_type, id->cu_model);
}

static ssize_t
online_show (struct device *dev, char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);

	return sprintf(buf, cdev->online ? "1\n" : "0\n");
}

void
ccw_device_set_offline(struct ccw_device *cdev)
{
	int ret;

	if (!cdev)
		return;
	if (!cdev->online || !cdev->drv)
		return;

	if (cdev->drv->set_offline)
		if (cdev->drv->set_offline(cdev) != 0)
			return;

	cdev->online = 0;
	spin_lock_irq(cdev->ccwlock);
	ret = ccw_device_offline(cdev);
	spin_unlock_irq(cdev->ccwlock);
	if (ret == 0)
		wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	else
		//FIXME: we can't fail!
		pr_debug("ccw_device_offline returned %d, device %s\n",
			 ret, cdev->dev.bus_id);
}

void
ccw_device_set_online(struct ccw_device *cdev)
{
	int ret;

	if (!cdev)
		return;
	if (cdev->online || !cdev->drv)
		return;

	spin_lock_irq(cdev->ccwlock);
	ret = ccw_device_online(cdev);
	spin_unlock_irq(cdev->ccwlock);
	if (ret == 0)
		wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	else {
		pr_debug("ccw_device_online returned %d, device %s\n",
			 ret, cdev->dev.bus_id);
		return;
	}
	if (cdev->private->state != DEV_STATE_ONLINE)
		return;
	if (!cdev->drv->set_online || cdev->drv->set_online(cdev) == 0) {
		cdev->online = 1;
		return;
	}
	spin_lock_irq(cdev->ccwlock);
	ret = ccw_device_offline(cdev);
	spin_unlock_irq(cdev->ccwlock);
	if (ret == 0)
		wait_event(cdev->private->wait_q, dev_fsm_final_state(cdev));
	else 
		pr_debug("ccw_device_offline returned %d, device %s\n",
			 ret, cdev->dev.bus_id);
}

static ssize_t
online_store (struct device *dev, const char *buf, size_t count)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	int i;
	char *tmp;

	if (!cdev->drv)
		return count;

	i = simple_strtoul(buf, &tmp, 16);
	if (i == 1 && cdev->drv->set_online)
		ccw_device_set_online(cdev);
	else if (i == 0 && cdev->drv->set_offline)
		ccw_device_set_offline(cdev);
	else
		return -EINVAL;

	return count;
}

static void ccw_device_unbox_recog(void *data);

static ssize_t
stlck_store(struct device *dev, const char *buf, size_t count)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	int ret;

	/* We don't care what was piped to the attribute 8) */
	ret = ccw_device_stlck(cdev);
	if (ret != 0) {
		printk(KERN_WARNING
		       "Unconditional reserve failed on device %s, rc=%d\n",
		       dev->bus_id, ret);
		return ret;
	}

	/*
	 * Device was successfully unboxed.
	 * Trigger removal of stlck attribute and device recognition.
	 */
	INIT_WORK(&cdev->private->kick_work,
		  ccw_device_unbox_recog, (void *) cdev);
	queue_work(ccw_device_work, &cdev->private->kick_work);
	
	return 0;
}

static DEVICE_ATTR(chpids, 0444, chpids_show, NULL);
static DEVICE_ATTR(pimpampom, 0444, pimpampom_show, NULL);
static DEVICE_ATTR(devtype, 0444, devtype_show, NULL);
static DEVICE_ATTR(cutype, 0444, cutype_show, NULL);
static DEVICE_ATTR(online, 0644, online_show, online_store);
static DEVICE_ATTR(steal_lock, 0200, NULL, stlck_store);

/* A device has been unboxed. Start device recognition. */
static void
ccw_device_unbox_recog(void *data)
{
	struct ccw_device *cdev;

	cdev = (struct ccw_device *)data;
	if (!cdev)
		return;

	/* Remove stlck attribute. */
	device_remove_file(&cdev->dev, &dev_attr_steal_lock);

	spin_lock_irq(cdev->ccwlock);

	/* Device is no longer boxed. */
	cdev->private->state = DEV_STATE_NOT_OPER;

	/* Finally start device recognition. */
	ccw_device_recognition(cdev);
	spin_unlock_irq(cdev->ccwlock);
}

static struct attribute * subch_attrs[] = {
	&dev_attr_chpids.attr,
	&dev_attr_pimpampom.attr,
	NULL,
};

static struct attribute_group subch_attr_group = {
	.attrs = subch_attrs,
};

static inline int
subchannel_add_files (struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &subch_attr_group);
}

static struct attribute * ccwdev_attrs[] = {
	&dev_attr_devtype.attr,
	&dev_attr_cutype.attr,
	&dev_attr_online.attr,
	NULL,
};

static struct attribute_group ccwdev_attr_group = {
	.attrs = ccwdev_attrs,
};

static inline int
device_add_files (struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &ccwdev_attr_group);
}

static inline void
device_remove_files(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &ccwdev_attr_group);
}

/*
 * Add a "steal lock" attribute to boxed devices.
 * This allows to trigger an unconditional reserve ccw to eckd dasds
 * (if the device is something else, there should be no problems more than
 * a command reject; we don't have any means of finding out the device's
 * type if it was boxed at ipl/attach for older devices and under VM).
 */
void
ccw_device_add_stlck(void *data)
{
	struct ccw_device *cdev;

	cdev = (struct ccw_device *)data;
	device_create_file(&cdev->dev, &dev_attr_steal_lock);
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

void
ccw_device_unregister(void *data)
{
	struct device *dev;

	dev = (struct device *)data;

	device_remove_files(dev);
	device_unregister(dev);
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
static void
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
		goto out;
	}

	ret = subchannel_add_files(cdev->dev.parent);
	if (ret)
		printk(KERN_WARNING "%s: could not add attributes to %04x\n",
		       __func__, sch->irq);
	if (cdev->private->state == DEV_STATE_BOXED)
		device_create_file(&cdev->dev, &dev_attr_steal_lock);
out:
	put_device(&sch->dev);
}

/*
 * subchannel recognition done. Called from the state machine.
 */
void
io_subchannel_recog_done(struct ccw_device *cdev)
{
	struct subchannel *sch;

	if (css_init_done == 0)
		return;
	switch (cdev->private->state) {
	case DEV_STATE_NOT_OPER:
		/* Remove device found not operational. */
		sch = to_subchannel(cdev->dev.parent);
		sch->dev.driver_data = 0;
		put_device(&sch->dev);
		if (cdev->dev.release)
			cdev->dev.release(&cdev->dev);
		break;
	case DEV_STATE_BOXED:
		/* Device did not respond in time. */
	case DEV_STATE_OFFLINE:
		/* 
		 * We can't register the device in interrupt context so
		 * we schedule a work item.
		 */
		INIT_WORK(&cdev->private->kick_work,
			  io_subchannel_register, (void *) cdev);
		queue_work(ccw_device_work, &cdev->private->kick_work);
		break;
	}
	if (atomic_dec_and_test(&ccw_device_init_count))
		wake_up(&ccw_device_init_wq);
}

static int
io_subchannel_recog(struct ccw_device *cdev, struct subchannel *sch)
{
	int rc;

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
	snprintf (cdev->dev.bus_id, BUS_ID_SIZE, "0.0.%04x",
		  sch->schib.pmcw.dev);

	/* Increase counter of devices currently in recognition. */
	atomic_inc(&ccw_device_init_count);

	/* Start async. device sensing. */
	spin_lock_irq(cdev->ccwlock);
	rc = ccw_device_recognition(cdev);
	spin_unlock_irq(cdev->ccwlock);
	if (rc) {
		if (atomic_dec_and_test(&ccw_device_init_count))
			wake_up(&ccw_device_init_wq);
	}
	return rc;
}

static int
io_subchannel_probe (struct device *pdev)
{
	struct subchannel *sch;
	struct ccw_device *cdev;
	int rc;

	sch = to_subchannel(pdev);
	if (sch->dev.driver_data) {
		/*
		 * This subchannel already has an associated ccw_device.
		 * Register it and exit. This happens for all early
		 * device, e.g. the console.
		 */
		cdev = sch->dev.driver_data;
		device_initialize(&cdev->dev);
		ccw_device_register(cdev);
		subchannel_add_files(&sch->dev);
		/*
		 * Check if the device is already online. If it is
		 * the reference count needs to be corrected
		 * (see ccw_device_online and css_init_done for the
		 * ugly details).
		 */
		if (cdev->private->state != DEV_STATE_NOT_OPER &&
		    cdev->private->state != DEV_STATE_OFFLINE &&
		    cdev->private->state != DEV_STATE_BOXED)
			get_device(&cdev->dev);
		return 0;
	}
	cdev  = kmalloc (sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;
	memset(cdev, 0, sizeof(struct ccw_device));
	cdev->private = kmalloc(sizeof(struct ccw_device_private), 
				GFP_KERNEL | GFP_DMA);
	if (!cdev->private) {
		kfree(cdev);
		return -ENOMEM;
	}
	memset(cdev->private, 0, sizeof(struct ccw_device_private));
	cdev->dev = (struct device) {
		.parent = pdev,
		.release = ccw_device_release,
	};
	/* Do first half of device_register. */
	device_initialize(&cdev->dev);

	if (!get_device(&sch->dev)) {
		if (cdev->dev.release)
			cdev->dev.release(&cdev->dev);
		return 0;
	}

	rc = io_subchannel_recog(cdev, to_subchannel(pdev));
	if (rc) {
		sch->dev.driver_data = 0;
		put_device(&sch->dev);
		if (cdev->dev.release)
			cdev->dev.release(&cdev->dev);
	}

	return 0;
}

#ifdef CONFIG_CCW_CONSOLE
static struct ccw_device console_cdev;
static struct ccw_device_private console_private;
static int console_cdev_in_use;

static int
ccw_device_console_enable (struct ccw_device *cdev, struct subchannel *sch)
{
	int rc;

	/* Initialize the ccw_device structure. */
	cdev->dev = (struct device) {
		.parent = &sch->dev,
	};
	/* Initialize the subchannel structure */
	sch->dev = (struct device) {
		.parent = &css_bus_device,
		.bus	= &css_bus_type,
	};

	rc = io_subchannel_recog(cdev, sch);
	if (rc)
		return rc;

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
	console_cdev.online = 1;
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

		if (dev && !strncmp(bus_id, dev->bus_id, BUS_ID_SIZE))
			break;
		else if (dev) {
			put_device(dev);
			dev = NULL;
		}
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
	pr_debug("removing device %s, sch %d, devno %x\n",
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
