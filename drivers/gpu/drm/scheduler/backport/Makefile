
LINUXINCLUDE := $(DKMS_INCLUDE_PREFIX) $(LINUXINCLUDE)

ifndef BUILD_AS_DKMS
ccflags-y += -include dkms/config/config.h
endif

ccflags-y += \
	-I$(SCHED_FULL_PATH) \
	-include backport/backport.h
