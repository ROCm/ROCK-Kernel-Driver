/*
 *  drivers/s390/cio/ccwgroup.c
 *  bus driver for ccwgroup
 *   $Revision: 1.7 $
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *                       IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/ctype.h>

#include <asm/semaphore.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>

/* In Linux 2.4, we had a channel device layer called "chandev"
 * that did all sorts of obscure stuff for networking devices.
 * This is another driver that serves as a replacement for just
 * one of its functions, namely the translation of single subchannels
 * to devices that use multiple subchannels.
 */

/* a device matches a driver if all its slave devices match the same
 * entry of the driver */
static int
ccwgroup_bus_match (struct device * dev, struct device_driver * drv)
{
	struct ccwgroup_device *gdev;
	struct ccwgroup_driver *gdrv;

	gdev = container_of(dev, struct ccwgroup_device, dev);
	gdrv = container_of(drv, struct ccwgroup_driver, driver);

	if (gdev->creator_id == gdrv->driver_id)
		return 1;

	return 0;
}
static int
ccwgroup_hotplug (struct device *dev, char **envp, int num_envp, char *buffer,
		  int buffer_size)
{
	/* TODO */
	return 0;
}

static struct bus_type ccwgroup_bus_type = {
	.name    = "ccwgroup",
	.match   = ccwgroup_bus_match,
	.hotplug = ccwgroup_hotplug,
};

static void
ccwgroup_release (struct device *dev)
{
	struct ccwgroup_device *gdev;
	int i;

	gdev = to_ccwgroupdev(dev);

	for (i = 0; i < gdev->count; i++)
		put_device(&gdev->cdev[i]->dev);
	kfree(gdev);
}

/*
 * try to add a new ccwgroup device for one driver
 * argc and argv[] are a list of bus_id's of devices
 * belonging to the driver.
 */
int
ccwgroup_create(struct device *root,
		unsigned int creator_id,
		struct ccw_driver *cdrv,
		int argc, char *argv[])
{
	struct ccwgroup_device *gdev;
	int i;

	if (argc > 256) /* disallow dumb users */
		return -EINVAL;

	gdev = kmalloc(sizeof(*gdev) + argc*sizeof(gdev->cdev[0]), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	for (i = 0; i < argc; i++) {
		gdev->cdev[i] = get_ccwdev_by_busid(cdrv, argv[i]);

		/* all devices have to be of the same type in
		 * order to be grouped */
		if (!gdev->cdev[i]
		    || gdev->cdev[i]->id.driver_info !=
		       gdev->cdev[0]->id.driver_info)
			goto error;
	}

	*gdev = (struct ccwgroup_device) {
		.creator_id = creator_id,
		.count = argc,
		.dev = {
			.bus = &ccwgroup_bus_type,
			.parent = root,
			.release = &ccwgroup_release,
		},
	};

	snprintf (gdev->dev.bus_id, BUS_ID_SIZE, "%s",
			gdev->cdev[0]->dev.bus_id);

	/* TODO: make symlinks for sysfs */
	return device_register(&gdev->dev);

error:
	for (i = 0; i < argc; i++)
		if (gdev->cdev[i])
			put_device(&gdev->cdev[i]->dev);

	kfree(gdev);

	return -EINVAL;
}

static int __init
init_ccwgroup (void)
{
	return bus_register (&ccwgroup_bus_type);
}

static void __exit
cleanup_ccwgroup (void)
{
	bus_unregister (&ccwgroup_bus_type);
}

module_init(init_ccwgroup);
module_exit(cleanup_ccwgroup);

/************************** driver stuff ******************************/

static int
ccwgroup_set_online(struct ccwgroup_device *gdev)
{
	struct ccwgroup_driver *gdrv;
	int ret;

	if (gdev->state == CCWGROUP_ONLINE)
		return 0;

	if (!gdev->dev.driver)
		return -EINVAL;

	gdrv = to_ccwgroupdrv (gdev->dev.driver);
	if ((ret = gdrv->set_online(gdev)))
		return ret;

	gdev->state = CCWGROUP_ONLINE;
	return 0;
}

static int
ccwgroup_set_offline(struct ccwgroup_device *gdev)
{
	struct ccwgroup_driver *gdrv;
	int ret;

	if (gdev->state == CCWGROUP_OFFLINE)
		return 0;

	if (!gdev->dev.driver)
		return -EINVAL;

	gdrv = to_ccwgroupdrv (gdev->dev.driver);
	if ((ret = gdrv->set_offline(gdev)))
		return ret;

	gdev->state = CCWGROUP_OFFLINE;
	return 0;
}

static ssize_t
ccwgroup_online_store (struct device *dev, const char *buf, size_t count)
{
	struct ccwgroup_device *gdev;
	unsigned int value;

	gdev = to_ccwgroupdev(dev);
	if (!dev->driver)
		return count;

	value = simple_strtoul(buf, 0, 0);

	if (value == 1)
		ccwgroup_set_online(gdev);
	else if (value == 0)
		ccwgroup_set_offline(gdev);
	else
		return -EINVAL;

	return count;
}

static ssize_t
ccwgroup_online_show (struct device *dev, char *buf)
{
	int online;

	online = (to_ccwgroupdev(dev)->state == CCWGROUP_ONLINE);

	return sprintf(buf, online ? "yes\n" : "no\n");
}

static DEVICE_ATTR(online, 0644, ccwgroup_online_show, ccwgroup_online_store);

static int
ccwgroup_probe (struct device *dev)
{
	struct ccwgroup_device *gdev;
	struct ccwgroup_driver *gdrv;

	int ret;

	gdev = to_ccwgroupdev(dev);
	gdrv = to_ccwgroupdrv(dev->driver);

	if ((ret = device_create_file(dev, &dev_attr_online)))
		return ret;

	pr_debug("%s: device %s\n", __func__, gdev->dev.name);
	ret = gdrv->probe ? gdrv->probe(gdev) : -ENODEV;
	if (ret)
		device_remove_file(dev, &dev_attr_online);

	return ret;
}

static int
ccwgroup_remove (struct device *dev)
{
	struct ccwgroup_device *gdev;
	struct ccwgroup_driver *gdrv;
	int ret;

	gdev = to_ccwgroupdev(dev);
	gdrv = to_ccwgroupdrv(dev->driver);

	pr_debug("%s: device %s\n", __func__, gdev->dev.name);

	device_remove_file(dev, &dev_attr_online);
	ccwgroup_set_offline(gdev);

	ret = (gdrv && gdrv->remove) ? gdrv->remove(gdev) : 0;

	return ret;
}

int
ccwgroup_driver_register (struct ccwgroup_driver *cdriver)
{
	/* register our new driver with the core */
	cdriver->driver = (struct device_driver) {
		.bus = &ccwgroup_bus_type,
		.name = cdriver->name,
		.probe = ccwgroup_probe,
		.remove = ccwgroup_remove,
	};

	return driver_register(&cdriver->driver);
}

void
ccwgroup_driver_unregister (struct ccwgroup_driver *cdriver)
{
	driver_unregister(&cdriver->driver);
}

int
ccwgroup_probe_ccwdev(struct ccw_device *cdev)
{
	return 0;
}

int
ccwgroup_remove_ccwdev(struct ccw_device *cdev)
{
	return 0;
}

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(ccwgroup_driver_register);
EXPORT_SYMBOL(ccwgroup_driver_unregister);
EXPORT_SYMBOL(ccwgroup_create);
EXPORT_SYMBOL(ccwgroup_probe_ccwdev);
EXPORT_SYMBOL(ccwgroup_remove_ccwdev);
