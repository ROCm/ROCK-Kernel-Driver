#ifndef AMDGPU_BACKPORT_H
#define AMDGPU_BACKPORT_H

#if defined(BUILD_AS_DKMS)
#include <kcl/kcl_amd_asic_type.h>
#endif
#include <kcl/kcl_kref.h>
#include <kcl/kcl_fence.h>
#include <kcl/kcl_drm.h>
#include <kcl/kcl_bitops.h>
#include <kcl/kcl_reservation.h>
#include <kcl/kcl_amdgpu.h>
#include <kcl/kcl_mm.h>
#include <kcl/kcl_vga_switcheroo.h>
#include <kcl/kcl_fence_array.h>
#include <kcl/kcl_kthread.h>
#include <kcl/kcl_io.h>
#include <kcl/kcl_mn.h>
#include <kcl/kcl_acpi.h>
#include <kcl/kcl_device.h>
#include <kcl/kcl_hwmon.h>
#include <kcl/kcl_fs.h>
#include <kcl/kcl_tracepoint.h>
#include <kcl/kcl_bitmap.h>
#include <kcl/kcl_kernel.h>
#include <kcl/kcl_preempt.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
#include <kcl/kcl_interval_tree_generic.h>
#endif
#include <kcl/kcl_pci.h>
#include <kcl/kcl_firmware.h>
#include <kcl/kcl_compat.h>
#include <kcl/kcl_wait.h>
#include <kcl/kcl_idr.h>
#include <kcl/kcl_drm_atomic_helper.h>
#include <kcl/kcl_video.h>
#include <kcl/kcl_drm_connector.h>
#include <kcl/kcl_device_cgroup.h>

#endif /* AMDGPU_BACKPORT_H */
