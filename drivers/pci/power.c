#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/init.h>

/*
 * PCI Power management..
 *
 * This needs to be done centralized, so that we power manage PCI
 * devices in the right order: we should not shut down PCI bridges
 * before we've shut down the devices behind them, and we should
 * not wake up devices before we've woken up the bridge to the
 * device.. Eh?
 *
 * We do not touch devices that don't have a driver that exports
 * a suspend/resume function. That is just too dangerous. If the default
 * PCI suspend/resume functions work for a device, the driver can
 * easily implement them (ie just have a suspend function that calls
 * the pci_set_power_state() function).
 */

static int pci_pm_save_state_device(struct pci_dev *dev, u32 state)
{
	int error = 0;
	if (dev) {
		struct pci_driver *driver = dev->driver;
		if (driver && driver->save_state) 
			error = driver->save_state(dev,state);
	}
	return error;
}

static int pci_pm_suspend_device(struct pci_dev *dev, u32 state)
{
	int error = 0;
	if (dev) {
		struct pci_driver *driver = dev->driver;
		if (driver && driver->suspend)
			error = driver->suspend(dev,state);
	}
	return error;
}

static int pci_pm_resume_device(struct pci_dev *dev)
{
	int error = 0;
	if (dev) {
		struct pci_driver *driver = dev->driver;
		if (driver && driver->resume)
			error = driver->resume(dev);
	}
	return error;
}

static int pci_pm_save_state_bus(struct pci_bus *bus, u32 state)
{
	struct list_head *list;
	int error = 0;

	list_for_each(list, &bus->children) {
		error = pci_pm_save_state_bus(pci_bus_b(list),state);
		if (error) return error;
	}
	list_for_each(list, &bus->devices) {
		error = pci_pm_save_state_device(pci_dev_b(list),state);
		if (error) return error;
	}
	return 0;
}

static int pci_pm_suspend_bus(struct pci_bus *bus, u32 state)
{
	struct list_head *list;

	/* Walk the bus children list */
	list_for_each(list, &bus->children) 
		pci_pm_suspend_bus(pci_bus_b(list),state);

	/* Walk the device children list */
	list_for_each(list, &bus->devices)
		pci_pm_suspend_device(pci_dev_b(list),state);
	return 0;
}

static int pci_pm_resume_bus(struct pci_bus *bus)
{
	struct list_head *list;

	/* Walk the device children list */
	list_for_each(list, &bus->devices)
		pci_pm_resume_device(pci_dev_b(list));

	/* And then walk the bus children */
	list_for_each(list, &bus->children)
		pci_pm_resume_bus(pci_bus_b(list));
	return 0;
}

static int pci_pm_save_state(u32 state)
{
	struct pci_bus *bus = NULL;
	int error = 0;

	while ((bus = pci_find_next_bus(bus)) != NULL) {
		error = pci_pm_save_state_bus(bus,state);
		if (!error)
			error = pci_pm_save_state_device(bus->self,state);
	}
	return error;
}

static int pci_pm_suspend(u32 state)
{
	struct pci_bus *bus = NULL;

	while ((bus = pci_find_next_bus(bus)) != NULL) {
		pci_pm_suspend_bus(bus,state);
		pci_pm_suspend_device(bus->self,state);
	}
	return 0;
}

static int pci_pm_resume(void)
{
	struct pci_bus *bus = NULL;

	while ((bus = pci_find_next_bus(bus)) != NULL) {
		pci_pm_resume_device(bus->self);
		pci_pm_resume_bus(bus);
	}
	return 0;
}

static int 
pci_pm_callback(struct pm_dev *pm_device, pm_request_t rqst, void *data)
{
	int error = 0;

	switch (rqst) {
	case PM_SAVE_STATE:
		error = pci_pm_save_state((unsigned long)data);
		break;
	case PM_SUSPEND:
		error = pci_pm_suspend((unsigned long)data);
		break;
	case PM_RESUME:
		error = pci_pm_resume();
		break;
	default: break;
	}
	return error;
}

static int __init pci_pm_init(void)
{
	pm_register(PM_PCI_DEV, 0, pci_pm_callback);
	return 0;
}

subsys_initcall(pci_pm_init);
