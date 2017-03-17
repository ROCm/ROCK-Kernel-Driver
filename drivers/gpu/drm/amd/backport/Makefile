LINUXINCLUDE := $(DKMS_INCLUDE_PREFIX) $(LINUXINCLUDE)

BACKPORT_OBJS :=

amdgpu-y += $(addprefix ../backport/,$(BACKPORT_OBJS))

ccflags-y += \
	-I$(FULL_AMD_PATH)/backport/include \
	-include ../backport/backport.h
