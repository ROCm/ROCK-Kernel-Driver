# SPDX-License-Identifier: MIT
amdkcl-y += main.o symbols.o kcl_mn.o kcl_memory.o \
	kcl_ioctl.o

amdkcl-y += kcl_device_cgroup.o kcl_drm_cache.o kcl_drm.o \
	kcl_fence_array.o kcl_fence.o kcl_io.o kcl_kthread.o kcl_mm.o kcl_pci.o \
	kcl_perf_event.o kcl_reservation.o kcl_suspend.o kcl_workqueue.o \
	kcl_seq_file.o kcl_connector.o kcl_backlight.o kcl_drm_atomic_helper.o
amdkcl-y += dma-buf/dma-resv.o

# for trace-points
CFLAGS_kcl_fence.o := -I$(src)

ccflags-y += -include $(src)/../dkms/config/config.h

obj-m += amdkcl.o
