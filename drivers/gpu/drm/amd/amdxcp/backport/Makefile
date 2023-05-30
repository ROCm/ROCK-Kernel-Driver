BACKPORT_OBJS := kcl_drm_drv.o

amdxcp-y += $(addprefix ./backport/,$(BACKPORT_OBJS))

ccflags-y += \
	-I$(AMD_XCP_PATH)/ \
	-I$(AMD_XCP_PATH)/backport/include \
	-I$(AMD_XCP_PATH)/../dkms \
	-include config/config.h \
	-include backport/backport.h
