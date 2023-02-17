/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_PCI_BACKPORT_H
#define AMDKCL_PCI_BACKPORT_H

#include <linux/pci.h>
#include <linux/version.h>
#include <kcl/kcl_pci.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
#define AMDKCL_PCIE_BRIDGE_PM_USABLE
#endif

#endif
