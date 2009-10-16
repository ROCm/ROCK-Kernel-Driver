/*
 * vim:shiftwidth=8:noexpandtab
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <xen/interface/physdev.h>
#include "../../pci/pci.h"

static int (*pci_bus_probe)(struct device *dev);
static int (*pci_bus_remove)(struct device *dev);

static int pci_bus_probe_wrapper(struct device *dev)
{
	int r;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct physdev_manage_pci manage_pci;
	struct physdev_manage_pci_ext manage_pci_ext;

#ifdef CONFIG_PCI_IOV
	if (pci_dev->is_virtfn) {
		memset(&manage_pci_ext, 0, sizeof(manage_pci_ext));
		manage_pci_ext.bus = pci_dev->bus->number;
		manage_pci_ext.devfn = pci_dev->devfn;
		manage_pci_ext.is_virtfn = 1;
		manage_pci_ext.physfn.bus = pci_dev->physfn->bus->number;
		manage_pci_ext.physfn.devfn = pci_dev->physfn->devfn;
		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add_ext,
					  &manage_pci_ext);
	} else
#endif
	if (pci_ari_enabled(pci_dev->bus) && PCI_SLOT(pci_dev->devfn)) {
		memset(&manage_pci_ext, 0, sizeof(manage_pci_ext));
		manage_pci_ext.bus = pci_dev->bus->number;
		manage_pci_ext.devfn = pci_dev->devfn;
		manage_pci_ext.is_extfn = 1;
		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add_ext,
					  &manage_pci_ext);
	} else {
		manage_pci.bus = pci_dev->bus->number;
		manage_pci.devfn = pci_dev->devfn;
		r = HYPERVISOR_physdev_op(PHYSDEVOP_manage_pci_add,
					  &manage_pci);
	}
	if (r && r != -ENOSYS)
		return r;

	r = pci_bus_probe(dev);
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
