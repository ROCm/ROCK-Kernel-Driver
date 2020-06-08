/* SPDX-License-Identifier: MIT */
#ifndef AMDGPU_BACKPORT_H
#define AMDGPU_BACKPORT_H

#include <linux/version.h>
/*
 * linux/reservation.h must before all drm headers,
 * otherwise struct reservation_object is undefined.
 */
#include <kcl/reservation.h>
#include <kcl/backport/kcl_fence_backport.h>
#include <kcl/backport/kcl_drm_backport.h>
#include <kcl/kcl_mn.h>
#include <kcl/kcl_drm_print.h>
#include <kcl/kcl_fs.h>
#include <kcl/backport/kcl_mm_backport.h>
#include <kcl/kcl_kref.h>
#include <kcl/kcl_amdgpu.h>
#include <kcl/kcl_acpi.h>
#include <kcl/kcl_bitops.h>
#include <kcl/kcl_device.h>
#include <kcl/backport/kcl_device_cgroup_backport.h>
#include <kcl/kcl_dma_mapping.h>
#include <kcl/kcl_drm_dp_mst_helper.h>
#include <kcl/backport/kcl_drm_cache_backport.h>
#include <kcl/kcl_drm_connector.h>
#include <kcl/backport/kcl_drm_dp_helper_backport.h>
#include <kcl/backport/kcl_drm_vma_manager_backport.h>
#include <kcl/kcl_firmware.h>
#include <kcl/kcl_hwmon.h>
#include <kcl/kcl_interval_tree_generic.h>
#include <kcl/kcl_io.h>
#include <kcl/kcl_kernel.h>
#include <kcl/backport/kcl_kthread_backport.h>
#include <kcl/kcl_mmu_notifier.h>
#include <kcl/kcl_suspend.h>
#include <kcl/kcl_pagemap.h>
#include <kcl/backport/kcl_pci_backport.h>
#include <kcl/backport/kcl_perf_event_backport.h>
#include <kcl/kcl_suspend.h>
#include <kcl/kcl_timekeeping.h>
#include <kcl/backport/kcl_uaccess_backport.h>
#include <kcl/backport/kcl_vga_switcheroo_backport.h>
#include <kcl/kcl_types.h>
#include <kcl/backport/kcl_drm_dp_mst_helper_backport.h>
#include "kcl/kcl_backport_amdgpu.h"
#include <kcl/kcl_overflow.h>
#include <kcl/kcl_seq_file.h>
#include <kcl/kcl_ptrace.h>
#include <kcl/kcl_workqueue.h>
#include <kcl/kcl_preempt.h>
#include <kcl/kcl_video.h>
#include <kcl/kcl_idr.h>
#include <kcl/kcl_compiler_attributes.h>
#include <kcl/kcl_list.h>
#include <kcl/kcl_backlight.h>
#include <kcl/backport/kcl_drm_atomic_helper_backport.h>
#include <kcl/kcl_drm_atomic.h>
#include <kcl/kcl_amdgpu_drm_fb_helper.h>
#endif /* AMDGPU_BACKPORT_H */
