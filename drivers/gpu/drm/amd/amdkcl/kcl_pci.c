// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Bus Services, see include/linux/pci.h for further explanation.
 *
 * Copyright 1993 -- 1997 Drew Eckhardt, Frederic Potter,
 * David Mosberger-Tang
 *
 * Copyright 1997 -- 2000 Martin Mares <mj@ucw.cz>
 *   For codes copied from drivers/pci/pci.c
 *
 * (C) Copyright 2002-2004 Greg Kroah-Hartman <greg@kroah.com>
 * (C) Copyright 2002-2004 IBM Corp.
 * (C) Copyright 2003 Matthew Wilcox
 * (C) Copyright 2003 Hewlett-Packard
 * (C) Copyright 2004 Jon Smirl <jonsmirl@yahoo.com>
 * (C) Copyright 2004 Silicon Graphics, Inc. Jesse Barnes <jbarnes@sgi.com>
 *   For codes copied from drivers/pci/pci-sysfs.c
 */

#include <kcl/kcl_pci.h>
#include <linux/version.h>

#if !defined(HAVE_PCIE_GET_SPEED_AND_WIDTH_CAP)
/*
 * pcie_get_speed_cap - query for the PCI device's link speed capability
 * @dev: PCI device to query
 *
 * Query the PCI device speed capability.  Return the maximum link speed
 * supported by the device.
 */
enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *dev)
{
	u32 lnkcap2, lnkcap;

	/*
	 * PCIe r4.0 sec 7.5.3.18 recommends using the Supported Link
	 * Speeds Vector in Link Capabilities 2 when supported, falling
	 * back to Max Link Speed in Link Capabilities otherwise.
	 */
	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP2, &lnkcap2);
	if (lnkcap2) { /* PCIe r3.0-compliant */
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_16_0GB)
			return PCIE_SPEED_16_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_8_0GB)
			return PCIE_SPEED_8_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_5_0GB)
			return PCIE_SPEED_5_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_2_5GB)
			return PCIE_SPEED_2_5GT;
		return PCI_SPEED_UNKNOWN;
	}

	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
	if (lnkcap) {
		if (lnkcap & PCI_EXP_LNKCAP_SLS_16_0GB)
			return PCIE_SPEED_16_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_8_0GB)
			return PCIE_SPEED_8_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_5_0GB)
			return PCIE_SPEED_5_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_2_5GB)
			return PCIE_SPEED_2_5GT;
	}

	return PCI_SPEED_UNKNOWN;
}

/**
 * pcie_get_width_cap - query for the PCI device's link width capability
 * @dev: PCI device to query
 *
 * Query the PCI device width capability.  Return the maximum link width
 * supported by the device.
 */
enum pcie_link_width pcie_get_width_cap(struct pci_dev *dev)
{
	u32 lnkcap;

	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
	if (lnkcap)
		return (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;

	return PCIE_LNK_WIDTH_UNKNOWN;
}
#endif

enum pci_bus_speed (*_kcl_pcie_get_speed_cap)(struct pci_dev *dev);
EXPORT_SYMBOL(_kcl_pcie_get_speed_cap);

enum pcie_link_width (*_kcl_pcie_get_width_cap)(struct pci_dev *dev);
EXPORT_SYMBOL(_kcl_pcie_get_width_cap);

void amdkcl_pci_init(void)
{
#if !defined(HAVE_PCIE_GET_SPEED_AND_WIDTH_CAP)
	_kcl_pcie_get_speed_cap = amdkcl_fp_setup("pcie_get_speed_cap", pcie_get_speed_cap);
	_kcl_pcie_get_width_cap = amdkcl_fp_setup("pcie_get_width_cap", pcie_get_width_cap);
#endif
}
