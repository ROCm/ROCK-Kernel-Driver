#ifndef AMDKFD_BACKPORT_H
#define AMDKFD_BACKPORT_H

#include <linux/version.h>
#if defined(BUILD_AS_DKMS)
#include <kcl/kcl_amd_asic_type.h>
#endif
#include <kcl/kcl_compat.h>
#include <kcl/kcl_pci.h>
#include <kcl/kcl_mn.h>
#include <kcl/kcl_fence.h>

#endif
