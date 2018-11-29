#ifndef AMDKCL_PCI_H
#define AMDKCL_PCI_H

#include <linux/pci.h>
#include <linux/version.h>

#if !defined(HAVE_PCIE_GET_SPEED_AND_WIDTH_CAP)
extern enum pci_bus_speed (*_kcl_pcie_get_speed_cap)(struct pci_dev *dev);
extern enum pcie_link_width (*_kcl_pcie_get_width_cap)(struct pci_dev *dev);
#endif

static inline enum pci_bus_speed kcl_pcie_get_speed_cap(struct pci_dev *dev)
{
#if defined(HAVE_PCIE_GET_SPEED_AND_WIDTH_CAP)
	return pcie_get_speed_cap(dev);
#else
	return _kcl_pcie_get_speed_cap(dev);
#endif
}

static inline enum pcie_link_width kcl_pcie_get_width_cap(struct pci_dev *dev)
{
#if defined(HAVE_PCIE_GET_SPEED_AND_WIDTH_CAP)
	return pcie_get_width_cap(dev);
#else
	return _kcl_pcie_get_width_cap(dev);
#endif
}

#endif /* AMDKCL_PCI_H */
