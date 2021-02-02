/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_PCI_BACKPORT_H
#define AMDKCL_PCI_BACKPORT_H

#include <linux/pci.h>
#include <linux/version.h>
#include <kcl/kcl_pci.h>

#if !defined(HAVE_PCIE_GET_SPEED_AND_WIDTH_CAP)
#define pcie_get_speed_cap _kcl_pcie_get_speed_cap
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
#define AMDKCL_PCIE_BRIDGE_PM_USABLE
#endif

/*
 * v4.18-rc1-3-gb1277a226d8c PCI: Cleanup PCI_REBAR_CTRL_BAR_SHIFT handling
 * v4.18-rc1-2-gd3252ace0bc6 PCI: Restore resized BAR state on resume
 * v4.14-rc3-3-g8bb705e3e79d PCI: Add pci_resize_resource() for resizing BARs
 * v4.14-rc3-2-g276b738deb5b PCI: Add resizable BAR infrastructure
 */
#ifdef PCI_REBAR_CTRL_BAR_SHIFT
#define AMDKCL_ENABLE_RESIZE_FB_BAR
#endif

#endif
