# SPDX-License-Identifier: MIT
BACKPORT_OBJS := kcl_drm_fb_helper.o

amdgpu-y += $(addprefix ../backport/,$(BACKPORT_OBJS))
amdgpu-$(CONFIG_MMU_NOTIFIER) += $(addprefix ../backport/,kcl_mmu_notifier.o)

ccflags-y += \
	-I$(FULL_AMD_PATH) \
	-I$(FULL_AMD_PATH)/backport/include \
	-I$(FULL_AMD_PATH)/dkms \
	-include config/config.h \
	-include backport/backport.h

ccflags-y += -DHAVE_CONFIG_H
