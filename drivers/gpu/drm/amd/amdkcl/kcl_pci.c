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
#include <linux/acpi.h>

#if !defined(HAVE_PCIE_BANDWIDTH_AVAILABLE)
const unsigned char *_kcl_pcie_link_speed;

const unsigned char _kcl_pcie_link_speed_stub[] = {
	PCI_SPEED_UNKNOWN,              /* 0 */
	PCIE_SPEED_2_5GT,               /* 1 */
	PCIE_SPEED_5_0GT,               /* 2 */
	PCIE_SPEED_8_0GT,               /* 3 */
	PCI_SPEED_UNKNOWN,              /* 4 */
	PCI_SPEED_UNKNOWN,              /* 5 */
	PCI_SPEED_UNKNOWN,              /* 6 */
	PCI_SPEED_UNKNOWN,              /* 7 */
	PCI_SPEED_UNKNOWN,              /* 8 */
	PCI_SPEED_UNKNOWN,              /* 9 */
	PCI_SPEED_UNKNOWN,              /* A */
	PCI_SPEED_UNKNOWN,              /* B */
	PCI_SPEED_UNKNOWN,              /* C */
	PCI_SPEED_UNKNOWN,              /* D */
	PCI_SPEED_UNKNOWN,              /* E */
	PCI_SPEED_UNKNOWN               /* F */
};

/**
 * pcie_bandwidth_available - determine minimum link settings of a PCIe
 *                           device and its bandwidth limitation
 * @dev: PCI device to query
 * @limiting_dev: storage for device causing the bandwidth limitation
 * @speed: storage for speed of limiting device
 * @width: storage for width of limiting device
 *
 * Walk up the PCI device chain and find the point where the minimum
 * bandwidth is available.  Return the bandwidth available there and (if
 * limiting_dev, speed, and width pointers are supplied) information about
 * that point.  The bandwidth returned is in Mb/s, i.e., megabits/second of
 * raw bandwidth.
 */
u32 _kcl_pcie_bandwidth_available(struct pci_dev *dev, struct pci_dev **limiting_dev,
			enum pci_bus_speed *speed,
			enum pcie_link_width *width)
{
	u16 lnksta;
	enum pci_bus_speed next_speed;
	enum pcie_link_width next_width;
	u32 bw, next_bw;

	if (speed)
		*speed = PCI_SPEED_UNKNOWN;
	if (width)
		*width = PCIE_LNK_WIDTH_UNKNOWN;

	bw = 0;

	while (dev) {
		pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &lnksta);

		next_speed = _kcl_pcie_link_speed[lnksta & PCI_EXP_LNKSTA_CLS];
		next_width = (lnksta & PCI_EXP_LNKSTA_NLW) >>
		PCI_EXP_LNKSTA_NLW_SHIFT;

		next_bw = next_width * PCIE_SPEED2MBS_ENC(next_speed);

		/* Check if current device limits the total bandwidth */
		if (!bw || next_bw <= bw) {
			bw = next_bw;

		if (limiting_dev)
			*limiting_dev = dev;
		if (speed)
			*speed = next_speed;
		if (width)
			*width = next_width;
		}

		dev = pci_upstream_bridge(dev);
	}

	return bw;
}
EXPORT_SYMBOL(_kcl_pcie_bandwidth_available);
#endif /* HAVE_PCIE_BANDWIDTH_AVAILABLE */

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
#if !defined(HAVE_PCIE_BANDWIDTH_AVAILABLE)
	_kcl_pcie_link_speed = (const unsigned char *) amdkcl_fp_setup("pcie_link_speed", _kcl_pcie_link_speed_stub);
#endif
}

#if !defined(HAVE_PCIE_ENABLE_ATOMIC_OPS_TO_ROOT)
/**
 * pci_enable_atomic_ops_to_root - enable AtomicOp requests to root port
 * @dev: the PCI device
 * @comp_caps: Caps required for atomic request completion
 *
 * Return 0 if all upstream bridges support AtomicOp routing, egress
 * blocking is disabled on all upstream ports, and the root port
 * supports the requested completion capabilities (32-bit, 64-bit
 * and/or 128-bit AtomicOp completion), or negative otherwise.
 *
 */
