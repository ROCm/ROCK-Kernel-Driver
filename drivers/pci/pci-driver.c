/*
 * drivers/pci/pci-driver.c
 *
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/init.h>

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

static int pci_device_probe(struct device * dev)
{
	int error = 0;
	struct pci_driver *drv;
	struct pci_dev *pci_dev;

	drv = to_pci_driver(dev->driver);
	pci_dev = to_pci_dev(dev);

	if (drv->probe) {
		const struct pci_device_id *id;

		id = pci_match_device(drv->id_table, pci_dev);
		if (id)
			error = drv->probe(pci_dev, id);
		if (error >= 0) {
			pci_dev->driver = drv;
			error = 0;
		}
	}
	return error;
}

static int pci_device_remove(struct device * dev)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * drv = pci_dev->driver;

	if (drv) {
		if (drv->remove)
			drv->remove(pci_dev);
		pci_dev->driver = NULL;
	}
	return 0;
}

static int pci_device_suspend(struct device * dev, u32 state, u32 level)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
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
	struct pci_dev * pci_dev = to_pci_dev(dev);

	if (pci_dev->driver) {
		/* We may not call PCI drivers resume at
		   RESUME_POWER_ON because interrupts are not yet
		   working at that point. Calling resume at
		   RESUME_RESTORE_STATE seems like solution. */
		if (level == RESUME_RESTORE_STATE && pci_dev->driver->resume)
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

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &pci_bus_type;
	drv->driver.probe = pci_device_probe;
	drv->driver.resume = pci_device_resume;
	drv->driver.suspend = pci_device_suspend;
	drv->driver.remove = pci_device_remove;

	/* register with core */
	count = driver_register(&drv->driver);
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
	remove_driver(&drv->driver);
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

/**
 * pci_bus_match - Tell if a PCI device structure has a matching PCI device id structure
 * @ids: array of PCI device id structures to search in
 * @dev: the PCI device structure to match against
 * 
 * Used by a driver to check whether a PCI device present in the
 * system is in its list of supported devices.Returns the matching
 * pci_device_id structure or %NULL if there is no match.
 */
static int pci_bus_match(struct device * dev, struct device_driver * drv) 
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * pci_drv = to_pci_driver(drv);
	const struct pci_device_id * ids = pci_drv->id_table;

	if (!ids) 
		return 0;

	while (ids->vendor || ids->subvendor || ids->class_mask) {
		if ((ids->vendor == PCI_ANY_ID || ids->vendor == pci_dev->vendor) &&
		    (ids->device == PCI_ANY_ID || ids->device == pci_dev->device) &&
		    (ids->subvendor == PCI_ANY_ID || ids->subvendor == pci_dev->subsystem_vendor) &&
		    (ids->subdevice == PCI_ANY_ID || ids->subdevice == pci_dev->subsystem_device) &&
		    !((ids->class ^ pci_dev->class) & ids->class_mask))
			return 1;
		ids++;
	}
	return 0;
}

struct bus_type pci_bus_type = {
	name:	"pci",
	match:	pci_bus_match,
};

static int __init pci_driver_init(void)
{
	return bus_register(&pci_bus_type);
}

postcore_initcall(pci_driver_init);

EXPORT_SYMBOL(pci_match_device);
EXPORT_SYMBOL(pci_register_driver);
EXPORT_SYMBOL(pci_unregister_driver);
EXPORT_SYMBOL(pci_dev_driver);
EXPORT_SYMBOL(pci_bus_type);
