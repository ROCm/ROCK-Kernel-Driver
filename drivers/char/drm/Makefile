#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ifneq (,$(MACHINE))
MACHINE := $(ARCH)
endif
ifneq (,findstring x86_64,$(MACHINE))
DO_IOCTL32 := y
endif

drm-objs    :=	drm_auth.o drm_bufs.o drm_context.o drm_dma.o drm_drawable.o \
		drm_drv.o drm_fops.o drm_init.o drm_ioctl.o drm_irq.o \
		drm_lock.o drm_memory.o drm_proc.o drm_stub.o drm_vm.o \
		drm_agpsupport.o drm_scatter.o ati_pcigart.o drm_pci.o \
		drm_sysfs.o

gamma-objs  := gamma_drv.o gamma_dma.o
tdfx-objs   := tdfx_drv.o
r128-objs   := r128_drv.o r128_cce.o r128_state.o r128_irq.o r128_ioctl32.o
mga-objs    := mga_drv.o mga_dma.o mga_state.o mga_warp.o mga_irq.o mga_ioctl32.o
i810-objs   := i810_drv.o i810_dma.o
i830-objs   := i830_drv.o i830_dma.o i830_irq.o
i915-objs   := i915_drv.o i915_dma.o i915_irq.o i915_mem.o
radeon-objs := radeon_drv.o radeon_cp.o radeon_state.o radeon_mem.o radeon_irq.o radeon_ioctl32.o
ffb-objs    := ffb_drv.o ffb_context.o
sis-objs    := sis_drv.o sis_ds.o sis_mm.o
via-objs    := via_irq.o via_drv.o via_ds.o via_map.o via_mm.o via_dma.o via_verifier.o
ioctl32-objs := drm_ioctl32.c

#CONFIG_DRM_IOCTL32 = m
CONFIG_DRM = m

ifeq ($(DO_IOCTL32),y)
r128-objs += r128_ioctl32.o
radeon-objs += radeon_ioctl32.o
mga-objs += mga_ioctl32.o
CONFIG_DRM_IOCTL32 = $(CONFIG_DRM)
endif

obj-$(CONFIG_DRM)	+= drm.o
obj-$(CONFIG_DRM_GAMMA) += gamma.o
obj-$(CONFIG_DRM_TDFX)	+= tdfx.o
obj-$(CONFIG_DRM_R128)	+= r128.o
obj-$(CONFIG_DRM_RADEON)+= radeon.o
obj-$(CONFIG_DRM_MGA)	+= mga.o
obj-$(CONFIG_DRM_I810)	+= i810.o
obj-$(CONFIG_DRM_I830)	+= i830.o
obj-$(CONFIG_DRM_I915)  += i915.o
obj-$(CONFIG_DRM_FFB)   += ffb.o
obj-$(CONFIG_DRM_SIS)   += sis.o
obj-$(CONFIG_DRM_VIA)   += via.o
obj-$(CONFIG_DRM_IOCTL32) += drm_ioctl32.o