int _kcl_pci_enable_atomic_ops_to_root(struct pci_dev *dev, u32 comp_caps)
{
	struct pci_bus *bus = dev->bus;

	if (!pci_is_pcie(dev))
		return -EINVAL;

	switch (pci_pcie_type(dev)) {
	/*
	 * PCIe 3.0, 6.15 specifies that endpoints and root ports are permitted
	 * to implement AtomicOp requester capabilities.
	 */
	case PCI_EXP_TYPE_ENDPOINT:
	case PCI_EXP_TYPE_LEG_END:
	case PCI_EXP_TYPE_RC_END:
		break;
	default:
		return -EINVAL;
	}

	while (bus->parent) {
		struct pci_dev *bridge = bus->self;
		u32 cap;

		pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &cap);

		switch (pci_pcie_type(bridge)) {
		/*
		 * Upstream, downstream and root ports may implement AtomicOp
		 * routing capabilities. AtomicOp routing via a root port is
		 * not considered.
		 */
		case PCI_EXP_TYPE_UPSTREAM:
		case PCI_EXP_TYPE_DOWNSTREAM:
			if (!(cap & PCI_EXP_DEVCAP2_ATOMIC_ROUTE))
				return -EINVAL;
			break;

		/*
		 * Root ports are permitted to implement AtomicOp completion
		 * capabilities.
		 */
		case PCI_EXP_TYPE_ROOT_PORT:
			if ((cap & comp_caps) != comp_caps)
				return -EINVAL;
			break;
		}

		/*
		 * Upstream ports may block AtomicOps on egress.
		 */
#if defined(OS_NAME_RHEL_6)
		if (pci_pcie_type(bridge) == PCI_EXP_TYPE_DOWNSTREAM) {
#else
		if (!bridge->has_secondary_link) {
#endif
			u32 ctl2;

			pcie_capability_read_dword(bridge, PCI_EXP_DEVCTL2,
						   &ctl2);
			if (ctl2 & PCI_EXP_DEVCTL2_ATOMIC_BLOCK)
				return -EINVAL;
		}

		bus = bus->parent;
	}

	pcie_capability_set_word(dev, PCI_EXP_DEVCTL2,
				 PCI_EXP_DEVCTL2_ATOMIC_REQ);

	return 0;
}
EXPORT_SYMBOL(_kcl_pci_enable_atomic_ops_to_root);
#endif

#if !defined(HAVE_PCI_CONFIGURE_EXTENDED_TAGS)
void _kcl_pci_configure_extended_tags(struct pci_dev *dev)
{
	u32 cap;
	u16 ctl;
	int ret;

	if (!pci_is_pcie(dev))
		return;

	ret = pcie_capability_read_dword(dev, PCI_EXP_DEVCAP, &cap);
	if (ret)
		return;

	if (!(cap & PCI_EXP_DEVCAP_EXT_TAG))
		return;

	ret = pcie_capability_read_word(dev, PCI_EXP_DEVCTL, &ctl);
	if (ret)
		return;

	if (!(ctl & PCI_EXP_DEVCTL_EXT_TAG)) {
		pcie_capability_set_word(dev, PCI_EXP_DEVCTL,
					PCI_EXP_DEVCTL_EXT_TAG);
	}
}
EXPORT_SYMBOL(_kcl_pci_configure_extended_tags);
#endif

#ifndef HAVE_PCI_PR3_PRESENT
#ifdef CONFIG_ACPI
bool _kcl_pci_pr3_present(struct pci_dev *pdev)
{
	struct acpi_device *adev;

	if (acpi_disabled)
		return false;

	adev = ACPI_COMPANION(&pdev->dev);
	if (!adev)
		return false;

	return adev->power.flags.power_resources &&
		acpi_has_method(adev->handle, "_PR3");
}
EXPORT_SYMBOL_GPL(_kcl_pci_pr3_present);
#endif
#endif /* HAVE_PCI_PR3_PRESENT */
