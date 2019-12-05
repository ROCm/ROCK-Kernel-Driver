# SPDX-License-Identifier: MIT
amdkcl-y += main.o symbols.o kcl_common.o

amdkcl-y += kcl_kernel_params.o
amdkcl-y += dma-buf/dma-resv.o

amdkcl-y += kcl_backlight.o kcl_ioctl.o \
	kcl_kthread.o kcl_io.o kcl_perf_event.o \
	kcl_suspend.o kcl_pci.o kcl_mm.o \
	kcl_fence.o kcl_fence_array.o kcl_reservation.o kcl_drm_cache.o \
	kcl_drm_fb.o kcl_fbmem.o \
	kcl_connector.o

CFLAGS_kcl_fence.o := -I$(src)

ccflags-y += \
	-include $(src)/../dkms/config/config.h \
	-include $(src)/kcl_common.h

ccflags-y += -DHAVE_CONFIG_H

obj-m += amdkcl.o
