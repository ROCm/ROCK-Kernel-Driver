#ifndef AMDKFD_BACKPORT_H
#define AMDKFD_BACKPORT_H

#include <linux/version.h>
#include <kcl/kcl_compat.h>
#include <kcl/kcl_pci.h>
#include <kcl/kcl_mn.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#define KFD_NO_IOMMU_V2_SUPPORT
#endif

#endif
