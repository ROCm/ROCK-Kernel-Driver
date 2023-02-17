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

enum pci_bus_speed (*_kcl_pcie_get_speed_cap)(struct pci_dev *dev);
EXPORT_SYMBOL(_kcl_pcie_get_speed_cap);

enum pcie_link_width (*_kcl_pcie_get_width_cap)(struct pci_dev *dev);
EXPORT_SYMBOL(_kcl_pcie_get_width_cap);

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

#ifdef AMDKCL_CREATE_MEASURE_FILE
/* Copied from drivers/pci/pci-sysfs.c */
static ssize_t max_link_speed_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%s\n", PCIE_SPEED2STR(kcl_pcie_get_speed_cap(pdev)));
}
static DEVICE_ATTR_RO(max_link_speed);

static ssize_t max_link_width_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%u\n", kcl_pcie_get_width_cap(pdev));
}
static DEVICE_ATTR_RO(max_link_width);

static ssize_t current_link_speed_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u16 linkstat;
	int err;
	const char *speed;

	err = pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &linkstat);
	if (err)
		return -EINVAL;

	switch (linkstat & PCI_EXP_LNKSTA_CLS) {
	case PCI_EXP_LNKSTA_CLS_16_0GB:
		speed = "16 GT/s";
		break;
	case PCI_EXP_LNKSTA_CLS_8_0GB:
		speed = "8 GT/s";
		break;
	case PCI_EXP_LNKSTA_CLS_5_0GB:
		speed = "5 GT/s";
		break;
	case PCI_EXP_LNKSTA_CLS_2_5GB:
		speed = "2.5 GT/s";
		break;
	default:
		speed = "Unknown speed";
	}

	return sprintf(buf, "%s\n", speed);
}
static DEVICE_ATTR_RO(current_link_speed);

static ssize_t current_link_width_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	u16 linkstat;
	int err;

	err = pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &linkstat);
	if (err)
		return -EINVAL;

	return sprintf(buf, "%u\n",
		(linkstat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT);
}
static DEVICE_ATTR_RO(current_link_width);

static struct attribute *pcie_dev_attrs[] = {
	&dev_attr_current_link_speed.attr,
	&dev_attr_current_link_width.attr,
	&dev_attr_max_link_width.attr,
	&dev_attr_max_link_speed.attr,
	NULL,
};

int _kcl_pci_create_measure_file(struct pci_dev *pdev)
{
	int ret = 0;

	ret = device_create_file(&pdev->dev, &dev_attr_current_link_speed);
	if (ret) {
		dev_err(&pdev->dev,
				"Failed to create current_link_speed sysfs files: %d\n", ret);
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_current_link_width);
	if (ret) {
		dev_err(&pdev->dev,
				"Failed to create current_link_width sysfs files: %d\n", ret);
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_max_link_width);
	if (ret) {
		dev_err(&pdev->dev,
				"Failed to create max_link_width sysfs files: %d\n", ret);
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_max_link_speed);
	if (ret) {
		dev_err(&pdev->dev,
				"Failed to create max_link_speed sysfs files: %d\n", ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL(_kcl_pci_create_measure_file);

void _kcl_pci_remove_measure_file(struct pci_dev *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_current_link_speed);
	device_remove_file(&pdev->dev, &dev_attr_current_link_width);
	device_remove_file(&pdev->dev, &dev_attr_max_link_width);
	device_remove_file(&pdev->dev, &dev_attr_max_link_speed);
}
EXPORT_SYMBOL(_kcl_pci_remove_measure_file);
#endif /* AMDKCL_CREATE_MEASURE_FILE */

#ifdef AMDKCL_ENABLE_RESIZE_FB_BAR
/* Copied from drivers/pci/pci.c */
#ifndef HAVE_PCI_REBAR_BYTES_TO_SIZE
static int _kcl_pci_rebar_find_pos(struct pci_dev *pdev, int bar)
{
	unsigned int pos, nbars, i;
	u32 ctrl;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_REBAR);
	if (!pos)
		return -ENOTSUPP;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
	nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >>
		    PCI_REBAR_CTRL_NBAR_SHIFT;

	for (i = 0; i < nbars; i++, pos += 8) {
		int bar_idx;

		pci_read_config_dword(pdev, pos + PCI_REBAR_CTRL, &ctrl);
		bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
		if (bar_idx == bar)
			return pos;
	}

	return -ENOENT;
}

u32 _kcl_pci_rebar_get_possible_sizes(struct pci_dev *pdev, int bar)
{
	int pos;
	u32 cap;

	pos = _kcl_pci_rebar_find_pos(pdev, bar);
	if (pos < 0)
		return 0;

	pci_read_config_dword(pdev, pos + PCI_REBAR_CAP, &cap);
	cap &= PCI_REBAR_CAP_SIZES;

	/* Sapphire RX 5600 XT Pulse has an invalid cap dword for BAR 0 */
	if (pdev->vendor == PCI_VENDOR_ID_ATI && pdev->device == 0x731f &&
	    bar == 0 && cap == 0x7000)
		cap = 0x3f000;

	return cap >> 4;
}
EXPORT_SYMBOL(_kcl_pci_rebar_get_possible_sizes);
#endif /* HAVE_PCI_REBAR_BYTES_TO_SIZE */
#endif /* AMDKCL_ENABLE_RESIZE_FB_BAR */
