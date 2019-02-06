/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	pci.h
 *
 *	PCI defines and function prototypes
 *	Copyright 1994, Drew Eckhardt
 *	Copyright 1997--1999 Martin Mares <mj@ucw.cz>
 *
 *	PCI Express ASPM defines and function prototypes
 *	Copyright (c) 2007 Intel Corp.
 *		Zhang Yanmin (yanmin.zhang@intel.com)
 *		Shaohua Li (shaohua.li@intel.com)
 *
 *	For more information, please consult the following manuals (look at
 *	http://www.pcisig.com/ for how to get them):
 *
 *	PCI BIOS Specification
 *	PCI Local Bus Specification
 *	PCI to PCI Bridge Specification
 *	PCI Express Specification
 *	PCI System Design Guide
 */
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

#if !defined(HAVE_PCIE_ENABLE_ATOMIC_OPS_TO_ROOT)
int _kcl_pci_enable_atomic_ops_to_root(struct pci_dev *dev, u32 comp_caps);
static inline
int pci_enable_atomic_ops_to_root(struct pci_dev *dev, u32 cap_mask)
{
	return _kcl_pci_enable_atomic_ops_to_root(dev, cap_mask);
}
#endif

#if !defined(HAVE_PCI_UPSTREAM_BRIDGE)
static inline struct pci_dev *pci_upstream_bridge(struct pci_dev *dev)
{
	dev = pci_physfn(dev);
	if (pci_is_root_bus(dev->bus))
		return NULL;

	return dev->bus->self;
}
#endif

#if !defined(HAVE_PCIE_BANDWIDTH_AVAILABLE)
u32 _kcl_pcie_bandwidth_available(struct pci_dev *dev, struct pci_dev **limiting_dev,
			     enum pci_bus_speed *speed,
			     enum pcie_link_width *width);
static inline
u32 pcie_bandwidth_available(struct pci_dev *dev, struct pci_dev **limiting_dev,
				enum pci_bus_speed *speed,
				enum pcie_link_width *width)
{
	return _kcl_pcie_bandwidth_available(dev, limiting_dev, speed, width);
}
#endif

#if !defined(HAVE_PCI_CONFIGURE_EXTENDED_TAGS)
void _kcl_pci_configure_extended_tags(struct pci_dev *dev);
#endif

static inline void kcl_pci_configure_extended_tags(struct pci_dev *dev)
{
#if !defined(HAVE_PCI_CONFIGURE_EXTENDED_TAGS)
	_kcl_pci_configure_extended_tags(dev);
#endif
}

#if !defined(HAVE_PCI_DEV_ID)
static inline u16 pci_dev_id(struct pci_dev *dev)
{
	return PCI_DEVID(dev->bus->number, dev->devfn);
}
#endif /* HAVE_PCI_DEV_ID */

#ifndef HAVE_PCI_IS_THUNDERBOLD_ATTACHED
static inline bool pci_is_thunderbolt_attached(struct pci_dev *pdev)
{
	return false;
}
#endif /* HAVE_PCI_IS_THUNDERBOLD_ATTACHED */

#endif /* AMDKCL_PCI_H */
