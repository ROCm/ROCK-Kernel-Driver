#include <linux/pci.h>
#include <linux/module.h>
#include "pci.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

static void pci_free_resources(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = dev->resource + i;
		if (res->parent)
			release_resource(res);
	}
}

static void pci_destroy_dev(struct pci_dev *dev)
{
	pci_proc_detach_device(dev);
	device_unregister(&dev->dev);

	/* Remove the device from the device lists, and prevent any further
	 * list accesses from this device */
	spin_lock(&pci_bus_lock);
	list_del(&dev->bus_list);
	list_del(&dev->global_list);
	dev->bus_list.next = dev->bus_list.prev = NULL;
	dev->global_list.next = dev->global_list.prev = NULL;
	spin_unlock(&pci_bus_lock);

	pci_free_resources(dev);
	pci_dev_put(dev);
}

/**
 * pci_remove_device_safe - remove an unused hotplug device
 * @dev: the device to remove
 *
 * Delete the device structure from the device lists and 
 * notify userspace (/sbin/hotplug), but only if the device
 * in question is not being used by a driver.
 * Returns 0 on success.
 */
int pci_remove_device_safe(struct pci_dev *dev)
{
	if (pci_dev_driver(dev))
		return -EBUSY;
	pci_destroy_dev(dev);
	return 0;
}
EXPORT_SYMBOL(pci_remove_device_safe);

/**
 * pci_remove_bus_device - remove a PCI device and any children
 * @dev: the device to remove
 *
 * Remove a PCI device from the device lists, informing the drivers
 * that the device has been removed.  We also remove any subordinate
 * buses and children in a depth-first manner.
 *
 * For each device we remove, delete the device structure from the
 * device lists, remove the /proc entry, and notify userspace
 * (/sbin/hotplug).
 */
void pci_remove_bus_device(struct pci_dev *dev)
{
	if (dev->subordinate) {
		struct pci_bus *b = dev->subordinate;

		pci_remove_behind_bridge(dev);
		pci_proc_detach_bus(b);

		spin_lock(&pci_bus_lock);
		list_del(&b->node);
		spin_unlock(&pci_bus_lock);

		kfree(b);
		dev->subordinate = NULL;
	}

	pci_destroy_dev(dev);
}

/**
 * pci_remove_behind_bridge - remove all devices behind a PCI bridge
 * @dev: PCI bridge device
 *
 * Remove all devices on the bus, except for the parent bridge.
 * This also removes any child buses, and any devices they may
 * contain in a depth-first manner.
 */
void pci_remove_behind_bridge(struct pci_dev *dev)
{
	struct list_head *l, *n;

	if (dev->subordinate) {
		list_for_each_safe(l, n, &dev->subordinate->devices) {
			struct pci_dev *dev = pci_dev_b(l);

			pci_remove_bus_device(dev);
		}
	}
}

EXPORT_SYMBOL(pci_remove_bus_device);
EXPORT_SYMBOL(pci_remove_behind_bridge);
