/*
 * drivers/pci/pci-driver.c
 *
 */

#include <linux/pci.h>
#include <linux/module.h>

/*
 *  Registration of PCI drivers and handling of hot-pluggable devices.
 */

/**
 * pci_match_device - Tell if a PCI device structure has a matching PCI device id structure
 * @ids: array of PCI device id structures to search in
 * @dev: the PCI device structure to match against
 * 
 * Used by a driver to check whether a PCI device present in the
 * system is in its list of supported devices.Returns the matching
 * pci_device_id structure or %NULL if there is no match.
 */
const struct pci_device_id *
pci_match_device(const struct pci_device_id *ids, const struct pci_dev *dev)
{
	while (ids->vendor || ids->subvendor || ids->class_mask) {
		if ((ids->vendor == PCI_ANY_ID || ids->vendor == dev->vendor) &&
		    (ids->device == PCI_ANY_ID || ids->device == dev->device) &&
		    (ids->subvendor == PCI_ANY_ID || ids->subvendor == dev->subsystem_vendor) &&
		    (ids->subdevice == PCI_ANY_ID || ids->subdevice == dev->subsystem_device) &&
		    !((ids->class ^ dev->class) & ids->class_mask))
			return ids;
		ids++;
	}
	return NULL;
}

int
pci_announce_device(struct pci_driver *drv, struct pci_dev *dev)
{
	const struct pci_device_id *id;
	int ret = 0;

	if (drv->id_table) {
		id = pci_match_device(drv->id_table, dev);
		if (!id) {
			ret = 0;
			goto out;
		}
	} else
		id = NULL;

	dev_probe_lock();
	if (drv->probe(dev, id) >= 0) {
		dev->driver = drv;
		ret = 1;
	}
	dev_probe_unlock();
out:
	return ret;
}


static int pci_device_probe(struct device * dev)
{
	int error = 0;

	struct pci_driver * drv = list_entry(dev->driver,struct pci_driver,driver);
	struct pci_dev * pci_dev = list_entry(dev,struct pci_dev,dev);

	if (drv->probe)
		error = drv->probe(pci_dev,NULL);
	printk("%s: returning %d\n",__FUNCTION__,error);
	return error > 0 ? 0 : -ENODEV;
}

static int pci_device_remove(struct device * dev, u32 flags)
{
	struct pci_dev * pci_dev = list_entry(dev,struct pci_dev,dev);

	if (dev->driver) {
		struct pci_driver * drv = list_entry(dev->driver,struct pci_driver,driver);
		if (drv->remove)
			drv->remove(pci_dev);
	}
	return 0;
}

static int pci_device_suspend(struct device * dev, u32 state, u32 level)
{
	struct pci_dev * pci_dev = (struct pci_dev *)list_entry(dev,struct pci_dev,dev);
	int error = 0;

	if (pci_dev->driver) {
		if (level == SUSPEND_SAVE_STATE && pci_dev->driver->save_state)
			error = pci_dev->driver->save_state(pci_dev,state);
		else if (level == SUSPEND_POWER_DOWN && pci_dev->driver->suspend)
			error = pci_dev->driver->suspend(pci_dev,state);
	}
	return error;
}

static int pci_device_resume(struct device * dev, u32 level)
{
	struct pci_dev * pci_dev = (struct pci_dev *)list_entry(dev,struct pci_dev,dev);

	if (pci_dev->driver) {
		if (level == RESUME_POWER_ON && pci_dev->driver->resume)
			pci_dev->driver->resume(pci_dev);
	}
	return 0;
}

/**
 * pci_register_driver - register a new pci driver
 * @drv: the driver structure to register
 * 
 * Adds the driver structure to the list of registered drivers
 * Returns the number of pci devices which were claimed by the driver
 * during registration.  The driver remains registered even if the
 * return value is zero.
 */
int
pci_register_driver(struct pci_driver *drv)
{
	int count = 0;
	struct pci_dev * dev;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &pci_bus_type;
	drv->driver.probe = pci_device_probe;
	drv->driver.resume = pci_device_resume;
	drv->driver.suspend = pci_device_suspend;
	drv->driver.remove = pci_device_remove;

	/* register with core */
	count = driver_register(&drv->driver);

	pci_for_each_dev(dev) {
		if (!pci_dev_driver(dev))
			pci_announce_device(drv, dev);
	}
	return count ? count : 1;
}

/**
 * pci_unregister_driver - unregister a pci driver
 * @drv: the driver structure to unregister
 * 
 * Deletes the driver structure from the list of registered PCI drivers,
 * gives it a chance to clean up by calling its remove() function for
 * each device it was responsible for, and marks those devices as
 * driverless.
 */

void
pci_unregister_driver(struct pci_driver *drv)
{
	list_t * node;
	
	node = drv->driver.devices.next;

	while (node != &drv->driver.devices) {
		struct device * dev = list_entry(node,struct device,driver_list);
		struct pci_dev * pci_dev = list_entry(dev,struct pci_dev,dev);

		if (drv->remove)
			drv->remove(pci_dev);
		pci_dev->driver = NULL;
		dev->driver = NULL;
		list_del_init(&dev->driver_list);
	}
	put_driver(&drv->driver);
}

static struct pci_driver pci_compat_driver = {
	name: "compat"
};

/**
 * pci_dev_driver - get the pci_driver of a device
 * @dev: the device to query
 *
 * Returns the appropriate pci_driver structure or %NULL if there is no 
 * registered driver for the device.
 */
struct pci_driver *
pci_dev_driver(const struct pci_dev *dev)
{
	if (dev->driver)
		return dev->driver;
	else {
		int i;
		for(i=0; i<=PCI_ROM_RESOURCE; i++)
			if (dev->resource[i].flags & IORESOURCE_BUSY)
				return &pci_compat_driver;
	}
	return NULL;
}

struct bus_type pci_bus_type = {
	name:	"pci",
};

static int __init pci_driver_init(void)
{
	return bus_register(&pci_bus_type);
}

subsys_initcall(pci_driver_init);

EXPORT_SYMBOL(pci_match_device);
EXPORT_SYMBOL(pci_register_driver);
EXPORT_SYMBOL(pci_unregister_driver);
EXPORT_SYMBOL(pci_dev_driver);
