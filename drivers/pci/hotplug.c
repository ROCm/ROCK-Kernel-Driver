#include <linux/pci.h>
#include <linux/module.h>
#include "pci.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

static void pci_free_resources(struct pci_dev *dev);

#ifdef CONFIG_HOTPLUG
int pci_hotplug (struct device *dev, char **envp, int num_envp,
		 char *buffer, int buffer_size)
{
	struct pci_dev *pdev;
	char *scratch;
	int i = 0;
	int length = 0;

	if (!dev)
		return -ENODEV;

	pdev = to_pci_dev(dev);
	if (!pdev)
		return -ENODEV;

	scratch = buffer;

	/* stuff we want to pass to /sbin/hotplug */
	envp[i++] = scratch;
	length += snprintf (scratch, buffer_size - length, "PCI_CLASS=%04X",
			    pdev->class);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += snprintf (scratch, buffer_size - length, "PCI_ID=%04X:%04X",
			    pdev->vendor, pdev->device);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += snprintf (scratch, buffer_size - length,
			    "PCI_SUBSYS_ID=%04X:%04X", pdev->subsystem_vendor,
			    pdev->subsystem_device);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += snprintf (scratch, buffer_size - length, "PCI_SLOT_NAME=%s",
			    pdev->slot_name);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;

	envp[i] = 0;

	return 0;
}

static int pci_visit_bus (struct pci_visit * fn, struct pci_bus_wrapped *wrapped_bus, struct pci_dev_wrapped *wrapped_parent)
{
	struct list_head *ln;
	struct pci_dev *dev;
	struct pci_dev_wrapped wrapped_dev;
	int result = 0;

	DBG("scanning bus %02x\n", wrapped_bus->bus->number);

	if (fn->pre_visit_pci_bus) {
		result = fn->pre_visit_pci_bus(wrapped_bus, wrapped_parent);
		if (result)
			return result;
	}

	ln = wrapped_bus->bus->devices.next; 
	while (ln != &wrapped_bus->bus->devices) {
		dev = pci_dev_b(ln);
		ln = ln->next;

		memset(&wrapped_dev, 0, sizeof(struct pci_dev_wrapped));
		wrapped_dev.dev = dev;

		result = pci_visit_dev(fn, &wrapped_dev, wrapped_bus);
		if (result)
			return result;
	}

	if (fn->post_visit_pci_bus)
		result = fn->post_visit_pci_bus(wrapped_bus, wrapped_parent);

	return result;
}

static int pci_visit_bridge (struct pci_visit * fn,
			     struct pci_dev_wrapped *wrapped_dev,
			     struct pci_bus_wrapped *wrapped_parent)
{
	struct pci_bus *bus;
	struct pci_bus_wrapped wrapped_bus;
	int result = 0;

	DBG("scanning bridge %02x, %02x\n", PCI_SLOT(wrapped_dev->dev->devfn),
	    PCI_FUNC(wrapped_dev->dev->devfn));

	if (fn->visit_pci_dev) {
		result = fn->visit_pci_dev(wrapped_dev, wrapped_parent);
		if (result)
			return result;
	}

	bus = wrapped_dev->dev->subordinate;
	if(bus) {
		memset(&wrapped_bus, 0, sizeof(struct pci_bus_wrapped));
		wrapped_bus.bus = bus;

		result = pci_visit_bus(fn, &wrapped_bus, wrapped_dev);
	}
	return result;
}

/**
 * pci_visit_dev - scans the pci buses.
 * Every bus and every function is presented to a custom
 * function that can act upon it.
 */
int pci_visit_dev (struct pci_visit *fn, struct pci_dev_wrapped *wrapped_dev,
		   struct pci_bus_wrapped *wrapped_parent)
{
	struct pci_dev* dev = wrapped_dev ? wrapped_dev->dev : NULL;
	int result = 0;

	if (!dev)
		return 0;

	if (fn->pre_visit_pci_dev) {
		result = fn->pre_visit_pci_dev(wrapped_dev, wrapped_parent);
		if (result)
			return result;
	}

	switch (dev->class >> 8) {
		case PCI_CLASS_BRIDGE_PCI:
			result = pci_visit_bridge(fn, wrapped_dev,
						  wrapped_parent);
			if (result)
				return result;
			break;
		default:
			DBG("scanning device %02x, %02x\n",
			    PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
			if (fn->visit_pci_dev) {
				result = fn->visit_pci_dev (wrapped_dev,
							    wrapped_parent);
				if (result)
					return result;
			}
	}

	if (fn->post_visit_pci_dev)
		result = fn->post_visit_pci_dev(wrapped_dev, wrapped_parent);

	return result;
}
EXPORT_SYMBOL(pci_visit_dev);

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
	device_unregister(&dev->dev);
	list_del(&dev->bus_list);
	list_del(&dev->global_list);
	pci_free_resources(dev);
#ifdef CONFIG_PROC_FS
	pci_proc_detach_device(dev);
#endif
	return 0;
}
EXPORT_SYMBOL(pci_remove_device_safe);

#else /* CONFIG_HOTPLUG */

int pci_hotplug (struct device *dev, char **envp, int num_envp,
		 char *buffer, int buffer_size)
{
	return -ENODEV;
}

#endif /* CONFIG_HOTPLUG */

/**
 * pci_insert_device - insert a pci device
 * @dev: the device to insert
 * @bus: where to insert it
 *
 * Link the device to both the global PCI device chain and the 
 * per-bus list of devices, add the /proc entry.
 */
void
pci_insert_device(struct pci_dev *dev, struct pci_bus *bus)
{
	list_add_tail(&dev->bus_list, &bus->devices);
	list_add_tail(&dev->global_list, &pci_devices);
#ifdef CONFIG_PROC_FS
	pci_proc_attach_device(dev);
#endif
	/* add sysfs device files */
	pci_create_sysfs_dev_files(dev);
}

static void
pci_free_resources(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = dev->resource + i;
		if (res->parent)
			release_resource(res);
	}
}

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

#ifdef CONFIG_PROC_FS
		pci_proc_detach_bus(b);
#endif

		list_del(&b->node);
		kfree(b);
		dev->subordinate = NULL;
	}

	device_unregister(&dev->dev);
	list_del(&dev->bus_list);
	list_del(&dev->global_list);
	pci_free_resources(dev);
#ifdef CONFIG_PROC_FS
	pci_proc_detach_device(dev);
#endif

	pci_put_dev(dev);
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

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pci_insert_device);
EXPORT_SYMBOL(pci_remove_bus_device);
EXPORT_SYMBOL(pci_remove_behind_bridge);
#endif
