# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the drm device driver.  This driver provides support for the

ifneq (,$(BUILD_AS_DKMS))
	TTM_NAME = amdttm
else
	TTM_NAME = ttm
endif

$(TTM_NAME)-y := ttm_memory.o ttm_tt.o ttm_bo.o \
	ttm_bo_util.o ttm_bo_vm.o ttm_module.o \
	ttm_execbuf_util.o ttm_page_alloc.o ttm_bo_manager.o
$(TTM_NAME)-$(CONFIG_AGP) += ttm_agp_backend.o
$(TTM_NAME)-$(CONFIG_DRM_TTM_DMA_PAGE_POOL) += ttm_page_alloc_dma.o

obj-$(CONFIG_DRM_TTM) += $(TTM_NAME).o

TTM_FULL_PATH = $(src)
include $(TTM_FULL_PATH)/backport/Makefile
