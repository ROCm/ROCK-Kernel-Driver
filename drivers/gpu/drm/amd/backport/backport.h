#ifndef AMDGPU_BACKPORT_H
#define AMDGPU_BACKPORT_H

#include <linux/version.h>
#include <kcl/kcl_reservation.h>
#include <kcl/kcl_kref.h>
#include <kcl/kcl_drm.h>
#include <kcl/kcl_amdgpu.h>
#include <kcl/kcl_bitops.h>
#include <kcl/kcl_drm_connector.h>
#include <kcl/kcl_fence.h>
#include <kcl/kcl_drm_atomic_helper.h>
#include <kcl/kcl_drm_cache.h>
#include <kcl/kcl_mm.h>
#include <kcl/kcl_vga_switcheroo.h>
#include <kcl/kcl_fence_array.h>
#include <kcl/kcl_hwmon.h>
#include <kcl/kcl_acpi.h>
#include <kcl/kcl_device.h>
#include <kcl/kcl_fs.h>
#include <kcl/kcl_mn.h>
#include <kcl/kcl_kernel.h>
#if !defined(HAVE_INTERVAL_TREE_DEFINE)
#include <kcl/kcl_interval_tree_generic.h>
#endif
#include <kcl/kcl_device_cgroup.h>
#include <kcl/kcl_drm_dp_helper.h>
#include <kcl/kcl_mmu_notifier.h>
#include <kcl/kcl_overflow.h>
#include <kcl/kcl_seq_file.h>
#include <kcl/kcl_perf_event.h>
#include <kcl/kcl_ptrace.h>
#include <kcl/kcl_pci.h>
#include <kcl/kcl_suspend.h>
#include <kcl/kcl_kthread.h>
#include <kcl/kcl_firmware.h>
#include <kcl/kcl_timekeeping.h>
#include <kcl/kcl_io.h>
#include <kcl/kcl_workqueue.h>
#include <kcl/kcl_preempt.h>
#include <kcl/kcl_video.h>
#include <kcl/kcl_idr.h>

#endif /* AMDGPU_BACKPORT_H */
