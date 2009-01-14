#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
via_chrome9-y    :=  via_chrome9_drv.o via_chrome9_drm.o via_chrome9_mm.o via_chrome9_dma.o via_chrome9_verifier.o

obj-$(CONFIG_DRM_VIA_CHROME9)	+= via_chrome9.o