#include <linux/pci.h>
#include <linux/module.h>
#include "pci.h"


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
#else
int pci_hotplug (struct device *dev, char **envp, int num_envp,
		 char *buffer, int buffer_size)
{
	return -ENODEV;
}
#endif

/**
 * pci_insert_device - insert a pci device
 * @dev: the device to insert
 * @bus: where to insert it
 *
 * Link the device to both the global PCI device chain and the 
 * per-bus list of devices, add the /proc entry, and notify
 * userspace (/sbin/hotplug).
 */
void
pci_insert_device(struct pci_dev *dev, struct pci_bus *bus)
{
	list_add_tail(&dev->bus_list, &bus->devices);
	list_add_tail(&dev->global_list, &pci_devices);
#ifdef CONFIG_PROC_FS
	pci_proc_attach_device(dev);
#endif
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
 * pci_remove_device - remove a pci device
 * @dev: the device to remove
 *
 * Delete the device structure from the device lists,
 * remove the /proc entry, and notify userspace (/sbin/hotplug).
 */
void
pci_remove_device(struct pci_dev *dev)
{
	put_device(&dev->dev);
	list_del(&dev->bus_list);
	list_del(&dev->global_list);
	pci_free_resources(dev);
#ifdef CONFIG_PROC_FS
	pci_proc_detach_device(dev);
#endif
}

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pci_insert_device);
EXPORT_SYMBOL(pci_remove_device);
#endif
