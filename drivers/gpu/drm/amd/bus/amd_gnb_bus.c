/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include "amd_gnb_bus.h"

#define to_amd_gnb_bus_device(x) container_of((x), struct amd_gnb_bus_dev, dev)
#define to_amd_gnb_bus_driver(drv) (container_of((drv),			\
						struct amd_gnb_bus_driver, \
						driver))

static int amd_gnb_bus_match(struct device *dev, struct device_driver *drv)
{
	struct amd_gnb_bus_dev *amd_gnb_bus_dev = to_amd_gnb_bus_device(dev);
	struct amd_gnb_bus_driver *amd_gnb_bus_driver =
		to_amd_gnb_bus_driver(drv);

	return amd_gnb_bus_dev->ip == amd_gnb_bus_driver->ip ? 1 : 0;
}

#ifdef CONFIG_PM_SLEEP
static int amd_gnb_bus_legacy_suspend(struct device *dev, pm_message_t mesg)
{
	struct amd_gnb_bus_dev *amd_gnb_bus_dev = to_amd_gnb_bus_device(dev);
	struct amd_gnb_bus_driver *driver;

	if (!amd_gnb_bus_dev || !dev->driver)
		return 0;
	driver = to_amd_gnb_bus_driver(dev->driver);
	if (!driver->suspend)
		return 0;
	return driver->suspend(amd_gnb_bus_dev, mesg);
}

static int amd_gnb_bus_legacy_resume(struct device *dev)
{
	struct amd_gnb_bus_dev *amd_gnb_bus_dev = to_amd_gnb_bus_device(dev);
	struct amd_gnb_bus_driver *driver;

	if (!amd_gnb_bus_dev || !dev->driver)
		return 0;
	driver = to_amd_gnb_bus_driver(dev->driver);
	if (!driver->resume)
		return 0;
	return driver->resume(amd_gnb_bus_dev);
}

static int amd_gnb_bus_device_pm_suspend(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm)
		return pm_generic_suspend(dev);
	else
		return amd_gnb_bus_legacy_suspend(dev, PMSG_SUSPEND);
}

static int amd_gnb_bus_device_pm_resume(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm)
		return pm_generic_resume(dev);
	else
		return amd_gnb_bus_legacy_resume(dev);
}

static int amd_gnb_bus_device_pm_freeze(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm)
		return pm_generic_freeze(dev);
	else
		return amd_gnb_bus_legacy_suspend(dev, PMSG_FREEZE);
}

static int amd_gnb_bus_device_pm_thaw(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm)
		return pm_generic_thaw(dev);
	else
		return amd_gnb_bus_legacy_resume(dev);
}

static int amd_gnb_bus_device_pm_poweroff(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm)
		return pm_generic_poweroff(dev);
	else
		return amd_gnb_bus_legacy_suspend(dev, PMSG_HIBERNATE);
}

static int amd_gnb_bus_device_pm_restore(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm)
		return pm_generic_restore(dev);
	else
		return amd_gnb_bus_legacy_resume(dev);
}
#else /* !CONFIG_PM_SLEEP */
#define amd_gnb_bus_device_pm_suspend	NULL
#define amd_gnb_bus_device_pm_resume	NULL
#define amd_gnb_bus_device_pm_freeze	NULL
#define amd_gnb_bus_device_pm_thaw	NULL
#define amd_gnb_bus_device_pm_poweroff	NULL
#define amd_gnb_bus_device_pm_restore	NULL
#endif /* !CONFIG_PM_SLEEP */

static const struct dev_pm_ops amd_gnb_bus_device_pm_ops = {
	.suspend = amd_gnb_bus_device_pm_suspend,
	.resume = amd_gnb_bus_device_pm_resume,
	.freeze = amd_gnb_bus_device_pm_freeze,
	.thaw = amd_gnb_bus_device_pm_thaw,
	.poweroff = amd_gnb_bus_device_pm_poweroff,
	.restore = amd_gnb_bus_device_pm_restore,
	SET_RUNTIME_PM_OPS(
		pm_generic_runtime_suspend,
		pm_generic_runtime_resume,
		pm_runtime_idle
	)
};

