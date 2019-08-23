LINUXINCLUDE := $(DKMS_INCLUDE_PREFIX) $(LINUXINCLUDE)

BACKPORT_OBJS :=

amdgpu-y += $(addprefix ../backport/,$(BACKPORT_OBJS))

ifndef BUILD_AS_DKMS
ccflags-y += -include dkms/config/config.h
endif

ccflags-y += \
	-I$(FULL_AMD_PATH)/backport/include \
	-include ../backport/backport.h
