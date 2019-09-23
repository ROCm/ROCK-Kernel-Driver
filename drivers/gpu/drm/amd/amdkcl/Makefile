# SPDX-License-Identifier: MIT
amdkcl-y += main.o symbols.o kcl_common.o

amdkcl-y += dma-buf/dma-resv.o

amdkcl-y += kcl_workqueue.o kcl_backlight.o kcl_ioctl.o \
	kcl_kthread.o kcl_io.o kcl_perf_event.o kcl_seq_file.o \
	kcl_suspend.o kcl_pci.o kcl_mm.o kcl_memory.o kcl_sched.o \
	kcl_fence.o kcl_fence_array.o kcl_reservation.o kcl_drm_cache.o

# for trace-points
CFLAGS_kcl_fence.o := -I$(src)

ccflags-y += -include $(src)/../dkms/config/config.h
ccflags-y += -DHAVE_CONFIG_H

obj-m += amdkcl.o
