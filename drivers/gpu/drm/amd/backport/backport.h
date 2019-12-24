#ifndef AMDGPU_BACKPORT_H
#define AMDGPU_BACKPORT_H

#include <linux/version.h>
#include "kcl/kcl_backport_amdgpu.h"
#include <kcl/kcl_mn.h>
#include <kcl/reservation.h>
#include <kcl/kcl_drm_print.h>
#include <kcl/kcl_fs.h>
#include <kcl/kcl_amdgpu.h>
#include <kcl/kcl_drm_backport.h>
#include <kcl/kcl_acpi.h>
#include <kcl/kcl_bitops.h>
#include <kcl/kcl_device.h>
#include <kcl/kcl_device_cgroup_backport.h>
#include <kcl/kcl_kref.h>
#include <kcl/kcl_dma_mapping.h>
/*
 * linux/reservation.h must before all drm headers,
 * otherwise struct reservation_object is undefined.
 */
#include <kcl/kcl_fence_backport.h>
#include <kcl/kcl_drm_atomic_helper.h>
#include <kcl/kcl_drm_cache.h>
#include <kcl/kcl_drm_connector.h>
#include <kcl/kcl_drm_dp_helper_backport.h>
#include <kcl/kcl_drm_vma_manager_backport.h>
#include <kcl/kcl_firmware.h>
#include <kcl/kcl_hwmon.h>
#include <kcl/kcl_interval_tree_generic.h>
#include <kcl/kcl_io.h>
#include <kcl/kcl_kernel.h>
#include <kcl/kcl_kthread_backport.h>
#include <kcl/kcl_mm_backport.h>
#include <kcl/kcl_mmu_notifier.h>
#include <kcl/kcl_suspend.h>
#include <kcl/kcl_pagemap.h>
#include <kcl/kcl_pci_backport.h>
#include <kcl/kcl_perf_event_backport.h>
#include <kcl/kcl_suspend.h>
#include <kcl/kcl_timekeeping.h>
#include <kcl/kcl_uaccess_backport.h>
#include <kcl/kcl_vga_switcheroo_backport.h>
#include <kcl/kcl_types.h>
#include <kcl/kcl_drm_dp_mst_helper_backport.h>
#include <kcl/kcl_overflow.h>
#include <kcl/kcl_seq_file.h>
#include <kcl/kcl_ptrace.h>
#include <kcl/kcl_workqueue.h>
#include <kcl/kcl_preempt.h>
#include <kcl/kcl_video.h>
#include <kcl/kcl_idr.h>
#endif /* AMDGPU_BACKPORT_H */