/* The bus should only be registered by the first amd_gnb, but further
 * socs can add devices to the bus. */
struct bus_type amd_gnb_bus_type = {
	.name  = "amd_gnb",
	.match = amd_gnb_bus_match,
	.pm    = &amd_gnb_bus_device_pm_ops,
};
EXPORT_SYMBOL(amd_gnb_bus_type);

static int amd_gnb_bus_drv_probe(struct device *_dev)
{
	struct amd_gnb_bus_driver *drv = to_amd_gnb_bus_driver(_dev->driver);
	struct amd_gnb_bus_dev *dev = to_amd_gnb_bus_device(_dev);

	return drv->probe(dev);
}

static int amd_gnb_bus_drv_remove(struct device *_dev)
{
	struct amd_gnb_bus_driver *drv = to_amd_gnb_bus_driver(_dev->driver);
	struct amd_gnb_bus_dev *dev = to_amd_gnb_bus_device(_dev);

	return drv->remove(dev);
}

static void amd_gnb_bus_drv_shutdown(struct device *_dev)
{
	struct amd_gnb_bus_driver *drv = to_amd_gnb_bus_driver(_dev->driver);
	struct amd_gnb_bus_dev *dev = to_amd_gnb_bus_device(_dev);

	drv->shutdown(dev);
}

int amd_gnb_bus_register_driver(struct amd_gnb_bus_driver *drv,
			       struct module *owner,
			       const char *mod_name)
{
	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &amd_gnb_bus_type;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;

	if (drv->probe)
		drv->driver.probe = amd_gnb_bus_drv_probe;
	if (drv->remove)
		drv->driver.remove = amd_gnb_bus_drv_remove;
	if (drv->shutdown)
		drv->driver.shutdown = amd_gnb_bus_drv_shutdown;

	/* register with core */
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(amd_gnb_bus_register_driver);

void amd_gnb_bus_unregister_driver(struct amd_gnb_bus_driver *drv)
{
	/* register with core */
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(amd_gnb_bus_unregister_driver);

int amd_gnb_bus_register_device(struct amd_gnb_bus_dev *dev)
{
	dev->dev.bus = &amd_gnb_bus_type;
	return device_add(&dev->dev);
}
EXPORT_SYMBOL(amd_gnb_bus_register_device);

void amd_gnb_bus_unregister_device(struct amd_gnb_bus_dev *dev)
{
	if (dev)
		device_del(&dev->dev);
}
EXPORT_SYMBOL(amd_gnb_bus_unregister_device);

int amd_gnb_bus_device_init(struct amd_gnb_bus_dev *bus_dev,
			    enum amd_gnb_bus_ip ip,
			    char *dev_name,
			    void *handle,
			    struct device *parent)
{
	device_initialize(&bus_dev->dev);
	bus_dev->dev.init_name = dev_name;
	bus_dev->ip = ip;
	bus_dev->private_data = handle;
	bus_dev->dev.parent = parent;
	return amd_gnb_bus_register_device(bus_dev);
}
EXPORT_SYMBOL(amd_gnb_bus_device_init);

static int __init amd_gnb_bus_init(void)
{
	int ret = 0;
	/* does this need to be thread safe? */
	ret = bus_register(&amd_gnb_bus_type);
	if (ret)
		pr_err("%s: bus register failed\n", __func__);
	else
		pr_info("%s: initialization is successful\n", __func__);

	return ret;
}

static void __exit amd_gnb_bus_exit(void)
{
	bus_unregister(&amd_gnb_bus_type);
}

module_init(amd_gnb_bus_init);
module_exit(amd_gnb_bus_exit);

MODULE_AUTHOR("AMD");
MODULE_DESCRIPTION("AMD GPU bus");
MODULE_LICENSE("GPL and additional rights");
