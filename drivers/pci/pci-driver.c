/*
 * drivers/pci/pci-driver.c - default PCI driver.
 *
 */

#include <linux/pci.h>

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

struct device_driver pci_device_driver = {
	suspend:	pci_device_suspend,
	resume:		pci_device_resume,
};
