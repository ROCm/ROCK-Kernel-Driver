/*
 * vim:shiftwidth=8:noexpandtab
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <xen/interface/physdev.h>

static int (*pci_bus_probe)(struct device *dev);
static int (*pci_bus_remove)(struct device *dev);

static int pci_bus_probe_wrapper(struct device *dev)
{
	int r;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct physdev_manage_pci manage_pci;
	manage_pci.bus = pci_dev->bus->number;
	manage_pci.devfn = pci_dev->devfn;

	r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add, &manage_pci);
	if (r && r != -ENOSYS)
		return r;

	r = pci_bus_probe(dev);
	if (r) {
		int ret;

		ret = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_remove,
					    &manage_pci);
		WARN_ON(ret && ret != -ENOSYS);
	}

	return r;
}

static int pci_bus_remove_wrapper(struct device *dev)
{
	int r;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct physdev_manage_pci manage_pci;
	manage_pci.bus = pci_dev->bus->number;
	manage_pci.devfn = pci_dev->devfn;

	r = pci_bus_remove(dev);
	/* dev and pci_dev are no longer valid!! */

	WARN_ON(HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_remove,
		&manage_pci));
	return r;
}

static int __init hook_pci_bus(void)
{
	if (!is_running_on_xen() || !is_initial_xendomain())
		return 0;

	pci_bus_probe = pci_bus_type.probe;
	pci_bus_type.probe = pci_bus_probe_wrapper;

	pci_bus_remove = pci_bus_type.remove;
	pci_bus_type.remove = pci_bus_remove_wrapper;

	return 0;
}

core_initcall(hook_pci_bus);
