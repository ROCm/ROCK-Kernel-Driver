# SPDX-License-Identifier: MIT
amdkcl-y += main.o kcl_common.o

amdkcl-y += kcl_kernel_params.o
amdkcl-y += dma-buf/dma-resv.o kcl_dma-resv.o

amdkcl-y += kcl_backlight.o kcl_ioctl.o \
	kcl_kthread.o kcl_io.o kcl_seq_file.o \
	kcl_suspend.o kcl_pci.o kcl_mm.o kcl_memory.o \
	kcl_fence.o kcl_drm_cache.o \
	kcl_drm_fb.o kcl_drm_print.o kcl_drm_edid.o\
	kcl_drm_crtc.o kcl_drm_connector.o kcl_drm_atomic_helper.o \
	kcl_device_cgroup.o kcl_mn.o kcl_drm_modes.o kcl_time.o \
	kcl_acpi_table.o kcl_page_alloc.o kcl_numa.o kcl_fs_read_write.o kcl_drm_aperture.o \
	kcl_drm_simple_kms_helper.o kcl_bitmap.o kcl_vmscan.o kcl_dma_fence_chain.o \
	kcl_mce_amd.o kcl_workqueue.o kcl_cpumask.o kcl_drm_dsc_helper.o kcl_mm_slab.o \
	kcl_irqdesc.o kcl_drm_suballoc.o kcl_drm_exec.o kcl_drm_dp_helper.o kcl_drm_prime.o \
	kcl_drm_vblank.o kcl_drm_dp_mst_topology.o kcl_drm_syncobj.o kcl_drm_client_event.o \
	kcl_scatterlist.o kcl_kfifo.o kcl_cec_adap.o

amdkcl-$(CONFIG_DRM_AMD_DC_HDCP) += kcl_drm_hdcp.o
amdkcl-$(CONFIG_MMU_NOTIFIER) += kcl_mn.o
amdkcl-$(CONFIG_DEBUG_FS) += kcl_debugfs_inode.o kcl_debugfs_file.o
amdkcl-$(CONFIG_SYSFS) += kcl_sysfs_emit.o
amdkcl-$(CONFIG_AMD_WBRF) += kcl_wbrf.o
CFLAGS_kcl_fence.o := -I$(src)

ccflags-y += \
	-include $(src)/../dkms/config/config.h \
	-include $(src)/kcl_common.h

ccflags-y += -DHAVE_CONFIG_H

obj-m += amdkcl.o
