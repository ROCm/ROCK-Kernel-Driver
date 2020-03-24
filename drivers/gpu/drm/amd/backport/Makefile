# SPDX-License-Identifier: MIT
BACKPORT_OBJS :=

amdgpu-y += $(addprefix ../backport/,$(BACKPORT_OBJS))

ccflags-y += \
	-I$(FULL_AMD_PATH) \
	-I$(FULL_AMD_PATH)/backport/include \
	-I$(FULL_AMD_PATH)/dkms \
	-include config/config.h \
	-include backport/backport.h

ccflags-y += -DHAVE_CONFIG_H
